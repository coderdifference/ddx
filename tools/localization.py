#!/usr/bin/env python3
"""
compile_loc2.py
================

Compiles a Darkest Dungeon `*.string_table.xml` file into one `.loc2`
binary file per `<language>` block, matching the game's own compiler
byte-for-byte.

Format reverse-engineered from a verified-correct reference file
(`true_pass.loc2`) and confirmed with an exact binary diff.

--------------------------------------------------------------------
FILE FORMAT
--------------------------------------------------------------------
Header (12 bytes): three little-endian uint32 offsets `o0, o1, o2`.

  [0      : 12 ) header: o0, o1, o2
  [12     : o0 ) TABLE 1 - hash index
        [12 : o0 - N*12)  bucket array: CAP buckets x 8 bytes
                          (pointer:u32, run_length:u32)
                          pointer == 0 and run_length == 0 => empty bucket
                          pointer is an ABSOLUTE file offset into the
                          "sorted entries" list below.
        [o0-N*12 : o0  )  sorted entries: N x 12 bytes
                          (hash:u32, index:u32, 1:u32)
                          sorted ascending by `hash`. `index` is this
                          entry's position (0..N-1) in TABLE 3 below
                          (which uses the same hash-sorted order).
  [o0     : o1 ) TABLE 2 - N x 8 bytes: (i:u32, 1:u32) for i in 0..N-1
                          (trivial sequential index list)
  [o1     : o2 ) TABLE 3 - N x 12 bytes:
                          (string_offset:u32, string_len_incl_null:u32, 256:u32)
                          string_offset is relative to the start of the
                          string blob (i.e. relative to o2).
  [o2     : EOF) STRING BLOB - all values, UTF-8, each null-terminated,
                          concatenated in the same hash-sorted order as
                          TABLE 3 / the sorted entries list.

Hash algorithm (Darkest Dungeon's standard string hash):

    h = 0
    for byte in utf8_bytes(s):
        h = (h * 53 + byte) mod 2**32

Bucket index = the TOP `log2(CAP)` bits of the 32-bit hash, i.e.
`hash >> (32 - log2(CAP))`  -- NOT `hash % CAP`. Because entries are
processed in ascending-hash order, all entries that share the same
bucket end up contiguous in the sorted list, so a bucket only needs to
store a single (pointer, run_length) pair to describe its whole chain.

CAP (bucket-table capacity) is always 512: the game looks keys up in
512 buckets whatever the entry count. Verified 2026-10-19: the game's
english.loc2 has 9426 entries in 512 buckets (runs simply get longer).
A file with more buckets breaks lookups: a client mod table passed 512
entries, the old fallback doubled CAP to 1024, and the game reported
most of the mod's strings missing (localization.log).
--------------------------------------------------------------------
"""

import argparse
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

DEFAULT_HASH_TABLE_CAPACITY = 512  # the game's fixed bucket count (its english.loc2: 9426 entries, 512 buckets)


COLOUR_TAG = re.compile(r"\{colour_start\|([^}]+)\}|\{colour_end\}")


def colour_code(colour_id: str) -> bytes:
    """The 6 bytes after <c> for a colour id (TextureFont_CatColourStart; see tools/colour_codes.py)."""
    h = dd_hash(colour_id)
    return bytes([((h >> (6 * k)) & 0x3F) + 0x41 for k in range(5)] + [(h >> 30) + 0x41])


def encode_value(value: str) -> bytes:
    """UTF-8 bytes of a string, with {colour_start|<id>} -> <c> + code and {colour_end} -> </c>, as the
    game's own compiler does (the game only understands the encoded form; codes can hold bytes 0x7f/0x80,
    which is why they're written as raw bytes here rather than typed into the XML)."""
    out = bytearray()
    pos = 0
    for m in COLOUR_TAG.finditer(value):
        out += value[pos:m.start()].encode("utf-8")
        out += (b"<c>" + colour_code(m.group(1))) if m.group(1) else b"</c>"
        pos = m.end()
    out += value[pos:].encode("utf-8")
    return bytes(out)


def dd_hash(s: str) -> int:
    """Darkest Dungeon's string hash: h = h*53 + byte (mod 2**32)."""
    h = 0
    for b in s.encode("utf-8"):
        h = (h * 53 + b) & 0xFFFFFFFF
    return h


def _bucket_shift(cap: int) -> int:
    bits = cap.bit_length() - 1
    if (1 << bits) != cap:
        raise ValueError(f"capacity must be a power of two, got {cap}")
    return 32 - bits


def compile_language(
    entries: list[tuple[str, str]],
    capacity: int = DEFAULT_HASH_TABLE_CAPACITY,
    lang_id: str = "",
) -> bytes:
    """
    entries: list of (id_str, value_str) tuples, in any order.
    lang_id: language name, used only for the progress log.
    Returns the compiled .loc2 file contents as bytes.
    """
    n = len(entries)
    if n == 0:
        raise ValueError("cannot compile a language block with zero entries")

    for id_str, _ in entries:
        print(f"Processing {lang_id}.{id_str} ...")

    # Never grow the table: the game always reads 512 buckets (see the module docstring).
    cap = capacity
    shift = _bucket_shift(cap)

    # Sort entries by hash ascending; this fixes both the string-blob
    # order and the TABLE 3 order, and lets buckets store a single
    # contiguous run instead of a real linked chain.
    keyed = sorted(
        ((dd_hash(id_str), value) for id_str, value in entries),
        key=lambda t: t[0],
    )

    # String blob + TABLE 3 entries (offset, length incl. NUL, flag=256)
    blob = bytearray()
    table3: list[tuple[int, int, int]] = []
    for _, value in keyed:
        raw = encode_value(value) + b"\x00"
        table3.append((len(blob), len(raw), 256))
        blob += raw

    # Sorted (hash, index, 1) tail of TABLE 1
    tail = [(h, idx, 1) for idx, (h, _) in enumerate(keyed)]

    # TABLE 2: trivial (i, 1) for i in 0..N-1
    table2 = [(i, 1) for i in range(n)]

    o0 = 12 + cap * 8 + n * 12
    o1 = o0 + n * 8
    o2 = o1 + n * 12
    tail_base = o0 - n * 12  # == 12 + cap * 8

    # Build bucket array: each bucket points at the first entry of its
    # contiguous run in `tail`, plus how many entries are in that run.
    buckets = [(0, 0)] * cap
    i = 0
    while i < n:
        b = tail[i][0] >> shift
        j = i
        while j < n and (tail[j][0] >> shift) == b:
            j += 1
        buckets[b] = (tail_base + i * 12, j - i)
        i = j

    out = bytearray()
    out += struct.pack("<III", o0, o1, o2)
    for ptr, cnt in buckets:
        out += struct.pack("<II", ptr, cnt)
    for h, idx, c in tail:
        out += struct.pack("<III", h, idx, c)
    for idx, c in table2:
        out += struct.pack("<II", idx, c)
    for off, length, flag in table3:
        out += struct.pack("<III", off, length, flag)
    out += blob

    assert len(out) == o2 + len(blob), "internal size mismatch while compiling"
    return bytes(out)


def parse_string_table_xml(
    xml_path: str,
) -> dict[str, list[tuple[str, str]]]:
    """
    Returns a dict: {language_id: [(entry_id, value_text), ...]}
    """
    tree = ET.parse(xml_path)
    root = tree.getroot()
    languages: dict[str, list[tuple[str, str]]] = {}
    for lang in root.findall("language"):
        lang_id = lang.attrib["id"]
        entries = [
            (entry.attrib["id"], entry.text or "")
            for entry in lang.findall("entry")
        ]
        languages[lang_id] = entries
    return languages


# Operating system files that are never part of a mod (Finder, Windows Explorer, AppleDouble resource forks).
SYSTEM_FILES = {".DS_Store", "Thumbs.db", "desktop.ini", "Icon\r"}


def is_system_file(path: Path) -> bool:
    return path.name in SYSTEM_FILES or path.name.startswith("._")


def write_mod_file_manifest(mod_root: Path, mod_name: str | None = None) -> Path:
    """Write each mod file's path relative to the mod root and byte size (system files left out)."""
    manifest_path = mod_root / f"{mod_name or mod_root.name}_modfiles.txt"
    files = sorted(
        path
        for path in mod_root.rglob("*")
        if path.is_file()
        and path != manifest_path
        and ".git" not in path.relative_to(mod_root).parts
        and not is_system_file(path)
    )
    manifest_path.write_text(
        "".join(
            f"{path.relative_to(mod_root).as_posix()} {path.stat().st_size}\n"
            for path in files
        ),
        encoding="utf-8",
    )
    return manifest_path


COLOUR_DEF = re.compile(r'^\s*colour:\s*\.id\s+"([^"]+)"', re.M)
STEAM_DATA = Path.home() / "Library/Application Support/Steam/steamapps/common/DarkestDungeon/_osx/Darkest.app/Contents/Resources/data"


def known_colours(mod_root: Path) -> tuple[set[str], list[Path]]:
    """Colour ids from the mod's colours/*.colours.darkest and the vanilla ones (the repo's read-only
    _reference/base_game/data, else the game's data folder: DD_MODS_DIR/.., else the macOS Steam install),
    base game and DLC. Returns the ids and the data folders searched."""
    import os
    roots = [mod_root]
    candidates = [Path(__file__).resolve().parent.parent / "_reference" / "base_game" / "data"]
    if os.environ.get("DD_MODS_DIR"):
        candidates.append(Path(os.environ["DD_MODS_DIR"]).parent)
    candidates.append(STEAM_DATA)
    vanilla = next((c for c in candidates if (c / "colours").is_dir()), None)
    if vanilla:
        roots.append(vanilla)
    ids: set[str] = set()
    for root in roots:
        for path in root.rglob("*.colours.darkest"):
            if root == vanilla and "mods" in path.relative_to(root).parts:
                continue                       # other mods installed in the game's data/mods
            ids.update(COLOUR_DEF.findall(path.read_text(encoding="utf-8", errors="replace")))
    return ids, roots


def check_colours(languages: dict[str, list[tuple[str, str]]], mod_root: Path) -> int:
    """Warn about {colour_start|<id>} tags naming a colour no colours file defines (the game would show its
    error colour and log "Invalid Colour Id"). Returns the number of warnings."""
    ids, roots = known_colours(mod_root)
    if len(roots) < 2:
        print("warning: vanilla colours not found (_reference/base_game/data, DD_MODS_DIR, Steam install): "
              "only the mod's own colours are checked", file=sys.stderr)
    seen: set[tuple[str, str]] = set()
    for entries in languages.values():
        for entry_id, value in entries:
            for m in COLOUR_TAG.finditer(value):
                cid = m.group(1)
                if cid and cid not in ids and (entry_id, cid) not in seen:
                    seen.add((entry_id, cid))
                    print(f"warning: {entry_id}: unknown colour '{cid}'", file=sys.stderr)
    return len(seen)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0] if __doc__ else ""
    )
    parser.add_argument("xml_path", help="path to the *.string_table.xml file")
    parser.add_argument(
        "-o", "--outdir", default=None, help="directory to write .loc2 files into (default: beside the XML file)"
    )
    parser.add_argument(
        "--mod-name",
        help="the mod's deployed folder name, which prefixes the .loc2 and manifest files "
        "(default: the folder holding localization/)",
    )
    parser.add_argument(
        "--capacity",
        type=int,
        default=DEFAULT_HASH_TABLE_CAPACITY,
        help="hash bucket table capacity, a power of two (default: 512, the game's fixed bucket count; "
        "any other value makes the game miss strings)",
    )
    args = parser.parse_args()

    languages = parse_string_table_xml(args.xml_path)
    if not languages:
        print("No <language> blocks found in the XML.", file=sys.stderr)
        sys.exit(1)

    outdir = Path(args.outdir) if args.outdir else Path(args.xml_path).resolve().parent
    outdir.mkdir(parents=True, exist_ok=True)

    mod_root = Path(args.xml_path).resolve().parent.parent
    mod_name = args.mod_name or mod_root.name
    check_colours(languages, mod_root)
    for lang_id, entries in languages.items():
        data = compile_language(entries, capacity=args.capacity, lang_id=lang_id)
        out_path = outdir / f"{mod_name}_{lang_id}.loc2"
        out_path.write_bytes(data)
        print(f"wrote {out_path}  ({len(entries)} entries, {len(data)} bytes)")

    manifest_path = write_mod_file_manifest(mod_root, mod_name)
    print(f"wrote {manifest_path}")


if __name__ == "__main__":
    main()
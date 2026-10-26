// Shared declarations for ddx (see ../README.md and ../../docs/architecture.md).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game_layout.h"

// ---------------------------------------------------------------- log.c
void log_open(void);
void crash_report_install(void); // crash_win.c: Windows crash lines in ddx.log
void combat_log_open(void);
void combat_log_event(const char *name, const void *event, size_t bytes);
void combat_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void combat_log_skill_use(const void *actor, const void *skill, const void *targets, bool flag);
void combat_log_actor_event(const char *name, const void *actor, const char *kind, unsigned guid);
void combat_log_install(void); // log every hook point (a "*" listener)
void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// A native client's own log files: called in its engine_client_init with a name (letters, digits, _ and -, no .log),
// its logf_ / combat_logf lines go to <name>.log / <name>_combat.log beside ddx.log (fresh each run) instead of the
// library's. One name per client binary.
void log_file(const char *name);
extern bool g_debug; // config "debug": true -> verbose logging
#define dbgf_(...)                                                                                                     \
    do {                                                                                                               \
        if (g_debug)                                                                                                   \
            logf_(__VA_ARGS__);                                                                                        \
    } while (0)

// ---------------------------------------------------------------- paths.c
// The game's data folder (it holds mods/ and app.log): macOS .../Darkest.app/Contents/Resources/data,
// Windows the install folder (the exe is _windows/win64/Darkest.exe under it). False if unknown.
bool game_data_dir(char *out, size_t n);
// Steam's Workshop folder for Darkest Dungeon (262060), subscribed mods; false if unknown.
bool workshop_dir(char *out, size_t n);
// Calls fn with each mod's engine folder (<mods>/<mod>/engine, the game's mods folder, then the Workshop's) until it
// returns false; the number of folders visited. The library package is left out.
int mod_engine_dirs(bool (*fn)(const char *engine_dir, void *ud), void *ud);
// The library package's engine folder: the mod folder (game mods folder first, then the Workshop's) whose engine/ has
// library.json. It ships the library's defaults (engine/ui layouts, art, strings) and its binary. False if none.
bool library_engine_dir(char *out, size_t n);

// ---------------------------------------------------------------- mods.c (the mods built on the library, per campaign)
// Every mod folder with an engine/ folder is a mod (index 0..mods_count()-1, mod_engine_dirs order); -1 is the library
// itself. The current campaign applies some of them, in its mod list's order (mods_pos: its place, -1 not applied);
// only those are active (mods_active; the library's own always is). No mod is active outside a campaign.
// Registrations record mods_current() (the mod whose client init or config is running) as their owner. Every registry
// (hook points, game hook wrappers, and each client list, setting and layout below) counts an entry only while the
// campaign applies its owner's mod; outside a campaign only the library's own. Where entries answer in turn (the first
// answer wins) the top mod's are asked first and the library's last; observers, rules that pass an answer along, and
// chains run the library's first and the top mod's last; a single setting (a layout, a place callback) is the top
// applied mod's.
// Priority: a mod higher in the campaign's mod list (a lower mods_pos; the top of the game's list) wins over one below
// it where two set the same thing (a layout name, a class config, a setting). mods_by_priority: the active mods,
// highest priority first (up to max; returns how many).
// mods_on_campaign: fn runs after each campaign load's list is read, and when the campaign closes (up to 16).
void mods_install(void); // after game_init, before configs and clients
int mods_count(void);
int mods_find(const char *path); // the mod whose folder holds path, -1 if none
const char *mods_title(int mod);
const char *mods_folder(int mod);
const char *mods_engine_dir(int mod); // mod's engine/ folder, for client-owned extension data
int mods_current(void);
void mods_set_current(int mod);
bool mods_campaign(void); // a campaign's mod list is known
int mods_pos(int mod);
bool mods_active(int mod);
int mods_by_priority(int *out, int max);
bool mods_package_applied(void); // the library package is applied to the campaign
void mods_on_campaign(void (*fn)(void));
unsigned mods_generation(void); // changes at each campaign load and close (before fn runs)
// mods_list.c: a registry's entries by owner, dispatched in campaign order. mods_order: of n entries owned by
// owners[i] (-1: the library), the active ones' indices into out; top_first false: the library's, then the mods from
// the lowest in the campaign's list to the highest (the top mod last: it has the last word, as chains and observers
// want); true: the top mod's first, the library's last (for "the first answer wins" lists). One owner's entries keep
// their registration order. A ModsList (zeroed, static) keeps the owners and that order, rebuilt after a registration
// or a campaign load or close: mods_list_add records mods_current() for the registry's next entry (its index, the
// same as the registry's own array; -1: full); mods_list_order fills l->order with the active entries' indices and
// returns how many (use one top_first per list); mods_list_first: the top active entry (-1: none); mods_list_active:
// entry i's owner is active.
#define MODS_LIST_MAX 64
typedef struct {
    int n, owner[MODS_LIST_MAX], norder, order[MODS_LIST_MAX];
    unsigned gen;
    bool top_first;
} ModsList;
int mods_order(const int *owners, int n, bool top_first, int *out);
int mods_list_add(ModsList *l);
int mods_list_order(ModsList *l, bool top_first);
int mods_list_first(ModsList *l);
bool mods_list_active(const ModsList *l, int i);
// for each active entry i of list l, in mods_list_order's order: MODS_EACH(g_clients_mods, false, i) g_clients[i]...
#define MODS_EACH(l, top_first, i)                                                                                     \
    for (int i##_n_ = mods_list_order(&(l), (top_first)), i##_k_ = 0, i = 0;                                           \
         i##_k_ < i##_n_ && ((i = (l).order[i##_k_]), 1); i##_k_++)

// ---------------------------------------------------------------- clients.c
// Native clients: mods' own compiled code on the library's API. Every <mod>/engine/*.dylib (Windows *.dll) is loaded
// at start-up and its exported engine_client_init called (ENGINE_CLIENT_INIT), after the configs and the library's
// own hook point listeners, before the library's features install (so a client registers with them first). A client
// returns 0, or its error is logged and it stays loaded but off. Clients load in mods folder order.
#define ENGINE_CLIENT_API  1
#define ENGINE_CLIENT_INIT "engine_client_init"
typedef struct EngineClientInfo {
    int api;                // ENGINE_CLIENT_API of the library loading it
    const char *engine_dir; // the client's mod's engine folder (<mods>/<mod>/engine)
} EngineClientInfo;
typedef int (*EngineClientInitFn)(const EngineClientInfo *info);
void clients_load_all(void);

// ---------------------------------------------------------------- hook.c (macOS) / hook_win.c (Windows)
// Inline hook: replaces the first `len` bytes of `target` (which must equal `expected`: whole
// instructions, no relative jumps or calls, >= 14 bytes) with a jump to `detour`. Returns a trampoline
// that calls the original, or NULL (hook off) if anything doesn't match.
// Windows: `expected` is ignored; the bytes checked and replaced (a 5-byte jump) are the address table's for
// `target` (syms_win.c).
void *install_hook(const char *name, void *target, void *detour, const uint8_t *expected, size_t len);
// Same, for bytes with relative operands (mov rax,[rip+disp32], lea, cmp [rip+..], call / jmp / jcc rel32, and
// jcc / jmp rel8): for each, in order, `disp` is its displacement's offset in `expected`, `next` the end of its
// instruction (next == disp + 1: a rel8 jump). macOS puts the trampoline within +-2 GB of `target`, recomputes each
// rel32 there and rewrites each rel8 jump as a rel32 one. A branch back into the replaced bytes is refused.
// Windows: the table entry describes its own operand, so this is install_hook.
typedef struct {
    uint8_t disp, next;
} HookRip;
void *install_hook_rip(const char *name, void *target, void *detour, const uint8_t *expected, size_t len,
                       const HookRip *rip, size_t nrip);
// hook_reloc.c: `code` (len bytes, copied from `from`) now runs at `to`: fix its RIP-relative disp32 (at rip_disp,
// instruction ending at rip_next; rip_disp 0: none) so it still reaches the same address. False if out of reach.
bool hook_relocate_rip(uint8_t *code, size_t len, size_t rip_disp, size_t rip_next, uintptr_t from, uintptr_t to);
// hook_reloc.c: copy `len` bytes of code from `from` to `out` (running at `to`, room for `cap` bytes), fixing every
// operand in `rip` (install_hook_rip's rules). Returns the copy's length (rel8 jumps grow), 0 if one can't be moved.
size_t hook_relocate(const uint8_t *code, size_t len, const HookRip *rip, size_t nrip, uintptr_t from, uint8_t *out,
                     size_t cap, uintptr_t to);
void *install_hook_with_trampoline(const char *name, void *target, void *detour, const uint8_t *expected, size_t len,
                                   const uint8_t *tramp_code, size_t tramp_len);
// Compare site: a type test inside a function (too deep for a function hook). `site` is one `cmp r32, [rip+disp32]`
// instruction (`expected`: its exact bytes, 6, or 7 with a REX prefix; not esp); afterwards fn(ud, the register's
// value, the memory operand's value) decides it: ZF is set when fn returns true. Only ZF is reproduced, so check that
// nothing after the site reads another flag (je / jne / sete / setne / cmove ... only). fn runs on the game's thread
// with an aligned stack; the volatile registers are kept. Windows: `site` is a syms_win.c entry, whose bytes are the
// instruction (`expected` is ignored), or the pair `mov r32, [rip+disp32]; cmp [base+disp8], r32` MSVC also emits (the
// value is then the field, the operand the constant). hook_cmp.c builds the stubs.
typedef bool (*HookCmpFn)(void *ud, unsigned value, unsigned operand);
bool install_cmp_site(const char *name, void *site, const uint8_t *expected, size_t len, HookCmpFn fn, void *ud);
int hook_cmp_register(const uint8_t *insn, size_t len); // the compared register (0-15), -1: not a supported cmp
int hook_cmp_pair(const uint8_t *insn, size_t len, int *base, int8_t *disp); // the pair form's register, or -1
size_t hook_cmp_stub(uint8_t *out, size_t cap, const uint8_t *insn, size_t len, uintptr_t site, uintptr_t at,
                     HookCmpFn fn, void *ud); // the stub's length, 0: unsupported or out of reach
size_t hook_cmp_stub_win64(uint8_t *out, size_t cap, const uint8_t *insn, size_t len, uintptr_t site, uintptr_t at,
                           HookCmpFn fn, void *ud); // the same for Windows (MS ABI fn; either form)
// Game symbol by mangled name (no leading underscore): exported via dlsym, else the executable's
// symbol table (private symbols). Windows (no symbols): the macOS name's entry in syms_win.c's address
// table, or its stand-in there for a function the Windows build inlined.
void *sym(const char *mangled);
// The game's general registers at a mid-function site (install_mid_hook, game_hook_site), in push order.
typedef struct {
    uint64_t rax, rcx, rdx, rbx, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15;
} Regs;
#ifdef _WIN32
// Mid-function hook (Windows, where MSVC inlined a function we'd hook on macOS): at `site` (named by a
// syms_win.c entry, whose bytes are replaced) fn runs with the game's registers, then the replaced
// instructions and the rest of the game's code. fn may read the registers (not change them).
bool install_mid_hook(const char *name, void *site, void (*fn)(const Regs *regs));
// Same, but fn may change the general registers: the game goes on with the Regs as fn leaves them (the flags and
// xmm0-5 are restored as they were).
bool install_mid_hook_rw(const char *name, void *site, void (*fn)(Regs *regs));

// syms_win.c: the game's addresses by macOS name (or a Windows-only name), for one Darkest.exe build.
// rva: offset from the image base; len/bytes: the code there (whole instructions), checked before use and replaced
// by a hook; native: our stand-in for a function MSVC inlined. rip_disp/rip_next: one relative operand in those bytes
// (a RIP-relative disp32 or a jcc / jmp rel32: its offset and the end of its instruction; a jcc / jmp rel8 when
// rip_next == rip_disp + 1, rewritten as rel32), fixed up where the bytes are copied (hook_relocate's rules).
typedef struct {
    const char *name;
    uint32_t rva;
    uint8_t len;
    uint8_t bytes[15];
    void *native;
    uint8_t rip_disp, rip_next;
} WinSym;
extern const WinSym g_win_syms[];
extern const int g_win_nsyms;
extern const uint32_t g_win_timestamp; // the PE header's TimeDateStamp of that build
bool win_game_supported(void);         // hook_win.c: is the running exe that build?
#endif
// push rbp; mov rbp,rsp; push r15; push r14; push r13; push r12; push rbx; sub rsp,imm32
#define PROLOGUE_SUB32(a, b)                                                                                           \
    {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,                                                       \
     0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, (a),  (b),  0x00, 0x00}
// same, with sub rsp,imm8
#define PROLOGUE_SUB8(a)                                                                                               \
    {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, (a)}

// ---------------------------------------------------------------- game_hooks.c (shared game hooks)
// The game functions clients may change, each hooked by the library (its catalogue, game_hooks.c: symbol and expected
// bytes) and wrapped by any number of clients. A wrapper has the game function's own signature and calls *next to go
// on (the wrapper inside it, or the game). Each wrapper belongs to the registering mod (mods_current) and is linked
// only while that mod is applied to the campaign: the library's innermost, then the mods from the lowest in the
// campaign's list to the highest (the top mod's outermost: a higher mod wins); one owner's later wrappers outside its
// earlier ones. The chain is relinked at each campaign load and close, so *next can change: read it at each call. The
// library installs a hook with its first wrapper: with none the game runs untouched. False (and *next NULL): unknown
// id or the hook is off.
bool game_hook_wrap(const char *id, void *fn, void **next);
// A catalogued mid-function site: each fn reads the game's registers there (not change them), before the site's own
// instructions run; only while its mod is applied, in the wrappers' order (the library's first, the top mod's last).
bool game_hook_site(const char *id, void (*fn)(const Regs *regs));
// Inside a site observer, Windows: the game's xmm<i> there (4 floats), which the observer may change (the game goes on
// with it; MSVC keeps values such as a draw cursor in xmm6-15). NULL on macOS (sites save no SSE register there).
float *game_site_xmm(int i);
// Inside a site observer: the game's general registers there, writable (the game goes on with them as the observer
// leaves them; the flags are restored). For an observer that must move a value the game keeps in a register.
Regs *game_site_regs(void);

// ---------------------------------------------------------------- game.c (functions and layout)
extern unsigned (*game_string_hash)(const char *, unsigned);
extern bool (*game_remove_quirk_with_class_id)(void *hero, unsigned class_hash, bool);
extern unsigned (*game_hero_get_guid)(const void *hero);
bool game_init(void);
bool is_hero(const void *actor);
bool is_monster(const void *actor);
// glob.c: whether s matches a glob pattern (* and ?; macOS fnmatch with no flags).
bool glob_match(const char *pattern, const char *s);

// Remove every buff on the actor whose id matches a glob pattern (glob_match); returns how many.
int actor_remove_buffs(void *actor, const char *const *patterns, int npatterns);
int actor_remove_buffs_by_source_ids(void *actor, unsigned source, const unsigned *source_ids, int nsource_ids);
unsigned hero_guid(const void *hero);
unsigned monster_guid(const void *monster);
unsigned hero_class_hash(const void *hero); // Hero+0x1298 -> HeroClass, id hash at +0xd0
unsigned skill_id_hash(const void *skill);  // ActorCombatSkill+0x4c
unsigned name_hash(const char *s);          // the game's string_hash(s, 0)
const void *find_effect(const char *name);  // RuleBook::FindEffect(&Rules, name), NULL if none

// ---------------------------------------------------------------- json.c (small reader)
typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;
typedef struct JVal {
    JType t;
    bool b;
    double num;
    char *str;
    int n;              // array / object size
    struct JVal *items; // array items / object values
    char **keys;        // object keys
} JVal;
JVal *json_parse(const char *text, char *err, size_t errlen);
void json_free(JVal *v);
const JVal *json_get(const JVal *obj, const char *key);

// ---------------------------------------------------------------- combat_context.c (macOS)
// A battle's state for rules and checks: the cheap values copied (CombatContext), accessors for the rest. Out of
// battle (in_battle false) the heroes are listed and there are no monsters.
#define CTX_MAX_HEROES   4
#define CTX_MAX_MONSTERS 8
typedef struct {
    void *actor;                     // the game's Actor (for the accessors below)
    bool hero;                       // a hero (else a monster)
    const char *class_id;            // hero or monster class id ("crusader", "cultist_brawler_A")
    int rank;                        // the front rank it holds, 1-4 (0: unknown)
    float hp, max_hp, hp_pct;        // hp_pct 0-100; max_hp with buffs
    float stress;                    // heroes (0 for monsters)
    bool alive, corpse, deaths_door; // deaths_door: a hero at 0 HP
    const char *trait;               // heroes: the affliction or virtue id, NULL: none
    bool afflicted, virtuous;
    int resolve_level; // heroes
} CtxActor;
typedef struct {
    void *raid, *battle;
    bool in_battle;
    int round;   // the battle's round, from 1 (0: no battle)
    float torch; // 0-100
    int nheroes, nmonsters;
    CtxActor heroes[CTX_MAX_HEROES], monsters[CTX_MAX_MONSTERS];
    const CtxActor *self; // the actor it's for (one of the above), or NULL
} CombatContext;
bool combat_context_build(CombatContext *ctx, const void *self); // false: no raid (or no game symbols)
float actor_stat(const void *actor, const char *stat);           // a combat stat with buffs (damage_low, damage_high,
                                                                 // defense_rating, protection_rating, speed_rating,
                                                                 // attack_rating, crit_chance, max_hp,
// riposte_on_hit_chance, riposte_on_miss_chance); NAN: unknown
bool actor_has_buff(const void *actor, const char *pattern); // buff ids, globs (glob_match)
int actor_marked(const void *actor);                         // marked (Actor::HasTag): 1, 0, -1: unknown
int actor_count_buffs(const void *actor, const char *pattern);
// An actor keeps at most max instances of buff id from one kind of source (effects.c, on Buff::Group::AddBuff): one
// more removes the oldest first, so a new application refreshes the stack. Gear buffs with the same id are another
// source and never count. Call again to change the limit. macOS only.
void buff_stack_limit(const char *id, int max);
bool hero_has_quirk(const void *hero, const char *pattern);   // quirk ids (diseases included), globs
bool hero_has_disease(const void *hero, const char *pattern); // NULL: any disease
bool hero_has_item(const void *hero, const char *pattern);    // equipped item ids (trinkets, registered slots), globs
// Expression bindings over a CombatContext (expr_eval_fn(text, combat_context_lookup, combat_context_call, &ctx, ...));
// the names and calls are listed in combat_context.c and the guide.
double combat_context_lookup(const char *name, bool *ok, void *ctx);

// ---------------------------------------------------------------- expr.c
// Evaluate a formula: numbers (70% = 0.7), field names, + - * / and brackets; also conditions: < <= > >= == !=,
// && || !, giving 1 or 0 (non-zero is true), and calls name(args) whose arguments are expressions or "strings".
// lookup(name) gives a name's value, fn a call's (expr_eval: no calls); either sets *ok false for an unknown name.
// Returns false (and logs) on a syntax error or an unknown name.
#define EXPR_MAX_ARGS 4
typedef struct {
    bool is_str;
    double num;
    char str[64];
} ExprArg;
bool expr_eval(const char *text, double (*lookup)(const char *name, bool *ok, void *ctx), void *ctx, double *out);
bool expr_eval_fn(const char *text, double (*lookup)(const char *name, bool *ok, void *ctx),
                  double (*fn)(const char *name, const ExprArg *args, int nargs, bool *ok, void *ctx), void *ctx,
                  double *out);
double combat_context_call(const char *name, const ExprArg *args, int nargs, bool *ok, void *ctx); // combat_context.c

// ---------------------------------------------------------------- config.c
#define MAX_FIELDS 64
int field_count(void);
const char *field_name(int i);
int field_find(const char *name);
int field_register(const char *name);
void config_load_all(void);
// per class
typedef struct ClassConfig ClassConfig;
const ClassConfig *config_for_hero(const void *hero);
const ClassConfig *config_for_class_hash(unsigned class_hash);
bool config_skill_allowed(const ClassConfig *c, const void *hero, unsigned skill_hash);
// Item use limits (config "items"): -1 no class configures the item, 0 not for this hero (or not in battle),
// 1 allowed.
int config_item_allowed(const void *hero, unsigned item_hash, bool in_battle);
// Skill "dmg" formula for this hero, if the class configures one for the skill (fraction, e.g. -0.3).
bool config_skill_dmg(const ClassConfig *c, const void *hero, unsigned skill_hash, double *out);
// Skill "heal" formula (heal points).
bool config_skill_heal(const ClassConfig *c, const void *hero, unsigned skill_hash, double *out);
// Effect "heal" formula (config "effects"), evaluated on the performer's fields.
bool config_effect_heal(const ClassConfig *c, const void *hero, const char *effect_name, double *out);
bool config_apply_action(const ClassConfig *c, const void *hero, const char *effect_name);
// Animation variants: fill template i ("{field}" -> value, enum fields by name) into out.
// Returns false when there's no template i.
bool config_any_formulas(void);
// Effect variants: if effect `name` has a template for this class, fill it into out and return true.
bool config_effect_variant(const ClassConfig *c, const void *hero, const char *name, char *out, size_t outlen);
bool config_animation_variant(const ClassConfig *c, const void *hero, int i, char *out, size_t outlen);
// Tooltips: fill the [[...]] tokens of `in` for this hero (may be NULL) and skill; false if there are none.
// vars: extra names (e.g. the skill's own stats), used when no field or formula has the name.
typedef struct {
    const char *name;
    double value;
} TipVar;
// with: a field change ("hp+1") applied to tokens without their own @ (may be NULL).
bool config_tooltip_fill(const ClassConfig *c, const void *hero, unsigned skill_hash, const TipVar *vars, int nvars,
                         const char *with, const char *in, char *out, size_t outlen);
// The name line template ("[[name]] ([[rank]])") for the skill, or NULL.
const char *config_tooltip_title(const ClassConfig *c, unsigned skill_hash);
// The skill's config "tooltip" lines whose conditions hold for the hero (may be NULL); returns how many.
// replace: the lines take the place of the game's info text.
typedef struct {
    const char *text, *font, *with;
} TipOut;
int config_skill_tooltip(const ClassConfig *c, const void *hero, unsigned skill_hash, TipOut *out, int max,
                         bool *replace);
// Base (not live) lines: the skill's tooltip lines without "with" or "base": false, conditions read with no hero and
// `field` (-1: none) as `value`.
int config_skill_tooltip_base(const ClassConfig *c, unsigned skill_hash, int field, int value, TipOut *out, int max,
                              bool *replace);
// The field a skill needs a minimum of ("hp_min": 1) and its range up to the field's limit, for base tooltips that
// list each value; false if none.
bool config_skill_base_range(const ClassConfig *c, unsigned skill_hash, int *field, int *lo, int *hi);

// Cooldowns (config "cooldowns"; skills' "cooldown"): turns left before the skill can be used (0 = ready).
int config_cooldown_left(const ClassConfig *c, const void *hero, unsigned skill_hash);
void config_cooldown_start(const ClassConfig *c, const void *hero, unsigned skill_hash, bool own_turn);
void config_cooldown_tick(const ClassConfig *c, unsigned guid, bool actor); // actor: an actor's turn started
void config_cooldown_reset(const ClassConfig *c, unsigned guid);
bool config_any_cooldowns(void);
// Global "act_outs" section (any engine.json): allow_back_to_back: the game's no-repeat check of turn-start act-outs
// is off (act_outs.c), so the same act-out may follow itself, across battles too. chances: per source id (a trait:
// affliction or virtue; or a quirk), its act-outs' percentages, replacing the game's weights (act_outs.c rolls).
#define ACT_OUT_MAX_TRAITS 32
#define ACT_OUT_MAX_TYPES  32
typedef struct {
    char type[32];
    float weight;
} ActOutWeight;
typedef struct {
    char trait[32];
    unsigned trait_hash;
    int n;
    ActOutWeight w[ACT_OUT_MAX_TYPES];
} ActOutChances;
// prerequisites (per_source): {"<source>": {"match": "all" | "any", "requires": [{"source": id, "within": "turn" |
// "battle" | "expedition", "nothing_counts": bool}]}}: the source rolls only once its prerequisite sources have fired
// (given an act-out; nothing_counts: rolling nothing counts too) within that span, for the same hero (act_outs.c).
#define ACT_OUT_MAX_PREREQ_SOURCES 32
#define ACT_OUT_MAX_PREREQS        8
enum { ACT_OUT_WITHIN_TURN, ACT_OUT_WITHIN_BATTLE, ACT_OUT_WITHIN_EXPEDITION };
typedef struct {
    char source[64];
    int within;
    bool nothing_counts;
} ActOutPrereq;
typedef struct {
    char source[64];
    bool any;
    int n;
    ActOutPrereq req[ACT_OUT_MAX_PREREQS];
} ActOutPrereqs;
// rules: [{"source": glob, "act_out": glob, "when": condition, "weight": factor or formula}] (act_outs.c registers them)
#define ACT_OUT_MAX_JSON_RULES 64
typedef struct {
    char source[64], act_out[64], when[256], weight[256];
    char file[128];
} ActOutJsonRule;
// types: {"<id>": {"like": vanilla id, "result": 0-2, "bark_chance": 0-1, "burndown": seconds, "outcome": "heal" |
// "effect", "target": ...}}: new act-out types (act_outs.c registers them; act_out_type_register below)
#define ACT_OUT_MAX_JSON_TYPES 16
typedef struct {
    char id[32], like[32], outcome[16], target[32];
    int result;
    float bark_chance, burndown;
    char file[128];
} ActOutJsonType;
typedef struct {
    bool allow_back_to_back;
    bool use_max;     // each act-out at its highest source's weight (act_outs.c)
    bool per_source;  // each source rolls its own act-out, all of them run
    int max_per_turn; // per_source: at most this many a turn (0: no limit)
    int ntraits;
    ActOutChances traits[ACT_OUT_MAX_TRAITS];
    int nrules;
    ActOutJsonRule rules[ACT_OUT_MAX_JSON_RULES];
    int nprereqs;
    ActOutPrereqs prereqs[ACT_OUT_MAX_PREREQ_SOURCES];
    int ntypes;
    ActOutJsonType types[ACT_OUT_MAX_JSON_TYPES];
} ActOutConfig;
const ActOutConfig *config_act_outs(void);
// Global "riposte" (riposte.c): {"skills": ["melee"], "no_skill_bark": "<string id>"}. With "skills", a hero's
// riposte is one of its own usable skills of those types (single target, able to hit the attacker) instead of its
// class's riposte_skill; the bark plays when a riposte would have fired but no skill qualifies. Any file; the last
// file read wins for each key.
#define RIPOSTE_MAX_TYPES 4
typedef struct {
    int ntypes;                        // 0: vanilla ripostes
    char types[RIPOSTE_MAX_TYPES][16]; // skill .type values ("melee", "ranged")
    char no_skill_bark[64];            // a string id; "": no bark
} RiposteConfig;
const RiposteConfig *config_riposte(void);
const ClassConfig *config_class_at(int i);                                  // NULL past the last class
const char *config_cooldown_tooltip(const ClassConfig *c, int left);        // the line for `left` turns, or NULL
int config_cooldown_turns(const ClassConfig *c, unsigned skill_hash);       // the skill's cooldown length (0: none)
const char *config_cooldown_ready_tooltip(const ClassConfig *c, int turns); // the ready line, or NULL
// Skill bar paging (config "skill_pages"): skills per page (0 = no paging); tab icons: 0 prev, 1 next,
// 2 prev disabled, 3 next disabled ("" if not given).
int config_page_size(const ClassConfig *c);
const char *config_page_icon(const ClassConfig *c, int i);
// Guild layout (skills' "guild_row"): the skill's row (1 = top, 0 = none); order: its place in the config's
// skills section (for sorting within a row).
int config_guild_row(const ClassConfig *c, unsigned skill_hash, int *order);
int config_guild_first_rank(const ClassConfig *c, unsigned skill_hash);
bool config_skill_hidden(const ClassConfig *c, unsigned skill_hash);
int config_sheet_page(const ClassConfig *c, unsigned skill_hash);
bool config_guild_row_centred(
    const ClassConfig *c,
    int row); // "guild_centred_rows" has it             // "sheet_page", else "guild_row" (0: none)            // skill "hidden": true   // skill's "guild_first_rank" (default 1)

// ---------------------------------------------------------------- herodata.c
int herodata_get(unsigned guid, int field); // 0 if unset
void herodata_set(unsigned guid, int field, int value);
bool herodata_has(unsigned guid, int field);
void herodata_foreach(void (*fn)(unsigned guid, void *ctx), void *ctx); // every hero with data
void herodata_install(void);                                            // Hero::Save / Hero::Restore hooks
void herodata_on_restore(void (*fn)(void *hero));    // after a hero's fields are restored (up to 4 clients)
int monsterdata_get(const void *monster, int field); // 0 if unset
void monsterdata_set(const void *monster, int field, int value);
void monsterdata_install(void); // Monster::Save / Monster::Restore hooks
// itemdata.c: per-item fields for inventory items that never stack (trinkets), keyed by an item id (iid) the library
// assigns and keeps in the item's tags; saved in the item's own block as ddx_iid and ddx_<field>. macOS only.
int itemdata_id(const void *item);             // 0: none
unsigned itemdata_type_hash(const void *item); // Inventory::Item type id hash
unsigned itemdata_item_hash(const void *item); // Inventory::Item item id hash
int itemdata_ensure_id(void *item);            // the iid, assigned if none (0: the item can stack, refused)
int itemdata_get(const void *item, int field); // 0 if unset
bool itemdata_has(const void *item, int field);
bool itemdata_set(void *item, int field, int value); // assigns an iid if needed; false if refused
typedef void (*ItemSaveHook)(void *item);
void itemdata_on_save(ItemSaveHook fn); // runs before each item is saved (up to 4)
void itemdata_enable(void);             // a client needs item data (before itemdata_install)
void itemdata_install(void);            // Inventory::Item::Save / Restore hooks, if enabled

// ---------------------------------------------------------------- ui.c (shared UI helpers)
typedef struct {
    float x, y;
} Vec2; // the game's vec2 (macOS: packed in xmm0; Windows: in a GPR)
#ifdef _WIN32
typedef struct {
    uint8_t buf[0x38];
    void *f;
} StdFunction; // MSVC std::function<void()>
#else
typedef struct {
    uint8_t buf[0x20];
    void *f;
} StdFunction; // libc++ std::function<void()>
#endif
// A std::function<void()> that calls call(data) (up to 16 bytes of data, copied).
void ui_function(StdFunction *out, void (*call)(const uint8_t *data), const void *data, size_t n);
#ifdef _WIN32
// Destroy a by-value std::function argument not passed on (MSVC: the callee destroys its arguments).
void ui_function_destroy(StdFunction *f);
// Copy one, to pass a by-value std::function argument to more than one call (each callee destroys its own).
void ui_function_copy(StdFunction *dst, const StdFunction *src);
#endif
// Draw text at pos (current transform) with a font_ref id and colour id (NULL: leave as is).
void ui_text(Vec2 pos, const char *s, const char *font, const char *colour);
// The GUI's current colour (GUI_Colour(rgba), 0xffffffff white): pictures and text queued after it take it. Game code
// can leave it tinted (a hidden text's colour); a panel body sets white before drawing.
void ui_colour(uint32_t rgba);
// The same with the text's anchor (the LayoutAnchor at TextParameters 0x110: 0-2 top left / centre / right of pos,
// 3-5 the same with pos on the text's middle line, 6-8 on its bottom) and a
// wrap width (TextParameters 0x100; 0: no wrapping).
void ui_text_anchored(Vec2 pos, const char *s, const char *font, const char *colour, int anchor, float width);
// Text with inline pictures, wrapped at width, each line from pos.x or centred on it (top at pos.y): "{icon|<path>}" (a
// texture) or "{currency|<id>}" (the estate currency's icon), drawn icon square with gap either side, on the line's
// middle. draw false only measures. Returns the height. ui_rich_has_icons: the text holds a token.
float ui_rich_text(Vec2 pos, const char *s, const char *font, const char *colour, float width, float icon, float gap,
                   bool centred, bool draw);
bool ui_rich_has_icons(const char *s);
// GUI_ImageButton clients: before may change the button's parameters and return true to use `click` (which it
// fills, e.g. with ui_function) in place of the game's click; after runs once the button is drawn.
typedef bool (*UiButtonBefore)(void *params, StdFunction *click);
typedef void (*UiButtonAfter)(const void *params);
void ui_on_image_button(UiButtonBefore before, UiButtonAfter after); // register before ui_install
bool ui_image_button(const void *params, StdFunction *click);        // draw an extra button (no clients)
// The same clicked within pos / size (current transform) rather than the texture's own size from its position (the
// game's hit rect whatever the drawn size). False: not drawn.
bool ui_image_button_rect(const void *params, StdFunction *click, Vec2 pos, Vec2 size);

// ---------------------------------------------------------------- game types
// (The game's structure offsets, ITEM_ID and the rest: game_layout.h.)
// Localization::LocalizedString (returned in rax:rdx): the text, then length and flags.
typedef struct {
    const char *text;
    uint64_t info;
} GameLocString;

// ---------------------------------------------------------------- ui_draw.c (drawing for panels; docs/11)
// Parameters the game's GUI calls take (ImageParameters / ImageButtonParameters), on the stack.
#define UI_PARAMS_SIZE 0x400
typedef struct {
    _Alignas(16) uint8_t b[UI_PARAMS_SIZE];
} UiParams;
// A texture by path (relative to the game's data folder; a mod's files override the game's), loaded once and kept.
// out: the game's ResourceHandle<Texture> (16 bytes). False: missing (logged once).
bool ui_texture(const char *path, uint8_t out[16]);
// Every texture the game loads by path (TextureFactory::CreateTexture) passes through the registered redirects first:
// fn returns the path to load instead (it may write it into buf, cap bytes) or NULL to keep the path. For a file the
// game loads but a mod never shows, or shows from elsewhere, so the mod needn't ship it. Up to 8, first match wins;
// register before ui_draw_install (no redirects: no hook).
typedef const char *(*UiTextureRedirect)(const char *path, char *buf, size_t cap);
void ui_texture_redirect(UiTextureRedirect fn);
void ui_draw_install(void);
// Pictures in the current transform, at pos (top left). ui_image / ui_image_sized are drawn in the GUI's order, with the
// panel (later calls on top); ui_image_now at once, for code running inside another draw (a tooltip line, a callback).
// size {0, 0}: the texture's own.
void ui_image(Vec2 pos, const uint8_t tex[16]);
void ui_image_sized(Vec2 pos, const uint8_t tex[16], Vec2 size);
bool ui_texture_size(const uint8_t tex[16], Vec2 *size); // the texture's own size in pixels (false: none)
#ifdef _WIN32
// A counted copy of a ResourceHandle<Texture> (the game's copy constructor), for a by-value handle argument: MSVC's
// callee releases it, so each call needs its own (ui_texture's handles included).
void ui_texture_copy(uint8_t dst[16], const uint8_t src[16]);
#endif
// The game's LayoutAnchor: which point of a rect a position names (the eight compass points and the centre).
typedef enum {
    UI_ANCHOR_TOP_LEFT,
    UI_ANCHOR_TOP,
    UI_ANCHOR_TOP_RIGHT,
    UI_ANCHOR_LEFT,
    UI_ANCHOR_CENTRE,
    UI_ANCHOR_RIGHT,
    UI_ANCHOR_BOTTOM_LEFT,
    UI_ANCHOR_BOTTOM,
    UI_ANCHOR_BOTTOM_RIGHT
} UiAnchor;
Vec2 ui_anchor_point(UiAnchor a, Vec2 size); // that point of a size-sized rect, from its top left
void ui_image_now(Vec2 pos, const uint8_t tex[16], Vec2 size);
bool ui_image_path(Vec2 pos, const char *path); // ui_texture + ui_image (false: missing)
// A Spine sprite (SpineSprite*) queued like ui_image, its origin at pos; its animation is the owner's to advance.
bool ui_sprite(Vec2 pos, const void *sprite);
// The game's trinket sparkle (fx/trinket_sparkle, as the item grids draw on Ancestral and Trophy trinkets around a
// 72 x 144 icon, origin at the icon's centre): kind 0 boss_sparkle, 1 ancestral_sparkle; index picks one of the
// game's copies (they pulse out of step). NULL outside a campaign.
const void *ui_trinket_sparkle(int kind, unsigned index);
// A clickable picture (GUI_ImageButton, hover and click sounds as the game's): on_click(data) runs when it's clicked
// (data: up to 16 bytes, copied). id: any number unique on the screen (it keys the hover state). enabled false: drawn,
// no hover or click. ui_button_keep leaves its parameters in *p for code that needs them (a drop target on the same
// rect); free them with ui_params_free.
bool ui_button(Vec2 pos, const uint8_t tex[16], unsigned id, bool enabled, void (*on_click)(const uint8_t *data),
               const void *data, size_t n);
bool ui_button_sized(Vec2 pos, Vec2 size, const uint8_t tex[16], unsigned id, bool enabled,
                     void (*on_click)(const uint8_t *data), const void *data, size_t n);
bool ui_button_keep(UiParams *p, Vec2 pos, const uint8_t tex[16], unsigned id, bool enabled,
                    void (*on_click)(const uint8_t *data), const void *data, size_t n);
void ui_params_free(UiParams *p);
// A drop target: a rect that takes things dragged onto it (items, heroes ...). Init once (keep the struct alive), then
// call ui_drop_target every frame it should be live, in the current transform. accepts: a mask of the drag types it
// takes (bit = the game's drag type id). on_drop returns 0 to refuse (the drag goes back where it came from).
typedef struct UiDropTarget UiDropTarget;
typedef uint64_t (*UiDropFn)(UiDropTarget *t, const void *pos, void *drag);
struct UiDropTarget {
    void **vtbl;
    uint32_t accepts;
    uint8_t game[4];
    bool enabled;
    uint8_t reserved[0x40 - 0x11]; // the game's DropContext
    UiDropFn on_drop;
    void *ctx; // the client's
};
void ui_drop_target_init(UiDropTarget *t, UiDropFn on_drop, void *ctx);
void ui_drop_target(UiDropTarget *t, uint32_t accepts, Vec2 pos, Vec2 size);
// The game's own contexts for a rect this frame (current transform), as a drawn widget registers them: drag (a
// DragContext: pressing there starts a drag) and drop (a DropContext); either may be NULL. For a game object that is
// both (an inventory slot interface: DragContext at +0, DropContext at +0x10) drawn somewhere of the client's choosing.
void ui_contexts(void *drag, void *drop, Vec2 pos, Vec2 size);
// Localized text: the string table entry (a mod's .loc2 included), or the id itself when there's none.
const char *ui_loc(const char *id);
GameLocString ui_loc_string(const char *id); // the entry as the game hands it to GUI_Text
// ui_font.c: the TextureFont for a font_ref id (FontLibrary::GetFontFromId), mods' fonts included: a mod's
// fonts/*.fonts.darkest (vanilla fonts.darkest syntax) is added to the game's fonts the first time one of its refs
// is asked for once the mod is loaded. NULL: unknown (or not loaded yet).
const void *ui_font(const char *id);
// The width of text in a font (a font_ref id), as the GUI measures it (0: unknown font).
float ui_text_width(const char *font, const char *s);
// The height of text in a font wrapped at width (0: not wrapped), as the GUI lays it out (0: unknown font).
float ui_text_height(const char *font, const char *s, float width);
// ui_loc(id) with each {key} replaced by vals[i] for keys[i].
void ui_format(char *out, size_t cap, const char *id, const char *const *keys, const char *const *vals, int n);

// ---------------------------------------------------------------- ui_text.c (one owner of GUI_Text, for filters)
// Every GUI_Text call (both overloads: char const* and LocalizedString) passes through the registered filters before
// it's drawn. caller: the call's return address (to pick out one call in a known function). Return UI_TEXT_KEEP,
// UI_TEXT_SKIP (not drawn) or UI_TEXT_REPLACE with *replacement set (drawn with the same parameters).
typedef enum { UI_TEXT_KEEP, UI_TEXT_SKIP, UI_TEXT_REPLACE } UiTextAction;
typedef UiTextAction (*UiTextFilter)(const void *caller, const char *text, const char **replacement);
void ui_on_text(UiTextFilter fn); // up to 8; register before ui_text_install
// A string table entry drawn as another wherever the GUI draws it: replacement() gives the id to draw instead, "" to
// hide it, or NULL to keep the original (decided each time it's drawn). Up to 16.
void ui_text_substitute(const char *id, const char *(*replacement)(void));
void ui_text_install(void);

// ---------------------------------------------------------------- ui_tooltip.c (tooltips)
// A tooltip (GuiTooltipParameters, as the game's Create*Tooltip functions build them) drawn as part of the current
// panel, in its widget order: later controls and dialogs cover it (GUI_Tooltip's tooltips are drawn over everything).
// Takes ownership of tip (the caller doesn't destroy it); false: not drawn (the caller still owns it).
bool ui_tooltip_in_panel(void *tip);
// Before GUI_Tooltip shows a tooltip (GuiTooltipParameters; its Text at +0x10): ret is the call's return address
// (call_site_return tells which game code), caller the calling function's frame (its rbp). Register before
// ui_tooltip_install; up to 4 clients.
typedef void (*TooltipShowHook)(void *params, const void *ret, const uint8_t *caller);
void ui_tooltip_on_show(TooltipShowHook fn);
// A tooltip's lines (GuiTooltipParameters, e.g. from Inventory::Display::CreateItemTooltip) drawn as panel content,
// as the tooltip draws them but with no box: lines first .. first + count - 1 (count < 0: to the end), from pos (top
// left of the text area, current transform), wrapped at width, in the GUI's order (clipped by the active clip). Line
// icons and badges are drawn as in a tooltip. Returns their height; the caller keeps (and frees) tip.
float ui_tooltip_lines(const void *tip, int first, int count, Vec2 pos, float width);
float ui_tooltip_lines_height(const void *tip, int first, int count, float width);
// Lines of text (a font from ui_font, a colour from ui_colour_hvec4, encoded markup in the text) drawn the same way:
// from pos (top left of the text area), wrapped at width, each line under the last, in the anchor's column (0 left,
// 1 centred, 2 right). The text must live until the frame ends. Line icons and badges registered for a line's text are
// drawn with it. Returns the lines' height.
typedef struct {
    const void *font;
    uint64_t colour;
    const char *text;
} UiTextLine;
float ui_text_lines(const UiTextLine *lines, int n, Vec2 pos, float width, int anchor);
float ui_text_lines_height(const UiTextLine *lines, int n, float width);
uint64_t ui_colour_hvec4(const char *id); // a colours.darkest id's colour, as text lines take it
// The same lines drawn at once (for code running inside another draw: a tooltip's content line, a draw callback).
float ui_text_lines_now(const UiTextLine *lines, int n, Vec2 pos, float width, int anchor);
// The widest of the lines as a tooltip measures them (unwrapped; this frame's line icons and badges counted).
float ui_text_lines_width(const UiTextLine *lines, int n);
// fn(data) (up to 16 bytes, copied) runs in the current panel, in its widget order and transform (GUI_Callback): for
// code that draws at once (ui_image_now, ui_text_lines_now). False: not queued.
bool ui_draw_callback(void (*fn)(const uint8_t *data), const void *data, size_t n);
// A picture of size (0: its own) centred on centre, drawn at once, with text centred on it in a font (a font_ref id;
// NULL: no text) and colour (0xAABBGGRR), moved by text_offset.
void ui_badge_now(const uint8_t tex[16], Vec2 size, Vec2 centre, const char *text, const char *font, uint32_t rgba,
                  Vec2 text_offset);
// This frame, the tooltip line whose string is `line` is content its owner draws: it measures size (so the game sizes,
// places and clamps the box around it), and where its text would be drawn, draw(pos, size, ud) runs instead (pos: the
// content's top left, in the tooltip's transform; draw at once). Up to 8 a frame; false: refused (full, or not hooked).
bool ui_tooltip_content_line(const char *line, Vec2 size, void (*draw)(Vec2 pos, Vec2 size, void *ud), void *ud);
// This frame, a tooltip line whose string is `line` (the pointer a line got from GuiTooltipParameters::Text::Line::
// SetString) gets an icon at each end, gap units from the text; the tooltip is measured with room for them.
void ui_tooltip_line_icons(const char *line, const char *icon_path, float gap);
// This frame, the tooltip whose line `line` is (its first line: the name) gets a badge at its top right: a picture,
// size big (0: its own size), centred on the corner of the text area moved by offset, with text centred on it in a font
// (a font_ref id) and colour (0xAABBGGRR), moved by text_offset (a font's glyphs may sit off centre); the line measures
// wider so the box keeps the badge clear of it. For example the item's resolve level on the game's resolve disc.
void ui_tooltip_badge(const char *line, const char *icon_path, Vec2 size, Vec2 offset, const char *text,
                      const char *font, uint32_t rgba, Vec2 text_offset);
// A badge inside a tooltip line's text: the line's byte `at` (a space) is widened to room for the icon (size, gap each
// side) in the line's font, and the icon drawn there, centred on the line's first row and moved by offset, with text on
// it as above. For example "Needs resolve (disc) to equip".
void ui_tooltip_inline_badge(const char *line, size_t at, const char *icon_path, Vec2 size, Vec2 offset, float gap,
                             const char *text, const char *font, uint32_t rgba, Vec2 text_offset);
// fn runs once a frame, after the frame's tooltips are drawn (the GUI's last pass). Up to 8.
void ui_on_frame_end(void (*fn)(void));
// An empty GuiTooltipParameters (UI_TIP_SIZE bytes, 16-aligned), as the game builds one inline before a Create*Tooltip
// call; free it with ui_tooltip_params_free.
#define UI_TIP_SIZE 0x570
void ui_tooltip_params_init(void *tip);
void ui_tooltip_params_free(void *tip);
// An inventory item's tooltip, as the inventories show it on hover (Inventory::Display::CreateItemTooltip for actor,
// at pos in the current transform, then GUI_Tooltip). False: not shown (no item class).
bool ui_item_tooltip(const void *actor, const uint8_t *item, Vec2 pos);
void ui_tooltip_install(void);

// ---------------------------------------------------------------- ui_input.c (mouse and modifier keys, from SDL)
// The mouse as SDL sees it, below the game's GUI (whose controls can take a press before your code sees it). Sampled
// once a frame, at its first use; pressed / released are true in the frame it happened, with where (GUI coordinates)
// and which modifiers were held at that moment.
#define UI_MOUSE_LEFT   0
#define UI_MOUSE_RIGHT  1
#define UI_MOUSE_MIDDLE 2
#define UI_MOD_SHIFT    1u
#define UI_MOD_CTRL     2u
#define UI_MOD_ALT      4u
#define UI_MOD_CMD      8u
typedef struct {
    bool down, pressed, released;      // held now; pressed / released since the last frame
    Vec2 press_pos, release_pos;       // the last press's and release's position
    unsigned press_mods, release_mods; // UI_MOD_* held at the last press / release
} UiMouseButton;
const UiMouseButton *ui_mouse(int button);
Vec2 ui_mouse_pos(void);
unsigned ui_modifiers(void);           // UI_MOD_* held now
float ui_mouse_wheel(void);            // the wheel this frame (y; > 0 away from the user, up)
float ui_mouse_wheel_take(void);       // the same, and no one else gets it this frame (a scroll area uses it)
Vec2 ui_mouse_local(void);             // the mouse in the current transform (as GUI_MouseOver)
bool ui_mouse_in(Vec2 pos, Vec2 size); // the mouse is in the rect (current transform) and the active clip
bool ui_click(int button, float slop); // released this frame within slop of where it was pressed
// macOS: SDL reports Ctrl + left click as a right click (the one-button Mac habit), so a Ctrl-click handler should take
// either button. The button (UI_MOUSE_LEFT or _RIGHT) pressed with Ctrl this frame, or -1; and the same for a click
// (released within slop, Ctrl held at both ends).
int ui_ctrl_pressed(void);
int ui_ctrl_click(float slop);
void ui_input_install(void); // after ui_tooltip_install (it counts the frames)
unsigned ui_frame(void);     // the frames drawn so far (ui_input.c)

// ---------------------------------------------------------------- ui_confirm.c (every question dialog)
// A question with options in a row of buttons along the bottom; the library draws every confirm dialog (the game's too:
// a frame sized to the question, layout in dialog.layout.json "confirm"). The question: a string id, or text. An option: key (handed to its callback),
// label (a string id, or text), fn (NULL: just closes). data: up to UI_CONFIRM_DATA bytes, copied and handed back.
// ui_confirm_yes_always_no: the default set, keys "yes" / "always" / "no" (strings engine_confirm_yes / _always / _no),
// all to fn; a caller keeps its own "always". Picked once: Enter / Esc pick a lone option, as the game's.
#define UI_CONFIRM_MAX  8
#define UI_CONFIRM_DATA 16
typedef void (*UiConfirmFn)(const char *key, const uint8_t *data);
typedef struct {
    const char *key, *label;
    UiConfirmFn fn;
} UiConfirmOption;
void ui_confirm(const char *text, const UiConfirmOption *opts, int n, const void *data, size_t len);
void ui_confirm_yes_always_no(const char *text, UiConfirmFn fn, const void *data, size_t len);
// ui_confirm drawn with dialog.layout.json section `section` (NULL: "confirm"): each name it lacks comes from "confirm",
// and its "blocks" (in the file's order, above the question text) add lines of text in their own font and colour
// ({"text": string id, "font", "colour", "width", "gap"}) and pictures ({"image": path, "size", "gap"}).
void ui_confirm_styled(const char *section, const char *text, const UiConfirmOption *opts, int n, const void *data,
                       size_t len);
bool ui_confirm_busy(void); // a question dialog (the game's or ours) is up or queued
// A "lines" block ({"lines": "<list name>", "font", "colour", "line_gap", "bullet": string id before each line,
// "heading": string id, "heading_font",
// "heading_colour", "heading_gap", "width", "gap"}) draws the lines a named list gives when the dialog is drawn: fn
// fills up to max lines (text already localized; font / colour NULL: the block's) and returns how many. Up to 8 lists.
typedef struct {
    const char *text, *font, *colour;
} UiConfirmLine;
typedef int (*UiConfirmLinesFn)(UiConfirmLine *out, int max);
void ui_confirm_lines(const char *name, UiConfirmLinesFn fn);
void ui_confirm_install(void); // ConfirmDialog::System::ShowEntry's one owner

// ---------------------------------------------------------------- ui_dialog.c (a dialog frame at any size)
// A frame drawn from pieces <art>_<name>.png: a nine-slice (corners tl, tr, bl, br at their own size; edges l, r, b and
// the middle c stretched; the top edge in two, tw and te, either side of an ornament centred on it at its own size, or
// tw alone without one) over splatter pieces splat_tl ... at their own size, each at an offset from its corner. The
// numbers are a layout file's section `style`: "art" (the path before _<name>.png), "border" {left, top, right, bottom},
// "ornament" (its size; absent: none), "splatter" {tl, tr, bl, br: offsets; show: 0 hides it}. The frame's rect is pos
// (top left) and size in the current transform, drawn in the GUI's order (later controls and tooltips lie over it).
typedef struct UiLayout UiLayout;
void ui_dialog_frame(UiLayout *l, const char *style, Vec2 pos, Vec2 size);
// Where a dialog of a size goes: centred on the screen, centred on a point, or its top left at a point (the point in the
// current transform). Returns its top left. ui_place_parse: "screen" (or anything else), "centre", "top_left".
typedef enum { UI_PLACE_SCREEN, UI_PLACE_CENTRE, UI_PLACE_TOP_LEFT } UiPlace;
UiPlace ui_place_parse(const char *s);
Vec2 ui_dialog_place(UiPlace place, Vec2 point, Vec2 size);
Vec2 ui_screen_centre(void);                               // the screen's centre in the current transform
void ui_screen_bounds(Vec2 *top_left, Vec2 *bottom_right); // screen rect in the current transform

// ---------------------------------------------------------------- ui_layout.c (layout files)
// A JSON layout file: positions and sizes by name ("anvil": {"pos": [x, y], "size": 64} -> "anvil.pos", "anvil.size").
// Loaded on first use and again whenever the file changes (checked at most once a second), so a layout can be edited
// while the game runs. A missing name logs once and gives the fallback.
typedef struct UiLayout UiLayout;
UiLayout *ui_layout_load(const char *path); // absolute path; NULL only with too many files
UiLayout *ui_layout_mod(
    const char *
        file); // paths.c: the active mods' engine/ui/<file> (highest priority first), then the library package's, chained per name and again per campaign; NULL if none
bool ui_layout_chain(UiLayout *l,
                     UiLayout *next); // names l lacks are looked up in next (and its chain); false if it would loop
float ui_layout_num(UiLayout *l, const char *name, float fallback);
Vec2 ui_layout_vec2(UiLayout *l, const char *name, Vec2 fallback);
const char *ui_layout_str(UiLayout *l, const char *name, const char *fallback);
// The keys of the object at name, in the file's order (up to max; they live until the file reloads): 0 if none.
int ui_layout_keys(UiLayout *l, const char *name, const char **keys, int max);
bool ui_layout_has(UiLayout *l, const char *name); // for optional names (no log when missing)

// ---------------------------------------------------------------- callsite.c
// The return address of the call at fn + off if it's a `call rel32` (e8) to target (target NULL: any); else NULL. For
// telling one call of a known function apart (a filter's caller), checked so a game update turns it off, not wrong.
const uint8_t *call_site_return(const void *fn, unsigned off, const void *target);

// ---------------------------------------------------------------- features
void effects_install(void); // effects.c
void ugc_install(void);     // ugc.c: local mod list fixes (titles, unique ids)
// campaigndata.c: integer fields of the whole campaign by name (up to 32, names under 40 characters), saved with
// it (persist.game.json "profile_options", as ddx_<name>). A field is read back only once its name is known: call
// campaigndata_field before a campaign loads (a client's init). Unset fields read 0. Values clear when a campaign
// ends, and before another's are read. macOS only.
void campaigndata_field(const char *name);
int campaigndata_get(const char *name);
bool campaigndata_has(const char *name);
void campaigndata_set(const char *name, int value);
void campaigndata_install(void);
// announce.c: the library's announcement, when a campaign first runs with the library (campaign field "announced")
// and again whenever a mod built on it is added to the campaign (each registered mod applied to it: its field
// mod_<name hash>; the list marks it new); only while the library package is applied to the campaign too;
// on the first screen ready for the player: the Old Road or any expedition (raid_ready: exploring, or a hero's turn in battle), or
// the Hamlet (its estate bar drawn, not fading, no building, news, town event or question dialog open). Text, fonts,
// colours and pictures: dialog.layout.json "announce" (enabled 0: never shown). Before events_install (its
// raid_ready listener).
void announce_install(void);
// A mod built on the library names itself for the announcement's list ("ddx_mods"): name_id is a string id in the
// mod's own table (localized when shown; colour markup welcome), font a font_ref id and colour a colour id (NULL: the
// list's). Once per mod (a repeat of the same name_id is ignored); up to 24. A config's global "announce": {"name",
// "font", "colour"} does the same. Call it from a client's init.
void announce_mod(const char *name_id, const char *font, const char *colour);
// The mod is the registering one (mods_current: a client's init or its config); it's listed, and brings the dialog
// back, only while applied to the campaign, in the campaign's order.
void localization_install(void); // localization.c: owns the string lookups; logs missing keys
// localization.c: fn may name another string id to look up instead of `id` (NULL: keep it). First non-NULL wins; up
// to 4 clients. The lookups by id only (not by hash).
typedef const char *(*LocRedirectHook)(const char *id);
void loc_on_redirect(LocRedirectHook fn);
// localization.c: is `id` in the current language's string table (without logging it as missing). For optional strings.
bool loc_exists(const char *id);
// The text of the string lookups by id made until loc_capture_end (the last one's), copied into out (barks.c: the text
// RaidDisplay::QueueBark picked, variation and all). Not nested.
void loc_capture_begin(char *out, size_t cap);
void loc_capture_end(void);
// effects.c: fn runs for every applied effect whose name starts with prefix, before the game applies it (register
// before effects_install; up to 4 clients). The registry copies prefix. Give such effects no payload.
typedef void (*EffectNameHook)(const char *name, void *target, void *performer);
void effects_on_prefix(const char *prefix, EffectNameHook fn);
// A client may change an effect's float fields (e.g. .heal 0xc8, .healstress 0xd4) for one application: it fills up
// to cap patches and returns how many; each is written before EffectApplyInternal runs and restored after. For every
// effect that isn't a library effect (remove_quirk_, ddx_set_ / ddx_add_, prefix clients).
typedef struct {
    unsigned offset;
    float value;
} EffectPatch;
typedef int (*EffectPatchHook)(const void *effect, void *target, void *performer, EffectPatch *out, int cap);
void effects_on_patch(EffectPatchHook fn);
// A client may swap an effect for another for one application: it returns the replacement's name (an effect in the
// game's or a mod's effects files, applied to the same target and performer), or NULL to keep it. The first client
// whose replacement exists wins; patches and config actions then see the replacement. After config effect_variants.
typedef const char *(*EffectSwapHook)(const void *effect, void *target, void *performer);
void effects_on_swap(EffectSwapHook fn);
void skills_install(void); // skills.c
// skills.c: the skills the hero can use now (Hero::GetModeSelectedCombatSkills: its selected skills valid in its
// mode, less config-disallowed ones), every page of a paged bar; HeroCombatSkill pointers into out, how many (<= max).
int skills_usable(const void *hero, void **out, int max);
void riposte_install(void); // riposte.c: config "riposte" (macOS)
void anim_install(void);    // anim.c
void popups_install(void);  // popups.c: combat popup text log
// Buff origins (buff_origin.c, macOS): what put each buff on an actor, by name. buff_source_effect: the next
// application of the effect named `effect` on `target` is a named source: its bleed / blight / marked popups on the
// target and the Buff! / Debuff! / Riposte! popup of a tray icon its buffs light show `popup`, and the character
// sheet's stat tooltips name its buffs `label` (either may be NULL). Other buffs added by an effect are named by the
// skill and who used it ("Battle Ballad (Junia)"); clients name their own (buff_origin_on); the game's trinket and
// quirk buffs are named after the trinket or quirk. Pending names clear at battle end.
void buff_source_effect(const void *target, const char *effect, const char *popup, const char *label);
// A client naming a buff (a Buff::Instance in the actor's group: source +0x54, source id +0x58, id +0x61) for the stat
// tooltips: the label, or NULL (the next client, then the recorded origin). Asked before everything else.
typedef const char *(*BuffOriginName)(const void *actor, const void *buff);
void buff_origin_on(BuffOriginName fn);
bool buff_origin_label(const void *actor, const void *buff, char *out, size_t cap); // false: the game's source name
// A client relabeling a line of the character sheet's stat tooltips by the game's label id ("str_stat_base_weapon",
// "str_stat_additional_armour"...): the text, or NULL (the next client, then the game's).
typedef const char *(*StatLabel)(const void *hero, const char *id);
void stat_label_on(StatLabel fn);
void buff_origin_enter(const void *target, const void *performer, const void *effect); // effects.c, EffectApplyInternal
void buff_origin_leave(void);
void buff_origin_added(const void *actor, unsigned guid, const void *instance); // effects.c, Buff::Group::AddBuff
void popups_buff_added(const void *actor, unsigned guid); // effects.c, Buff::Group::AddBuff: names the next tray popup
const char *buff_origin_scope_popup(const void *actor); // the named popup of the application running on actor, or NULL
const char *buff_origin_popup(const void *actor, unsigned guid); // a buff's named popup, or NULL
void buff_origin_install(void);
void crit_info_install(void); // crit_info.c: class crit bonuses (CRIT tooltip, popups, labels)
// A client naming the Buff! / Debuff! / Riposte! popup of a tray icon that turned on, by one of its buffs (a
// Buff::Instance in the actor's group: source +0x54, id +0x61): the text, or NULL (the next client, then the game's).
// Asked only when none of the icon's buffs has a named popup.
typedef const char *(*PopupBuffText)(const void *actor, const void *buff);
void popup_on_buff(PopupBuffText fn);
// A text popup of our own on an actor, with a popup type's colours, sound and display rules
// (RaidDisplay::AddPlusPopupStringText; types are RaidUI::Screen::PopupTextTypes, k_PopupTextTypeData: 0x2 hp regen
// onset, 0x6 no damage, 0x15 buff, 0x21 cured...). The display shows one popup per actor at a time, in queue order, and
// holds types back during the attack zoom unless the type allows it (k_PopupTextTypeData +0x46): queued just before a
// number popup of a type with the same rules, the text shows first.
void popup_text(void *actor, int type, const char *text);
// barks.c: queue a bark by string id on actor (RaidDisplay::QueueBark, as the game's skill barks: a variation picked,
// the actor's bark colour, the barks option's lifetime). False outside an expedition. macOS only.
bool bark_queue(void *actor, const char *id);
// Every popup as the display is asked for it (after naming): actor, type, its text ("" for a number popup), number;
// named: the text is a client's (popup_text, a named effect or tray icon), not the game's; rounds: for a tray icon's
// popup (Buff!, Debuff!, Riposte!), the longest rounds left of the icon's buffs (Buff::Instance +0x4c; 0: none, or not
// a tray popup); buff: the Buff::Instance whose name a tray popup shows (a client's or a named effect's), else NULL.
// Up to 4 observers (expedition_log.c).
// duration_type: that buff's Buff::Duration type (DURATION_*), with rounds its amount.
typedef struct {
    const void *actor;
    int type;
    const char *text;
    unsigned number;
    bool named;
    int rounds;
    const void *buff;
    int duration_type;
} PopupEvent;
typedef void (*PopupObserver)(const PopupEvent *popup);
void popup_on_add(PopupObserver fn);
const char *popup_type_id(int type); // k_PopupTextTypeData's id ("damage", "bleed"...), NULL: unknown
// effects.c: observers of an effect being applied (EffectApplyInternal): enter before the game applies it, with its
// target, a DoT's rate per round (0: none), its duration and the duration's type (DURATION_*, the effect's
// .buff_duration_type); leave after. For details of the popups it raises (an expedition log). Up to 4.
typedef void (*EffectApplyEnter)(const void *target, int dot_rate, int duration, int duration_type);
void effects_on_apply(EffectApplyEnter enter, void (*leave)(void));
// riposte.c: observers of the library's ripostes (config "riposte"): scope(true / false) around Battle::RiposteAttempt,
// skill when GetRiposteSkill answers inside it (the skill the hero ripostes with, if it fires). Up to 4.
void riposte_on(void (*scope)(bool on), void (*skill)(const void *actor, const void *skill));
// popups.c: a popup in words, for text that reports popups (an expedition log). A client gives its own wording for
// popups it knows (a client mod: class crit bonuses): wording, a template with {actor}, {source} and {detail} (the
// detail in the popup's colour; durations are the client's to include), and returns true; popup_line asks each
// client in turn (false: none had wording). Up to 4.
typedef struct {
    char wording[192], source[96], detail[320];
} PopupLine;
typedef bool (*PopupLineFn)(const PopupEvent *popup, PopupLine *out);
void popup_on_line(PopupLineFn fn);
bool popup_line(const PopupEvent *popup, PopupLine *out);
// Buff::Duration types, as the game numbers them (Buff::Duration::k_TypeData, filled in this order by its static
// initialiser; "round" is after_turn's alias): an effect's .buff_duration_type (Effect +0x3b0; a DoT without one:
// before_turn), a buff's (Buff::Instance +0x50, its amount at +0x4c; -1 with none: until camp or the expedition's end).
enum {
    DURATION_COMBAT_END,
    DURATION_QUEST_COMPLETE,
    DURATION_QUEST_NOT_COMPLETE,
    DURATION_QUEST_END,
    DURATION_ACTIVITY_END,
    DURATION_IDLE_START_TOWN_VISIT,
    DURATION_TILL_REMOVED,
    DURATION_NONE,
    DURATION_BEFORE_TURN,
    DURATION_AFTER_TURN,
    DURATION_AFTER_ROUND
};
// A raw duration in words, as the game ends it. Rounds (before_turn, after_turn, after_round): " (2 rds)" for
// 3, " (until end of round)" for 1; combat_end: " (until end of battle)" for 1, " (2 battles)"; types that last until
// camp, the quest's end or removal, and 0 or less: "" (strings engine_duration*, the library package's).
void duration_text(int amount, int type, char *out, size_t cap);
// items.c: an inventory item's name and colour id in words (a client's items: a client mod rolled gear): each namer
// in turn (false: the next), then the game's title (Inventory::Display::GetTitle, else its id) in white. Up to 4.
typedef bool (*ItemDisplayName)(const uint8_t *item, char *name, size_t name_cap, char *colour, size_t colour_cap);
void items_on_display_name(ItemDisplayName fn);
void items_display_name(const uint8_t *item, char *name, size_t cap, char *colour, size_t colour_cap);
// The skill's name as the combat UI shows it (ActorDisplay::App::GetCombatSkillName), "" when unknown.
void actor_skill_name(const void *actor, const void *skill, char *out, size_t n);
void barks_install(void); // barks.c: the bark point
typedef struct {
    uint32_t w[17];
} SourceId;                    // ActOut::SourceIdentifier, 0x44 bytes (passed by value)
void events_install(void);     // events.c
void game_fixes_install(void); // game_fixes.c: the game's own bugs (raid_start / raid_finish listeners)
void act_outs_install(void);   // act_outs.c: act_out, act_out_check (macOS)
// act_outs.c: per-source act-outs left to run this turn: keep the battle in bs_before_turn_Bark (events.c asks it in its
// Battle::IncrementState hook) so the game calls Combat_StartTurn again once the last one has played.
bool act_outs_hold(void);
// act_outs.c: rules weighing a hero's turn-start act-outs, checked in the roll (before act_out). For each candidate
// act-out whose source (the hero's trait (affliction or virtue) id, or the quirk's id for a quirk's act-out) and
// act-out id match, check(ctx, ...) returns a factor for its weight: 0 rules it out this turn, 1 leaves it, 2
// doubles it; the factors of every matching rule multiply. ctx: the battle (combat_context_build), ctx->self the
// hero. Register before act_outs_install (up to 64; config "act_outs.rules" registers the JSON ones).
typedef float (*ActOutCheck)(const CombatContext *ctx, const char *source, const char *act_out, void *ud);
typedef struct {
    const char *name;    // for the log
    const char *source;  // trait or quirk id (glob: "*", "disease_*"); NULL: any
    const char *act_out; // act-out id (glob); NULL: any
    ActOutCheck check;
    void *ud;
} ActOutRule;
bool act_out_rule_register(const ActOutRule *rule);
// act_out_check's refuse bits: the game's checks (repeat: the party's last act-out; disabled: by a monster in the
// battle; limit: the data's raid_limit reached), and a cancel (after only)
enum { ACT_OUT_REPEAT = 1, ACT_OUT_DISABLED = 2, ACT_OUT_LIMIT = 4, ACT_OUT_CANCELLED = 8 };
// act_outs.c: new turn-start act-out types. Once registered, trait and quirk files list the id in
// combat_start_turn_act_outs like a vanilla one (its data's number_value and string_value are the outcome's). The
// library runs it (the game never sees its number): the checks and points as for a vanilla act-out, then the game's
// setup steps (count, burndown, the source's sound and halo, the bark "str_<id>_<source>"), then the outcome, played
// after the bark. Register at the library's start, before act_outs_install (the game reads its data afterwards; config
// "act_outs.types" registers the JSON ones).
typedef struct {
    void *actor;             // the hero acting out
    const void *data;        // its ActOut::Data::CombatStartTurnData entry
    void *rng;               // the battle's rng (prng_minstd)
    const char *id, *source; // the act-out's id; the trait's or quirk's id
    float number;            // the entry's number_value
    const char *string;      // and string_value
} ActOutRun;
typedef void (*ActOutOutcome)(const ActOutRun *run, void *ud);
typedef struct {
    const char *id;
    const char *like;      // a vanilla act-out giving the defaults below (NULL: 0, 1, 0)
    int result;            // the turn result: 0 normal, 1 the hero then acts, 2 lost; -1: like's
    float bark_chance;     // 0-1; < 0: like's
    float burndown;        // seconds added to the battle's burndown; < 0: like's
    ActOutOutcome outcome; // NULL: none (bark only)
    void *ud;
} ActOutTypeDef;
int act_out_type_register(const ActOutTypeDef *def); // its type number (0x100 up), -1: not registered
// Outcome helpers (act_outs.c): heal (amount: a share of the target's max HP, as heal_self does; the healer: actor),
// a side effect by name (as the stress heals do). A target from a name: "self", "party_lowest_hp",
// "party_lowest_hp_pct", "party_random", "ally_random" (not self), "party" (every living hero: count > 1). NULL: none.
void act_out_heal(void *target, void *actor, void *rng, float share_of_max_hp);
void act_out_side_effect(void *actor, void *target, const char *effect);
int act_out_targets(const char *name, void *actor, void *rng, void **out, int max); // how many
// hook_rules.c: the global config section "hooks" (any engine file), rules on hook points: filters, changes before the
// game acts, and actions (game_actions.c). Parsed while the configs load; registered before the *_install functions.
void hook_rules_parse(const JVal *list, const char *path);
void hook_rules_register(void);
// game_actions.c: doing things through the game's own code, so its modifiers, popups and hook points apply (false:
// not done). Amounts are the game's raw inputs. Ids are the game's: damage types ("attack", "bleed"...) and
// sources ("trap", "hunger"...), heal and stress sources (NULL: the first), loot types (battle, chest: curios, camping,
// quest, hero_death, threshold; NULL: chest).
bool game_damage(void *actor, float amount, const char *type, const char *source, void *attacker);
bool game_heal(void *actor, float amount, const void *healer, const char *source);
bool game_stress(void *actor, float amount, const void *by, const char *source); // amount < 0 heals stress
bool game_torch(float change);                                                   // signed
bool game_apply_effect(void *performer, void *target, const char *effect);       // an .effects.darkest effect
bool game_add_quirk(void *hero, const char *id);                                 // as a curio gives one (resists apply)
bool game_remove_quirk(void *hero, const char *id);
bool game_give_item(const char *type, const char *id, int amount); // party inventory, else the estate (item_store)
// Creates a game-owned item object for clients to attach custom fields before copying it to an inventory.
// The caller owns the returned object and must release it with game_item_free; inventories copy it.
void *game_item_create(const char *type, const char *id, int amount);
void game_item_free(void *item);
bool game_inventory_copy(void *inventory, const void *item);
// Campaign objects are borrowed; hero enumeration skips the same dismissed entries as the game's unequip-all.
void *game_roster_hero(unsigned guid);
int game_roster_heroes(void **out, int max);
int game_party_heroes(void **out, int max, bool living_only);
int64_t game_inventory_amount(const void *inventory, unsigned type);
void *game_estate_storage(void);
int game_estate_amount(unsigned currency);
bool game_estate_add(unsigned currency, int amount); // signed; saves even when amount is zero
typedef struct {
    unsigned currency;
    int amount;
} CurrencyCost;
// Checks all nonnegative costs before deducting; currency ids must be distinct. Saves once on success.
bool game_estate_pay(const CurrencyCost *costs, int count);
// Into an Inventory::System (e.g. the loot window's, HookContext.inventory), stacking as the game's own loot bonus does.
bool game_inventory_add(void *inventory, const char *type, const char *id, int amount);
bool game_queue_loot(const char *loot_type, const char *code); // a loot table code into the expedition's loot
void *game_rng(void);                                          // the game's global prng (Rules)
float game_random(void);                                       // 0 <= x < 1 from it
int game_random_int(void *rng, int min, int max);              // min..max inclusive (rng NULL: game_rng)
float *game_resist_clamp(int type, bool flag);                 // the game's {min, max} for a resistance type (NULL:
                                                               // unknown); writable: every clamp of that type reads it
// The dungeon screen's centre announcement (as a curio's "<quirk> Quirk Removed!"), in the notable colour for the
// purge's time; false while the dungeon screen isn't up (town, an expedition loading).
bool game_announce(const char *text);
// quirk_notices.c: "<quirk> Quirk Removed!" on the dungeon screen for every quirk lost there but a curio's purge
// (which shows its own). Its listener registers before the *_install functions.
void quirk_notices_install(void);
bool quirk_notices_on(void);      // prop_hooks.c: hook the purge's Apply for it
int prop_applying(void);          // prop_hooks.c: the curio result type whose Apply
                                  // runs (6: purge), -1 none
void combat_events_install(void); // combat_events.c
// hooks.c: the hook point registry (every point is declared there; design/docs/13-expedition-hooks.md). Register
// before the *_install functions run: a module installs its game hooks only for points that are listened to.
typedef enum { HOOK_BEFORE, HOOK_AFTER } HookPhase;
typedef struct {
    const char *point; // set by hook_emit
    HookPhase phase;   // set by hook_emit
    void *raid, *battle;
    void *area;              // area_enter, scout: the Area
    void *prop;              // curio_result, curio_apply: the Prop (curio, obstacle, door); bark: a curio bark's curio
    void *inventory;         // loot_generated: the loot window's Inventory::System (clients may add items)
    void *actor;             // the actor the point is about (the hero for per-hero moments)
    void *attacker, *target; // combat: per target of an attack
    const void *skill, *item;
    const void *attack;      // combat: Battle::Attack, borrowed for the callback
    const void *target_data; // combat: Battle::Attack::TargetData, borrowed
    float amount;            // the point's value (combat: damage); before: may be changed
    float result;            // after, value points: what the game did (HP lost or healed, stress change, torch change)
    int kind;                // the game's type for the amount (a damage, heal or stress source), -1: none
    const char *source;      // value points: the game's id for kind ("monster", "trap", "camping"...), or NULL
    const char *type;        // hp_damage: the game's damage type id ("attack", "bleed", "poisoned", "burn"...);
                             // act_out: the act-out's id ("ignore_command", "attack_friendly"...)
    const char *skip;        // turn_start: the turn is skipped, and why ("stun"); NULL: the actor acts
    unsigned refuse; // act_out_check: why the act-out is refused (ACT_OUT_*), 0: allowed; before: may be changed
    bool hit, crit;
    bool synthetic;   // fired from inside another point's callback (a follow-up hit)
    bool cancel;      // before: set to stop the game acting
    const char *text; // bark: the text shown
} HookContext;
typedef void (*HookCallback)(HookContext *ctx, void *userdata);
// The listener belongs to the registering mod (mods_current) and runs only while that mod is applied to the campaign:
// the library's first, then the mods from the lowest in the campaign's list to the highest (the top mod's before
// changes stand), one mod's in registration order; a point's own listeners before the "*" ones.
bool hook_on(const char *point, HookPhase phase, HookCallback callback, void *userdata); // point "*": every point
// Every point, and wanted: every point's game hook is installed ("*" listens only to points installed for others).
bool hook_on_all(HookPhase phase, HookCallback callback, void *userdata);
bool hook_emit(const char *point, HookPhase phase, HookContext *ctx); // false: a before callback cancelled
bool hook_wanted(const char *point);                                  // something listens (or config "debug" is on)
bool hook_any_wanted(const char *const *points, int n);
bool hook_point_exists(const char *name);
int hook_point_count(void);
const char *hook_point_name(int i);
const char *hook_point_about(int i);
bool hook_point_has_phase(int i, HookPhase phase);
const char *hook_phase_name(HookPhase phase);
// combat_hooks.c: the combat points (hit, miss, dodge, crit, kill, hit_taken, crit_taken, deathblow_resist) and
// helpers that act through the game's combat code. macOS only.
// Apply a follow-up through the game's regular hit resolver. It copies the source
// TargetData, substitutes target/damage/crit, filters skill side effects, and suppresses
// recursive hit points. A NULL filter permits all skill side effects.
typedef bool (*CombatFollowupEffectFilter)(const void *effect, void *userdata);
bool combat_apply_followup_hit(const HookContext *source, void *target, float damage_scale, bool crit,
                               CombatFollowupEffectFilter filter, void *userdata);
// Apply an additional native effect through the game's status chance/resistance path, with the resolved hit's
// TargetData (its target replaced): performer is the effect's performer (whose stats and skill it counts as; its
// ".target performer" effects land on it), target the actor it's aimed at. combat_apply_side_effect: the hit's
// attacker on the hit's target, as the skill's own side effects.
bool combat_apply_effect(const HookContext *source, void *performer, void *target, const void *effect);
bool combat_apply_side_effect(const HookContext *source, const void *effect);
void combat_hooks_install(void);
// value_hooks.c: hp_damage, hp_heal, stress_damage, stress_heal, torch_change (every HP, stress and torch change,
// in and out of combat). macOS only.
void value_hooks_install(void);
void expedition_hooks_install(void);                // expedition_hooks.c: DoT ticks, steps, areas, hunger, scout
void story_intro_skip_install(void);                // story_intro_skip.c: early story-raid loading prompt
void expedition_raid_ready_turn(const void *actor); // events.c's turn_start: raid_ready for a save loaded mid-battleing
void prop_hooks_install(void);                      // prop_hooks.c: curios, obstacles, doors, traps
void hero_hooks_install(void);                      // hero_hooks.c: quirks, Death's Door, deaths, camping
// prop_hooks.c: a Prop's id (Prop::GetName: "crate", "rubble", ...; NULL for no prop) and its display name from the
// game's string table (curios str_curio_title_<UI name>, obstacles str_obstacle_<id>_title; else the id made
// readable: "cove_door" -> "Cove Door"). macOS.
const char *prop_id(const void *prop);
const char *prop_title(const void *prop);
void loot_hooks_install(void); // loot_hooks.c: loot queued and generated
void stats_install(void);      // stats.c
// stats.c: run before / after each Hero::CalculateDerivedStatsInternal (register before stats_install, which then
// installs that hook even with no formulas configured). Up to 4 (befores in order, afters in reverse).
typedef void (*StatsCalcHook)(void *hero);
void stats_on_calc(StatsCalcHook before, StatsCalcHook after);
// The same override outside the calc (tooltips reading the hero's weapon row): begin, read, end.
void stats_rows_begin(void *hero);
void stats_rows_end(void *hero);
typedef int (*StatsFriendlyHealMinHook)(void *hero, const void *skill);
void stats_on_friendly_heal_min(StatsFriendlyHealMinHook fn);
void tooltip_install(void);                       // tooltip.c
void tooltip_encode_colours(char *s, size_t cap); // {colour_start|x} / {colour_end} markup, in place
// tooltip.c: base (not live) rewrite of a GuiTooltipParameters built from the game's skill line helpers: title
// template per skill (its name line), the skill's base lines (spenders once per Holy Power value), tokens and
// colour markup; for configured classes only. hero (may be NULL): weapon damage for hit ranges.
void tooltip_rewrite_base(void *tip_params, const void *cls, const void *hero, const void *const *skills, int nskills);
void items_install(void); // items.c
// items.c: who may use an item, in an expedition (DungeonController::CanHeroUseItem: "use item", the party inventory's
// marks) and in town (the realm inventory's validity: items the character panel's hero can't use are darkened). The
// answer is the game's, then the item's registered type's can_use (item_types.c), then each rule in registration order
// (up to 4; each gets the answer so far and returns the new one), then, in an expedition, the class configs' "items"
// rules. Register rules and the fallback before items_install.
typedef struct {
    const void *hero;
    const uint8_t *item; // the Inventory::Item; NULL in an expedition (by id only)
    unsigned type_hash, id_hash;
    bool in_town, in_battle;
} ItemUseQuery;
typedef bool (*ItemUseRule)(const ItemUseQuery *q, bool usable);
void items_on_use(ItemUseRule rule);
// The hero town marks are for when no character sheet is open (NULL: none, everything stays usable). Up to 4, the top
// mod's asked first.
void items_town_hero(const void *(*fallback)(void));
// The hero items are shown for: the character sheet's, else the town hero fallback's (NULL: none).
const void *items_active_hero(void);
// Item drags (items.c): what an InputManager drag carries, for drop targets and drag sources. Kinds (bits): from an
// inventory (the party's, loot), from a hero's equipment slot, from the stash.
enum { ITEM_DRAG_INVENTORY = 1 << 0, ITEM_DRAG_EQUIPMENT = 1 << 1, ITEM_DRAG_STASH = 1 << 2, ITEM_DRAG_ANY = 7 };
unsigned item_drag_kind(const void *drag);       // the drag's ITEM_DRAG_* (0: not an item drag)
const uint8_t *item_drag_item(const void *drag); // the Inventory::Item it carries (NULL: not an item drag)
uint32_t item_drag_mask(unsigned kinds);         // a DropContext accept mask for those kinds
void *item_drag_current(void);                   // the DragData being dragged now (NULL: none)
// A drop target's answer to the source: the item it hands back (the source puts it where the dragged one was), or NULL
// (the source removes the dragged item).
void item_drag_hand_back(void *drag, const uint8_t *item);
// Puts a copy of item in the party inventory (on an expedition, if it has room) or the estate's trinket storage.
// Returns where ("the party inventory", "the estate"), NULL if nowhere.
const char *item_store(const uint8_t *item);
// audio.c: plays a sound file (WAV, OGG, FLAC, MP3; path relative to the data folder or a mod) on the game's FMOD, at
// the volume of the game's setting for its channel. False: not played (a missing or bad file is logged once).
typedef enum { AUDIO_SFX, AUDIO_MUSIC, AUDIO_VOICE } AudioChannel;
bool audio_play_file(const char *path, AudioChannel channel);
// item_types.c: item types beyond the game's own. Register before item_types_install; with none registered, nothing is
// hooked. home: where the type's items go when the party's inventory is brought home at the end of an expedition
// (ITEM_HOME_NONE: dropped, as the game drops any type it doesn't know; ITEM_HOME_CURRENCY: its amount is added to
// the estate currency `currency`, as the game banks gold, Shards and heirlooms; give it no sell value).
// joins: the vanilla checks the type passes as if it were a trinket (the game tests `type == kTrinket` inline at
// each; item_types.c lists the sites per check).
typedef enum { ITEM_HOME_NONE, ITEM_HOME_STASH, ITEM_HOME_CURRENCY } ItemHome;
// Item drawing: wherever an item of a registered type is drawn (the inventory grids' buttons, the dragged item,
// equip_slots.c's slots, item_draw, a picture a client names with item_draw_note), the type's compose callback lists
// what to draw, bottom first: out[0] under out[1] and so on. One layer may be the base picture (base: drawn as the
// game draws the item, so a button stays a button); with none, the base picture isn't shown. Each layer is placed
// by its anchor: that point of the layer meets that point of the item's rect, then the offset (scaled as the item
// is). size {0, 0}: the picture's own size, scaled as the item is (the item's drawn size over its base picture's).
typedef enum { ITEM_DRAW_GRID, ITEM_DRAW_DRAG, ITEM_DRAW_SLOT, ITEM_DRAW_OTHER } ItemDrawPlace;
typedef struct {
    const uint8_t *item; // the Inventory::Item
    const void *hero;    // the active hero (items_active_hero; may be NULL)
    ItemDrawPlace place;
    const uint8_t *base; // the base picture (texture handle)
    Vec2 size;           // the item's drawn size
    float scale;         // drawn size over the base picture's own
} ItemDrawQuery;
typedef struct {
    bool base;          // the item's base picture (tex and sprite unused)
    uint8_t tex[16];    // a picture, or
    const void *sprite; // a Spine sprite (animated; its origin on the anchor point)
    UiAnchor anchor;
    Vec2 offset, size;
} ItemLayer;
#define ITEM_MAX_LAYERS 8
// Draws the item at pos (top left) and size (0: its base picture's), as `place`, for hero (NULL: the active hero).
bool item_draw(const uint8_t *item, const void *hero, Vec2 pos, Vec2 size, ItemDrawPlace place);
// A client about to draw an item's picture itself (GUI_Image): the next image with that texture is drawn as the item.
// Up to 4 notes; item_draw_note(NULL, ...) forgets them (the client's item pointers are only read while noted).
void item_draw_note(const uint8_t *item, const uint8_t tex[16], ItemDrawPlace place);
void item_draw_install(void); // item_draw.c: after item_types_install, before ui_install
// Item content (item_tips.c): what an item's tooltip and its description show, supplied by its type (ItemTypeDef.tooltip
// and .describe, or item_type_view for the game's own types) as layers on a content rect, the way an item's picture is
// composed. The tooltip callback fills the tooltip the game builds wherever an item is hovered
// (Inventory::Display::CreateItemTooltip: inventories, the stash, shops, equipment slots); the game still sizes, places,
// clamps and frames the box, around the content's size. The description callback gives content a panel shows
// (item_describe, item_content_draw). Content has two regions, the head (the name) over the body (the rest): a panel may
// put the head in a header of its own; a tooltip shows both, each as wide as the wider. A region's size is what the box
// or panel holds; a layer may reach outside it on purpose (a badge over the tooltip's corner). Each layer is placed by
// two points: `at`, a point of its region, gets the layer's own point `anchor`, then the offset.
#define ITEM_CONTENT_LINES  64
#define ITEM_CONTENT_LAYERS 16
#define ITEM_CONTENT_TEXT   12288
#define ITEM_MAX_EQUIPPED   8
typedef enum { ITEM_REGION_BODY = 0, ITEM_REGION_HEAD = 1 } ItemRegion;
enum { ITEM_REGIONS_HEAD = 1, ITEM_REGIONS_BODY = 2, ITEM_REGIONS_ALL = 3 }; // which regions item_content_draw draws
typedef enum { ITEM_LAYER_TEXT, ITEM_LAYER_IMAGE } ItemContentLayerKind;
typedef struct {
    ItemContentLayerKind kind;
    ItemRegion region;
    UiAnchor at, anchor;
    Vec2 offset;
    int first, count;         // text: the content's lines first .. first + count - 1
    float width;              // text: wrapped at (0: its region's width)
    int column;               // text: 0 left, 1 centred, 2 right, in that width
    uint8_t tex[16];          // image: the picture
    Vec2 size;                // image: its size (0: its own)
    char label[16], font[32]; // image: text centred on it, in a font (a font_ref id)
    uint32_t rgba;            // image: the text's colour (0xAABBGGRR)
    Vec2 label_offset;        // image: the text moved by (a font may sit off centre)
} ItemContentLayer;
typedef struct {
    Vec2 head, body; // the regions' sizes
    ItemContentLayer layer[ITEM_CONTENT_LAYERS];
    int nlayers;
    UiTextLine line[ITEM_CONTENT_LINES]; // text layers' lines (their text in `text`)
    int nlines;
    char text[ITEM_CONTENT_TEXT];
    size_t used;
} ItemContent;
typedef struct {
    const uint8_t *item;                        // the Inventory::Item
    const void *hero;                           // the hero it's shown for (NULL: none)
    bool hero_implied;                          // tooltip: it came with no hero, the active hero stands in
    const uint8_t *equipped[ITEM_MAX_EQUIPPED]; // the hero's equipment: trinkets, then the registered slot
    int nequipped;                              // kinds' items (equip_slots_items)
    float width;                                // describe: the panel's width (tooltip: 0, as needed)
    const UiTextLine *game;                     // tooltip: the game's own lines for the item (this frame's)
    int ngame;
} ItemContentQuery;
// Fill out (empty when called); false: show the game's own tooltip (description: none).
typedef bool (*ItemContentFn)(const ItemContentQuery *q, ItemContent *out, void *ud);
typedef struct {
    const char *label; // string id ("Salvage for:")
    const char *icon;  // picture path: what it gives (a currency's icon)
    int amount;
} ItemSalvageQuote;
enum {
    ITEM_CHECK_SELL = 1 << 0,       // sold from the stash at its class's sell price
    ITEM_CHECK_STASH_DROP = 1 << 1, // can be dropped on the stash (moved within it)
};
typedef struct {
    const char *type; // the inventory_item .type string (copied)
    ItemHome home;
    const char *currency; // ITEM_HOME_CURRENCY: the estate currency id (copied)
    unsigned joins;       // ITEM_CHECK_* flags
    const char *sound;    // pickup sound (copied): a sound file's data path
                          // (has a '/' or '.': audio_play_file), else the game's
                          // event:/ui/dungeon/loot_take_<sound> ("trinket",
                          // "supply", "gold", ...); NULL: none
    bool (*can_use)(const ItemUseQuery *q, bool usable, void *ud); // may this hero use it (items.c; usable: the
                                                                   // game's answer); NULL: the game's answer
    // The item's base picture (a texture handle the type keeps; true: use it), wherever the game asks for an item's
    // icon (Inventory::Item::GetIcon, Inventory::Display::GetIconTexture). NULL or false: its class's icon.
    bool (*icon)(const uint8_t *item, uint8_t tex[16], void *ud);
    // What's drawn for the item (see "Item drawing" below): the layers, bottom first. NULL: the base picture alone.
    int (*compose)(const ItemDrawQuery *q, ItemLayer *out, int max, void *ud);
    ItemContentFn tooltip;  // its tooltip's content (NULL: the game's lines)
    ItemContentFn describe; // its description (NULL: none)
    // Its prices in gold (item_sale.c), wherever the game reads one (shops and their tooltips, the stash's sale and
    // "Sell for:"): buy and sell come filled with its class's (the data file's purchase_gold_value, sell_gold_value);
    // change either. The game still applies its own discounts and sell-back. NULL: the class's.
    void (*price)(const uint8_t *item, int *buy, int *sell, void *ud);
    // Salvage: Ctrl-click in the estate stash (item_sale.c). salvage_quote says what salvaging it gives, shown in place
    // of the hint under the grid while Ctrl is held over it (false: it can't be salvaged now); salvage does it on a
    // Ctrl-click (the type may ask first: *dont_ask is the player's "don't ask again", kept until the stash closes).
    bool (*salvage_quote)(const uint8_t *item, ItemSalvageQuote *q, void *ud);
    void (*salvage)(const uint8_t *item, bool *dont_ask, void *ud);
    void *ud; // for the callbacks
} ItemTypeDef;
bool item_type_register(const ItemTypeDef *def);
const ItemTypeDef *item_type_find(unsigned type_hash); // NULL: not a registered type
int item_types_count(void);
const ItemTypeDef *item_type_at(int i); // the i-th registered type (NULL: none)
void item_types_install(void);
// item_tips.c: item content (above). A game type's tooltip and description (a registered type's are its definition's);
// before item_tips_install.
bool item_type_view(const char *type, ItemContentFn tooltip, ItemContentFn describe, void *ud);
// Building content: a line of text (font: ui_font's; colour: ui_colour_hvec4's; text copied) for text layers, its index
// (-1: full); its stored string (for line icons and badges registered by pointer: ui_tooltip_line_icons, ...); a new
// layer (zeroed but kind and region; NULL: full).
int item_content_line(ItemContent *c, const void *font, uint64_t colour, const char *text);
const char *item_content_string(const ItemContent *c, int line);
ItemContentLayer *item_content_add(ItemContent *c, ItemContentLayerKind kind, ItemRegion region);
float item_content_lines_width(const ItemContent *c, int first, int count); // the widest (unwrapped)
float item_content_lines_height(const ItemContent *c, int first, int count, float width);
bool item_is_equipped(const ItemContentQuery *q, const uint8_t *item); // one of q->equipped
// The item's description for hero (NULL: the active hero) in a panel `width` wide (NULL: its type has none). The
// content is the library's, kept until a few more are built.
const ItemContent *item_describe(const uint8_t *item, const void *hero, float width);
// Content's regions (ITEM_REGIONS_*) drawn in the current panel at pos (top left), `width` wide (0: as wide as the
// widest drawn region), the head over the body; returns the height.
float item_content_draw(const ItemContent *c, unsigned regions, Vec2 pos, float width);
Vec2 item_content_size(const ItemContent *c, unsigned regions);
// Where every item tooltip (CreateItemTooltip's) goes: the client's callback returns the box's top left from the
// place its screen asked for (slot: the item's position as the caller passed it; game: where the game would put the
// box, slot plus the game's offset), the mouse and the screen's bounds, all in the tooltip's transform. The game still
// clamps the box onto the screen. NULL: the game's place. Before item_tips_install.
typedef struct {
    Vec2 slot, game, mouse, screen_min, screen_max;
} ItemTipPlace;
typedef Vec2 (*ItemTipPlaceFn)(const ItemTipPlace *p, void *ud);
void item_tips_place(ItemTipPlaceFn fn, void *ud);
void item_tips_install(void); // after item_types_install
// item_sale.c: registered types' prices (ItemTypeDef.price) and stash salvage (.salvage_quote, .salvage). The salvage
// prompt's look: layout key entries text_offset, price_offset, icon_size, icon_gap, font, colour, amount_font,
// click_slop.
void item_sale_layout(UiLayout *l, const char *key);
bool item_sale_prompting(void); // the salvage prompt shows this frame (Ctrl over an item)
void item_sale_install(void);   // after item_types_install, before stash_view_install
// equip_slots.c: a hero's equipment drawn as slots placed by a layout file, in place of the game's drawing, on the
// character sheet, the expedition hero panel and the embark screen's quick trinket select (design/docs/15). A slot has a
// kind ("trinket": the hero's trinket slot `index`, the game's own slot behaviour), a position relative to where the
// game draws the trinkets, a size, an empty picture, and the highlight while a matching item is dragged.
// Layout (key's entries): <placement>.slots.<name>: kind, index, pos, size, and the settings empty (a picture),
// highlight (valid, invalid: pictures; offset: from the slot, default the game's; grow: its size over the slot's) and
// tooltip_offset. A setting is the slot's, else its placement's (<placement>.<setting>), else the key's, else the game's.
// A placement with no slots is the game's. Set the layout before equip_slots_install; none: nothing hooked.
// Other kinds are registered by a client: each slot is then a drag source and a drop target of the library's, driven
// by the kind's callbacks (index: the layout's).
typedef struct {
    const char *id; // what layouts name in .kind (not copied)
    // what's in the hero's slot (an Inventory::Item the kind owns, valid until the next call; NULL: empty)
    const uint8_t *(*item)(const void *hero, int index, void *ud);
    // does the slot take that item? 0: not its kind (no highlight); 1: yes; 2: its kind, but not for this hero (the
    // invalid highlight); refused unless 1
    int (*accepts)(const void *hero, int index, const uint8_t *item, void *ud);
    // put (a copy of) item in the slot, NULL: empty it. False: refused, nothing changed
    bool (*set)(void *hero, int index, const uint8_t *item, void *ud);
    // may the slot change now (picked up, dropped on)? NULL: always
    bool (*movable)(const void *hero, int index, void *ud);
    int count; // the hero's slots of the kind, indices 0 .. count-1 (0: 1)
    void *ud;
} EquipSlotKind;
bool equip_slot_kind_register(const EquipSlotKind *kind); // before equip_slots_install (up to 8)
// What the hero has in the registered kinds' slots (each kind's item for indices 0 .. count-1, empty ones left out),
// in registration order; returns the count (at most max).
int equip_slots_items(const void *hero, const uint8_t **out, int max);
// item_tips.c: everything the hero has equipped: its trinkets, then the registered kinds' slots (equip_slots_items).
int hero_equipped(const void *hero, const uint8_t **out, int max);
// Empties the hero's slots of the registered kinds whose item want() takes (and that are movable now), a copy of each
// item going into the inventory `into` (an Inventory::System). Returns how many were emptied.
int equip_slots_unequip(void *hero, void *into, bool (*want)(const uint8_t *item, void *ud), void *ud);
// When the layout applies (default: always; with several, while each says so); otherwise every placement is the game's.
void equip_slots_active(bool (*active)(void));
// Clients may watch the heroes whose equipment is shown (each frame a placement draws it, laid out or not). Up to 4.
void equip_slots_on_show(void (*shown)(const void *hero));
void equip_slots_layout(UiLayout *l, const char *key);
void equip_slots_install(void);
// stash_view.c: the estate stash as tabs, one per group of items, each with its own sorts (docs/11 "The stash
// view"). Register tabs before stash_view_install, in the order they're shown; an item belongs to the first tab that
// claims it (has), and a tab with no `has` takes every item no other tab claims. A drop on the stash lands in the
// item's own tab whichever is open (registered item types join ITEM_CHECK_STASH_DROP to be accepted). Each sort fills
// a key: items are ordered by k[0], k[1], k[2], then name (the item id unless the key changes it), all ascending.
// Pictures: a sort's icon has optional _on (chosen) and _hover variants; the tab backgrounds come from the layout.
// With no tab registered, or the active test false, the stash is the game's.
#define STASH_MAX_SORTS 4
typedef struct {
    int64_t k[3];
    char name[72];
} StashKey;
typedef void (*StashKeyFn)(const uint8_t *item, StashKey *key, void *ud);
typedef struct {
    const char *icon;    // picture path (sort row button)
    const char *tooltip; // string id
    StashKeyFn key;
} StashSortDef;
typedef struct {
    const char *id;                             // for stash_view_tab (copied, up to 31 characters)
    const char *icon;                           // picture path, drawn on the tab
    const char *tooltip;                        // string id, shown when the tab's icon is hovered
    const char *instruction;                    // string id: the hint under the grid on this tab (NULL:
                                                // the game's, "Shift-click to sell")
    bool (*has)(const uint8_t *item, void *ud); // the tab's items; NULL: the rest
    StashSortDef sorts[STASH_MAX_SORTS];        // in the row's order; the first is chosen at the start
    void *ud;
    // The sort row's "Unequip all" button on this tab (at the row's right end, in place of the game's): after a
    // confirmation, every hero's equipped items that belong to this tab (in the trinket slots and the registered
    // equipment slot kinds) go to the stash.
    struct {
        bool off;             // no button on this tab
        const char *icon;     // picture path (optional _hover variant); NULL: the game's
        const char *tooltip;  // string id; NULL: the game's (str_unequip_all_trinkets)
        const char *question; // string id of the confirmation; NULL: the game's
        const char *yes, *no; // its answers' string ids; NULL: the game's
    } unequip;
} StashTabDef;
bool stash_tab_register(const StashTabDef *def);
void stash_view_active(bool (*active)(void)); // when the tabs apply (default: always; several: each says so)
// The tabs' and sort row's layout (tabs_key's entries: pos, size, spacing, icon_offset, open_offset, background,
// strip_background, strip_pos, strip_size (a picture behind the tabs), sort_button_size, sort_row_reserved (places at
// the row's right end kept clear of the sorts: 1, the unequip button's), drop_pos,
// drop_size: where an item dropped on the stash is taken) and the grid's scrolling (ui_invgrid_scroll's layout and key).
void stash_view_layout(UiLayout *tabs, const char *tabs_key, UiLayout *scroll, const char *scroll_key);
// Clients of the stash panel's draw (before and after it; tabs are drawn before `after`) and of a sale (before gets
// the item sold; after runs once the game's sale is done). Up to 4 each.
void stash_view_on_panel(void (*before)(void *realm), void (*after)(void *realm));
void stash_view_on_sell(void (*before)(void *item), void (*after)(void));
const char *stash_view_tab(void); // the open tab's id (NULL: the stash is the game's)
void stash_key_trinket_rarity(const uint8_t *item, StashKey *k, void *ud); // rarest first
void stash_key_trinket_class(const uint8_t *item, StashKey *k, void *ud);  // by class (any first), then rarest
void stash_view_install(void); // after the clients register; after ui_invgrid_install
void cooldown_install(void);   // cooldown.c
void paging_install(void);     // paging.c
void guild_install(void);      // guild.c
void sheet_install(void);      // sheet.c
// sheet.c: the class skill index shown in character sheet grid slot `slot` (paging), for the grid's lambdas
unsigned sheet_skill_index(unsigned slot);
void ui_install(void); // ui.c (after its clients)

// ---------------------------------------------------------------- ui_scroll.c (a vertical scroll bar)
typedef struct {
    float pos;
    bool dragging;
} UiScroll; // the caller keeps it (static): pos is how far it's scrolled
// Draw a scroll bar at `at` (its top left; current transform), h tall, for positions 0..max, and move s->pos by its
// arrows and the wheel (a notch or an arrow click is `step`), and its pip (dragged: any position). s->pos is clamped to
// 0..max. The wheel counts over the bar or the area (area_pos, area_size), and is taken (ui_mouse_wheel_take). id:
// unique on the screen, its low bit clear (the arrows use id and id | 1). Read from the layout under <key>:
// .arrow_size ([41, 25]), .track_width (21), .cap_height (12), .pip_size (24), .art.up / .down / .top / .mid /
// .bottom / .pip (the game's shared/widgets/ scroll bar art). Too short for its track: not drawn.
void ui_scrollbar(UiLayout *l, const char *key, UiScroll *s, float max, float step, Vec2 at, float h, Vec2 area_pos,
                  Vec2 area_size, unsigned id);

// ---------------------------------------------------------------- ui_panel.c (clipped panels, scrolling, panel boxes)
// Panels draw nothing of their own (no frame, no padding): a background is the content's to draw. Content drawn inside
// a panel with the game's GUI (images, text, buttons, item grids, drop targets, ui_tooltip_lines) is clipped to it and
// can't be hovered or clicked outside it; ui_mouse_in tests the clip too. Panels nest.
typedef struct {
    Vec2 pos, size;
} UiRect;
bool ui_clip_push(Vec2 pos, Vec2 size); // a clip (current transform), within the active one
void ui_clip_pop(void);
// A scroll panel: pos and size in the current transform; the content is drawn from (0, 0) (the panel's top left) and
// its height given at the end. Types: off (clipped, never scrolls); hidden (the wheel scrolls it, nothing is drawn);
// arrow / auto_arrow (arrows over the panel, centred, in the style of the town bar's open-tab line: ------v------- at
// the bottom while it isn't scrolled to the end, the same flipped at the top while it isn't at the start; a click pages
// that way; with nothing to scroll neither shows, so the two types are the same); scroller / auto_scroller (the game's scroll bar inset on
// the right, the content narrower by its width: always, or only when the content is taller). Pixel scrolling, eased.
// The state is the caller's (static). Layout, under <key>: .step (48: px per wheel notch or bar arrow), .speed (14),
// .bar.inset (41: the bar's width taken from the content), .bar.offset, .bar.depth (0: drawn over what the caller draws
// at a lower z), .bar.unclip (0; 1: drawn outside clips the panel sits in), .bar.* (ui_scrollbar's art and sizes),
// .arrow.v_size ([30, 20]), .arrow.inset (12: the line's ends from the sides), .arrow.edge (2: from the bottom / top),
// .arrow.shade_height (40: a dark band behind the arrow, fading from the edge; 0: none),
// .arrow.depth (2), .arrow.page (0.8: of the panel's height per click), .arrow.down.art.l / .c / .r, .arrow.up.art.*.
typedef enum {
    UI_SCROLL_OFF,
    UI_SCROLL_HIDDEN,
    UI_SCROLL_ARROW,
    UI_SCROLL_AUTO_ARROW,
    UI_SCROLL_SCROLLER,
    UI_SCROLL_AUTO_SCROLLER
} UiScrollType;
typedef struct {
    UiScroll scroll;      // the target position (the bar's)
    float step;           // px per wheel notch or bar arrow (0: <key>.step); the caller's
    float shown, content; // the position shown (eased); last frame's content height
    double t;             // the last end, for the easing
    UiLayout *l;
    char key[48];
    UiScrollType type;
    Vec2 pos, size;
    float width;
    bool bar, open; // this frame
} UiScrollPanel;
// "off", "hidden", "arrow", "auto_arrow", "scroller", "auto_scroller" ("on" and "auto" mean the scroller ones)
UiScrollType ui_scroll_type_parse(const char *s, UiScrollType fallback);
float ui_scrollpanel_begin(UiScrollPanel *p, UiLayout *l, const char *key, UiScrollType type, Vec2 pos, Vec2 size);
// returns the content's width; draw the content, then:
void ui_scrollpanel_end(UiScrollPanel *p, float content_height);
void ui_scrollpanel_reset(UiScrollPanel *p); // back to the top at once (new content)
// The rows of a list in a scroll panel worth drawing: n rows of row_h from top_pad, the panel showing view_h from
// position shown (UiScrollPanel.shown, before ui_scrollpanel_begin), plus margin rows above and below (a row cut by
// an edge, the arrows' shade). False if none; else rows first..last (inclusive).
bool ui_scroll_rows(float shown, float view_h, float row_h, float top_pad, int margin, int n, int *first, int *last);
// A panel box: anchored panels at the top and bottom (the box's full width) and left and right (between them), each
// present when its size in the layout is above 0 (<key>.top / .bottom: heights; .left / .right: widths), and a centre
// panel in what's left, a scroll panel of type <key>.scroll ("auto": a scroll bar when needed; "hidden", "on", or any
// ui_scroll_type_parse name), its layout under <key>.centre. The state is the caller's (static).
typedef enum { UI_BOX_TOP, UI_BOX_BOTTOM, UI_BOX_LEFT, UI_BOX_RIGHT, UI_BOX_CENTRE } UiBoxPart;
typedef struct {
    UiScrollPanel centre;
    UiRect part[5];
    UiLayout *l;
    char key[48];
    int open;
} UiPanelBox;
void ui_panelbox_begin(UiPanelBox *b, UiLayout *l, const char *key, Vec2 pos, Vec2 size);
// An anchored panel: false if it's absent; else its content is drawn from (0, 0), size is its size, then part_end.
bool ui_panelbox_part(UiPanelBox *b, UiBoxPart part, Vec2 *size);
void ui_panelbox_part_end(UiPanelBox *b);
// The centre: returns its content width (height: its height); draw the content, then centre_end with its height.
float ui_panelbox_centre(UiPanelBox *b, float *height);
void ui_panelbox_centre_end(UiPanelBox *b, float content_height);
void ui_panel_install(void);
// A caller-owned panel box displaying an item's registered description. resolve < 0 hides the level badge.
UiRect ui_item_panel(UiPanelBox *state, UiLayout *layout, const char *key, const uint8_t *item, const void *hero,
                     int resolve, Vec2 pos, Vec2 size);
void ui_item_panel_reset(UiPanelBox *state);
void ui_level_badge(UiLayout *layout, const char *key, Vec2 centre, Vec2 size, int level);

// ---------------------------------------------------------------- ui_invgrid.c (inventory grids: observers, scrolling)
// The one owner of Inventory::Display::InventoryButtonGrid (every mouse-driven inventory grid) and InventoryButtonInGrid.
// Observers run around each grid's draw, in the caller's transform: begin before (it may swap the interface's slot
// vector for the call, as a client mod stash view does), end after with the item under the cursor (or NULL).
typedef void (*UiInvGridBegin)(void *iface);
typedef void (*UiInvGridEnd)(void *iface, void *hovered);
void ui_invgrid_on(UiInvGridBegin begin, UiInvGridEnd end); // up to 8, any time
bool ui_invgrid_ready(void);                                // the grid hook is in
// Scroll every grid whose inventory has this config type (inventory/*.inventory.system_configs.darkest .type): drawn
// whole inside a scroll panel (ui_panel.c) of <key>.visible_rows rows (2) of the grid's step (or the height a
// ui_invgrid_scroll_height function gives, from the grid's top left), the scroll bar right of it, a row per wheel notch.
// Layout under <key>, read every frame: .scroll ("auto_scroller"; any ui_scroll_type_parse name), .off (1: not
// scrolled), and the scroll panel's (.bar.offset, .bar.depth, .bar.unclip, .bar.inset, .bar.* art). The setting is
// the registering mod's (mods_current): used only while the campaign applies that mod, the highest one winning.
void ui_invgrid_scroll(const char *config_type, UiLayout *layout, const char *key);
typedef float (*UiInvGridHeight)(const void *iface, Vec2 grid_pos); // the viewport's height (the grid's transform);
                                                                    // 0: not scrolled this frame (the game's grid)
void ui_invgrid_scroll_height(const char *config_type, UiInvGridHeight height); // after ui_invgrid_scroll
void ui_invgrid_reset(const void *iface); // back to the top (inv_slots.c, as a panel opens)
void ui_invgrid_install(void);            // before the clients that ask ui_invgrid_ready

// ---------------------------------------------------------------- ui_hotspot.c (GUI_HotSpot's one owner)
// Invisible clickable rects (params: HotSpotParameters; id u64 at +0x08, position vec2 at +0x18). before returns false
// to skip the hot spot this frame (no hover, no click); after runs once it's drawn.
typedef bool (*UiHotspotBefore)(const uint8_t *params);
typedef void (*UiHotspotAfter)(uint8_t *params);
void ui_on_hotspot(UiHotspotBefore before, UiHotspotAfter after); // up to 8, any time
const void *ui_hotspot_caller(void); // in a client: the return address of the GUI_HotSpot call being made
bool ui_hotspot_ready(void);
void ui_hotspot_install(void);

// ---------------------------------------------------------------- inv_slots.c (inventory sizes)
// Extra slots for every inventory of a config type: the data file's .max_slots plus each provider's answer (> 0).
// Applied when a panel shows the inventory (so a change shows the next time it opens); shrinking drops only empty
// slots at the end, never an item. Loading a save keeps every saved item, whatever the size now.
void inv_slots_provider(const char *config_type, int (*extra)(void));
int inv_slots(const char *config_type); // the size now (-1: unknown type)
void inv_slots_install(void);
// paging.c: keep only the current page of a hero's (already filtered) skill list; reset at battle start/end
int paging_window(const ClassConfig *c, const void *hero, void **skills, int n);
void paging_reset(const void *hero);
// events.c -> cooldown.c
void cooldown_on_turn_end(void *hero);
void cooldown_on_turn_start(void *actor);
void cooldown_on_battle(void *hero);
unsigned skill_use_id(void);                    // the skill use being performed now, 0 outside one
const void *skill_resolution_performer(void);   // current Actor_DoSkill performer, or NULL
const void *skill_resolution_skill(void);       // current Actor_DoSkill skill, or NULL
int skill_resolution_target_count(void);        // current Actor_DoSkill target count, or 0
const void *skill_resolution_target(int index); // target at index, or NULL
// The actor's last skill (Actor_DoSkill, kept after it returns: queued effects may land later) and that use's id
// (skill_use_id numbering); NULL if it used none since the library loaded.
const void *skill_last_used(const void *actor, unsigned *use_id);

// Copies string arguments; placement supplies an offset and can decline the split.
bool ui_name_frame_split(const void *caller, const char *title, const char *blank_title, const char *texture,
                         bool (*placement)(Vec2 *offset));

// Semantic object views, owned equipment rows, and native UI adapters.
#include "game_objects.h"

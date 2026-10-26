# Vanilla popup source audit

Audited against `_reference/Contents/MacOS/Darkest` on 2026-10-25 with `otool -tvV`.
Direct calls to `RaidDisplay::QueuePopupText` originate in `AddPlusPopupText`.
Other references to QueuePopupText are its own sorting implementation. The audit
found 45 AddPopupText call sites in 24 functions and 16 AddPlusPopupText call
sites in 16 functions. These counts describe static call sites, not popup types.

| Path | Observation and source |
| --- | --- |
| AddPopupText / AddPlusPopupStringText / numeric popup helpers | Common AddPlusPopupText observer; Windows uses the existing ABI adapter in syms_win.c. |
| Buff / Debuff / Riposte tray activation | Newly added buff GUID selects recorded effect origin, client name, or vanilla source label. Existing buffs must not name a subsequent activation. |
| Bleed / Blight / Mark / regeneration onset | Effect scope names the popup, including vanilla skill and performer. |
| Immediate effect feedback: resist, heal, cure, movement and other types | popup_line falls back to the active target's effect source after registered description callbacks. |
| Queued skill effects | Existing last-used-skill fallback requires the effect to belong to that skill's effect set. |
| Quirks / equipment / recovery / other buff sources | Existing buff_origin_label resolves specific origins where available; otherwise the localized Buff::Source category is used. |
| Custom named popups and class crit descriptions | Existing client naming and description callbacks retain priority. |
| Queue consumption | Existing ProcessQueuedPopupText hook logs shown/dropped status; attribution is captured when requested. |

No PopupEvent ABI change is needed. DDXPopups.TryDescribe and an event-log client
consume the shared native handling automatically.

Limits: a popup outside an effect scope without a new buff has no proven source;
we do not assign the last attacker speculatively. A combined tray activation still
selects one naming buff, as before. Vanilla source categories may be less specific
than a recorded skill origin. This audit establishes the macOS call graph; Windows
uses the existing mapped entry point and needs in-game verification. Delayed monster
skills, simultaneous buff sources, status resists, ripostes and recovery should be
checked in-game; offline builds cannot establish their on-screen timing.

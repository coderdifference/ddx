#ifndef DDX_MODMENU_V1_H
#define DDX_MODMENU_V1_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* DDX built-in form service exports, introduced 2026-10-27.
 * x64 cdecl. All functions and callbacks run synchronously on the game thread.
 * UTF-8 input is borrowed for the call; reply belongs to client until release.
 * NULL reply means failure: retain previous view and show a host error.
 */
typedef char *(*ModMenuDispatch)(const char *json, size_t length);
typedef void (*ModMenuRelease)(char *reply);
int modmenu_host_version(void); /* 1 when platform symbol/prologue preflight passes */
/* Copies id, stores callbacks for process lifetime. Exactly one menu provider.
 * Returns 1 on success, 0 on conflict/unavailable. No callbacks before return. */
int modmenu_register(const char *id, ModMenuDispatch dispatch, ModMenuRelease release);
/* Validate token against active, loaded campaign. Atomically replace the menu
 * namespace in campaign save; return 1 only after profile save, flush and successful namespace read-back, 0 otherwise.
 * Never reenter dispatch from here. Queue settings-changed until it returns. */
int modmenu_commit(const char *token, size_t token_length, const char *json, size_t json_length);
int modmenu_form_register(const char *schema, ModMenuDispatch event, ModMenuRelease release);
const char *modmenu_client_event(const char *id, const char *request);
const char *modmenu_values(const char *id);
#ifdef __cplusplus
}
#endif
#endif

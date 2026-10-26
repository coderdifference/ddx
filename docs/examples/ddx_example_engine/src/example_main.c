// DDX Example's entry point. The library loads DDXExample/engine/ddx_example.dylib at start-up and calls
// engine_client_init once, before its own features install: register everything here.
#include "example.h"

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int engine_client_init(const EngineClientInfo *info) {
    if (!info || info->api != ENGINE_CLIENT_API) { // built against another library version: stay off
        logf_("DDX Example: library client API %d, built for %d", info ? info->api : -1, ENGINE_CLIENT_API);
        return 1;
    }
    log_file("DDXExample");                           // our logf_ lines go to DDXExample.log beside ddx.log
    announce_mod("str_ddx_example_name", NULL, NULL); // listed in the library's "mods built on DDX" announcement
    example_kills_install();
    example_overlay_install();
    logf_("DDX Example: ready (engine folder %s)", info->engine_dir);
    return 0; // anything else: logged, and the client stays off
}

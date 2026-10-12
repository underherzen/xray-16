#pragma once

// Minimal named-slot profiler for the browser build: accumulates wall time
// per slot, read and reset by the dev probes (tools/web, ?mem=) through
// xr_web_prof_report(). Compiles to nothing elsewhere -- use Tracy there.
//
//   XR_WEB_PROF(SoundOcclusion); // times the rest of the enclosing scope

#ifdef XR_PLATFORM_WEB
#include <emscripten/emscripten.h>

namespace xray::web_prof
{
enum Slot
{
    SoundUpdate,
    SoundEmitters,
    SoundOcclusion,
    SoundTargetUpdate,
    SoundListener,
    SoundScene,
    SlotCount
};

inline const char* const names[SlotCount] = {
    "snd_update", "snd_emitters", "snd_occlusion", "snd_target_update", "snd_listener", "snd_scene",
};
inline double ms[SlotCount]{};
inline unsigned calls[SlotCount]{};

struct Scope
{
    Slot slot;
    double start;
    explicit Scope(Slot s) : slot(s), start(emscripten_get_now()) {}
    ~Scope()
    {
        ms[slot] += emscripten_get_now() - start;
        ++calls[slot];
    }
};
} // namespace xray::web_prof

#define XR_WEB_PROF(slot) const ::xray::web_prof::Scope xr_web_prof_scope{ ::xray::web_prof::slot }

// One-shot timeline marks for blocking init code: logs the time since the
// previous mark (wall clock -- CTimer follows the game's pause/time factor).
#define XR_WEB_MARK(what) ::xray::web_prof::mark(what)
namespace xray::web_prof
{
inline double last_mark = 0.0;
inline void mark(const char* what)
{
    const double now = emscripten_get_now();
    if (last_mark > 0.0)
        Msg("* [startup] %s: %.0f ms", what, now - last_mark);
    last_mark = now;
}
} // namespace xray::web_prof
#else
#define XR_WEB_PROF(slot) do {} while (false)
#define XR_WEB_MARK(what) do {} while (false)
#endif

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
#else
#define XR_WEB_PROF(slot) do {} while (false)
#endif

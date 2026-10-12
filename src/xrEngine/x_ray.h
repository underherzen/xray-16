#ifndef __X_RAY_H__
#define __X_RAY_H__

#include <mutex>
#include <array>

#include "xrEngine/Engine.h"

struct SDL_Window;
struct SDL_Surface;

namespace discord
{
class Core;
}

// definition
class ENGINE_API CApplication final
{
    SDL_Window* m_window{};
    std::thread m_splash_thread;
    std::atomic_bool m_should_exit;

    SDL_Surface* m_surface;

private:
    std::mutex m_discord_lock;
    discord::Core* m_discord_core{};

private:
    GameModule* m_game_module{};

private:
    void SplashProc();

    void ShowSplash(bool topmost);
    void HideSplash();

    void InitializeDiscord();
    void UpdateDiscordStatus();

    // One iteration of the application frame loop (window events + a frame),
    // shared by the native while-loop and the web main-loop callback.
    static void RunApplicationFrame(CApplication* app);

#if defined(XR_PLATFORM_WEB)
    // Registered with emscripten_set_main_loop: the browser owns the frame.
    static void WebApplicationFrame();
#endif

    // The application whose frame loop is running; set by Run().
    static CApplication* s_application;

public:
    // Other
    CApplication(pcstr commandLine, GameModule* game, const std::array<RendererModule*, 2>& modules);
    ~CApplication();

    int Run();
};

#endif //__XR_BASE_H__

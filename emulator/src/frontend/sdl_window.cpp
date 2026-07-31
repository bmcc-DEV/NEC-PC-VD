#include "sdl_window.h"
#include <cstdio>
#include <cstdlib>

SdlWindow::SdlWindow() = default;
SdlWindow::~SdlWindow() { close(); }

bool SdlWindow::init(int width, int height, const char* title) {
    // Check if a video driver is available before attempting init
    const char* forced = getenv("SDL_VIDEODRIVER");
    if (!forced || !forced[0]) {
        // Try to detect a display: check DISPLAY (X11) or WAYLAND_DISPLAY
        if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
            fprintf(stderr, "No display detected (DISPLAY/WAYLAND_DISPLAY not set).\n");
            fprintf(stderr, "Set SDL_VIDEODRIVER=dummy for headless, or run in a desktop.\n");
            return false;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    fprintf(stderr, "SDL video driver: %s\n", SDL_GetCurrentVideoDriver());

    // Create window at 2x scale for readability
    int win_w = width * 2;
    int win_h = height * 2;
    m_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                win_w, win_h, SDL_WINDOW_SHOWN);
    if (!m_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
    if (!m_renderer) {
        m_surface = SDL_GetWindowSurface(m_window);
    } else {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
        m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, width, height);
    }

    return true;
}

void SdlWindow::close() {
    if (m_texture) SDL_DestroyTexture(m_texture);
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void SdlWindow::present() {
    if (m_renderer) {
        SDL_RenderPresent(m_renderer);
    } else if (m_surface) {
        SDL_UpdateWindowSurface(m_window);
    }
}

void SdlWindow::render_framebuffer(const uint32_t* pixels, int fb_w, int fb_h) {
    if (m_texture) {
        int win_w, win_h;
        SDL_GetWindowSize(m_window, &win_w, &win_h);
        int tex_pitch = fb_w * 4;
        SDL_UpdateTexture(m_texture, nullptr, pixels, tex_pitch);
        SDL_RenderClear(m_renderer);
        SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
    } else if (m_surface) {
        SDL_LockSurface(m_surface);
        auto* dst = (uint32_t*)m_surface->pixels;
        int dst_pitch = m_surface->pitch / 4;
        int copy_w = std::min(fb_w, m_surface->w);
        int copy_h = std::min(fb_h, m_surface->h);
        for (int y = 0; y < copy_h; y++)
            memcpy(dst + y * dst_pitch, pixels + y * fb_w, copy_w * 4);
        SDL_UnlockSurface(m_surface);
    }
}

bool SdlWindow::poll_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) m_quit = true;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) m_quit = true;
    }
    return !m_quit;
}

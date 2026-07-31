#pragma once
#include <cstdint>
#include <SDL2/SDL.h>

class SdlWindow {
public:
    SdlWindow();
    ~SdlWindow();

    bool init(int width, int height, const char* title);
    void close();

    void present();
    void set_pixel(int x, int y, uint32_t color);
    void render_framebuffer(const uint32_t* pixels, int fb_w, int fb_h);

    bool poll_events();
    bool should_quit() const { return m_quit; }

    SDL_Window* window() { return m_window; }

private:
    SDL_Window* m_window = nullptr;
    SDL_Surface* m_surface = nullptr;
    SDL_Texture* m_texture = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    bool m_quit = false;
};

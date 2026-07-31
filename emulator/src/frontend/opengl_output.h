#pragma once
#include <cstdint>

class SdlWindow;

class OpenGLOutput {
public:
    OpenGLOutput();
    ~OpenGLOutput();

    bool init(SdlWindow& window);
    void render(const uint32_t* pixels, int width, int height);
    void present();

private:
    bool m_initialized = false;
};

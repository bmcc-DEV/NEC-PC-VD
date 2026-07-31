#include "opengl_output.h"
#include "sdl_window.h"
#include <cstdio>

OpenGLOutput::OpenGLOutput() = default;
OpenGLOutput::~OpenGLOutput() = default;

bool OpenGLOutput::init(SdlWindow& window) {
    m_initialized = true;
    return true;
}

void OpenGLOutput::render(const uint32_t* pixels, int width, int height) {
    // Placeholder — será implementado com shaders OpenGL quando
    // o Voodoo 3D estiver funcionando
}

void OpenGLOutput::present() {
}

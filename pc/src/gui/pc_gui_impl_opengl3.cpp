/* Dear ImGui's OpenGL 3 renderer with the port's GL: glad on desktops (the
 * functions it loaded), OpenGL ES 3.0 on Android (PC_GLES) */
#ifdef PC_GLES
#define IMGUI_IMPL_OPENGL_ES3
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif
#include "backends/imgui_impl_opengl3.cpp"

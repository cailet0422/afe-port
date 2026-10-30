/* pc_gl.h - OpenGL 3.3 core (desktop, through glad) or OpenGL ES 3.0
 * (PC_GLES: Android, or a desktop build that tests the ES path).
 *
 * The renderer uses the subset the two share; this header papers over the
 * few calls that differ. Shaders are written for "#version 330 core" and
 * pc_gl_shader_source rewrites that line for ES. */
#ifndef PC_GL_H
#define PC_GL_H

#ifdef PC_GLES
#include <GLES3/gl3.h>

#define glClearDepth(depth) glClearDepthf((GLfloat)(depth))
#define glDepthRange(nearz, farz) glDepthRangef((GLfloat)(nearz), (GLfloat)(farz))
#define glPointSize(size) ((void)(size)) /* points are sized in the shader in ES */
#else
#include <glad/gl.h>
#endif

#include <stdlib.h>
#include <string.h>

/* glShaderSource for a "#version 330 core" source */
static inline void pc_gl_shader_source(GLuint shader, const char* source) {
#ifdef PC_GLES
    static const char es_header[] = "#version 300 es\n"
                                    "precision highp float;\n"
                                    "precision highp int;\n"
                                    "precision highp sampler2D;\n";
    const char* sources[2] = { es_header, source };

    if (strncmp(source, "#version", 8) == 0) {
        const char* line_end = strchr(source, '\n');

        sources[1] = line_end != NULL ? line_end + 1 : "";
    }
    glShaderSource(shader, 2, sources, NULL);
#else
    glShaderSource(shader, 1, &source, NULL);
#endif
}

#endif

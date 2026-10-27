#pragma once
// Android: OpenGL ES 3.2 stands in for GLEW (PrusaSlicer is built with SLIC3R_OPENGL_ES).
// Provides the few GLEW symbols the portable render code uses, plus the desktop-only GL names
// it still references outside of its ES branches.
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#define GLEW_OK 0
#define GLEW_NO_ERROR 0
#define GLEW_ARB_compatibility 0
#define GLEW_OES_vertex_array_object 1

static inline int glewInit(void) { return GLEW_OK; }

// Desktop-only enums. Only referenced in tables / code paths that ES never takes.
#ifndef GL_TEXTURE_1D
#define GL_TEXTURE_1D 0x0DE0
#endif
#ifndef GL_TEXTURE_1D_ARRAY
#define GL_TEXTURE_1D_ARRAY 0x8C18
#endif
#ifndef GL_TEXTURE_RECTANGLE
#define GL_TEXTURE_RECTANGLE 0x84F5
#endif
#ifndef GL_MIRROR_CLAMP_TO_EDGE
#define GL_MIRROR_CLAMP_TO_EDGE 0x8743
#endif
#ifndef GL_COMPRESSED_RGB
#define GL_COMPRESSED_RGB 0x84ED
#endif
#ifndef GL_COMPRESSED_RGBA
#define GL_COMPRESSED_RGBA 0x84EE
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif

// Multisampling is always on for a multisampled EGL surface and cannot be toggled in ES.
#define GL_MULTISAMPLE 0x809D
static inline void slic3r_gles_enable(GLenum cap) { if (cap != GL_MULTISAMPLE) glEnable(cap); }
static inline void slic3r_gles_disable(GLenum cap) { if (cap != GL_MULTISAMPLE) glDisable(cap); }
#define glEnable slic3r_gles_enable
#define glDisable slic3r_gles_disable

static inline void glClearDepth(double depth) { glClearDepthf((GLfloat) depth); }

static inline void glDrawBuffer(GLenum buffer) { glDrawBuffers(1, &buffer); }

static inline void* glMapBuffer(GLenum target, GLenum access)
{
    GLint size = 0;
    glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    const GLbitfield flags = access == GL_READ_ONLY  ? GL_MAP_READ_BIT
                           : access == GL_WRITE_ONLY ? GL_MAP_WRITE_BIT
                                                     : (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
    return glMapBufferRange(target, 0, size, flags);
}

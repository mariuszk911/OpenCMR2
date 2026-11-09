/*
 * The GL ES 2.0 calls the GLES2 renderer makes, as a table of function
 * pointers, and the context the renderer draws in (AmigaOS only).
 *
 * The renderer (gfx_gles2.cpp) shares port/types.h with the game, whose BOOL,
 * LONG and ULONG clash with the NDK's, so it cannot include <proto/ogles2.h>.
 * ogles2_runtime.c does, and fills this table from the SDK's wrappers. Only
 * GLES2/gl2.h (plain C types) is used here.
 */
#ifndef OPENCMR2_RENDER_GLES2_GLES2_API_H
#define OPENCMR2_RENDER_GLES2_GLES2_API_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* X(return type, name, (parameters), (arguments)); the call is gl<name>. */
#define GLES2_FUNCTIONS(X)                                                                                         \
    X(const GLubyte *, GetString, (GLenum name), (name))                                                           \
    X(void, GetIntegerv, (GLenum pname, GLint * params), (pname, params))                                          \
    X(GLenum, GetError, (void), ())                                                                                \
    X(void, Enable, (GLenum cap), (cap))                                                                           \
    X(void, Disable, (GLenum cap), (cap))                                                                          \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h), (x, y, w, h))                                      \
    X(void, Scissor, (GLint x, GLint y, GLsizei w, GLsizei h), (x, y, w, h))                                       \
    X(void, DepthRangef, (GLfloat n, GLfloat f), (n, f))                                                           \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a), (r, g, b, a))                                \
    X(void, ClearDepthf, (GLfloat d), (d))                                                                         \
    X(void, Clear, (GLbitfield mask), (mask))                                                                      \
    X(void, ColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a), (r, g, b, a))                         \
    X(void, DepthMask, (GLboolean flag), (flag))                                                                   \
    X(void, DepthFunc, (GLenum func), (func))                                                                      \
    X(void, BlendFunc, (GLenum s, GLenum d), (s, d))                                                               \
    X(void, CullFace, (GLenum mode), (mode))                                                                       \
    X(void, FrontFace, (GLenum mode), (mode))                                                                      \
    X(void, PixelStorei, (GLenum pname, GLint param), (pname, param))                                              \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *data),          \
      (x, y, w, h, format, type, data))                                                                            \
    X(void, Finish, (void), ())                                                                                    \
    X(void, GenTextures, (GLsizei n, GLuint * textures), (n, textures))                                            \
    X(void, DeleteTextures, (GLsizei n, const GLuint *textures), (n, textures))                                    \
    X(void, BindTexture, (GLenum target, GLuint texture), (target, texture))                                       \
    X(void, ActiveTexture, (GLenum texture), (texture))                                                            \
    X(void, TexImage2D,                                                                                            \
      (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format,        \
       GLenum type, const void *data),                                                                             \
      (target, level, internalformat, w, h, border, format, type, data))                                           \
    X(void, TexSubImage2D,                                                                                         \
      (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,             \
       const void *data),                                                                                          \
      (target, level, x, y, w, h, format, type, data))                                                             \
    X(void, CompressedTexImage2D,                                                                                  \
      (GLenum target, GLint level, GLenum internalformat, GLsizei w, GLsizei h, GLint border, GLsizei size,        \
       const void *data),                                                                                          \
      (target, level, internalformat, w, h, border, size, data))                                                   \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param), (target, pname, param))                     \
    X(void, CopyTexSubImage2D,                                                                                     \
      (GLenum target, GLint level, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h),                    \
      (target, level, xo, yo, x, y, w, h))                                                                         \
    X(void, GenBuffers, (GLsizei n, GLuint * buffers), (n, buffers))                                               \
    X(void, DeleteBuffers, (GLsizei n, const GLuint *buffers), (n, buffers))                                       \
    X(void, BindBuffer, (GLenum target, GLuint buffer), (target, buffer))                                          \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage), (target, size, data, usage)) \
    X(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data),                    \
      (target, offset, size, data))                                                                                \
    X(void, EnableVertexAttribArray, (GLuint index), (index))                                                      \
    X(void, DisableVertexAttribArray, (GLuint index), (index))                                                     \
    X(void, VertexAttribPointer,                                                                                   \
      (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer),          \
      (index, size, type, normalized, stride, pointer))                                                            \
    X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count), (mode, first, count))                           \
    X(void, DrawElements, (GLenum mode, GLsizei count, GLenum type, const void *indices),                          \
      (mode, count, type, indices))                                                                                \
    X(void, DrawElementsBaseVertexOES,                                                                             \
      (GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex),                            \
      (mode, count, type, indices, basevertex))                                                                    \
    X(void, GenFramebuffers, (GLsizei n, GLuint * fbs), (n, fbs))                                                  \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint *fbs), (n, fbs))                                          \
    X(void, BindFramebuffer, (GLenum target, GLuint fb), (target, fb))                                             \
    X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level), \
      (target, attachment, textarget, texture, level))                                                             \
    X(void, FramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb),               \
      (target, attachment, rbtarget, rb))                                                                          \
    X(GLenum, CheckFramebufferStatus, (GLenum target), (target))                                                   \
    X(void, GenRenderbuffers, (GLsizei n, GLuint * rbs), (n, rbs))                                                 \
    X(void, DeleteRenderbuffers, (GLsizei n, const GLuint *rbs), (n, rbs))                                         \
    X(void, BindRenderbuffer, (GLenum target, GLuint rb), (target, rb))                                            \
    X(void, RenderbufferStorage, (GLenum target, GLenum format, GLsizei w, GLsizei h), (target, format, w, h))     \
    X(GLuint, CreateShader, (GLenum type), (type))                                                                 \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length),       \
      (shader, count, string, length))                                                                             \
    X(void, CompileShader, (GLuint shader), (shader))                                                              \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint * params), (shader, pname, params))                   \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei size, GLsizei * length, GLchar * log),                       \
      (shader, size, length, log))                                                                                 \
    X(void, DeleteShader, (GLuint shader), (shader))                                                               \
    X(GLuint, CreateProgram, (void), ())                                                                           \
    X(void, AttachShader, (GLuint program, GLuint shader), (program, shader))                                      \
    X(void, BindAttribLocation, (GLuint program, GLuint index, const GLchar *name), (program, index, name))        \
    X(void, LinkProgram, (GLuint program), (program))                                                              \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint * params), (program, pname, params))                \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei size, GLsizei * length, GLchar * log),                     \
      (program, size, length, log))                                                                                \
    X(void, DeleteProgram, (GLuint program), (program))                                                            \
    X(void, UseProgram, (GLuint program), (program))                                                               \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar *name), (program, name))                            \
    X(void, Uniform1i, (GLint location, GLint v0), (location, v0))                                                 \
    X(void, Uniform4fv, (GLint location, GLsizei count, const GLfloat *value), (location, count, value))

typedef struct Gles2Api {
#define GLES2_MEMBER(ret, name, params, args) ret(*name) params;
    GLES2_FUNCTIONS(GLES2_MEMBER)
#undef GLES2_MEMBER
} Gles2Api;

/* The display: a full-screen context on a 32-bit RTG mode (ogles2_runtime.c). */
typedef struct Gles2Display {
    unsigned width, height;   /* the screen's size in pixels */
    void *inputWindow;        /* the window that receives input events */
} Gles2Display;

/* Opens ogles2.library (1.3.3 or newer), finds a 32-bit mode, creates a
   full-screen context with a 24-bit depth buffer and makes it current. Fills
   *api (valid until Gles2Display_Close) and *display. On failure returns 0 and
   sets *why to a static sentence. */
int Gles2Display_Open(int vsync, Gles2Api *api, Gles2Display *display, const char **why);
/* Shows the frame drawn since the last swap. */
void Gles2Display_Swap(void);
void Gles2Display_SetVsync(int vsync);
/* Destroys the context and closes the library. Safe when not open. */
void Gles2Display_Close(void);
/* Whether a context could be opened here, without creating one: sets
   *libraryOpens (ogles2.library 1.3.3 or newer found) and returns 1 when a
   32-bit mode was found too. *why is a static sentence when it returns 0. */
int Gles2Display_Probe(int *libraryOpens, const char **why);

#ifdef __cplusplus
}
#endif

#endif

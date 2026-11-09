// The OpenGL ES 2.0 executor for AmigaOS (ogles2.library on a PiStorm's V3D):
// runs a frame recorded by gfx_core.cpp, as gfx_gl.cpp does on OpenGL 3.3, with
// the shaders of gfx_gles2_shaders.h and the uniform packing of
// gles2_uniforms.h. See docs/AMIGA_GLES2_PLAN.md.
//
// Images keep Direct3D's rows, as in gfx_gl.cpp: the first row of every target
// is its top, the vertex shaders flip y, faces wind the other way round, and
// only the copy to the screen is drawn the right way up.
//
// What is left out against the host executor, on purpose: the distance fade
// (Gfx_CaptureBackdrop; the Amiga uses fixed-function fog as MiniGL did) and the
// distance-field, menu-dot and vector-icon textures (host-only enhancements).
#include "port/gfx_texel.h"
#include "port/sys.h"
#include "platform/amiga/amiga_sys.h"
#include "render/gfx_internal.h"
#include "render/gles2/gfx_gles2_shaders.h"
#include "render/gles2/gles2_api.h"
#include "render/gles2/gles2_uniforms.h"
#include "render/presentation.h"

#include <math.h>
#include <string.h>

#include <algorithm>
#include <vector>

#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif

namespace {

Gles2Api gl;

GLenum BlendFactor(int d3d)
{
    switch (d3d) {
    case GFX_BLEND_ZERO: return GL_ZERO;
    case GFX_BLEND_ONE: return GL_ONE;
    case GFX_BLEND_SRCCOLOR: return GL_SRC_COLOR;
    case GFX_BLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case GFX_BLEND_SRCALPHA: return GL_SRC_ALPHA;
    case GFX_BLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case GFX_BLEND_DESTALPHA: return GL_DST_ALPHA;
    case GFX_BLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case GFX_BLEND_DESTCOLOR: return GL_DST_COLOR;
    case GFX_BLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case GFX_BLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
    }
}

GLenum CompareFunc(int d3d)
{
    switch (d3d) {
    case GFX_CMP_NEVER: return GL_NEVER;
    case GFX_CMP_LESS: return GL_LESS;
    case GFX_CMP_EQUAL: return GL_EQUAL;
    case GFX_CMP_LESSEQUAL: return GL_LEQUAL;
    case GFX_CMP_GREATER: return GL_GREATER;
    case GFX_CMP_NOTEQUAL: return GL_NOTEQUAL;
    case GFX_CMP_GREATEREQUAL: return GL_GEQUAL;
    default: return GL_ALWAYS;
    }
}

GLenum PrimitiveMode(int d3d)
{
    switch (d3d) {
    case GFX_POINTLIST: return GL_POINTS;
    case GFX_LINELIST: return GL_LINES;
    case GFX_LINESTRIP: return GL_LINE_STRIP;
    case GFX_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
    default: return GL_TRIANGLES;
    }
}

struct GlTexture {
    GLuint texture = 0;
    GLuint depth = 0;               // render targets: depth and stencil
    GLuint framebuffers[6] = {};    // render targets: one per face
    bool compressed = false;        // BC data uploaded as it is
    bool specified = false;         // storage made (later uploads replace pixels)
    uint32_t samplerKey = ~0u;      // sampler state last set on the texture
};

struct GlVertexBuffer {
    GLuint buffer = 0;
    uint32_t version = 0;
};

struct Program {
    GLuint id = 0;
    GLint locV = -1, locF = -1;
    int vertexVec4 = 0;
    // The uniform block (offset in its vector) last given to this program in
    // this Execute, -1: none.
    long lastV = -1, lastF = -1;
};

// What a draw needs besides its state: where its uniforms are.
struct DrawUniforms {
    long vertexOffset = -1, fragmentOffset = -1;   // float offsets into the frame's vectors
};

struct Executor : GfxExecutor {
    bool open = false;
    Gles2Display display = {};
    Program program3d, programTL, programBlit;
    GLuint vertexBuffer = 0, indexBuffer = 0, quadBuffer = 0;
    GLuint backFramebuffer = 0, backColor = 0, backDepth = 0;
    int backWidth = 0, backHeight = 0;
    GLuint white2D = 0, whiteCube = 0;
    bool bcSupported = false, baseVertex = false;
    int vsync = -1;
    int boundAttributes = 0;    // vertex attribute arrays enabled
    std::vector<float> vertexData3D, vertexDataTL, fragmentData;
    std::vector<DrawUniforms> drawUniforms;

    struct Bound {
        GLuint program = 0, framebuffer = ~0u, vertices = ~0u;
        bool transformed = false;
        long firstVertexBound = -1;    // pointer offset of the bound attributes (no base vertex)
        int blend = -1, srcBlend = -1, destBlend = -1, depthTest = -1, depthWrite = -1, depthFunc = -1, cull = -1;
        GLint viewport[4] = { -1, -1, -1, -1 }, scissor[4] = { -1, -1, -1, -1 };
        float depthRange[2] = { -1, -1 };
        GLuint textures[4] = { ~0u, ~0u, ~0u, ~0u };
    } bound;

    ~Executor() override
    {
        if (!open)
            return;
        // The input window belongs to the context: detach it first.
        AmigaSys_SetWindow(NULL);
        GLuint textures[] = { backColor, white2D, whiteCube };
        gl.DeleteTextures(3, textures);
        gl.DeleteRenderbuffers(1, &backDepth);
        gl.DeleteFramebuffers(1, &backFramebuffer);
        GLuint buffers[] = { vertexBuffer, indexBuffer, quadBuffer };
        gl.DeleteBuffers(3, buffers);
        gl.DeleteProgram(program3d.id);
        gl.DeleteProgram(programTL.id);
        gl.DeleteProgram(programBlit.id);
        Gles2Display_Close();
    }

    // ---- device ----

    GLuint Compile(GLenum stage, const char *source)
    {
        GLuint shader = gl.CreateShader(stage);
        gl.ShaderSource(shader, 1, &source, NULL);
        gl.CompileShader(shader);
        GLint ok = 0;
        gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            gl.GetShaderInfoLog(shader, sizeof(log), NULL, log);
            Sys_Log("gfx: gles2 shader: %s", log);
            gl.DeleteShader(shader);
            return 0;
        }
        return shader;
    }

    bool Link(Program &p, const char *vertexSource, const char *fragmentSource, const char *const *attributes,
              int attributeCount, int vertexVec4)
    {
        GLuint vs = Compile(GL_VERTEX_SHADER, vertexSource), fs = Compile(GL_FRAGMENT_SHADER, fragmentSource);
        if (vs == 0 || fs == 0)
            return false;
        GLuint id = gl.CreateProgram();
        gl.AttachShader(id, vs);
        gl.AttachShader(id, fs);
        for (int i = 0; i < attributeCount; i++)
            gl.BindAttribLocation(id, (GLuint)i, attributes[i]);
        gl.LinkProgram(id);
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        GLint ok = 0;
        gl.GetProgramiv(id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048];
            gl.GetProgramInfoLog(id, sizeof(log), NULL, log);
            Sys_Log("gfx: gles2 program: %s", log);
            gl.DeleteProgram(id);
            return false;
        }
        gl.UseProgram(id);
        bound.program = id;
        static const char *const samplerNames[4] = { "tex0", "cube0", "tex1", "cube1" };
        for (int i = 0; i < 4; i++) {
            GLint loc = gl.GetUniformLocation(id, samplerNames[i]);
            if (loc >= 0)
                gl.Uniform1i(loc, i);
        }
        p.id = id;
        p.locV = gl.GetUniformLocation(id, "u_v");
        p.locF = gl.GetUniformLocation(id, "u_f");
        p.vertexVec4 = vertexVec4;
        return true;
    }

    bool HasExtension(const char *name)
    {
        const char *list = (const char *)gl.GetString(GL_EXTENSIONS);
        if (list == NULL)
            return false;
        const size_t length = strlen(name);
        for (const char *p = list; (p = strstr(p, name)) != NULL; p += length) {
            const bool start = p == list || p[-1] == ' ';
            const char end = p[length];
            if (start && (end == ' ' || end == '\0'))
                return true;
        }
        return false;
    }

    GLuint WhiteTexture(GLenum target)
    {
        static const uint8_t white[4] = { 255, 255, 255, 255 };
        GLuint texture;
        gl.GenTextures(1, &texture);
        gl.BindTexture(target, texture);
        if (target == GL_TEXTURE_CUBE_MAP) {
            for (int face = 0; face < 6; face++)
                gl.TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                              white);
        } else {
            gl.TexImage2D(target, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        }
        gl.TexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return texture;
    }

    bool CreateDevice()
    {
        const char *why = "";
        vsync = Sys_GetOption("video.vsync", 1) != 0;
        if (!Gles2Display_Open(vsync, &gl, &display, &why)) {
            Sys_Log("gfx: gles2: %s", why);
            return false;
        }
        open = true;
        AmigaSys_SetWindow(display.inputWindow);
        Sys_Log("gfx: gles2: %s (%s), screen %ux%u, input window %lx", (const char *)gl.GetString(GL_RENDERER),
                (const char *)gl.GetString(GL_VERSION), display.width, display.height,
                (unsigned long)display.inputWindow);
        bcSupported = HasExtension("GL_EXT_texture_compression_s3tc");
        baseVertex = HasExtension("GL_OES_draw_elements_base_vertex");
        Sys_Log("gfx: gles2: S3TC %s, base vertex draws %s", bcSupported ? "native" : "decoded",
                baseVertex ? "native" : "by attribute offsets");

        static const char *const attributes3D[6] = { "aPosition", "aNormal", "aDiffuse", "aSpecular", "aTex0", "aTex1" };
        static const char *const attributesTL[4] = { "aPosition", "aDiffuse", "aSpecular", "aTex" };
        static const char *const attributesBlit[1] = { "aPosition" };
        if (!Link(program3d, kGles2Vertex3D, kGles2Fragment, attributes3D, 6, GLES2_VERTEX_VEC4) ||
            !Link(programTL, kGles2VertexTL, kGles2Fragment, attributesTL, 4, GLES2_TL_VEC4) ||
            !Link(programBlit, kGles2VertexBlit, kGles2FragmentBlit, attributesBlit, 1, 0))
            return false;
        white2D = WhiteTexture(GL_TEXTURE_2D);
        whiteCube = WhiteTexture(GL_TEXTURE_CUBE_MAP);
        GLuint buffers[3];
        gl.GenBuffers(3, buffers);
        vertexBuffer = buffers[0];
        indexBuffer = buffers[1];
        quadBuffer = buffers[2];
        static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
        gl.BindBuffer(GL_ARRAY_BUFFER, quadBuffer);
        gl.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl.Enable(GL_SCISSOR_TEST);
        // Direct3D's front faces are clockwise; drawn upside down they wind
        // counter-clockwise.
        gl.FrontFace(GL_CCW);
        const GLenum error = gl.GetError();
        if (error != GL_NO_ERROR)
            Sys_Log("gfx: gles2: error %#x setting up", (unsigned)error);
        return true;
    }

    bool SetVideoMode(int width, int height, bool) override
    {
        if (!open && !CreateDevice())
            return false;
        if (backFramebuffer == 0 || backWidth != width || backHeight != height) {
            if (backFramebuffer == 0) {
                gl.GenFramebuffers(1, &backFramebuffer);
                gl.GenTextures(1, &backColor);
                gl.GenRenderbuffers(1, &backDepth);
            }
            gl.BindTexture(GL_TEXTURE_2D, backColor);
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl.BindRenderbuffer(GL_RENDERBUFFER, backDepth);
            gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, width, height);
            gl.BindFramebuffer(GL_FRAMEBUFFER, backFramebuffer);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, backColor, 0);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, backDepth);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, backDepth);
            bound.framebuffer = backFramebuffer;
            InvalidateTextureBinding();
            if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                // Without packed depth and stencil: 16-bit depth alone.
                gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
                gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
                if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    Sys_Log("gfx: gles2: back buffer %dx%d incomplete", width, height);
                    return false;
                }
                Sys_Log("gfx: gles2: back buffer with 16-bit depth");
            }
            backWidth = width;
            backHeight = height;
        }
        return true;
    }

    void SetFullscreen(bool) override {}

    // ---- textures ----

    GlTexture *Ensure(GfxTexture *t)
    {
        if (t->gpu != NULL)
            return (GlTexture *)t->gpu;
        GlTexture *g = new GlTexture;
        g->compressed = (t->format == GFX_FORMAT_BC1 || t->format == GFX_FORMAT_BC3) && bcSupported;
        gl.GenTextures(1, &g->texture);
        const GLenum target = (t->flags & GFX_TEXTURE_CUBE) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        gl.BindTexture(target, g->texture);
        gl.TexParameteri(target, GL_TEXTURE_MAX_LEVEL, t->mipLevels - 1);
        InvalidateTextureBinding();
        t->gpu = g;
        if (t->flags & GFX_TEXTURE_RENDER_TARGET) {
            // Storage now, so that it can be drawn into before any upload.
            Upload(t, g);
            gl.GenRenderbuffers(1, &g->depth);
            gl.BindRenderbuffer(GL_RENDERBUFFER, g->depth);
            gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, t->width, t->height);
        }
        t->dirty = true;
        return g;
    }

    // A level as uploaded: R, G, B, A bytes from the CPU copy, or the BC
    // blocks as they are where the GPU takes them.
    void ConvertLevel(GfxTexture *t, GlTexture *g, const uint8_t *src, int w, int h, std::vector<uint8_t> &out)
    {
        const size_t n = (size_t)w * h;
        if (g->compressed) {
            out.assign(src, src + Gfx_LevelSize(t->format, w, h, NULL));
            return;
        }
        out.resize(n * 4);
        uint8_t *dst = out.data();
        switch (t->format) {
        case GFX_FORMAT_BGRX8:
            GfxTexel_BgraToRgba(src, dst, n, 1);
            break;
        case GFX_FORMAT_BGRA8:
            GfxTexel_BgraToRgba(src, dst, n, 0);
            break;
        case GFX_FORMAT_BUMP_DUDVL:
            GfxTexel_BumpToRgba(src, dst, n);
            break;
        case GFX_FORMAT_BC1:
        case GFX_FORMAT_BC3: {
            const int blockBytes = t->format == GFX_FORMAT_BC1 ? 8 : 16;
            const int bw = (w + 3) / 4, bh = (h + 3) / 4;
            uint32_t block[16];
            for (int by = 0; by < bh; by++)
                for (int bx = 0; bx < bw; bx++) {
                    Gfx_DecodeBlock(t->format, src + (by * bw + bx) * blockBytes, block);
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++)
                            if (bx * 4 + x < w && by * 4 + y < h) {
                                // block[] holds B, G, R, A in a word's byte order.
                                const uint32_t px = block[y * 4 + x];
                                uint8_t *d = dst + ((size_t)(by * 4 + y) * w + bx * 4 + x) * 4;
                                d[0] = (uint8_t)(px >> 16);
                                d[1] = (uint8_t)(px >> 8);
                                d[2] = (uint8_t)px;
                                d[3] = (uint8_t)(px >> 24);
                            }
                }
            break;
        }
        default:
            memcpy(dst, src, n * 4);
            break;
        }
    }

    // Every level of every face of a texture from its CPU copy.
    void Upload(GfxTexture *t, GlTexture *g)
    {
        const bool cube = (t->flags & GFX_TEXTURE_CUBE) != 0;
        const GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(target, g->texture);
        InvalidateTextureBinding();
        const GLenum compressedFormat =
            t->format == GFX_FORMAT_BC1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        std::vector<uint8_t> level;
        for (int f = 0; f < (cube ? 6 : 1); f++) {
            const GLenum face = cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + f : GL_TEXTURE_2D;
            for (int m = 0; m < t->mipLevels; m++) {
                const int w = std::max(1, t->width >> m), h = std::max(1, t->height >> m);
                const std::vector<uint8_t> &cpu = t->levels[f][m];
                if (cpu.empty()) {
                    if (!g->compressed)
                        gl.TexImage2D(face, m, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                    continue;
                }
                ConvertLevel(t, g, cpu.data(), w, h, level);
                if (g->compressed)
                    gl.CompressedTexImage2D(face, m, compressedFormat, w, h, 0, (GLsizei)level.size(), level.data());
                else if (g->specified)
                    gl.TexSubImage2D(face, m, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, level.data());
                else
                    gl.TexImage2D(face, m, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, level.data());
            }
        }
        g->specified = true;
    }

    void UploadTextures(std::vector<GfxTexture *> &textures)
    {
        for (GfxTexture *t : textures) {
            if (!t->dirty)
                continue;
            Upload(t, Ensure(t));
            t->dirty = false;
        }
    }

    void DestroyTexture(GfxTexture *t) override
    {
        GlTexture *g = (GlTexture *)t->gpu;
        if (g == NULL || !open)
            return;
        gl.DeleteTextures(1, &g->texture);
        gl.DeleteRenderbuffers(1, &g->depth);
        for (GLuint &framebuffer : g->framebuffers)
            if (framebuffer != 0) {
                if (bound.framebuffer == framebuffer)
                    bound.framebuffer = ~0u;
                gl.DeleteFramebuffers(1, &framebuffer);
            }
        InvalidateTextureBinding();
        delete g;
        t->gpu = NULL;
    }

    // The framebuffer drawing into a texture (or a face of a cube).
    GLuint FramebufferFor(GfxTexture *root, int face)
    {
        GlTexture *g = Ensure(root);
        GLuint &framebuffer = g->framebuffers[face];
        if (framebuffer == 0) {
            gl.GenFramebuffers(1, &framebuffer);
            gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                    (root->flags & GFX_TEXTURE_CUBE) ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face
                                                                     : GL_TEXTURE_2D,
                                    g->texture, 0);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g->depth);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, g->depth);
            if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                Sys_Log("gfx: gles2: render target %dx%d incomplete", root->width, root->height);
            bound.framebuffer = framebuffer;
        }
        return framebuffer;
    }

    GLuint Framebuffer(GfxTexture *target)
    {
        GfxTexture *root = target->cube != NULL ? target->cube : target;
        return FramebufferFor(root, target->cube != NULL ? target->face : 0);
    }

    void ReadTexture(GfxTexture *t) override
    {
        GlTexture *g = (GlTexture *)t->gpu;
        if (g == NULL || (t->format != GFX_FORMAT_BGRX8 && t->format != GFX_FORMAT_BGRA8))
            return;
        const bool cube = (t->flags & GFX_TEXTURE_CUBE) != 0;
        std::vector<uint8_t> rgba((size_t)t->width * t->height * 4);
        for (int f = 0; f < (cube ? 6 : 1); f++) {
            BindFramebuffer(FramebufferFor(t, f));
            gl.ReadPixels(0, 0, t->width, t->height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            if (t->levels[f][0].size() >= rgba.size())
                GfxTexel_RgbaToBgra(rgba.data(), t->levels[f][0].data(), (size_t)t->width * t->height);
        }
    }

    void InvalidateTextureBinding()
    {
        for (GLuint &texture : bound.textures)
            texture = ~0u;
    }

    // ---- sampler state (per texture in ES 2.0) ----

    void ApplySampler(GlTexture *g, GfxTexture *t, const GfxStageState &stage, GLenum target)
    {
        const bool mips = t->mipLevels > 1 && stage.mipFilter != GFX_MIPFILTER_NONE;
        const uint32_t key = (uint32_t)(stage.minFilter & 3) | (uint32_t)(stage.magFilter & 3) << 2 |
                             (uint32_t)(mips ? stage.mipFilter & 3 : 1) << 4 | (uint32_t)(stage.address & 3) << 6;
        if (g->samplerKey == key)
            return;
        const bool linear = stage.minFilter == GFX_FILTER_LINEAR;
        GLint minFilter;
        if (!mips)
            minFilter = linear ? GL_LINEAR : GL_NEAREST;
        else if (stage.mipFilter == GFX_MIPFILTER_LINEAR)
            minFilter = linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
        else
            minFilter = linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
        const GLint wrap = stage.address == GFX_ADDRESS_CLAMP    ? GL_CLAMP_TO_EDGE
                           : stage.address == GFX_ADDRESS_MIRROR ? GL_MIRRORED_REPEAT
                                                                 : GL_REPEAT;
        gl.TexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
        gl.TexParameteri(target, GL_TEXTURE_MAG_FILTER, stage.magFilter == GFX_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
        gl.TexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
        gl.TexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
        g->samplerKey = key;
    }

    int TextureKind(const GfxStageState &stage)
    {
        GfxTexture *t = Gfx_DrawnTexture(stage.texture);
        if (t == NULL)
            return GLES2_KIND_NONE;
        return (t->flags & GFX_TEXTURE_CUBE) ? GLES2_KIND_CUBE : GLES2_KIND_2D;
    }

    // The two stages' textures: units 0 and 2 for 2D textures, 1 and 3 for
    // cubes (the white ones where the stage has none of that kind).
    void BindTextures(const GfxDrawState &s)
    {
        for (int i = 0; i < 2; i++) {
            const GfxStageState &stage = s.stages[i];
            GfxTexture *t = Gfx_DrawnTexture(stage.texture);
            GlTexture *g = t != NULL ? Ensure(t) : NULL;
            const bool cube = t != NULL && (t->flags & GFX_TEXTURE_CUBE);
            const GLuint textures[2] = { g != NULL && !cube ? g->texture : white2D,
                                         g != NULL && cube ? g->texture : whiteCube };
            for (int k = 0; k < 2; k++) {
                const int unit = i * 2 + k;
                const GLenum target = k == 0 ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
                if (bound.textures[unit] != textures[k]) {
                    gl.ActiveTexture(GL_TEXTURE0 + unit);
                    gl.BindTexture(target, textures[k]);
                    bound.textures[unit] = textures[k];
                }
                if (g != NULL && (k == 1) == cube) {
                    gl.ActiveTexture(GL_TEXTURE0 + unit);
                    ApplySampler(g, t, stage, target);
                }
            }
        }
    }

    // ---- uniforms ----

    static GfxViewport ToTarget(const GfxViewport &v, int screenW, int screenH, bool backBuffer, int targetW,
                                int targetH)
    {
        if (!backBuffer || screenW <= 0 || screenH <= 0)
            return v;
        const float kx = (float)targetW / screenW, ky = (float)targetH / screenH;
        const int x0 = std::min(std::max((int)lroundf(v.x * kx), 0), targetW);
        const int y0 = std::min(std::max((int)lroundf(v.y * ky), 0), targetH);
        const int x1 = std::min(std::max((int)lroundf((v.x + v.width) * kx), x0), targetW);
        const int y1 = std::min(std::max((int)lroundf((v.y + v.height) * ky), y0), targetH);
        GfxViewport r = v;
        r.x = x0;
        r.y = y0;
        r.width = x1 - x0;
        r.height = y1 - y0;
        return r;
    }

    // Every draw's uniforms, written once for the frame: a block is written
    // when it differs from the previous draw's, and a program is given a block
    // only when it is not the one it holds.
    void BuildUniforms(const GfxFrame &frame)
    {
        vertexData3D.clear();
        vertexDataTL.clear();
        fragmentData.clear();
        drawUniforms.assign(frame.commands.size(), DrawUniforms());
        GfxTexture *target = NULL;
        const GfxDrawState *vertexState = NULL;
        int vertexW = 0, vertexH = 0;
        bool vertexScaled = false, vertexTransformed = false;
        long vertexOffset = -1, fragmentOffset = -1;
        float fragment[GLES2_FRAGMENT_VEC4 * 4];
        int lastKind0 = -1, lastKind1 = -1;
        for (size_t i = 0; i < frame.commands.size(); i++) {
            const GfxCommand &c = frame.commands[i];
            if (c.type == GFX_CMD_TARGET) {
                target = c.target;
                continue;
            }
            if (c.type != GFX_CMD_DRAW)
                continue;
            int targetW = backWidth, targetH = backHeight;
            if (target != NULL) {
                GfxTexture *root = target->cube != NULL ? target->cube : target;
                targetW = root->width;
                targetH = root->height;
            }
            const GfxDrawState &s = frame.states[c.state];
            const bool scaled = target == NULL && s.screenW > 0 && s.screenH > 0;
            if (vertexState == NULL || vertexTransformed != c.transformed || vertexW != targetW || vertexH != targetH ||
                vertexScaled != scaled || (vertexState != &s && (c.transformed || !Gles2U_SameVertex(*vertexState, s)))) {
                if (c.transformed) {
                    vertexOffset = (long)vertexDataTL.size();
                    vertexDataTL.resize(vertexDataTL.size() + GLES2_TL_VEC4 * 4);
                    Gles2U_FillTL(s, targetW, targetH, scaled, &vertexDataTL[vertexOffset]);
                } else {
                    vertexOffset = (long)vertexData3D.size();
                    vertexData3D.resize(vertexData3D.size() + GLES2_VERTEX_VEC4 * 4);
                    Gles2U_FillVertex(s, targetW, targetH,
                                      ToTarget(s.viewport, s.screenW, s.screenH, target == NULL, targetW, targetH),
                                      scaled, &vertexData3D[vertexOffset]);
                }
                vertexState = &s;
                vertexTransformed = c.transformed;
                vertexW = targetW;
                vertexH = targetH;
                vertexScaled = scaled;
            }
            const int kind0 = TextureKind(s.stages[0]), kind1 = TextureKind(s.stages[1]);
            float f[GLES2_FRAGMENT_VEC4 * 4];
            Gles2U_FillFragment(s, kind0, kind1, f);
            if (fragmentOffset < 0 || kind0 != lastKind0 || kind1 != lastKind1 ||
                memcmp(f, fragment, sizeof(f)) != 0) {
                fragmentOffset = (long)fragmentData.size();
                fragmentData.insert(fragmentData.end(), f, f + GLES2_FRAGMENT_VEC4 * 4);
                memcpy(fragment, f, sizeof(f));
                lastKind0 = kind0;
                lastKind1 = kind1;
            }
            drawUniforms[i].vertexOffset = vertexOffset;
            drawUniforms[i].fragmentOffset = fragmentOffset;
        }
    }

    void UseProgram(Program &p)
    {
        if (bound.program == p.id)
            return;
        gl.UseProgram(p.id);
        bound.program = p.id;
    }

    // ---- geometry ----

    void UploadGeometry(const GfxFrame &frame)
    {
        if (!frame.vertices.empty()) {
            gl.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)frame.vertices.size(), frame.vertices.data(), GL_STREAM_DRAW);
            if (bound.vertices == vertexBuffer)
                bound.vertices = ~0u;
        }
        if (!frame.indices.empty()) {
            gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
            gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(frame.indices.size() * 2), frame.indices.data(),
                          GL_STREAM_DRAW);
        }
    }

    void UploadVertexBuffers(const GfxFrame &frame)
    {
        for (GfxVertexBuffer *b : frame.buffers) {
            GlVertexBuffer *g = (GlVertexBuffer *)b->gpu;
            if (g == NULL) {
                b->gpu = g = new GlVertexBuffer;
                gl.GenBuffers(1, &g->buffer);
            }
            if (g->version == b->version || b->vertices.empty())
                continue;
            gl.BindBuffer(GL_ARRAY_BUFFER, g->buffer);
            gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(b->vertices.size() * sizeof(GfxVertex)), b->vertices.data(),
                          GL_STATIC_DRAW);
            g->version = b->version;
            if (bound.vertices == g->buffer)
                bound.vertices = ~0u;
        }
    }

    void DestroyVertexBuffer(GfxVertexBuffer *b) override
    {
        GlVertexBuffer *g = (GlVertexBuffer *)b->gpu;
        if (g == NULL)
            return;
        if (open) {
            if (bound.vertices == g->buffer)
                bound.vertices = ~0u;
            gl.DeleteBuffers(1, &g->buffer);
        }
        delete g;
        b->gpu = NULL;
    }

    // The vertex format of a buffer from vertex `first` (0 when draws use base
    // vertex offsets).
    void BindVertices(GLuint buffer, bool transformed, long first)
    {
        if (bound.vertices == buffer && bound.transformed == transformed && bound.firstVertexBound == first)
            return;
        gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
        const int wanted = transformed ? 4 : 6;
        if (transformed) {
            const GLsizei stride = sizeof(GfxTLVertex);
            const uintptr_t base = (uintptr_t)first * stride;
            gl.VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 0));
            gl.VertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)(base + 16));
            gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)(base + 20));
            gl.VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 24));
        } else {
            const GLsizei stride = sizeof(GfxVertex);
            const uintptr_t base = (uintptr_t)first * stride;
            gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 0));
            gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 12));
            gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)(base + 24));
            gl.VertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)(base + 28));
            gl.VertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 32));
            gl.VertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 40));
        }
        SetAttributeCount(wanted);
        bound.vertices = buffer;
        bound.transformed = transformed;
        bound.firstVertexBound = first;
    }

    void SetAttributeCount(int wanted)
    {
        for (int i = boundAttributes; i < wanted; i++)
            gl.EnableVertexAttribArray((GLuint)i);
        for (int i = wanted; i < boundAttributes; i++)
            gl.DisableVertexAttribArray((GLuint)i);
        boundAttributes = wanted;
    }

    // ---- state ----

    void BindFramebuffer(GLuint framebuffer)
    {
        if (bound.framebuffer == framebuffer)
            return;
        gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        bound.framebuffer = framebuffer;
    }

    void SetViewport(GLint x, GLint y, GLint w, GLint h, float minZ, float maxZ)
    {
        const GLint v[4] = { x, y, w, h };
        if (memcmp(v, bound.viewport, sizeof(v)) != 0) {
            gl.Viewport(x, y, w, h);
            memcpy(bound.viewport, v, sizeof(v));
        }
        if (bound.depthRange[0] != minZ || bound.depthRange[1] != maxZ) {
            gl.DepthRangef(minZ, maxZ);
            bound.depthRange[0] = minZ;
            bound.depthRange[1] = maxZ;
        }
    }

    void SetScissor(const GfxViewport &v)
    {
        const GLint s[4] = { (GLint)v.x, (GLint)v.y, (GLint)v.width, (GLint)v.height };
        if (memcmp(s, bound.scissor, sizeof(s)) != 0) {
            gl.Scissor(s[0], s[1], s[2], s[3]);
            memcpy(bound.scissor, s, sizeof(s));
        }
    }

    void SetCapability(GLenum capability, int &current, bool on)
    {
        if (current == (int)on)
            return;
        if (on)
            gl.Enable(capability);
        else
            gl.Disable(capability);
        current = on;
    }

    void SetDrawState(const GfxDrawState &s)
    {
        SetCapability(GL_BLEND, bound.blend, s.alphaBlend);
        if (s.alphaBlend && (bound.srcBlend != s.srcBlend || bound.destBlend != s.destBlend)) {
            gl.BlendFunc(BlendFactor(s.srcBlend), BlendFactor(s.destBlend));
            bound.srcBlend = s.srcBlend;
            bound.destBlend = s.destBlend;
        }
        SetCapability(GL_DEPTH_TEST, bound.depthTest, s.depthTest);
        const int depthWrite = s.depthTest && s.depthWrite;
        if (bound.depthWrite != depthWrite) {
            gl.DepthMask(depthWrite ? GL_TRUE : GL_FALSE);
            bound.depthWrite = depthWrite;
        }
        if (s.depthTest && bound.depthFunc != s.depthFunc) {
            gl.DepthFunc(CompareFunc(s.depthFunc));
            bound.depthFunc = s.depthFunc;
        }
        const int cull = s.cull == GFX_CULL_CW ? 1 : s.cull == GFX_CULL_CCW ? 2 : 0;
        if (bound.cull != cull) {
            if (cull == 0) {
                gl.Disable(GL_CULL_FACE);
            } else {
                gl.Enable(GL_CULL_FACE);
                // CULL_CW removes Direct3D's front faces, CULL_CCW its back ones.
                gl.CullFace(cull == 1 ? GL_FRONT : GL_BACK);
            }
            bound.cull = cull;
        }
    }

    void Clear(const GfxCommand &c, bool backBuffer, int targetW, int targetH)
    {
        GLbitfield mask = 0;
        if (c.clearFlags & GFX_CLEAR_TARGET) {
            float rgba[4];
            Gles2U_Color(c.clearColor, rgba);
            gl.ClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (c.clearFlags & GFX_CLEAR_ZBUFFER) {
            gl.ClearDepthf(c.clearZ);
            if (bound.depthWrite != 1) {
                gl.DepthMask(GL_TRUE);
                bound.depthWrite = 1;
            }
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask == 0)
            return;
        SetScissor(ToTarget(c.clearViewport, c.clearScreenW, c.clearScreenH, backBuffer, targetW, targetH));
        gl.Clear(mask);
    }

    // ---- frame execution ----

    void GiveUniforms(Program &p, const DrawUniforms &u, bool transformed)
    {
        if (p.locV >= 0 && u.vertexOffset != p.lastV) {
            const std::vector<float> &data = transformed ? vertexDataTL : vertexData3D;
            gl.Uniform4fv(p.locV, p.vertexVec4, &data[u.vertexOffset]);
            p.lastV = u.vertexOffset;
        }
        if (p.locF >= 0 && u.fragmentOffset != p.lastF) {
            gl.Uniform4fv(p.locF, GLES2_FRAGMENT_VEC4, &fragmentData[u.fragmentOffset]);
            p.lastF = u.fragmentOffset;
        }
    }

    void Execute(GfxFrame &frame, std::vector<GfxTexture *> &textures, GfxReadback &readback, bool present,
                 bool vsyncWanted, bool stretch) override
    {
        if (!open)
            return;
        for (const GfxDrawState &s : frame.states)
            for (const GfxStageState &stage : s.stages)
                if (stage.texture != NULL)
                    Ensure(Gfx_DrawnTexture(stage.texture));
        for (const GfxCommand &c : frame.commands)
            if (c.type == GFX_CMD_TARGET && c.target != NULL)
                Ensure(c.target->cube != NULL ? c.target->cube : c.target);
        UploadTextures(textures);
        UploadGeometry(frame);
        UploadVertexBuffers(frame);
        BuildUniforms(frame);
        program3d.lastV = program3d.lastF = programTL.lastV = programTL.lastF = -1;

        GfxTexture *target = NULL;
        int targetW = backWidth, targetH = backHeight;
        BindFramebuffer(backFramebuffer);
        for (size_t i = 0; i < frame.commands.size(); i++) {
            const GfxCommand &c = frame.commands[i];
            if (c.type == GFX_CMD_TARGET) {
                target = c.target;
                if (target == NULL) {
                    BindFramebuffer(backFramebuffer);
                    targetW = backWidth;
                    targetH = backHeight;
                } else {
                    BindFramebuffer(Framebuffer(target));
                    GfxTexture *root = target->cube != NULL ? target->cube : target;
                    targetW = root->width;
                    targetH = root->height;
                }
                continue;
            }
            if (c.type == GFX_CMD_CLEAR) {
                Clear(c, target == NULL, targetW, targetH);
                continue;
            }
            if (c.type == GFX_CMD_BACKDROP)
                continue;   // no distance fade on this renderer
            const GfxDrawState &s = frame.states[c.state];
            Program &p = c.transformed ? programTL : program3d;
            UseProgram(p);
            SetDrawState(s);
            const GfxViewport viewport = ToTarget(s.viewport, s.screenW, s.screenH, target == NULL, targetW, targetH);
            if (c.transformed)
                SetViewport(0, 0, targetW, targetH, 0.0f, 1.0f);
            else
                SetViewport(viewport.x, viewport.y, viewport.width, viewport.height, viewport.minZ, viewport.maxZ);
            SetScissor(viewport);
            GiveUniforms(p, drawUniforms[i], c.transformed);
            BindTextures(s);
            const GLuint buffer = c.buffer != NULL ? ((GlVertexBuffer *)c.buffer->gpu)->buffer : vertexBuffer;
            const long firstVertex = (long)(c.firstVertexByte / (c.transformed ? sizeof(GfxTLVertex) : sizeof(GfxVertex)));
            if (c.firstIndex != ~0u) {
                if (baseVertex) {
                    BindVertices(buffer, c.transformed, 0);
                    gl.DrawElementsBaseVertexOES(PrimitiveMode(c.primitive), (GLsizei)c.indexCount, GL_UNSIGNED_SHORT,
                                                 (const void *)(uintptr_t)(c.firstIndex * 2), (GLint)firstVertex);
                } else {
                    BindVertices(buffer, c.transformed, firstVertex);
                    gl.DrawElements(PrimitiveMode(c.primitive), (GLsizei)c.indexCount, GL_UNSIGNED_SHORT,
                                    (const void *)(uintptr_t)(c.firstIndex * 2));
                }
            } else {
                BindVertices(buffer, c.transformed, 0);
                gl.DrawArrays(PrimitiveMode(c.primitive), (GLint)firstVertex, (GLsizei)c.vertexCount);
            }
        }
        if (present)
            Present(readback, vsyncWanted, stretch);
    }

    void Present(GfxReadback &readback, bool vsyncWanted, bool stretch)
    {
        BindFramebuffer(backFramebuffer);
        if (readback.requested && readback.width > 0 && readback.height > 0) {
            // The back buffer read of the frame just drawn (the lens flare
            // test): synchronous here, returned by Gfx_ReadBackBuffer as the
            // previous frame's.
            const int x = std::max(0, std::min(readback.x, backWidth - 1));
            const int y = std::max(0, std::min(readback.y, backHeight - 1));
            const int w = std::min(readback.width, backWidth - x), h = std::min(readback.height, backHeight - y);
            std::vector<uint8_t> rgba((size_t)w * h * 4);
            gl.ReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            readback.pixels.resize((size_t)w * h);
            for (size_t i = 0; i < readback.pixels.size(); i++)
                readback.pixels[i] = GfxTexel_RgbaBytesToArgb(&rgba[i * 4]);
            readback.readyWidth = w;
            readback.readyHeight = h;
            readback.ready = true;
            readback.requested = false;
        }
        const int interval = vsyncWanted ? 1 : 0;
        if (vsync != interval) {
            Gles2Display_SetVsync(interval);
            vsync = interval;
        }
        // The back buffer's first row is its top, drawn as it is on the screen.
        const PresentationRect rect =
            GetPresentationRect(backWidth, backHeight, (int)display.width, (int)display.height, stretch);
        gl.Disable(GL_SCISSOR_TEST);
        gl.Disable(GL_DEPTH_TEST);
        gl.Disable(GL_BLEND);
        gl.Disable(GL_CULL_FACE);
        bound.depthTest = 0;
        bound.blend = 0;
        bound.cull = 0;
        BindFramebuffer(0);
        gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (rect.width != (int)display.width || rect.height != (int)display.height) {
            gl.Viewport(0, 0, (GLsizei)display.width, (GLsizei)display.height);
            gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        gl.Viewport(rect.x, (int)display.height - rect.y - rect.height, rect.width, rect.height);
        bound.viewport[0] = -1;
        UseProgram(programBlit);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, backColor);
        bound.textures[0] = ~0u;
        gl.BindBuffer(GL_ARRAY_BUFFER, quadBuffer);
        gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (const void *)0);
        SetAttributeCount(1);
        bound.vertices = ~0u;
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        Gles2Display_Swap();
        gl.Enable(GL_SCISSOR_TEST);
        bound.scissor[0] = -1;
        bound.framebuffer = 0;
        bound.program = programBlit.id;
    }
};

} // namespace

GfxExecutor *Gfx_CreateGles2Executor(void)
{
    return new Executor;
}

// The OpenGL ES 2.0 executor for AmigaOS (ogles2.library on a PiStorm's V3D):
// runs a frame recorded by gfx_core.cpp, as gfx_gl.cpp does on OpenGL 3.3, with
// the shaders of gfx_gles2_shaders.h and the uniform packing of
// gles2_uniforms.h. See docs/AMIGA_PORT.md, "Renderer selection".
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
#include "render/gles2/gles2_batch.h"
#include "render/gles2/gles2_fragment.h"
#include "render/gles2/gles2_uniforms.h"
#include "render/presentation.h"

#include <math.h>
#include <stdio.h>
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
    GLuint buffer = 0;          // its own buffer (without the arena)
    uint32_t version = 0;
    long baseVertex = -1;       // its first vertex in the arena, -1: not placed
    size_t arenaIndex = 0;      // its place in Executor::arenaMembers
};

struct Program {
    GLuint id = 0;
    // u_v (the world*view slots of a 3D program, the target size of a TL one),
    // u_v[4] (the rest of a 3D program's vertex uniforms) and u_f.
    GLint locW = -1, locS = -1, locF = -1;
    int worldVec4 = 0;      // vec4 slots given through locW
    int fragVec4 = 0;       // vec4 slots given through locF
    // The uniform blocks (float offsets into the frame's vectors) last given to
    // this program in this Execute, -1: none.
    long lastW = -1, lastS = -1, lastF = -1;
};

// What a draw needs besides its state: its resolved program and where its
// uniforms are (float offsets into the frame's vectors).
struct DrawUniforms {
    Program *program = NULL;
    bool generic = false;
    long worldOffset = -1, staticOffset = -1, fragmentOffset = -1;
};

// A program with a fragment shader specialised for a draw state
// (gles2_fragment.h), made the first time such a draw is seen.
struct SpecialProgram {
    Gles2FragmentKey key;
    bool transformed = false;
    bool failed = false;
    Program program;
};

struct Executor : GfxExecutor {
    bool open = false;
    Gles2Display display = {};
    Program program3d, programTL, programBlit;
    GLuint vertexBuffer = 0, tlVertexBuffer = 0, indexBuffer = 0, quadBuffer = 0;
    // With base vertex draws, every static vertex buffer lives in one GL
    // buffer, the arena, at its own first vertex: draws from different objects
    // need no new BindBuffer and attribute pointers (7 calls a switch).
    bool useArena = false;
    bool staticDynVB = true;    // [video] gles2_static_dynvb: GL_STATIC_DRAW for 3D dynamic VBO
    GLuint arenaBuffer = 0;
    size_t arenaCapacity = 0, arenaUsed = 0;     // in vertices
    std::vector<GfxVertexBuffer *> arenaMembers;
    GLuint backFramebuffer = 0, backColor = 0, backDepth = 0;
    int backWidth = 0, backHeight = 0;
    int backAllocW = 0, backAllocH = 0;
    bool backHasStencil = false;
    GLuint white2D = 0, whiteCube = 0;
    bool bcSupported = false, baseVertex = false, discardFramebuffer = false, clipControl = false;
    bool directToScreen = false;
    bool frameHasBackDraws = false;
    bool frameClearedFullDisplay = false;
    bool anyTextureDirty = false;
    bool loggedBadTL = false;
    PresentationRect lastPresRect = {};
    int batchMode = 1;          // [video] gles2_batch (0 off, 1 full, 2 consecutive only)
    int vsync = -1;
    int boundAttributes = 0;    // vertex attribute arrays enabled
    int activeUnit = -1;        // the texture unit last made active
    Gles2Batcher batcher;
    std::vector<Gles2BatchInput> batchInputs;
    std::vector<Gles2BatchedCmd> batchedCommands;
    std::vector<uint16_t> batchedIndices;
    std::vector<GfxVertex> batchedVertices;
    std::vector<GfxTLVertex> tlVertices;
    std::vector<GLuint> batchGlBuffers;
    Gles2BatchStats lastBatchStats;
    std::vector<float> worldData, staticData, tlData, fragmentData;
    struct StaticCacheEntry {
        const GfxDrawState *state;
        int targetW;
        int targetH;
        int logicalW;
        int logicalH;
        bool isBack;
        long offset;
    };
    std::vector<StaticCacheEntry> staticEntries;
    std::vector<long> staticStateCache;
    std::vector<uint32_t> staticStateEpoch;
    std::vector<long> worldOffsetCache;
    std::vector<uint32_t> worldOffsetEpoch;
    std::vector<long> fragBlockOffsets;
    std::vector<DrawUniforms> drawUniforms;
    std::vector<Gles2FragmentKey> drawKeys;   // per batched command (draws only)
    std::vector<SpecialProgram *> specials;
    bool prewarming = false;        // compiling the remembered variants at startup
    bool genericFragment = false;   // [video] gles2_generic_fragment: the general shader for every draw
    bool simpleVertex = false, lightLoop = false;
    const char *vertex3D = kGles2Vertex3D;   // [video] gles2_light_loop = 1: the loop with a computed index

    // [video] gles2_stats = 1: where a frame's time goes, logged every 120
    // frames (milliseconds per frame); writing the log costs a few milliseconds. "other" is what the game and gfx_core
    // spend between two presents.
    struct Stats {
        bool enabled = false;
        double readback = 0, swap = 0;
        long readbacks = 0;
        double lastPresent = 0, upload = 0, uploadTex = 0, uploadVB = 0, uploadBatch = 0, uploadGeom = 0,
               build = 0, draw = 0, present = 0, period = 0;
        long frames = 0, draws = 0, rawDraws = 0, draws3d = 0, bakedDraws = 0, bakedGroups = 0, bakedVerts = 0,
             vertexUploads = 0, worldUploads = 0, fragmentUploads = 0,
             programSwitches = 0, states = 0, worlds = 0, vertBytes = 0, idxCount = 0,
             textureBinds = 0, vertexBinds = 0, unitSwitches = 0, targets = 0, cubeDraws = 0, targetDraws = 0;
    } stats;

    // [video] gles2_dump = N: every Nth frame with 3D draws, as RAM:cmr2-frame-00..15.ppm (the last 16 are kept),
    // read with glReadPixels: the way to see what the renderer draws, as a
    // full-screen ogles2 screen cannot be captured from outside.
    GfxTexture *dumpCube = NULL;   // the last cube map drawn into, dumped with the frame
    int dumpEvery = 0, dumped = 0, frameDraws3d = 0;   // only frames with 3D draws are counted and dumped
    long dumpFrame = 0;

    void DumpFrame()
    {
        std::vector<uint8_t> rgba((size_t)backWidth * backHeight * 4);
        gl.ReadPixels(0, 0, backWidth, backHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        char path[64];
        snprintf(path, sizeof(path), "RAM:cmr2-frame-%02d.ppm", dumped % 16);
        FILE *f = fopen(path, "wb");
        if (f == NULL) {
            Sys_Log("gfx: gles2: cannot write %s", path);
            return;
        }
        fprintf(f, "P6\n%d %d\n255\n", backWidth, backHeight);
        std::vector<uint8_t> row((size_t)backWidth * 3);
        for (int y = 0; y < backHeight; y++) {
            const uint8_t *src = &rgba[(size_t)y * backWidth * 4];
            for (int x = 0; x < backWidth; x++) {
                row[x * 3] = src[x * 4];
                row[x * 3 + 1] = src[x * 4 + 1];
                row[x * 3 + 2] = src[x * 4 + 2];
            }
            fwrite(row.data(), 1, row.size(), f);
        }
        fclose(f);
        Sys_Log("gfx: gles2: wrote %s (3D frame %ld)", path, dumpFrame);
        if (dumpCube != NULL && dumpCube->gpu != NULL && dumped % 4 == 0) {
            // The six faces of the last cube map drawn into, side by side.
            const int size = dumpCube->width;
            std::vector<uint8_t> faces((size_t)size * 6 * size * 3), face((size_t)size * size * 4);
            for (int fc = 0; fc < 6; fc++) {
                BindFramebuffer(FramebufferFor(dumpCube, fc));
                gl.ReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, face.data());
                for (int y = 0; y < size; y++)
                    for (int x = 0; x < size; x++)
                        for (int ch = 0; ch < 3; ch++)
                            faces[((size_t)y * 6 * size + fc * size + x) * 3 + ch] = face[((size_t)y * size + x) * 4 + ch];
            }
            BindFramebuffer(backFramebuffer);
            snprintf(path, sizeof(path), "RAM:cmr2-cube-%02d.ppm", dumped % 16);
            FILE *cf = fopen(path, "wb");
            if (cf != NULL) {
                fprintf(cf, "P6\n%d %d\n255\n", size * 6, size);
                fwrite(faces.data(), 1, faces.size(), cf);
                fclose(cf);
                Sys_Log("gfx: gles2: wrote %s", path);
            }
        }
    }

    struct Bound {
        GLuint program = 0, framebuffer = ~0u, vertices = ~0u;
        GLenum clipOrigin = GL_LOWER_LEFT_EXT;
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
        GLuint buffers[] = { vertexBuffer, tlVertexBuffer, indexBuffer, quadBuffer, arenaBuffer };
        gl.DeleteBuffers(arenaBuffer != 0 ? 5 : 4, buffers);
        for (SpecialProgram *e : specials) {
            if (e->program.id != 0)
                gl.DeleteProgram(e->program.id);
            delete e;
        }
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
        p.locW = gl.GetUniformLocation(id, "u_v");
        p.locS = vertexVec4 > GLES2_WORLD_VEC4 ? gl.GetUniformLocation(id, "u_v[4]") : -1;
        p.locF = gl.GetUniformLocation(id, "u_f");
        p.worldVec4 = vertexVec4 > GLES2_WORLD_VEC4 ? GLES2_WORLD_VEC4 : vertexVec4;
        p.fragVec4 = p.locF >= 0 ? GLES2_FRAGMENT_VEC4 : 0;
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
        stats.enabled = Sys_GetOption("video.gles2_stats", 0) != 0;
        dumpEvery = Sys_GetOption("video.gles2_dump", 0);
        AmigaSys_SetWindow(display.inputWindow);
        Sys_Log("gfx: gles2: %s (%s), screen %ux%u, input window %lx", (const char *)gl.GetString(GL_RENDERER),
                (const char *)gl.GetString(GL_VERSION), display.width, display.height,
                (unsigned long)display.inputWindow);
        bcSupported = HasExtension("GL_EXT_texture_compression_s3tc") && Sys_GetOption("video.gles2_native_bc", 1) != 0;
        baseVertex = HasExtension("GL_OES_draw_elements_base_vertex");
        discardFramebuffer = HasExtension("GL_EXT_discard_framebuffer");
        clipControl = HasExtension("GL_EXT_clip_control") && Sys_GetOption("video.gles2_direct", 1) != 0;
        useArena = baseVertex && Sys_GetOption("video.gles2_vertex_arena", 1) != 0;
        staticDynVB = Sys_GetOption("video.gles2_static_dynvb", 1) != 0;
        batchMode = Sys_GetOption("video.gles2_batch", 1);
        if (batchMode < 0 || batchMode > 2)
            batchMode = 1;
        Sys_Log("gfx: gles2: S3TC %s, base vertex draws %s, discard %s, direct %s, batch %d",
                bcSupported ? "native" : "decoded", baseVertex ? "native" : "by attribute offsets",
                discardFramebuffer ? "on" : "off", clipControl ? "on" : "off", batchMode);

        static const char *const attributes3D[6] = { "aPosition", "aNormal", "aDiffuse", "aSpecular", "aTex0", "aTex1" };
        static const char *const attributesTL[4] = { "aPosition", "aDiffuse", "aSpecular", "aTex" };
        static const char *const attributesBlit[1] = { "aPosition" };
        simpleVertex = Sys_GetOption("video.gles2_simple_vertex", 0) != 0;
        lightLoop = Sys_GetOption("video.gles2_light_loop", 0) != 0;
        vertex3D = lightLoop ? kGles2Vertex3DLoop : kGles2Vertex3D;
        genericFragment = Sys_GetOption("video.gles2_generic_fragment", 0) != 0;
        const bool simpleFragment = Sys_GetOption("video.gles2_simple_fragment", 0) != 0;
        if (simpleVertex || simpleFragment)
            Sys_Log("gfx: gles2: DIAGNOSTIC shaders: simple vertex %d, simple fragment %d", (int)simpleVertex,
                    (int)simpleFragment);
        if (!Link(program3d, simpleVertex ? kGles2Vertex3DSimple : vertex3D,
                  simpleFragment ? kGles2FragmentSimple : kGles2Fragment, attributes3D, 6, GLES2_VERTEX_VEC4) ||
            !Link(programTL, kGles2VertexTL, kGles2Fragment, attributesTL, 4, GLES2_TL_VEC4) ||
            !Link(programBlit, kGles2VertexBlit, kGles2FragmentBlit, attributesBlit, 1, 0))
            return false;
        white2D = WhiteTexture(GL_TEXTURE_2D);
        whiteCube = WhiteTexture(GL_TEXTURE_CUBE_MAP);
        GLuint buffers[4];
        gl.GenBuffers(4, buffers);
        vertexBuffer = buffers[0];
        tlVertexBuffer = buffers[1];
        indexBuffer = buffers[2];
        quadBuffer = buffers[3];
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
        PrewarmVariants();
        return true;
    }

    bool EnsureBackFramebuffer()
    {
        if (backFramebuffer != 0 && backAllocW == backWidth && backAllocH == backHeight)
            return true;
        if (backFramebuffer == 0) {
            gl.GenFramebuffers(1, &backFramebuffer);
            gl.GenTextures(1, &backColor);
            gl.GenRenderbuffers(1, &backDepth);
        }
        gl.BindTexture(GL_TEXTURE_2D, backColor);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, backWidth, backHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.BindRenderbuffer(GL_RENDERBUFFER, backDepth);
        gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, backWidth, backHeight);
        gl.BindFramebuffer(GL_FRAMEBUFFER, backFramebuffer);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, backColor, 0);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, backDepth);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, backDepth);
        bound.framebuffer = backFramebuffer;
        InvalidateTextureBinding();
        backHasStencil = true;
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            // Without packed depth and stencil: 16-bit depth alone.
            gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, backWidth, backHeight);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
            backHasStencil = false;
            if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                Sys_Log("gfx: gles2: back buffer %dx%d incomplete", backWidth, backHeight);
                return false;
            }
            Sys_Log("gfx: gles2: back buffer with 16-bit depth");
        }
        backAllocW = backWidth;
        backAllocH = backHeight;
        return true;
    }

    bool SetVideoMode(int width, int height, bool) override
    {
        if (!open && !CreateDevice())
            return false;
        backWidth = width;
        backHeight = height;
        frameClearedFullDisplay = false;
        if (!(clipControl && dumpEvery == 0))
            return EnsureBackFramebuffer();
        return true;
    }

    void SetFullscreen(bool) override {}

    // ---- textures ----

    // Rows of a stream texture's pixels (render/gfx_stream.h); the first upload
    // makes the storage from the whole frame.
    int UploadStreamRows(GfxTexture *t, const uint8_t *rgba, int y0, int rows) override
    {
        if (!open)
            return 0;
        GlTexture *g = (GlTexture *)t->gpu;
        if (g == NULL) {
            g = new GlTexture;
            gl.GenTextures(1, &g->texture);
            t->gpu = g;
        }
        SelectUnit(0);
        gl.BindTexture(GL_TEXTURE_2D, g->texture);
        InvalidateTextureBinding();
        if (!g->specified) {
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            g->specified = true;
            g->samplerKey = ~0u;
        } else {
            gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, t->width, rows, GL_RGBA, GL_UNSIGNED_BYTE,
                             rgba + (size_t)y0 * t->width * 4);
        }
        return 1;
    }

    GlTexture *Ensure(GfxTexture *t)
    {
        if (t->gpu != NULL)
            return (GlTexture *)t->gpu;
        if (t->flags & GFX_TEXTURE_STREAM) {
            // Drawn before its first frame: empty storage, no CPU copy to upload.
            GlTexture *stream = new GlTexture;
            gl.GenTextures(1, &stream->texture);
            gl.BindTexture(GL_TEXTURE_2D, stream->texture);
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            stream->specified = true;
            InvalidateTextureBinding();
            t->gpu = stream;
            t->dirty = false;
            return stream;
        }
        GlTexture *g = new GlTexture;
        g->compressed = (t->format == GFX_FORMAT_BC1 || t->format == GFX_FORMAT_BC3) && bcSupported;
        gl.GenTextures(1, &g->texture);
        const GLenum target = (t->flags & GFX_TEXTURE_CUBE) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        gl.BindTexture(target, g->texture);
        gl.TexParameteri(target, GL_TEXTURE_MAX_LEVEL, t->mipLevels - 1);
        InvalidateTextureBinding();
        t->gpu = g;
        if (t->flags & GFX_TEXTURE_RENDER_TARGET) {
            Sys_Log("gfx: gles2: render target %dx%d%s", t->width, t->height, (t->flags & GFX_TEXTURE_CUBE) ? " (cube map)" : "");
            // Storage now, so that it can be drawn into before any upload.
            Upload(t, g);
            gl.GenRenderbuffers(1, &g->depth);
            gl.BindRenderbuffer(GL_RENDERBUFFER, g->depth);
            gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, t->width, t->height);
        }
        t->dirty = true;
        anyTextureDirty = true;
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
        SelectUnit(0);
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
        anyTextureDirty = false;
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
        activeUnit = -1;
    }

    void SelectUnit(int unit)
    {
        if (activeUnit == unit)
            return;
        stats.unitSwitches++;
        gl.ActiveTexture(GL_TEXTURE0 + unit);
        activeUnit = unit;
    }

    // ---- sampler state (per texture in ES 2.0) ----

    uint32_t SamplerKey(const GfxTexture *t, const GfxStageState &stage)
    {
        const bool mips = t->mipLevels > 1 && stage.mipFilter != GFX_MIPFILTER_NONE;
        return (uint32_t)(stage.minFilter & 3) | (uint32_t)(stage.magFilter & 3) << 2 |
               (uint32_t)(mips ? stage.mipFilter & 3 : 1) << 4 | (uint32_t)(stage.address & 3) << 6;
    }

    // Sets the sampler state of the texture bound to the active unit.
    void ApplySampler(GlTexture *g, const GfxStageState &stage, GLenum target, uint32_t key)
    {
        const bool mips = ((key >> 4) & 3) != 1;
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

    int TextureKind(GfxTexture *texture)
    {
        GfxTexture *t = Gfx_DrawnTexture(texture);
        if (t == NULL)
            return GLES2_KIND_NONE;
        return (t->flags & GFX_TEXTURE_CUBE) ? GLES2_KIND_CUBE : GLES2_KIND_2D;
    }

    // The two stages' textures: units 0 and 2 for 2D textures, 1 and 3 for
    // cubes (the white ones where the stage has none of that kind).
    // key: the specialised program's key (only the units its shader samples
    // are bound), NULL for the general program (all four).
    void BindTextures(const GfxDrawState &s, GfxTexture *const cmdTextures[2], const Gles2FragmentKey *key)
    {
        for (int i = 0; i < 2; i++) {
            int needed = 3;     // bit 0: the 2D unit, bit 1: the cube unit
            if (key != NULL)
                needed = key->colorOp[0] == 0 || key->colorOp[i] == 0 ? 0
                         : key->kind[i] == GLES2_KIND_2D             ? 1
                         : key->kind[i] == GLES2_KIND_CUBE           ? 2
                                                                     : 0;
            if (needed == 0)
                continue;
            const GfxStageState &stage = s.stages[i];
            GfxTexture *t = Gfx_DrawnTexture(cmdTextures[i]);
            GlTexture *g = t != NULL ? Ensure(t) : NULL;
            const bool cube = t != NULL && (t->flags & GFX_TEXTURE_CUBE);
            const GLuint textures[2] = { g != NULL && !cube ? g->texture : white2D,
                                         g != NULL && cube ? g->texture : whiteCube };
            for (int k = 0; k < 2; k++) {
                if (!(needed & (1 << k)))
                    continue;
                const int unit = i * 2 + k;
                const GLenum target = k == 0 ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
                if (bound.textures[unit] != textures[k]) {
                    stats.textureBinds++;
                    SelectUnit(unit);
                    gl.BindTexture(target, textures[k]);
                    bound.textures[unit] = textures[k];
                }
                if (g != NULL && (k == 1) == cube) {
                    const uint32_t samplerKey = SamplerKey(t, stage);
                    if (g->samplerKey != samplerKey) {
                        SelectUnit(unit);
                        ApplySampler(g, stage, target, samplerKey);
                    }
                }
            }
        }
    }

    // ---- uniforms ----

    // Coordinate space the game's viewports, clears and TL vertices are given
    // in for a draw: virtual screen if set, or the game's back-buffer size when
    // direct-to-screen presents into a differently sized presentation rect.
    void SourceSize(int screenW, int screenH, bool isBack, int *outW, int *outH) const
    {
        if (isBack && screenW > 0 && screenH > 0) {
            *outW = screenW;
            *outH = screenH;
        } else if (isBack && directToScreen && backWidth > 0 && backHeight > 0) {
            *outW = backWidth;
            *outH = backHeight;
        } else {
            *outW = 0;
            *outH = 0;
        }
    }

    static GfxViewport ToTarget(const GfxViewport &v, int screenW, int screenH, bool backBuffer, int targetW,
                                int targetH)
    {
        if (!backBuffer || screenW <= 0 || screenH <= 0)
            return v;
        if (screenW == targetW && screenH == targetH)
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

    // Every batched draw's program and uniforms, written once for the frame: a
    // block is written when it differs from the previous draw's, and a program
    // is given a block only when it is not the one it holds.
    void BuildUniforms(const GfxFrame &frame, const std::vector<Gles2BatchedCmd> &commands, int backTargetW,
                       int backTargetH)
    {
        Gles2BatchDetail::ClearVector(worldData);
        Gles2BatchDetail::ClearVector(staticData);
        Gles2BatchDetail::ClearVector(tlData);
        Gles2BatchDetail::ClearVector(fragmentData);
        Gles2BatchDetail::ClearVector(staticEntries);
        Gles2BatchDetail::ClearVector(fragBlockOffsets);
        staticStateCache.resize(frame.states.size());
        staticStateEpoch.assign(frame.states.size(), 0u);
        worldOffsetCache.resize(frame.worlds.size());
        worldOffsetEpoch.assign(frame.worlds.size(), 0u);
        drawUniforms.assign(commands.size(), DrawUniforms());
        drawKeys.resize(commands.size());
        GfxTexture *target = NULL;
        const GfxDrawState *worldViewState = NULL;
        static const GfxMatrix kIdentityWorld = {
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
        };
        int staticTargetW = -1, staticTargetH = -1, tlLogicalW = -1, tlLogicalH = -1;
        bool staticTargetBack = false;
        uint32_t staticEpoch = 0u, viewEpoch = 0u;
        long idWorldOffset = -1;
        long worldOffset = -1, staticOffset = -1, tlOffset = -1, fragmentOffset = -1;
        uint32_t keyState = ~0u;
        int keyKind0 = -1, keyKind1 = -1;
        bool keyTransformed = false;
        Gles2FragmentKey lastKey;
        memset(&lastKey, 0, sizeof(lastKey));
        Program *lastProgram = NULL;
        bool lastGeneric = true;
        uint32_t fragState = ~0u;
        int fragKind0 = -1, fragKind1 = -1;
        bool fragGeneric = false;
        for (size_t i = 0; i < commands.size(); i++) {
            const GfxCommand &c = commands[i].cmd;
            if (c.type == GFX_CMD_TARGET) {
                target = c.target;
                continue;
            }
            if (c.type != GFX_CMD_DRAW)
                continue;
            const bool isBack = target == NULL;
            int targetW = backTargetW, targetH = backTargetH;
            if (!isBack) {
                GfxTexture *root = target->cube != NULL ? target->cube : target;
                targetW = root->width;
                targetH = root->height;
            }
            const GfxDrawState &s = frame.states[c.state];
            int effW = 0, effH = 0;
            SourceSize(s.screenW, s.screenH, isBack, &effW, &effH);
            const int logicalW = effW > 0 ? effW : targetW;
            const int logicalH = effH > 0 ? effH : targetH;
            if (c.transformed) {
                // Only the logical coordinate space matters to a pre-transformed draw.
                if (tlOffset < 0 || tlLogicalW != logicalW || tlLogicalH != logicalH) {
                    tlOffset = (long)tlData.size();
                    tlData.resize(tlData.size() + GLES2_TL_VEC4 * 4);
                    Gles2U_FillTL(s, logicalW, logicalH, false, &tlData[tlOffset]);
                    tlLogicalW = logicalW;
                    tlLogicalH = logicalH;
                }
                drawUniforms[i].worldOffset = tlOffset;
            } else {
                // Reuse exact per-frame staticOffset / worldOffset whenever a
                // state or (world, view) repeats across interleaved programs:
                // GiveUniforms then skips glUniform4fv and preserves ogles2's
                // cached coordinate/vertex shader uniform streams (cbuf_ver).
                if (staticEpoch == 0u || staticTargetW != targetW || staticTargetH != targetH ||
                    staticTargetBack != isBack) {
                    staticEpoch++;
                    staticTargetW = targetW;
                    staticTargetH = targetH;
                    staticTargetBack = isBack;
                }
                if (staticStateEpoch[c.state] == staticEpoch) {
                    staticOffset = staticStateCache[c.state];
                } else {
                    long foundStatic = -1;
                    for (size_t k = 0; k < staticEntries.size(); k++) {
                        const StaticCacheEntry &e = staticEntries[k];
                        if (e.targetW == targetW && e.targetH == targetH && e.logicalW == logicalW &&
                            e.logicalH == logicalH && e.isBack == isBack &&
                            (e.state == &s || e.state->vertexSerial == s.vertexSerial ||
                             Gles2U_SameStatic(*e.state, s))) {
                            foundStatic = e.offset;
                            break;
                        }
                    }
                    if (foundStatic >= 0) {
                        staticOffset = foundStatic;
                    } else {
                        staticOffset = (long)staticData.size();
                        staticData.resize(staticData.size() + GLES2_VERTEX_VEC4 * 4);
                        Gles2U_FillStatic(s, logicalW, logicalH,
                                          ToTarget(s.viewport, effW, effH, isBack, targetW, targetH), false,
                                          &staticData[staticOffset]);
                        StaticCacheEntry e = { &s, targetW, targetH, logicalW, logicalH, isBack, staticOffset };
                        staticEntries.push_back(e);
                    }
                    staticStateCache[c.state] = staticOffset;
                    staticStateEpoch[c.state] = staticEpoch;
                }
                if (worldViewState == NULL ||
                    (worldViewState != &s && worldViewState->vertexSerial != s.vertexSerial &&
                     memcmp(&worldViewState->view, &s.view, sizeof(s.view)) != 0)) {
                    viewEpoch++;
                    idWorldOffset = -1;
                    worldViewState = &s;
                }
                if (c.world < frame.worlds.size()) {
                    if (worldOffsetEpoch[c.world] == viewEpoch) {
                        worldOffset = worldOffsetCache[c.world];
                    } else {
                        worldOffset = (long)worldData.size();
                        worldData.resize(worldData.size() + GLES2_WORLD_VEC4 * 4);
                        Gles2U_FillWorldView(frame.worlds[c.world], s.view, &worldData[worldOffset]);
                        worldOffsetCache[c.world] = worldOffset;
                        worldOffsetEpoch[c.world] = viewEpoch;
                    }
                } else {
                    if (idWorldOffset < 0) {
                        idWorldOffset = (long)worldData.size();
                        worldData.resize(worldData.size() + GLES2_WORLD_VEC4 * 4);
                        Gles2U_FillWorldView(kIdentityWorld, s.view, &worldData[idWorldOffset]);
                    }
                    worldOffset = idWorldOffset;
                }
                drawUniforms[i].worldOffset = worldOffset;
                drawUniforms[i].staticOffset = staticOffset;
            }
            // The key and resolved program depend on the state, texture kinds
            // and 2D/3D flag alone.
            const int kind0 = TextureKind(c.textures[0]);
            const int kind1 = TextureKind(c.textures[1]);
            if (c.state != keyState || kind0 != keyKind0 || kind1 != keyKind1 || c.transformed != keyTransformed ||
                lastProgram == NULL) {
                Gles2FragmentKey newKey;
                Gles2Fragment_Key(s, kind0, kind1, &newKey);
                const bool keyChanged = lastProgram == NULL || c.transformed != keyTransformed ||
                                        !Gles2Fragment_SameKey(lastKey, newKey);
                lastKey = newKey;
                keyState = c.state;
                keyKind0 = kind0;
                keyKind1 = kind1;
                keyTransformed = c.transformed;
                if (keyChanged) {
                    lastProgram = &ProgramFor(c.transformed, lastKey);
                    lastGeneric = lastProgram == &program3d || lastProgram == &programTL;
                }
            }
            drawKeys[i] = lastKey;
            drawUniforms[i].program = lastProgram;
            drawUniforms[i].generic = lastGeneric;
            if (lastProgram->locF >= 0 && lastProgram->fragVec4 > 0) {
                if (c.state != fragState || kind0 != fragKind0 || kind1 != fragKind1 || lastGeneric != fragGeneric ||
                    fragmentOffset < 0) {
                    float f[GLES2_FRAGMENT_VEC4 * 4];
                    if (lastGeneric)
                        Gles2Fragment_Uniforms(s, lastKey, f);
                    else
                        Gles2Fragment_SpecialUniforms(s, lastKey, f);
                    long foundFrag = -1;
                    if (fragmentOffset >= 0 && memcmp(f, &fragmentData[fragmentOffset], sizeof(f)) == 0) {
                        foundFrag = fragmentOffset;
                    } else {
                        for (size_t k = 0; k < fragBlockOffsets.size(); k++) {
                            const long off = fragBlockOffsets[k];
                            if (memcmp(f, &fragmentData[off], sizeof(f)) == 0) {
                                foundFrag = off;
                                break;
                            }
                        }
                    }
                    if (foundFrag >= 0) {
                        fragmentOffset = foundFrag;
                    } else {
                        fragmentOffset = (long)fragmentData.size();
                        fragmentData.insert(fragmentData.end(), f, f + GLES2_FRAGMENT_VEC4 * 4);
                        fragBlockOffsets.push_back(fragmentOffset);
                    }
                    fragState = c.state;
                    fragKind0 = kind0;
                    fragKind1 = kind1;
                    fragGeneric = lastGeneric;
                }
                drawUniforms[i].fragmentOffset = fragmentOffset;
            } else {
                drawUniforms[i].fragmentOffset = -1;
            }
        }
    }

    // The program for a draw: its fragment shader specialised for the state,
    // compiled on first use. Where that fails (no compiled binary and no
    // compiler) the general program does the draw.
    Program &ProgramFor(bool transformed, const Gles2FragmentKey &key)
    {
        Program &generic = transformed ? programTL : program3d;
        if (genericFragment)
            return generic;
        for (SpecialProgram *e : specials)
            if (e->transformed == transformed && Gles2Fragment_SameKey(e->key, key))
                return e->failed ? generic : e->program;
        SpecialProgram *e = new SpecialProgram;
        e->key = key;
        e->transformed = transformed;
        const std::string source = Gles2Fragment::Source(key);
        static const char *const attributes3D[6] = { "aPosition", "aNormal", "aDiffuse", "aSpecular", "aTex0", "aTex1" };
        static const char *const attributesTL[4] = { "aPosition", "aDiffuse", "aSpecular", "aTex" };
        const double t0 = Sys_GetTicksPrecise();
        e->failed = !(transformed ? Link(e->program, kGles2VertexTL, source.c_str(), attributesTL, 4, GLES2_TL_VEC4)
                                  : Link(e->program, simpleVertex ? kGles2Vertex3DSimple : vertex3D,
                                         source.c_str(), attributes3D, 6, GLES2_VERTEX_VEC4));
        if (!e->failed)
            e->program.fragVec4 = e->program.locF >= 0 ? Gles2Fragment_SpecialVec4Count(key) : 0;
        const double built = Sys_GetTicksPrecise() - t0;
        // A binary found in PROGDIR:shaders/ or the cache loads in milliseconds; seconds
        // mean v3dglslc compiled it: that shader is not shipped yet.
        Sys_Log("gfx: gles2: %s shader variant %u %s in %.0f ms%s (kinds %d %d, ops %d/%d, alpha test %d, fog %d)",
                transformed ? "2D" : "3D", (unsigned)specials.size() + 1, e->failed ? "FAILED" : "built", built,
                built > 300.0 ? " COMPILED: not in the shader binaries" : "", key.kind[0], key.kind[1],
                key.colorOp[0], key.colorOp[1], key.alphaTest, key.fog);
        specials.push_back(e);
        if (!e->failed && !prewarming)
            RememberVariant(transformed, key);
        return e->failed ? generic : e->program;
    }

    // The shader variants a game used are listed in PROGDIR:gles2_shaders.lst and
    // built at the next start, while the game is loading: a two-stage 3D shader
    // takes seconds to compile the first time, which froze the screen between
    // the menu and a race. Compiled programs are cached by ogles2, so a start
    // with a list costs a few milliseconds a variant.
    static const char *VariantFile() { return "PROGDIR:gles2_shaders.lst"; }

    void RememberVariant(bool transformed, const Gles2FragmentKey &key)
    {
        FILE *f = fopen(VariantFile(), "ab");
        if (f == NULL)
            return;
        const std::string line = Gles2Fragment_KeyText(transformed, key) + "\n";
        fwrite(line.data(), 1, line.size(), f);
        fclose(f);
    }

    void PrewarmVariants()
    {
        if (genericFragment)
            return;
        FILE *f = fopen(VariantFile(), "rb");
        if (f == NULL)
            return;
        const double t0 = Sys_GetTicksPrecise();
        // Read the whole list first: variants built below are appended to it.
        std::vector<std::pair<bool, Gles2FragmentKey> > listed;
        char line[256];
        int bad = 0;
        while (fgets(line, sizeof(line), f) != NULL) {
            bool transformed = false;
            Gles2FragmentKey key;
            if (Gles2Fragment_ParseKey(line, &transformed, &key))
                listed.push_back(std::make_pair(transformed, key));
            else
                bad++;
        }
        fclose(f);
        // [video] gles2_warm_variants = 1: also the usual variations of each (see
        // Gles2Fragment_Variations), to build the shipped shader binaries without
        // playing every stage; they are remembered as they are built.
        const bool warm = Sys_GetOption("video.gles2_warm_variants", 0) != 0;
        for (size_t i = 0; i < listed.size(); i++) {
            if (warm) {
                const std::vector<std::pair<bool, Gles2FragmentKey> > all =
                    Gles2Fragment_Variations(listed[i].first, listed[i].second);
                for (size_t k = 0; k < all.size(); k++)
                    ProgramFor(all[k].first, all[k].second);
            } else {
                prewarming = true;   // already listed: not appended again
                ProgramFor(listed[i].first, listed[i].second);
                prewarming = false;
            }
        }
        Sys_Log("gfx: gles2: %u shader variants listed (%d bad lines), %u built, %.0f ms", (unsigned)listed.size(), bad,
                (unsigned)specials.size(), Sys_GetTicksPrecise() - t0);
    }

    void ResetUniformCache()
    {
        Program *all[2] = { &program3d, &programTL };
        for (Program *p : all)
            p->lastW = p->lastS = p->lastF = -1;
        for (SpecialProgram *e : specials)
            e->program.lastW = e->program.lastS = e->program.lastF = -1;
    }

    void UseProgram(Program &p)
    {
        if (bound.program == p.id)
            return;
        stats.programSwitches++;
        gl.UseProgram(p.id);
        bound.program = p.id;
    }

    // ---- geometry ----

    void UploadGeometry(GfxFrame &frame, bool useBatchedIndices)
    {
        bool hasDyn3D = !batchedVertices.empty();
        bool hasDynTL = false;
        for (size_t i = 0; i < batchedCommands.size(); i++) {
            const Gles2BatchedCmd &bc = batchedCommands[i];
            if (bc.cmd.type != GFX_CMD_DRAW)
                continue;
            if (bc.bufferId == 0u)
                hasDyn3D = true;
            else if (bc.bufferId == 2u)
                hasDynTL = true;
        }
        // Upload only the compact 2D/TL vertices to tlVertexBuffer (GL_STREAM_DRAW,
        // 4 attribs), while the 3D dynamic VBO uses GL_STATIC_DRAW (6 attribs <=
        // OGLES2_BUFFER_COPIES) so ogles2 byte-swaps each dynamic 3D vertex once
        // instead of per draw.
        if (hasDynTL && !tlVertices.empty()) {
            gl.BindBuffer(GL_ARRAY_BUFFER, tlVertexBuffer);
            gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(tlVertices.size() * sizeof(GfxTLVertex)), tlVertices.data(),
                          GL_STREAM_DRAW);
            if (bound.vertices == tlVertexBuffer)
                bound.vertices = ~0u;
        }
        if (!batchedVertices.empty()) {
            const size_t stride = sizeof(GfxVertex);
            const size_t oldSize = frame.vertices.size();
            const size_t aligned = (oldSize + stride - 1u) / stride * stride;
            const size_t addBytes = batchedVertices.size() * stride;
            frame.vertices.resize(aligned + addBytes);
            if (aligned > oldSize)
                memset(frame.vertices.data() + oldSize, 0, aligned - oldSize);
            memcpy(frame.vertices.data() + aligned, batchedVertices.data(), addBytes);
        }
        if (hasDyn3D && !frame.vertices.empty()) {
            gl.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
            gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)frame.vertices.size(), frame.vertices.data(),
                          staticDynVB ? GL_STATIC_DRAW : GL_STREAM_DRAW);
            if (bound.vertices == vertexBuffer)
                bound.vertices = ~0u;
        }
        const std::vector<uint16_t> &indices = useBatchedIndices ? batchedIndices : frame.indices;
        if (!indices.empty()) {
            gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
            gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * 2), indices.data(),
                          GL_STREAM_DRAW);
        }
    }

    // Makes room for `count` more vertices in the arena: a bigger arena, with
    // the members laid out again from their CPU copies (which are current).
    void GrowArena(size_t count)
    {
        size_t live = 0;
        for (GfxVertexBuffer *b : arenaMembers)
            live += b->vertices.size();
        size_t capacity = std::max<size_t>(arenaCapacity * 2, 16384);
        while (capacity < live + count)
            capacity *= 2;
        if (arenaBuffer == 0)
            gl.GenBuffers(1, &arenaBuffer);
        gl.BindBuffer(GL_ARRAY_BUFFER, arenaBuffer);
        gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(capacity * sizeof(GfxVertex)), NULL, GL_STATIC_DRAW);
        bound.vertices = ~0u;
        size_t used = 0;
        for (GfxVertexBuffer *b : arenaMembers) {
            GlVertexBuffer *g = (GlVertexBuffer *)b->gpu;
            g->baseVertex = (long)used;
            if (!b->vertices.empty())
                gl.BufferSubData(GL_ARRAY_BUFFER, (GLintptr)(used * sizeof(GfxVertex)),
                                 (GLsizeiptr)(b->vertices.size() * sizeof(GfxVertex)), b->vertices.data());
            g->version = b->version;
            used += b->vertices.size();
        }
        arenaCapacity = capacity;
        arenaUsed = used;
        Sys_Log("gfx: gles2: vertex arena %u vertices (%u used by %u buffers)", (unsigned)capacity, (unsigned)used,
                (unsigned)arenaMembers.size());
    }

    void UploadToArena(GfxVertexBuffer *b, GlVertexBuffer *g)
    {
        if (g->baseVertex < 0) {
            const size_t count = b->vertices.size();
            g->arenaIndex = arenaMembers.size();
            arenaMembers.push_back(b);
            if (arenaUsed + count > arenaCapacity) {
                GrowArena(count);   // places every member, this one too
                return;
            }
            g->baseVertex = (long)arenaUsed;
            arenaUsed += count;
        } else if (g->version == b->version) {
            return;
        }
        if (!b->vertices.empty()) {
            gl.BindBuffer(GL_ARRAY_BUFFER, arenaBuffer);
            bound.vertices = ~0u;
            gl.BufferSubData(GL_ARRAY_BUFFER, (GLintptr)(g->baseVertex * sizeof(GfxVertex)),
                             (GLsizeiptr)(b->vertices.size() * sizeof(GfxVertex)), b->vertices.data());
        }
        g->version = b->version;
    }

    void UploadVertexBuffers(const GfxFrame &frame)
    {
        for (GfxVertexBuffer *b : frame.buffers) {
            if (useArena) {
                if (b->gpu == NULL)
                    b->gpu = new GlVertexBuffer;
                UploadToArena(b, (GlVertexBuffer *)b->gpu);
                continue;
            }
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
        if (g->baseVertex >= 0) {
            // Its space stays unused until the arena grows next.
            GfxVertexBuffer *last = arenaMembers.back();
            arenaMembers[g->arenaIndex] = last;
            ((GlVertexBuffer *)last->gpu)->arenaIndex = g->arenaIndex;
            arenaMembers.pop_back();
        } else if (open && g->buffer != 0) {
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
        stats.vertexBinds++;
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

    void SetClipOrigin(GLenum origin)
    {
        if (!clipControl || bound.clipOrigin == origin)
            return;
        gl.ClipControlEXT(origin, GL_NEGATIVE_ONE_TO_ONE_EXT);
        bound.clipOrigin = origin;
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

    void SetScissorRect(GLint x, GLint y, GLint w, GLint h)
    {
        const GLint s[4] = { x, y, w, h };
        if (memcmp(s, bound.scissor, sizeof(s)) != 0) {
            gl.Scissor(s[0], s[1], s[2], s[3]);
            memcpy(bound.scissor, s, sizeof(s));
        }
    }

    void SetScissor(const GfxViewport &v)
    {
        SetScissorRect((GLint)v.x, (GLint)v.y, (GLint)v.width, (GLint)v.height);
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

    void DiscardDepth(bool hasStencil)
    {
        if (!discardFramebuffer)
            return;
        static const GLenum atts[2] = { GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
        gl.DiscardFramebufferEXT(GL_FRAMEBUFFER, hasStencil ? 2 : 1, atts);
    }

    void ClearLetterboxBars(const PresentationRect &presRect)
    {
        const int dispW = (int)display.width, dispH = (int)display.height;
        gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        if (presRect.x > 0) {
            SetScissorRect(0, 0, presRect.x, dispH);
            gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        const int rightX = presRect.x + presRect.width;
        if (rightX < dispW) {
            SetScissorRect(rightX, 0, dispW - rightX, dispH);
            gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        if (presRect.y > 0) {
            SetScissorRect(presRect.x, dispH - presRect.y, presRect.width, presRect.y);
            gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        const int bottomY = presRect.y + presRect.height;
        if (bottomY < dispH) {
            SetScissorRect(presRect.x, 0, presRect.width, dispH - bottomY);
            gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        frameClearedFullDisplay = true;
    }

    bool TargetUsedLater(size_t afterIndex, GfxTexture *t) const
    {
        for (size_t k = afterIndex + 1; k < batchedCommands.size(); k++) {
            const GfxCommand &c = batchedCommands[k].cmd;
            if (c.type == GFX_CMD_TARGET && c.target == t)
                return true;
        }
        return false;
    }

    void Clear(const GfxCommand &c, bool backBuffer, int targetW, int targetH, bool hasStencil, bool direct,
               const PresentationRect &presRect)
    {
        GLbitfield mask = 0;
        float rgba[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (c.clearFlags & GFX_CLEAR_TARGET) {
            Gles2U_Color(c.clearColor, rgba);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (c.clearFlags & GFX_CLEAR_ZBUFFER) {
            gl.ClearDepthf(c.clearZ);
            if (bound.depthWrite != 1) {
                gl.DepthMask(GL_TRUE);
                bound.depthWrite = 1;
            }
            mask |= GL_DEPTH_BUFFER_BIT;
            if (hasStencil)
                mask |= GL_STENCIL_BUFFER_BIT;
        }
        if (mask == 0)
            return;
        int effW = 0, effH = 0;
        SourceSize(c.clearScreenW, c.clearScreenH, backBuffer, &effW, &effH);
        const GfxViewport r = ToTarget(c.clearViewport, effW, effH, backBuffer, targetW, targetH);
        if (!direct || !backBuffer) {
            if (mask & GL_COLOR_BUFFER_BIT)
                gl.ClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
            SetScissor(r);
            gl.Clear(mask);
            return;
        }
        const int dispW = (int)display.width, dispH = (int)display.height;
        const bool fullPres = r.x == 0 && r.y == 0 && r.width == presRect.width && r.height == presRect.height;
        const bool fullDisp = presRect.x == 0 && presRect.y == 0 && presRect.width == dispW && presRect.height == dispH;
        // Full-display clear before any draws hits ogles2's hardware tile-buffer
        // clear (ogles2_job_clear) and avoids both a clear quad and a tile load.
        if (fullPres && !frameHasBackDraws) {
            if (fullDisp) {
                if (mask & GL_COLOR_BUFFER_BIT) {
                    gl.ClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
                    frameClearedFullDisplay = true;
                }
                SetScissorRect(0, 0, dispW, dispH);
                gl.Clear(mask);
                return;
            }
            if (!(mask & GL_COLOR_BUFFER_BIT)) {
                // Depth-only full clear: clear full display depth without
                // claiming the letterbox color bars have been cleared.
                SetScissorRect(0, 0, dispW, dispH);
                gl.Clear(mask);
                return;
            }
            const bool blackColor = (c.clearColor & 0x00ffffffu) == 0u;
            if (blackColor) {
                gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                SetScissorRect(0, 0, dispW, dispH);
                gl.Clear(mask);
                frameClearedFullDisplay = true;
                return;
            }
            // Letterboxed non-black clear: hardware-clear full screen black+depth
            // first so the pillarboxes stay black and depth uses tile clear, then
            // fill only presRect with the requested colour.
            gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            SetScissorRect(0, 0, dispW, dispH);
            gl.Clear(mask);
            frameClearedFullDisplay = true;
            gl.ClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
            SetScissorRect(presRect.x, dispH - (presRect.y + presRect.height), presRect.width, presRect.height);
            gl.Clear(GL_COLOR_BUFFER_BIT);
            return;
        }
        if (mask & GL_COLOR_BUFFER_BIT)
            gl.ClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
        const int sx = presRect.x + r.x;
        const int sy = presRect.y + r.y;
        SetScissorRect(sx, dispH - (sy + r.height), r.width, r.height);
        gl.Clear(mask);
    }

    // ---- frame execution ----

    void GiveUniforms(Program &p, const DrawUniforms &u, bool transformed)
    {
        if (p.locS >= 0 && u.staticOffset != p.lastS) {
            stats.vertexUploads++;
            gl.Uniform4fv(p.locS, GLES2_STATIC_VEC4, &staticData[u.staticOffset + GLES2_SLOT(GLES2_WORLD_VEC4)]);
            p.lastS = u.staticOffset;
        }
        if (p.locW >= 0 && u.worldOffset != p.lastW) {
            stats.worldUploads++;
            const std::vector<float> &data = transformed ? tlData : worldData;
            gl.Uniform4fv(p.locW, p.worldVec4, &data[u.worldOffset]);
            p.lastW = u.worldOffset;
        }
        if (p.locF >= 0 && p.fragVec4 > 0 && u.fragmentOffset >= 0 && u.fragmentOffset != p.lastF) {
            stats.fragmentUploads++;
            gl.Uniform4fv(p.locF, p.fragVec4, &fragmentData[u.fragmentOffset]);
            p.lastF = u.fragmentOffset;
        }
    }

    void PrepareBatches(const GfxFrame &frame, bool *usedBatchedIndices)
    {
        Gles2BatchDetail::ClearVector(tlVertices);
        Gles2BatchDetail::ClearVector(batchGlBuffers);
        batchGlBuffers.push_back(vertexBuffer);     // bufferId 0 (3D dynamic/baked)
        batchGlBuffers.push_back(arenaBuffer);      // bufferId 1 (static arena)
        batchGlBuffers.push_back(tlVertexBuffer);   // bufferId 2 (2D/TL stream)
        const size_t nc = frame.commands.size();
        batchInputs.resize(nc);
        for (size_t i = 0; i < nc; i++) {
            const GfxCommand &c = frame.commands[i];
            if (c.type != GFX_CMD_DRAW) {
                batchInputs[i].bufferId = 0;
                batchInputs[i].baseVertex = 0;
                continue;
            }
            if (c.transformed && c.buffer == NULL) {
                const uint32_t tlBase = (uint32_t)tlVertices.size();
                if (c.vertexCount > 0) {
                    if ((size_t)c.firstVertexByte + (size_t)c.vertexCount * sizeof(GfxTLVertex) <=
                        frame.vertices.size()) {
                        const GfxTLVertex *src = (const GfxTLVertex *)(frame.vertices.data() + c.firstVertexByte);
                        tlVertices.insert(tlVertices.end(), src, src + c.vertexCount);
                    } else if (!loggedBadTL) {
                        Sys_Log("gfx: gles2: TL draw out of bounds (byte %lu count %lu size %lu)",
                                (unsigned long)c.firstVertexByte, (unsigned long)c.vertexCount,
                                (unsigned long)frame.vertices.size());
                        loggedBadTL = true;
                    }
                }
                batchInputs[i].bufferId = 2u;
                batchInputs[i].baseVertex = tlBase;
                continue;
            }
            uint32_t firstVertex = (uint32_t)(c.firstVertexByte / (c.transformed ? sizeof(GfxTLVertex) : sizeof(GfxVertex)));
            uint32_t bufferId = c.transformed ? 2u : 0u;
            if (c.buffer != NULL) {
                const GlVertexBuffer *g = (const GlVertexBuffer *)c.buffer->gpu;
                if (g->baseVertex >= 0) {
                    bufferId = 1;
                    firstVertex += (uint32_t)g->baseVertex;
                } else {
                    const GLuint glBuf = g->buffer;
                    bufferId = 0;
                    for (size_t k = 3; k < batchGlBuffers.size(); k++) {
                        if (batchGlBuffers[k] == glBuf) {
                            bufferId = (uint32_t)k;
                            break;
                        }
                    }
                    if (bufferId == 0) {
                        bufferId = (uint32_t)batchGlBuffers.size();
                        batchGlBuffers.push_back(glBuf);
                    }
                }
            }
            batchInputs[i].bufferId = bufferId;
            batchInputs[i].baseVertex = firstVertex;
        }
        const uint32_t bakedBase =
            (uint32_t)((frame.vertices.size() + sizeof(GfxVertex) - 1u) / sizeof(GfxVertex));
        batcher.Run(frame, batchInputs, batchMode, batchedCommands, batchedIndices, usedBatchedIndices,
                    &batchedVertices, bakedBase, stats.enabled ? &lastBatchStats : NULL);
    }

    void Execute(GfxFrame &frame, std::vector<GfxTexture *> &textures, GfxReadback &readback, bool present,
                 bool vsyncWanted, bool stretch) override
    {
        if (!open)
            return;
        // A read the game did not collect must be done before the back buffer
        // is drawn into again.
        CollectReadback(readback);
        const bool direct = clipControl && dumpEvery == 0;
        directToScreen = direct;
        const PresentationRect presRect =
            direct ? GetPresentationRect(backWidth, backHeight, (int)display.width, (int)display.height, stretch)
                   : PresentationRect{ 0, 0, backWidth, backHeight };
        lastPresRect = presRect;
        if (!direct && !EnsureBackFramebuffer())
            return;

        const double t0 = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        for (const GfxCommand &c : frame.commands)
            if (c.type == GFX_CMD_TARGET && c.target != NULL)
                Ensure(c.target->cube != NULL ? c.target->cube : c.target);
        if (frame.hasDirtyTextures || anyTextureDirty)
            UploadTextures(textures);
        const double tTex = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        UploadVertexBuffers(frame);
        const double tVB = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        bool usedBatchedIndices = false;
        PrepareBatches(frame, &usedBatchedIndices);
        const double tBatch = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        UploadGeometry(frame, usedBatchedIndices);
        const double t1 = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        BuildUniforms(frame, batchedCommands, presRect.width, presRect.height);
        const double t2 = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        ResetUniformCache();

        bool hasRenderTargets = false;
        for (size_t i = 0; i < batchedCommands.size(); i++)
            if (batchedCommands[i].cmd.type == GFX_CMD_TARGET && batchedCommands[i].cmd.target != NULL) {
                hasRenderTargets = true;
                break;
            }

        // Run render-target draws (car reflection cube faces) in phase 0 and
        // back-buffer draws in phase 1 so V3D never splits the back-buffer
        // tile pass mid-frame (same invariant as gfx_gpu.cpp).
        const int firstPhase = hasRenderTargets ? 0 : 1;
        uint32_t lastDrawState = ~0u;
        bool vpValid = false;
        GfxViewport lastSrcViewport = {};
        int lastSrcScreenW = -1, lastSrcScreenH = -1, lastVpTargetW = -1, lastVpTargetH = -1;
        bool lastVpBack = false;
        GfxViewport viewport = {};
        const int dispW = (int)display.width, dispH = (int)display.height;

        for (int phase = firstPhase; phase < 2; phase++) {
            GfxTexture *target = NULL;
            GfxTexture *activeTarget = NULL;
            int targetW = presRect.width, targetH = presRect.height;
            if (phase == 1) {
                if (direct) {
                    BindFramebuffer(0);
                    SetClipOrigin(GL_UPPER_LEFT_EXT);
                } else {
                    BindFramebuffer(backFramebuffer);
                    SetClipOrigin(GL_LOWER_LEFT_EXT);
                }
            }
            for (size_t i = 0; i < batchedCommands.size(); i++) {
                const Gles2BatchedCmd &bc = batchedCommands[i];
                const GfxCommand &c = bc.cmd;
                if (c.type == GFX_CMD_TARGET) {
                    if (phase == 0 && activeTarget != NULL && activeTarget != c.target) {
                        if (!TargetUsedLater(i, activeTarget))
                            DiscardDepth(true);
                        activeTarget = NULL;
                    }
                    target = c.target;
                    if (phase == 0 && target != NULL) {
                        stats.targets++;
                        BindFramebuffer(Framebuffer(target));
                        SetClipOrigin(GL_LOWER_LEFT_EXT);
                        activeTarget = target;
                        GfxTexture *root = target->cube != NULL ? target->cube : target;
                        targetW = root->width;
                        targetH = root->height;
                    }
                    continue;
                }
                if ((target != NULL) != (phase == 0))
                    continue;
                if (c.type == GFX_CMD_CLEAR) {
                    const bool hasSt = target != NULL ? true : (direct ? false : backHasStencil);
                    Clear(c, target == NULL, targetW, targetH, hasSt, direct, presRect);
                    lastDrawState = ~0u;
                    continue;
                }
                if (c.type == GFX_CMD_BACKDROP)
                    continue;   // no distance fade on this renderer
                if (target == NULL)
                    frameHasBackDraws = true;
                const GfxDrawState &s = frame.states[c.state];
                if (target != NULL) {
                    stats.targetDraws++;
                    dumpCube = target->cube != NULL ? target->cube : (target->flags & GFX_TEXTURE_CUBE ? target : NULL);
                }
                const DrawUniforms &u = drawUniforms[i];
                const Gles2FragmentKey &fragmentKey = drawKeys[i];
                Program &p = *u.program;
                if (stats.enabled && (fragmentKey.kind[0] == GLES2_KIND_CUBE || fragmentKey.kind[1] == GLES2_KIND_CUBE))
                    stats.cubeDraws++;
                UseProgram(p);
                if (c.state != lastDrawState) {
                    SetDrawState(s);
                    lastDrawState = c.state;
                }
                const bool isBack = target == NULL;
                int effW = 0, effH = 0;
                SourceSize(s.screenW, s.screenH, isBack, &effW, &effH);
                if (!vpValid || isBack != lastVpBack || targetW != lastVpTargetW || targetH != lastVpTargetH ||
                    effW != lastSrcScreenW || effH != lastSrcScreenH ||
                    memcmp(&s.viewport, &lastSrcViewport, sizeof(s.viewport)) != 0) {
                    viewport = ToTarget(s.viewport, effW, effH, isBack, targetW, targetH);
                    lastSrcViewport = s.viewport;
                    lastSrcScreenW = effW;
                    lastSrcScreenH = effH;
                    lastVpTargetW = targetW;
                    lastVpTargetH = targetH;
                    lastVpBack = isBack;
                    vpValid = true;
                }
                if (direct && isBack) {
                    const int sx = presRect.x + viewport.x;
                    const int sy = presRect.y + viewport.y;
                    const int glSy = dispH - (sy + viewport.height);
                    if (c.transformed)
                        SetViewport(presRect.x, dispH - (presRect.y + presRect.height), presRect.width, presRect.height,
                                    0.0f, 1.0f);
                    else
                        SetViewport(sx, glSy, viewport.width, viewport.height, viewport.minZ, viewport.maxZ);
                    SetScissorRect(sx, glSy, viewport.width, viewport.height);
                } else {
                    if (c.transformed)
                        SetViewport(0, 0, targetW, targetH, 0.0f, 1.0f);
                    else
                        SetViewport(viewport.x, viewport.y, viewport.width, viewport.height, viewport.minZ,
                                    viewport.maxZ);
                    SetScissor(viewport);
                }
                GiveUniforms(p, u, c.transformed);
                BindTextures(s, c.textures, u.generic ? NULL : &fragmentKey);
                const GLuint buffer = batchGlBuffers[bc.bufferId];
                const long firstVertex = (long)bc.baseVertex;
                if (c.firstIndex != ~0u) {
                    if (bc.hasRange && firstVertex == 0) {
                        BindVertices(buffer, c.transformed, 0);
                        gl.DrawRangeElements(PrimitiveMode(c.primitive), (GLuint)bc.minVertex, (GLuint)bc.maxVertex,
                                             (GLsizei)c.indexCount, GL_UNSIGNED_SHORT,
                                             (const void *)(uintptr_t)(c.firstIndex * 2));
                    } else if (firstVertex == 0 && bc.bufferId != 2u && (bc.bufferId != 0u || staticDynVB) &&
                               c.vertexCount > 0 && c.vertexCount <= 65536u) {
                        // Static-draw VBOs keep cached GPU copies, so passing
                        // [0, vertexCount-1] skips ogles2's index scan loop
                        // with zero extra vertex conversion cost.
                        BindVertices(buffer, c.transformed, 0);
                        gl.DrawRangeElements(PrimitiveMode(c.primitive), 0u, (GLuint)(c.vertexCount - 1u),
                                             (GLsizei)c.indexCount, GL_UNSIGNED_SHORT,
                                             (const void *)(uintptr_t)(c.firstIndex * 2));
                    } else if (baseVertex) {
                        BindVertices(buffer, c.transformed, 0);
                        gl.DrawElementsBaseVertexOES(PrimitiveMode(c.primitive), (GLsizei)c.indexCount,
                                                     GL_UNSIGNED_SHORT, (const void *)(uintptr_t)(c.firstIndex * 2),
                                                     (GLint)firstVertex);
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
            if (phase == 0 && activeTarget != NULL && present)
                DiscardDepth(true);
        }
        if (dumpEvery > 0)
            for (const Gles2BatchedCmd &bc : batchedCommands)
                if (bc.cmd.type == GFX_CMD_DRAW && !bc.cmd.transformed)
                    frameDraws3d++;
        if (stats.enabled) {
            const double t3 = Sys_GetTicksPrecise();
            stats.upload += t1 - t0;
            stats.uploadTex += tTex - t0;
            stats.uploadVB += tVB - tTex;
            stats.uploadBatch += tBatch - tVB;
            stats.uploadGeom += t1 - tBatch;
            stats.build += t2 - t1;
            stats.draw += t3 - t2;
            stats.states += (long)frame.states.size();
            stats.worlds += (long)frame.worlds.size();
            stats.vertBytes += (long)frame.vertices.size();
            stats.idxCount += (long)(usedBatchedIndices ? batchedIndices.size() : frame.indices.size());
            stats.bakedDraws += (long)lastBatchStats.bakedDraws;
            stats.bakedGroups += (long)lastBatchStats.bakedGroups;
            stats.bakedVerts += (long)lastBatchStats.bakedVerts;
            for (const GfxCommand &c : frame.commands)
                if (c.type == GFX_CMD_DRAW)
                    stats.rawDraws++;
            for (const Gles2BatchedCmd &bc : batchedCommands)
                if (bc.cmd.type == GFX_CMD_DRAW) {
                    stats.draws++;
                    if (!bc.cmd.transformed)
                        stats.draws3d++;
                }
        }
        if (present)
            Present(readback, vsyncWanted, stretch);
    }

    void ReadDirectPixels(GfxReadback &readback, int rx, int ry, int rw, int rh)
    {
        if (backWidth <= 0 || backHeight <= 0 || rw <= 0 || rh <= 0)
            return;
        const double r0 = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        const int dispW = (int)display.width, dispH = (int)display.height;
        const float kx = (float)lastPresRect.width / backWidth;
        const float ky = (float)lastPresRect.height / backHeight;
        const int dx0 = std::max(0, std::min(lastPresRect.x + (int)lroundf(rx * kx), dispW - 1));
        const int dy0 = std::max(0, std::min(lastPresRect.y + (int)lroundf(ry * ky), dispH - 1));
        const int dx1 = std::max(dx0 + 1, std::min(lastPresRect.x + (int)lroundf((rx + rw) * kx), dispW));
        const int dy1 = std::max(dy0 + 1, std::min(lastPresRect.y + (int)lroundf((ry + rh) * ky), dispH));
        const int dw = dx1 - dx0, dh = dy1 - dy0;
        BindFramebuffer(0);
        std::vector<uint8_t> rgba((size_t)dw * dh * 4);
        gl.ReadPixels(dx0, dispH - (dy0 + dh), dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        readback.pixels.resize((size_t)rw * rh);
        // Default framebuffer ReadPixels returns rows bottom-up.
        for (int y = 0; y < rh; y++) {
            const int sy = dh - 1 - std::min(dh - 1, (y * dh) / rh);
            for (int x = 0; x < rw; x++) {
                const int sx = std::min(dw - 1, (x * dw) / rw);
                readback.pixels[(size_t)y * rw + x] =
                    GfxTexel_RgbaBytesToArgb(&rgba[((size_t)sy * dw + sx) * 4]);
            }
        }
        readback.readyWidth = rw;
        readback.readyHeight = rh;
        readback.ready = true;
        if (stats.enabled) {
            stats.readback += Sys_GetTicksPrecise() - r0;
            stats.readbacks++;
        }
    }

    void Present(GfxReadback &readback, bool vsyncWanted, bool stretch)
    {
        const double p0 = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        const bool wasClearedFull = frameClearedFullDisplay;
        frameHasBackDraws = false;
        frameClearedFullDisplay = false;
        const int interval = vsyncWanted ? 1 : 0;
        if (vsync != interval) {
            Gles2Display_SetVsync(interval);
            vsync = interval;
        }
        if (directToScreen) {
            const int dispW = (int)display.width, dispH = (int)display.height;
            const bool isLetterboxed = lastPresRect.x != 0 || lastPresRect.y != 0 ||
                                       lastPresRect.width != dispW || lastPresRect.height != dispH;
            if (isLetterboxed && !wasClearedFull) {
                BindFramebuffer(0);
                ClearLetterboxBars(lastPresRect);
                frameClearedFullDisplay = false;
            }
            if (readback.requested && readback.width > 0 && readback.height > 0 && backWidth > 0 && backHeight > 0) {
                // Direct mode swaps the default framebuffer immediately, so the
                // requested patch (sun lens-flare occlusion test) must be read
                // before the swap while frame N is still in FBO 0.
                const int rx = std::max(0, std::min(readback.x, backWidth - 1));
                const int ry = std::max(0, std::min(readback.y, backHeight - 1));
                const int rw = std::min(readback.width, backWidth - rx);
                const int rh = std::min(readback.height, backHeight - ry);
                ReadDirectPixels(readback, rx, ry, rw, rh);
                readback.requested = false;
                readbackPending = false;
            }
            const double s0 = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
            Gles2Display_Swap();
            if (stats.enabled)
                stats.swap += Sys_GetTicksPrecise() - s0;
            if (stats.enabled) {
                const double now = Sys_GetTicksPrecise();
                stats.present += now - p0;
                if (stats.lastPresent != 0.0)
                    stats.period += now - stats.lastPresent;
                stats.lastPresent = now;
                if (++stats.frames % 120 == 0)
                    LogStats();
            }
            return;
        }
        if (readback.requested && readback.width > 0 && readback.height > 0) {
            // The back buffer read of the frame just drawn (the lens flare
            // test), returned by Gfx_ReadBackBuffer as the previous frame's.
            // Not read here: glReadPixels waits for the GPU to finish the
            // frame, which stalled every 3D frame. It is read when the game
            // asks (CollectReadback), while the game builds the next frame
            // and the GPU has had time to render this one; the back buffer
            // still holds it then (the next frame is drawn at its Present).
            readbackPending = true;
            readbackX = std::max(0, std::min(readback.x, backWidth - 1));
            readbackY = std::max(0, std::min(readback.y, backHeight - 1));
            readbackW = std::min(readback.width, backWidth - readbackX);
            readbackH = std::min(readback.height, backHeight - readbackY);
            readback.requested = false;
        }
        BindFramebuffer(backFramebuffer);
        if (dumpEvery > 0) {
            if (frameDraws3d > 0 && ++dumpFrame % dumpEvery == 0) {
                DumpFrame();
                dumped++;
            }
            frameDraws3d = 0;
        }
        // The back buffer's first row is its top, drawn as it is on the screen.
        const PresentationRect rect =
            GetPresentationRect(backWidth, backHeight, (int)display.width, (int)display.height, stretch);
        // Discard the offscreen back buffer's depth/stencil before switching
        // to the default framebuffer so V3D does not store them back to RAM.
        DiscardDepth(backHasStencil);
        gl.Disable(GL_SCISSOR_TEST);
        gl.Disable(GL_DEPTH_TEST);
        gl.Disable(GL_BLEND);
        gl.Disable(GL_CULL_FACE);
        bound.depthTest = 0;
        bound.blend = 0;
        bound.cull = 0;
        BindFramebuffer(0);
        SetClipOrigin(GL_LOWER_LEFT_EXT);
        gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (rect.width != (int)display.width || rect.height != (int)display.height) {
            gl.Viewport(0, 0, (GLsizei)display.width, (GLsizei)display.height);
            gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        gl.Viewport(rect.x, (int)display.height - rect.y - rect.height, rect.width, rect.height);
        bound.viewport[0] = -1;
        UseProgram(programBlit);
        SelectUnit(0);
        gl.BindTexture(GL_TEXTURE_2D, backColor);
        bound.textures[0] = ~0u;
        gl.BindBuffer(GL_ARRAY_BUFFER, quadBuffer);
        gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (const void *)0);
        SetAttributeCount(1);
        bound.vertices = ~0u;
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        const double s0 = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        Gles2Display_Swap();
        if (stats.enabled)
            stats.swap += Sys_GetTicksPrecise() - s0;
        gl.Enable(GL_SCISSOR_TEST);
        bound.scissor[0] = -1;
        bound.framebuffer = 0;
        bound.program = programBlit.id;
        if (stats.enabled) {
            const double now = Sys_GetTicksPrecise();
            stats.present += now - p0;
            if (stats.lastPresent != 0.0)
                stats.period += now - stats.lastPresent;
            stats.lastPresent = now;
            if (++stats.frames % 120 == 0)
                LogStats();
        }
    }

    bool readbackPending = false;
    int readbackX = 0, readbackY = 0, readbackW = 0, readbackH = 0;

    void CollectReadback(GfxReadback &readback) override
    {
        if (!readbackPending || !open)
            return;
        const double r0 = stats.enabled ? Sys_GetTicksPrecise() : 0.0;
        readbackPending = false;
        BindFramebuffer(backFramebuffer);
        std::vector<uint8_t> rgba((size_t)readbackW * readbackH * 4);
        gl.ReadPixels(readbackX, readbackY, readbackW, readbackH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        readback.pixels.resize((size_t)readbackW * readbackH);
        for (size_t i = 0; i < readback.pixels.size(); i++)
            readback.pixels[i] = GfxTexel_RgbaBytesToArgb(&rgba[i * 4]);
        readback.readyWidth = readbackW;
        readback.readyHeight = readbackH;
        readback.ready = true;
        if (stats.enabled) {
            stats.readback += Sys_GetTicksPrecise() - r0;
            stats.readbacks++;
        }
    }

    void LogStats()
    {
        const double n = (double)stats.frames;
        const long f = stats.frames > 0 ? stats.frames : 1;
        Sys_Log("gfx: gles2 stats, %ld frames, ms/frame: period %.2f = upload %.2f (tex %.2f vb %.2f batch %.2f geom %.2f) "
                "+ uniforms %.2f + draw loop %.2f + present %.2f + other %.2f; "
                "per frame: draws %ld (from %ld, 3D %ld, baked %ld->%ld/%ldv), states %ld worlds %ld, vertB %ld idx %ld, "
                "uStatic %ld uWorld %ld uFrag %ld, prog %ld tex %ld vb %ld unit %ld, "
                "rt %.2f rtDraws %.1f cube %.1f; readback %.2f %.2f ms; swap %.2f ms",
                stats.frames, stats.period / n, stats.upload / n, stats.uploadTex / n, stats.uploadVB / n,
                stats.uploadBatch / n, stats.uploadGeom / n, stats.build / n, stats.draw / n, stats.present / n,
                (stats.period - stats.upload - stats.build - stats.draw - stats.present) / n,
                stats.draws / f, stats.rawDraws / f, stats.draws3d / f, stats.bakedDraws / f, stats.bakedGroups / f,
                stats.bakedVerts / f, stats.states / f, stats.worlds / f,
                stats.vertBytes / f, stats.idxCount / f, stats.vertexUploads / f, stats.worldUploads / f,
                stats.fragmentUploads / f, stats.programSwitches / f, stats.textureBinds / f, stats.vertexBinds / f,
                stats.unitSwitches / f, stats.targets / n, stats.targetDraws / n, stats.cubeDraws / n,
                stats.readbacks / n, stats.readback / n, stats.swap / n);
        const bool enabled = stats.enabled;
        stats = Stats();
        stats.enabled = enabled;
    }
};

} // namespace

GfxExecutor *Gfx_CreateGles2Executor(void)
{
    return new Executor;
}

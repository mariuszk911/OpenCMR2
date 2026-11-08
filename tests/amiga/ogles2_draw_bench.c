/*
 * ogles2 side of the draw-cost benchmark (Domain B, target only). Draws the
 * workload of draw_bench_workload.h through ogles2.library as a shader
 * backend of OpenCMR2 would, the vertices and indices in static buffer
 * objects. Compare with minigl_draw_bench.c.
 *
 *     amiga_ogles2_draw_bench [frames] [rebind] [full] [window] [draws]
 *
 * window 1: ogles2's own window of the native size instead of the full screen.
 * draws: draws per frame, at most the workload's 411 (0: clear and swap only).
 *
 * full 0 (light state): one program; the model-view-projection and normal
 *   matrices uploaded when the object changes, the texture and one stage
 *   vec4 when the texture changes, the lit flag when it changes.
 * full 1 (the host GL backend's state): the vertex state of
 *   render/gfx_gl.cpp's VertexUniforms (83 vec4: world-view, projection, two
 *   texture matrices, material, eight lights, fog and texture-coordinate
 *   setup) filled on the CPU and uploaded as one glUniform4fv when the object
 *   changes, and its FragmentUniforms (13 vec4) when the texture changes. The
 *   shaders read all of it (an eight-light loop, texture matrix, fog, alpha
 *   test). GLES2 has no base-vertex draw, so this mode re-binds the vertex
 *   attributes on every draw, as such a backend would.
 * rebind 1: re-bind the four attribute pointers on every draw (full 0 only;
 *   full 1 always does).
 *
 * Display: a full screen of its own in the Workbench screen's mode, the
 * native one (graphics.library's BestModeID offers 24-bit RTG modes only,
 * which ogles2 refuses), else ogles2's own window of that size.
 *
 * Built with vbcc and the SDK's ogles2.lib (the toolchain image's GNU ld 2.35
 * cannot read the SDK's libogles2.a), not with OpenCMR2's toolchain file:
 *   vc -c99 -cpu=68020 -fpu=68881 -O1 -I<ogles2 SDK>/include \
 *       ogles2_draw_bench.c <ogles2 SDK>/lib/ogles2.lib -lamiga -lm881 \
 *       -o amiga_ogles2_draw_bench
 */
#include <stdio.h>
#include <stdlib.h>

#include <graphics/modeid.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/ogles2.h>

#include "draw_bench_workload.h"

struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;

enum { kVertexVec4 = 83, kFragmentVec4 = 13 };

/* ---- light state (full 0) ------------------------------------------------ */

static const char *kVertexShader =
    "attribute vec3 a_position;\n"
    "attribute vec3 a_normal;\n"
    "attribute vec4 a_colour;\n"
    "attribute vec2 a_uv;\n"
    "uniform mat4 u_mvp;\n"
    "uniform mat3 u_normalMatrix;\n"
    "uniform float u_lit;\n"
    "uniform vec3 u_lightDir;\n"
    "uniform vec3 u_lightColour;\n"
    "uniform vec3 u_ambient;\n"
    "varying vec4 v_colour;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec3 n = normalize(u_normalMatrix * a_normal);\n"
    "    vec3 lit = u_ambient + max(dot(n, u_lightDir), 0.0) * u_lightColour;\n"
    "    v_colour = mix(a_colour, vec4(min(lit, vec3(1.0)), 1.0), u_lit);\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
    "}\n";

static const char *kFragmentShader =
    "precision mediump float;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_stage;\n"
    "varying vec4 v_colour;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec4 c = texture2D(u_texture, v_uv) * v_colour;\n"
    "    c.rgb = mix(c.rgb, c.rgb * u_stage.rgb, u_stage.a);\n"
    "    if (c.a < 0.5) discard;\n"
    "    gl_FragColor = c;\n"
    "}\n";

/* ---- the host GL backend's state (full 1) --------------------------------
 * u_v: 0-3 world-view, 4-7 projection, 8-15 texture matrices 0 and 1,
 * 16-19 material diffuse/ambient/specular/emissive, 20 global ambient,
 * 21-76 eight lights of seven vec4 (diffuse, specular, ambient, position,
 * direction, attenuation, spot), 77 light flags (x = lit, y = count),
 * 78 sources, 79 fog flags, 80 fog params, 81 texcoord index, 82 target.
 * u_f: 0-3 stage words, 4 texture factor, 5 alpha test, 6 alpha ref,
 * 7 fog colour, 8 fog mode, 9 fog params, 10 bump matrix, 11 bump
 * luminance, 12 distance fade. */
static const char *kFullVertexShader =
    "attribute vec3 a_position;\n"
    "attribute vec3 a_normal;\n"
    "attribute vec4 a_colour;\n"
    "attribute vec2 a_uv;\n"
    "uniform vec4 u_v[83];\n"
    "varying vec4 v_colour;\n"
    "varying vec2 v_uv;\n"
    "varying float v_fog;\n"
    "void main() {\n"
    "    mat4 wv = mat4(u_v[0], u_v[1], u_v[2], u_v[3]);\n"
    "    mat4 proj = mat4(u_v[4], u_v[5], u_v[6], u_v[7]);\n"
    "    mat4 tex0 = mat4(u_v[8], u_v[9], u_v[10], u_v[11]);\n"
    "    vec4 eye = wv * vec4(a_position, 1.0);\n"
    "    vec3 n = normalize((wv * vec4(a_normal, 0.0)).xyz);\n"
    "    vec3 c = u_v[19].rgb + u_v[20].rgb * u_v[17].rgb;\n"
    "    for (int i = 0; i < 8; i++) {\n"
    "        if (float(i) < u_v[77].y) {\n"
    "            vec3 l = -u_v[21 + i * 7 + 4].xyz;\n"
    "            c += u_v[21 + i * 7 + 2].rgb * u_v[17].rgb +\n"
    "                 max(dot(n, l), 0.0) * u_v[21 + i * 7].rgb * u_v[16].rgb;\n"
    "        }\n"
    "    }\n"
    "    v_colour = mix(a_colour, vec4(min(c, vec3(1.0)), u_v[16].a), u_v[77].x);\n"
    "    v_uv = (tex0 * vec4(a_uv, 1.0, 0.0)).xy;\n"
    "    v_fog = clamp((u_v[80].y + eye.z) / (u_v[80].y - u_v[80].x), 0.0, 1.0);\n"
    "    gl_Position = proj * eye;\n"
    "}\n";

static const char *kFullFragmentShader =
    "precision mediump float;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_f[13];\n"
    "varying vec4 v_colour;\n"
    "varying vec2 v_uv;\n"
    "varying float v_fog;\n"
    "void main() {\n"
    "    vec4 t = texture2D(u_texture, v_uv);\n"
    "    vec4 c = u_f[0].x > 0.5 ? t * v_colour : v_colour;\n"
    "    c = mix(c, c * u_f[4], u_f[0].y);\n"
    "    if (u_f[5].x > 0.5 && c.a < u_f[6].x) discard;\n"
    "    if (u_f[8].x > 0.5) c.rgb = mix(u_f[7].rgb, c.rgb, v_fog);\n"
    "    gl_FragColor = c;\n"
    "}\n";

static GLuint Compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        printf("gles-bench: shader: %s\n", log);
    }
    return s;
}

static GLuint Link(const char *vsSource, const char *fsSource)
{
    GLuint program = glCreateProgram();
    GLint linked = 0;
    glAttachShader(program, Compile(GL_VERTEX_SHADER, vsSource));
    glAttachShader(program, Compile(GL_FRAGMENT_SHADER, fsSource));
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_normal");
    glBindAttribLocation(program, 2, "a_colour");
    glBindAttribLocation(program, 3, "a_uv");
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        printf("gles-bench: link: %s\n", log);
        return 0;
    }
    return program;
}

static void SetAttributes(GLintptr base)
{
    const GLsizei stride = (GLsizei)sizeof(struct BenchVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 12));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)(base + 24));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 28));
}

static void Set4(float *v, float a, float b, float c, float d)
{
    v[0] = a; v[1] = b; v[2] = c; v[3] = d;
}

/* The vertex state of one draw, as FillVertexUniforms builds it. */
static void FillVertexState(float *u, const float modelview[16], const float projection[16], int lit)
{
    int i;
    memcpy(u, modelview, 16 * sizeof(float));
    memcpy(u + 16, projection, 16 * sizeof(float));
    memset(u + 32, 0, 32 * sizeof(float));
    u[32] = u[37] = u[42] = u[47] = 1.0f;
    u[48] = u[53] = u[58] = u[63] = 1.0f;
    Set4(u + 64, 0.8f, 0.8f, 0.8f, 1.0f);   /* material diffuse */
    Set4(u + 68, 0.8f, 0.8f, 0.8f, 1.0f);   /* ambient */
    Set4(u + 72, 0.0f, 0.0f, 0.0f, 1.0f);   /* specular */
    Set4(u + 76, 0.0f, 0.0f, 0.0f, 1.0f);   /* emissive */
    Set4(u + 80, 0.2f, 0.2f, 0.2f, 1.0f);   /* global ambient */
    for (i = 0; i < 8; ++i) {
        float *l = u + 84 + i * 28;
        Set4(l, i == 0 ? 0.9f : 0.0f, i == 0 ? 0.85f : 0.0f, i == 0 ? 0.8f : 0.0f, 1.0f);
        Set4(l + 4, 0.0f, 0.0f, 0.0f, 1.0f);
        Set4(l + 8, 0.05f, 0.05f, 0.05f, 1.0f);
        Set4(l + 12, 0.0f, 0.0f, 0.0f, 0.0f);
        Set4(l + 16, -0.3f, -0.6f, -0.75f, 0.0f);
        Set4(l + 20, 1.0f, 0.0f, 0.0f, 0.0f);
        Set4(l + 24, 0.0f, 0.0f, 1.0f, 0.0f);
    }
    Set4(u + 308, lit ? 1.0f : 0.0f, lit ? 1.0f : 0.0f, 0.0f, 0.0f);   /* light flags */
    Set4(u + 312, 0.0f, 0.0f, 0.0f, 0.0f);                              /* sources */
    Set4(u + 316, 1.0f, 0.0f, 0.0f, 0.0f);                              /* fog flags */
    Set4(u + 320, 5.0f, 40.0f, 0.0f, 0.0f);                             /* fog start, end */
    Set4(u + 324, 0.0f, 1.0f, 0.0f, 0.0f);                              /* texcoord index */
    Set4(u + 328, 1280.0f, 720.0f, 0.0f, 0.0f);                         /* target */
}

/* The fragment state of one draw, as FillFragmentUniforms builds it. */
static void FillFragmentState(float *f, int texture)
{
    memset(f, 0, kFragmentVec4 * 4 * sizeof(float));
    Set4(f, 1.0f, (float)(texture & 1) * 0.1f, 2.0f, 4.0f);   /* stage 0: textured, modulate */
    Set4(f + 4, 1.0f, 0.0f, 0.0f, 1.0f);                    /* stage 0b */
    Set4(f + 8, 1.0f, 0.0f, 0.0f, 0.0f);                    /* stage 1: off */
    Set4(f + 16, 1.0f, 1.0f, 1.0f, 1.0f);                   /* texture factor */
    Set4(f + 20, 1.0f, 7.0f, 0.0f, 0.0f);                   /* alpha test on, GEQUAL */
    Set4(f + 24, 0.5f, 0.0f, 0.0f, 0.0f);                   /* alpha ref */
    Set4(f + 28, 0.5f, 0.55f, 0.6f, 1.0f);                  /* fog colour */
    Set4(f + 32, 1.0f, 3.0f, 0.0f, 0.0f);                   /* fog mode */
    Set4(f + 36, 5.0f, 40.0f, 0.0f, 0.0f);
    Set4(f + 44, 1.0f, 0.0f, 0.0f, 0.0f);                   /* bump luminance */
}

/* A full-screen context in the Workbench screen's mode (the native one),
 * else ogles2's own window of that size. */
static void *OpenContext(int width, int height, int window)
{
    void *context = NULL;
    ULONG err = 0, mode = INVALID_ID, w = 0, h = 0;
    struct Screen *wb = window ? NULL : LockPubScreen(NULL);
    if (wb != NULL) {
        mode = GetVPModeID(&wb->ViewPort);
        UnlockPubScreen(NULL, wb);
    }
    if (mode != INVALID_ID) {
        context = aglCreateContextTags2(&err, OGLES2_CCT_MODEID, mode, OGLES2_CCT_DEPTH, 24,
                                        OGLES2_CCT_VSYNC, FALSE, OGLES2_CCT_GET_WIDTH, (ULONG)&w,
                                        OGLES2_CCT_GET_HEIGHT, (ULONG)&h, TAG_DONE);
        printf("gles-bench: Workbench mode %08lx full screen: %s (error %lu)\n", (unsigned long)mode,
               context ? "ok" : "refused", (unsigned long)err);
    }
    if (context == NULL) {
        context = aglCreateContextTags2(&err, OGLES2_CCT_PISTORM_WIDTH, (ULONG)width,
                                        OGLES2_CCT_PISTORM_HEIGHT, (ULONG)height, OGLES2_CCT_DEPTH, 24,
                                        OGLES2_CCT_VSYNC, FALSE, OGLES2_CCT_GET_WIDTH, (ULONG)&w,
                                        OGLES2_CCT_GET_HEIGHT, (ULONG)&h, TAG_DONE);
        printf("gles-bench: ogles2's own window: %s (error %lu)\n", context ? "ok" : "failed",
               (unsigned long)err);
    }
    if (context != NULL)
        printf("gles-bench: surface %lux%lu\n", (unsigned long)w, (unsigned long)h);
    return context;
}

int main(int argc, char **argv)
{
    const int frames = argc > 1 ? atoi(argv[1]) : 300;
    const int full = argc > 3 ? atoi(argv[3]) : 0;
    const int rebind = full ? 1 : (argc > 2 ? atoi(argv[2]) : 0);
    const int window = argc > 4 ? atoi(argv[4]) : 0;
    const int draws = argc > 5 && atoi(argv[5]) < kBenchDraws ? atoi(argv[5]) : kBenchDraws;
    const int warmup = 20;
    static unsigned char texels[kBenchTexSize * kBenchTexSize * 4];
    static float vertexState[kVertexVec4 * 4], fragmentState[kFragmentVec4 * 4];
    GLuint textures[kBenchTextures], buffers[2], program;
    GLint uMvp = -1, uNormal = -1, uLit = -1, uStage = -1, uV = -1, uF = -1;
    float projection[16], modelview[16], mvp[16], normal[9];
    struct DateStamp start, end;
    void *context = NULL;
    int width = 640, height = 480;
    int f, d, t, rc = 20;
    long ms;

    Bench_Build(0);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR) "graphics.library", 39);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR) "intuition.library", 39);
    if (GfxBase == NULL || IntuitionBase == NULL) {
        printf("gles-bench: needs graphics and intuition 39\n");
        goto out;
    }
    if (!OGLES2Open()) {
        printf("gles-bench: cannot open %s\n", OGLES2NAME);
        goto out;
    }
    Bench_NativeSize(&width, &height);
    context = OpenContext(width, height, window);
    if (context == NULL)
        goto out;
    aglMakeCurrent(context);

    program = full ? Link(kFullVertexShader, kFullFragmentShader) : Link(kVertexShader, kFragmentShader);
    if (program == 0)
        goto out;
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
    if (full) {
        uV = glGetUniformLocation(program, "u_v");
        uF = glGetUniformLocation(program, "u_f");
        printf("gles-bench: full state, u_v %ld, u_f %ld\n", (long)uV, (long)uF);
    } else {
        const float len = sqrtf(0.3f * 0.3f + 0.6f * 0.6f + 0.75f * 0.75f);
        uMvp = glGetUniformLocation(program, "u_mvp");
        uNormal = glGetUniformLocation(program, "u_normalMatrix");
        uLit = glGetUniformLocation(program, "u_lit");
        uStage = glGetUniformLocation(program, "u_stage");
        glUniform3f(glGetUniformLocation(program, "u_lightDir"), 0.3f / len, 0.6f / len, 0.75f / len);
        glUniform3f(glGetUniformLocation(program, "u_lightColour"), 0.9f, 0.85f, 0.8f);
        glUniform3f(glGetUniformLocation(program, "u_ambient"), 0.25f, 0.25f, 0.25f);
    }

    glGenBuffers(2, buffers);
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_benchVertices), g_benchVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(g_benchIndices), g_benchIndices, GL_STATIC_DRAW);
    for (t = 0; t < 4; ++t)
        glEnableVertexAttribArray((GLuint)t);
    SetAttributes(0);

    glGenTextures(kBenchTextures, textures);
    for (t = 0; t < kBenchTextures; ++t) {
        Bench_Texture(t, texels);
        glBindTexture(GL_TEXTURE_2D, textures[t]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kBenchTexSize, kBenchTexSize, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, texels);
    }
    glViewport(0, 0, width, height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
    Bench_Projection(projection, width, height);

    for (f = 0; f < warmup + frames; ++f) {
        int object = -1, texture = -1, lit = -1;
        if (f == warmup)
            DateStamp(&start);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (d = 0; d < draws; ++d) {
            const struct BenchDraw *draw = &g_benchDraws[d];
            if (full) {
                if (draw->object != object || draw->lit != lit) {
                    object = draw->object;
                    lit = draw->lit;
                    Bench_Modelview(object, f, modelview);
                    FillVertexState(vertexState, modelview, projection, lit);
                    glUniform4fv(uV, kVertexVec4, vertexState);
                }
                if (draw->texture != texture) {
                    texture = draw->texture;
                    glBindTexture(GL_TEXTURE_2D, textures[texture]);
                    FillFragmentState(fragmentState, texture);
                    glUniform4fv(uF, kFragmentVec4, fragmentState);
                }
            } else {
                if (draw->object != object) {
                    object = draw->object;
                    Bench_Modelview(object, f, modelview);
                    Bench_Multiply(mvp, projection, modelview);
                    normal[0] = modelview[0]; normal[1] = modelview[1]; normal[2] = modelview[2];
                    normal[3] = modelview[4]; normal[4] = modelview[5]; normal[5] = modelview[6];
                    normal[6] = modelview[8]; normal[7] = modelview[9]; normal[8] = modelview[10];
                    glUniformMatrix4fv(uMvp, 1, GL_FALSE, mvp);
                    glUniformMatrix3fv(uNormal, 1, GL_FALSE, normal);
                }
                if (draw->texture != texture) {
                    texture = draw->texture;
                    glBindTexture(GL_TEXTURE_2D, textures[texture]);
                    glUniform4f(uStage, 1.0f, 1.0f, 1.0f, (float)(texture & 1) * 0.1f);
                }
                if (draw->lit != lit) {
                    lit = draw->lit;
                    glUniform1f(uLit, lit ? 1.0f : 0.0f);
                }
            }
            if (rebind)
                SetAttributes(0);
            glDrawElements(GL_TRIANGLES, kBenchIndicesPerDraw, GL_UNSIGNED_SHORT,
                           (const void *)(d * kBenchIndicesPerDraw * sizeof(unsigned short)));
        }
        aglSwapBuffers();
    }
    glFinish();
    DateStamp(&end);
    ms = Bench_ElapsedMs(&start, &end);
    printf("gles-bench: %dx%d %s, %d frames, %d draws x %d triangles each, rebind %d, full %d: %ld ms, "
           "%ld.%02ld ms/frame\n",
           width, height, window ? "window" : "full screen", frames, draws, kBenchIndicesPerDraw / 3, rebind, full, ms, ms / frames,
           (ms * 100 / frames) % 100);
    fflush(stdout);
    rc = 0;

out:
    if (context != NULL)
        aglDestroyContext(context);
    OGLES2Close();
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase != NULL)
        CloseLibrary((struct Library *)GfxBase);
    return rc;
}

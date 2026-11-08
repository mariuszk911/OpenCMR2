/*
 * ogles2 side of the draw-cost benchmark (Domain B, target only). Draws the
 * workload of draw_bench_workload.h through ogles2.library as a shader
 * backend of OpenCMR2 would: the vertices and indices in static buffer
 * objects, one program whose vertex shader lights a draw or passes its
 * pre-lit colour, the model-view-projection and normal matrices uploaded when
 * the object changes, the texture and a stage-state uniform when the texture
 * changes, the lit flag when it changes. Compare with minigl_draw_bench.c.
 *
 *     amiga_ogles2_draw_bench [frames] [rebind]
 *
 * rebind 1: set the four vertex attribute pointers again on every draw, the
 * cost of a backend whose draws come from many vertex buffers.
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
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/ogles2.h>

#include "draw_bench_workload.h"

struct GfxBase *GfxBase;

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

static void SetAttributes(GLintptr base)
{
    const GLsizei stride = (GLsizei)sizeof(struct BenchVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 12));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)(base + 24));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (const void *)(base + 28));
}

int main(int argc, char **argv)
{
    const int frames = argc > 1 ? atoi(argv[1]) : 300;
    const int rebind = argc > 2 ? atoi(argv[2]) : 0;
    const int warmup = 20;
    static unsigned char texels[kBenchTexSize * kBenchTexSize * 4];
    GLuint textures[kBenchTextures], buffers[2], program, vs, fs;
    GLint uMvp, uNormal, uLit, uStage, linked = 0;
    float projection[16], modelview[16], mvp[16], normal[9];
    struct DateStamp start, end;
    void *context = NULL;
    ULONG err = 0, mode;
    int f, d, t, rc = 20;
    long ms;

    Bench_Build(0);
    if (!(GfxBase = (struct GfxBase *)OpenLibrary((STRPTR) "graphics.library", 39))) {
        printf("gles-bench: needs graphics.library 39\n");
        return 20;
    }
    if (!OGLES2Open()) {
        printf("gles-bench: cannot open %s\n", OGLES2NAME);
        goto out;
    }
    /* A full screen of its own, as MiniGL's; else ogles2's own window. */
    mode = BestModeID(BIDTAG_NominalWidth, kBenchWidth, BIDTAG_NominalHeight, kBenchHeight,
                      BIDTAG_Depth, 32, TAG_DONE);
    if (mode == INVALID_ID)
        mode = BestModeID(BIDTAG_NominalWidth, kBenchWidth, BIDTAG_NominalHeight, kBenchHeight,
                          BIDTAG_Depth, 24, TAG_DONE);
    if (mode != INVALID_ID) {
        printf("gles-bench: full screen, mode %08lx\n", (unsigned long)mode);
        context = aglCreateContextTags2(&err, OGLES2_CCT_MODEID, mode, OGLES2_CCT_DEPTH, 24,
                                        OGLES2_CCT_VSYNC, FALSE, TAG_DONE);
    }
    if (context == NULL) {
        printf("gles-bench: no full-screen context (error %lu), using ogles2's own 640x480 window\n",
               (unsigned long)err);
        context = aglCreateContextTags2(&err, OGLES2_CCT_PISTORM_WIDTH, kBenchWidth,
                                        OGLES2_CCT_PISTORM_HEIGHT, kBenchHeight, OGLES2_CCT_DEPTH, 24,
                                        OGLES2_CCT_VSYNC, FALSE, TAG_DONE);
    }
    if (context == NULL) {
        printf("gles-bench: aglCreateContextTags2 failed (error %lu)\n", (unsigned long)err);
        goto out;
    }
    aglMakeCurrent(context);

    vs = Compile(GL_VERTEX_SHADER, kVertexShader);
    fs = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
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
        goto out;
    }
    glUseProgram(program);
    uMvp = glGetUniformLocation(program, "u_mvp");
    uNormal = glGetUniformLocation(program, "u_normalMatrix");
    uLit = glGetUniformLocation(program, "u_lit");
    uStage = glGetUniformLocation(program, "u_stage");
    glUniform1i(glGetUniformLocation(program, "u_texture"), 0);
    {
        const float len = sqrtf(0.3f * 0.3f + 0.6f * 0.6f + 0.75f * 0.75f);
        glUniform3f(glGetUniformLocation(program, "u_lightDir"), 0.3f / len, 0.6f / len, 0.75f / len);
    }
    glUniform3f(glGetUniformLocation(program, "u_lightColour"), 0.9f, 0.85f, 0.8f);
    glUniform3f(glGetUniformLocation(program, "u_ambient"), 0.25f, 0.25f, 0.25f);

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
    glViewport(0, 0, kBenchWidth, kBenchHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0.1f, 0.12f, 0.15f, 1.0f);
    Bench_Projection(projection);

    for (f = 0; f < warmup + frames; ++f) {
        int object = -1, texture = -1, lit = -1;
        if (f == warmup)
            DateStamp(&start);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (d = 0; d < kBenchDraws; ++d) {
            const struct BenchDraw *draw = &g_benchDraws[d];
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
    printf("gles-bench: %d frames, %d draws x %d triangles each, rebind %d: %ld ms, %ld.%02ld ms/frame\n",
           frames, kBenchDraws, kBenchIndicesPerDraw / 3, rebind, ms, ms / frames,
           (ms * 100 / frames) % 100);
    fflush(stdout);
    rc = 0;

out:
    if (context != NULL)
        aglDestroyContext(context);
    OGLES2Close();
    CloseLibrary((struct Library *)GfxBase);
    return rc;
}

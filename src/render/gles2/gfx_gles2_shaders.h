/*
 * GLSL ES 1.00 shaders of the GLES2 renderer: the fixed-function emulation of
 * src/render/shaders/{fixed3d.vert,fixedtl.vert,fixed.frag}, written for
 * ogles2.library.
 *
 * They are constant strings on purpose: ogles2 finds a compiled program by a
 * hash of the exact text (PROGDIR:shaders/<hash>.v3dp), so nothing may be
 * added to them at run time. Any edit needs new binaries (see
 * docs/AMIGA_GLES2_PLAN.md, "Shipping the shader binaries").
 *
 * Differences from the host shaders, all forced by GLSL ES 1.00 or chosen to
 * keep the Amiga's GPU work small:
 *  - no uniform blocks: the blocks are packed into vec4 arrays (kLayout in
 *    gfx_gles2_uniforms.h gives each slot), integers travel as floats;
 *  - no switch, no integer bit operations: the texture coordinate source is
 *    decoded on the CPU (0 uv0, 1 uv1, 2 reflection vector);
 *  - clip space is fixed up as spirv-cross does for the host (y flipped, z
 *    from Direct3D's 0..1 to GL's -1..1);
 *  - vertex colours are four bytes in memory order A, R, G, B (the DWORD is
 *    big-endian), read as .yzwx;
 *  - only plain 2D and cube textures: no distance field, menu dot, vector
 *    icon, scaled point sampling or distance fade (host-only enhancements).
 */
#ifndef OPENCMR2_RENDER_GLES2_GFX_GLES2_SHADERS_H
#define OPENCMR2_RENDER_GLES2_GFX_GLES2_SHADERS_H

static const char kGles2Vertex3D[] = R"GLSL(
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aDiffuse;
attribute vec4 aSpecular;
attribute vec2 aTex0;
attribute vec2 aTex1;

uniform vec4 u_v[83];

varying vec4 vDiffuse;
varying vec4 vSpecular;
varying vec4 vTex0;
varying vec4 vTex1;
varying vec2 vFog;

vec4 MaterialColor(float source, vec4 material, vec4 color1, vec4 color2)
{
    if (u_v[77].y > 0.5) {
        if (abs(source - 1.0) < 0.5)
            return color1;
        if (abs(source - 2.0) < 0.5)
            return color2;
    }
    return material;
}

vec4 TexCoord(float mode, float transform, mat4 textureMatrix, vec3 eyePosition, vec3 eyeNormal)
{
    vec4 tc;
    if (mode > 1.5) {
        vec3 e = normalize(-eyePosition);
        tc = vec4(2.0 * dot(e, eyeNormal) * eyeNormal - e, 1.0);
    } else if (mode > 0.5) {
        tc = vec4(aTex1, 1.0, 0.0);
    } else {
        tc = vec4(aTex0, 1.0, 0.0);
    }
    if (transform > 0.5) {
        vec4 t = textureMatrix * vec4(tc.xyz, 1.0);
        tc = vec4(t.xyz, 1.0);
    }
    return tc;
}

// One light: the arguments are constants, so every access to u_v has a
// constant index (a loop-computed index read wrong data on the target).
void Light(int b, vec3 eyePosition, vec3 n, vec3 toEye, inout vec3 ambient, inout vec3 diffuseSum,
           inout vec3 specularSum)
{
    vec4 lSpot = u_v[b + 6];
    if (lSpot.z < 0.5)
        return;
    vec4 lPosition = u_v[b + 3];
    vec4 lDirection = u_v[b + 4];
    vec4 lAtten = u_v[b + 5];
    vec3 L;
    float atten = 1.0;
    float spotFactor = 1.0;
    if (lPosition.w > 2.5 && lPosition.w < 3.5) {
        L = normalize(-lDirection.xyz);
    } else {
        vec3 toLight = lPosition.xyz - eyePosition;
        float d = length(toLight);
        if (d > lDirection.w)
            return;
        L = d > 0.0 ? toLight / d : vec3(0.0, 0.0, 1.0);
        float denom = lAtten.x + lAtten.y * d + lAtten.z * d * d;
        atten = denom > 0.0 ? 1.0 / denom : 1.0;
        if (lPosition.w > 1.5 && lPosition.w < 2.5) {
            float rho = dot(-L, normalize(lDirection.xyz));
            if (rho <= lSpot.y)
                spotFactor = 0.0;
            else if (rho < lSpot.x)
                spotFactor = pow((rho - lSpot.y) / max(lSpot.x - lSpot.y, 1e-6), lAtten.w);
        }
    }
    float scale = atten * spotFactor;
    ambient += u_v[b + 2].rgb * scale;
    float nDotL = max(dot(n, L), 0.0);
    diffuseSum += u_v[b].rgb * nDotL * scale;
    if (u_v[77].z > 0.5 && nDotL > 0.0) {
        vec3 h = normalize(toEye + L);
        specularSum += u_v[b + 1].rgb * pow(max(dot(n, h), 0.0), u_v[20].w) * scale;
    }
}

void main()
{
    vec4 color1 = aDiffuse.yzwx;
    vec4 color2 = aSpecular.yzwx;
    mat4 worldView = mat4(u_v[0], u_v[1], u_v[2], u_v[3]);
    mat4 projection = mat4(u_v[4], u_v[5], u_v[6], u_v[7]);
    vec4 eye = worldView * vec4(aPosition, 1.0);
    vec3 n = mat3(worldView[0].xyz, worldView[1].xyz, worldView[2].xyz) * aNormal;
    if (u_v[79].y > 0.5 && dot(n, n) > 0.0)
        n = normalize(n);

    vec4 diffuse = color1;
    vec4 specular = color2;
    if (u_v[77].x > 0.5) {
        vec4 matDiffuse = MaterialColor(u_v[78].x, u_v[16], color1, color2);
        vec4 matSpecular = MaterialColor(u_v[78].y, u_v[18], color1, color2);
        vec4 matAmbient = MaterialColor(u_v[78].z, u_v[17], color1, color2);
        vec4 matEmissive = MaterialColor(u_v[78].w, u_v[19], color1, color2);
        vec3 ambient = u_v[20].rgb;
        vec3 diffuseSum = vec3(0.0);
        vec3 specularSum = vec3(0.0);
        vec3 toEye = u_v[77].w > 0.5 ? normalize(-eye.xyz) : vec3(0.0, 0.0, -1.0);
        Light(21, eye.xyz, n, toEye, ambient, diffuseSum, specularSum);
        Light(28, eye.xyz, n, toEye, ambient, diffuseSum, specularSum);
        Light(35, eye.xyz, n, toEye, ambient, diffuseSum, specularSum);
        Light(42, eye.xyz, n, toEye, ambient, diffuseSum, specularSum);
        Light(49, eye.xyz, n, toEye, ambient, diffuseSum, specularSum);
        Light(56, eye.xyz, n, toEye, ambient, diffuseSum, specularSum);
        Light(63, eye.xyz, n, toEye, ambient, diffuseSum, specularSum);
        Light(70, eye.xyz, n, toEye, ambient, diffuseSum, specularSum);
        diffuse = vec4(clamp(matEmissive.rgb + ambient * matAmbient.rgb + diffuseSum * matDiffuse.rgb, 0.0, 1.0),
                       matDiffuse.a);
        specular = vec4(clamp(specularSum * matSpecular.rgb, 0.0, 1.0), color2.a);
    }

    vDiffuse = diffuse;
    vSpecular = specular;
    vTex0 = TexCoord(u_v[81].x, u_v[81].z, mat4(u_v[8], u_v[9], u_v[10], u_v[11]), eye.xyz, n);
    vTex1 = TexCoord(u_v[81].y, u_v[81].w, mat4(u_v[12], u_v[13], u_v[14], u_v[15]), eye.xyz, n);
    float fog = u_v[79].z > 0.5 ? color2.a : 1.0;
    if (abs(u_v[79].x - 3.0) < 0.5) {
        float range = u_v[80].y - u_v[80].x;
        fog = range != 0.0 ? clamp((u_v[80].y - eye.z) / range, 0.0, 1.0) : 1.0;
    }
    vFog = vec2(fog, eye.z);

    vec4 clip = projection * eye;
    clip.xy += u_v[82].zw * clip.w;
    clip.y = -clip.y;
    clip.z = 2.0 * clip.z - clip.w;
    gl_Position = clip;
    gl_PointSize = 1.0;
}
)GLSL";

/* The light loop with a computed index: the first version, kept to compare. */
static const char kGles2Vertex3DLoop[] = R"GLSL(
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aDiffuse;
attribute vec4 aSpecular;
attribute vec2 aTex0;
attribute vec2 aTex1;

uniform vec4 u_v[83];

varying vec4 vDiffuse;
varying vec4 vSpecular;
varying vec4 vTex0;
varying vec4 vTex1;
varying vec2 vFog;

vec4 MaterialColor(float source, vec4 material, vec4 color1, vec4 color2)
{
    if (u_v[77].y > 0.5) {
        if (abs(source - 1.0) < 0.5)
            return color1;
        if (abs(source - 2.0) < 0.5)
            return color2;
    }
    return material;
}

vec4 TexCoord(float mode, float transform, mat4 textureMatrix, vec3 eyePosition, vec3 eyeNormal)
{
    vec4 tc;
    if (mode > 1.5) {
        vec3 e = normalize(-eyePosition);
        tc = vec4(2.0 * dot(e, eyeNormal) * eyeNormal - e, 1.0);
    } else if (mode > 0.5) {
        tc = vec4(aTex1, 1.0, 0.0);
    } else {
        tc = vec4(aTex0, 1.0, 0.0);
    }
    if (transform > 0.5) {
        vec4 t = textureMatrix * vec4(tc.xyz, 1.0);
        tc = vec4(t.xyz, 1.0);
    }
    return tc;
}

void main()
{
    vec4 color1 = aDiffuse.yzwx;
    vec4 color2 = aSpecular.yzwx;
    mat4 worldView = mat4(u_v[0], u_v[1], u_v[2], u_v[3]);
    mat4 projection = mat4(u_v[4], u_v[5], u_v[6], u_v[7]);
    vec4 eye = worldView * vec4(aPosition, 1.0);
    vec3 n = mat3(worldView[0].xyz, worldView[1].xyz, worldView[2].xyz) * aNormal;
    if (u_v[79].y > 0.5 && dot(n, n) > 0.0)
        n = normalize(n);

    vec4 diffuse = color1;
    vec4 specular = color2;
    if (u_v[77].x > 0.5) {
        vec4 matDiffuse = MaterialColor(u_v[78].x, u_v[16], color1, color2);
        vec4 matSpecular = MaterialColor(u_v[78].y, u_v[18], color1, color2);
        vec4 matAmbient = MaterialColor(u_v[78].z, u_v[17], color1, color2);
        vec4 matEmissive = MaterialColor(u_v[78].w, u_v[19], color1, color2);
        vec3 ambient = u_v[20].rgb;
        vec3 diffuseSum = vec3(0.0);
        vec3 specularSum = vec3(0.0);
        vec3 toEye = u_v[77].w > 0.5 ? normalize(-eye.xyz) : vec3(0.0, 0.0, -1.0);
        for (int i = 0; i < 8; i++) {
            int b = 21 + i * 7;
            vec4 lSpot = u_v[b + 6];
            if (lSpot.z < 0.5)
                continue;
            vec4 lPosition = u_v[b + 3];
            vec4 lDirection = u_v[b + 4];
            vec4 lAtten = u_v[b + 5];
            vec3 L;
            float atten = 1.0;
            float spotFactor = 1.0;
            if (lPosition.w > 2.5 && lPosition.w < 3.5) {
                L = normalize(-lDirection.xyz);
            } else {
                vec3 toLight = lPosition.xyz - eye.xyz;
                float d = length(toLight);
                if (d > lDirection.w)
                    continue;
                L = d > 0.0 ? toLight / d : vec3(0.0, 0.0, 1.0);
                float denom = lAtten.x + lAtten.y * d + lAtten.z * d * d;
                atten = denom > 0.0 ? 1.0 / denom : 1.0;
                if (lPosition.w > 1.5 && lPosition.w < 2.5) {
                    float rho = dot(-L, normalize(lDirection.xyz));
                    if (rho <= lSpot.y)
                        spotFactor = 0.0;
                    else if (rho < lSpot.x)
                        spotFactor = pow((rho - lSpot.y) / max(lSpot.x - lSpot.y, 1e-6), lAtten.w);
                }
            }
            float scale = atten * spotFactor;
            ambient += u_v[b + 2].rgb * scale;
            float nDotL = max(dot(n, L), 0.0);
            diffuseSum += u_v[b].rgb * nDotL * scale;
            if (u_v[77].z > 0.5 && nDotL > 0.0) {
                vec3 h = normalize(toEye + L);
                specularSum += u_v[b + 1].rgb * pow(max(dot(n, h), 0.0), u_v[20].w) * scale;
            }
        }
        diffuse = vec4(clamp(matEmissive.rgb + ambient * matAmbient.rgb + diffuseSum * matDiffuse.rgb, 0.0, 1.0),
                       matDiffuse.a);
        specular = vec4(clamp(specularSum * matSpecular.rgb, 0.0, 1.0), color2.a);
    }

    vDiffuse = diffuse;
    vSpecular = specular;
    vTex0 = TexCoord(u_v[81].x, u_v[81].z, mat4(u_v[8], u_v[9], u_v[10], u_v[11]), eye.xyz, n);
    vTex1 = TexCoord(u_v[81].y, u_v[81].w, mat4(u_v[12], u_v[13], u_v[14], u_v[15]), eye.xyz, n);
    float fog = u_v[79].z > 0.5 ? color2.a : 1.0;
    if (abs(u_v[79].x - 3.0) < 0.5) {
        float range = u_v[80].y - u_v[80].x;
        fog = range != 0.0 ? clamp((u_v[80].y - eye.z) / range, 0.0, 1.0) : 1.0;
    }
    vFog = vec2(fog, eye.z);

    vec4 clip = projection * eye;
    clip.xy += u_v[82].zw * clip.w;
    clip.y = -clip.y;
    clip.z = 2.0 * clip.z - clip.w;
    gl_Position = clip;
    gl_PointSize = 1.0;
}
)GLSL";

static const char kGles2VertexTL[] = R"GLSL(
attribute vec4 aPosition;
attribute vec4 aDiffuse;
attribute vec4 aSpecular;
attribute vec2 aTex;

uniform vec4 u_v[1];

varying vec4 vDiffuse;
varying vec4 vSpecular;
varying vec4 vTex0;
varying vec4 vTex1;
varying vec2 vFog;

void main()
{
    float w = aPosition.w != 0.0 ? 1.0 / aPosition.w : 1.0;
    float x = (aPosition.x + 0.5) / u_v[0].x * 2.0 - 1.0;
    float y = 1.0 - (aPosition.y + 0.5) / u_v[0].y * 2.0;
    vec4 clip = vec4(x * w, y * w, aPosition.z * w, w);
    vDiffuse = aDiffuse.yzwx;
    vSpecular = aSpecular.yzwx;
    vTex0 = vec4(aTex, 1.0, 0.0);
    vTex1 = vec4(aTex, 1.0, 0.0);
    vFog = vec2(aSpecular.x, w);
    clip.y = -clip.y;
    clip.z = 2.0 * clip.z - clip.w;
    gl_Position = clip;
    gl_PointSize = 1.0;
}
)GLSL";

static const char kGles2Fragment[] = R"GLSL(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

varying vec4 vDiffuse;
varying vec4 vSpecular;
varying vec4 vTex0;
varying vec4 vTex1;
varying vec2 vFog;

uniform sampler2D tex0;
uniform samplerCube cube0;
uniform sampler2D tex1;
uniform samplerCube cube1;

uniform vec4 u_f[13];

vec4 Arg(float arg, vec4 current, vec4 texel)
{
    float a = arg;
    bool replicate = a > 31.5;
    if (replicate)
        a -= 32.0;
    bool complement = a > 15.5;
    if (complement)
        a -= 16.0;
    vec4 v;
    if (a < 0.5)
        v = vDiffuse;
    else if (a < 1.5)
        v = current;
    else if (a < 2.5)
        v = texel;
    else if (a < 3.5)
        v = u_f[4];
    else if (a < 4.5)
        v = vSpecular;
    else
        v = current;
    if (complement)
        v = vec4(1.0) - v;
    if (replicate)
        v = v.aaaa;
    return v;
}

vec4 Op(float op, vec4 a, vec4 b)
{
    if (op < 2.5)
        return a;
    if (op < 3.5)
        return b;
    if (op < 4.5)
        return a * b;
    if (op < 5.5)
        return clamp(a * b * 2.0, 0.0, 1.0);
    if (op < 6.5)
        return clamp(a * b * 4.0, 0.0, 1.0);
    if (op < 7.5)
        return clamp(a + b, 0.0, 1.0);
    if (op < 8.5)
        return clamp(a + b - 0.5, 0.0, 1.0);
    if (op < 9.5)
        return clamp((a + b - 0.5) * 2.0, 0.0, 1.0);
    if (op < 10.5)
        return clamp(a - b, 0.0, 1.0);
    return a;
}

float SignedByte(float v)
{
    float b = floor(v * 255.0 + 0.5);
    return (b >= 128.0 ? b - 256.0 : b) / 128.0;
}

bool Compare(float func, float a, float b)
{
    if (func < 1.5)
        return false;
    if (func < 2.5)
        return a < b;
    if (func < 3.5)
        return a == b;
    if (func < 4.5)
        return a <= b;
    if (func < 5.5)
        return a > b;
    if (func < 6.5)
        return a != b;
    if (func < 7.5)
        return a >= b;
    return true;
}

void main()
{
    vec4 current = vDiffuse;
    vec2 bumpOffset = vec2(0.0);
    float luminance = 1.0;

    if (u_f[0].x > 1.5) {
        if (u_f[0].x > 21.5) {
            vec4 bump = texture2D(tex0, vTex0.xy);
            float du = SignedByte(bump.r);
            float dv = SignedByte(bump.g);
            bumpOffset = vec2(du * u_f[10].x + dv * u_f[10].z, du * u_f[10].y + dv * u_f[10].w);
            if (u_f[0].x > 22.5)
                luminance = clamp(bump.b * u_f[11].x + u_f[11].y, 0.0, 1.0);
        } else {
            vec4 texel = vec4(1.0);
            if (u_f[1].z > 1.5)
                texel = textureCube(cube0, vTex0.xyz);
            else if (u_f[1].z > 0.5)
                texel = texture2D(tex0, vTex0.xy);
            vec4 color = Op(u_f[0].x, Arg(u_f[0].y, current, texel), Arg(u_f[0].z, current, texel));
            float alpha = u_f[0].w < 1.5 ? current.a
                                         : Op(u_f[0].w, Arg(u_f[1].x, current, texel), Arg(u_f[1].y, current, texel)).a;
            current = vec4(color.rgb, alpha);
        }
        if (u_f[2].x > 1.5) {
            vec4 texel = vec4(1.0);
            if (u_f[3].z > 1.5)
                texel = textureCube(cube1, vTex1.xyz + vec3(bumpOffset, 0.0));
            else if (u_f[3].z > 0.5)
                texel = texture2D(tex1, vTex1.xy + bumpOffset);
            texel.rgb *= luminance;
            vec4 color = Op(u_f[2].x, Arg(u_f[2].y, current, texel), Arg(u_f[2].z, current, texel));
            float alpha = u_f[2].w < 1.5 ? current.a
                                         : Op(u_f[2].w, Arg(u_f[3].x, current, texel), Arg(u_f[3].y, current, texel)).a;
            current = vec4(color.rgb, alpha);
        }
    }

    if (u_f[8].z > 0.5)
        current.rgb = clamp(current.rgb + vSpecular.rgb, 0.0, 1.0);

    if (u_f[5].x > 0.5 && !Compare(u_f[5].y, floor(current.a * 255.0 + 0.5), floor(u_f[6].x * 255.0 + 0.5)))
        discard;

    if (u_f[8].x > 0.5) {
        float f = vFog.x;
        if (u_f[8].y > 2.5) {
            float range = u_f[9].y - u_f[9].x;
            f = range != 0.0 ? clamp((u_f[9].y - vFog.y) / range, 0.0, 1.0) : 1.0;
        }
        current.rgb = mix(u_f[7].rgb, current.rgb, f);
    }
    gl_FragColor = current;
}
)GLSL";

/* Diagnostics only ([video] gles2_simple_fragment / gles2_simple_vertex): the
   cheapest shaders that still draw textured geometry, to tell which stage of
   the full shaders the GPU time goes to. Wrong output on purpose. */
static const char kGles2FragmentSimple[] = R"GLSL(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec4 vDiffuse;
varying vec4 vSpecular;
varying vec4 vTex0;
varying vec4 vTex1;
varying vec2 vFog;
uniform sampler2D tex0;
uniform samplerCube cube0;
uniform sampler2D tex1;
uniform samplerCube cube1;
uniform vec4 u_f[13];
void main()
{
    gl_FragColor = vDiffuse * texture2D(tex0, vTex0.xy);
}
)GLSL";

static const char kGles2Vertex3DSimple[] = R"GLSL(
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aDiffuse;
attribute vec4 aSpecular;
attribute vec2 aTex0;
attribute vec2 aTex1;
uniform vec4 u_v[83];
varying vec4 vDiffuse;
varying vec4 vSpecular;
varying vec4 vTex0;
varying vec4 vTex1;
varying vec2 vFog;
void main()
{
    mat4 worldView = mat4(u_v[0], u_v[1], u_v[2], u_v[3]);
    mat4 projection = mat4(u_v[4], u_v[5], u_v[6], u_v[7]);
    vec4 clip = projection * (worldView * vec4(aPosition, 1.0));
    clip.xy += u_v[82].zw * clip.w;
    clip.y = -clip.y;
    clip.z = 2.0 * clip.z - clip.w;
    gl_Position = clip;
    gl_PointSize = 1.0;
    vDiffuse = aDiffuse.yzwx;
    vSpecular = vec4(0.0);
    vTex0 = vec4(aTex0, 1.0, 0.0);
    vTex1 = vec4(0.0);
    vFog = vec2(1.0, 0.0);
}
)GLSL";

/* Copies the back buffer to the screen. */
static const char kGles2VertexBlit[] = R"GLSL(
attribute vec2 aPosition;
varying vec2 vUV;
void main()
{
    vUV = vec2(aPosition.x * 0.5 + 0.5, 0.5 - aPosition.y * 0.5);
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)GLSL";

static const char kGles2FragmentBlit[] = R"GLSL(
precision mediump float;
varying vec2 vUV;
uniform sampler2D tex0;
void main()
{
    gl_FragColor = vec4(texture2D(tex0, vUV).rgb, 1.0);
}
)GLSL";

#endif

#include "port/d3d_light.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-4f;
}

static GfxColorValue Grey(float v, float a = 1.0f)
{
    GfxColorValue c = { v, v, v, a };
    return c;
}

static GfxLight White(int type)
{
    GfxLight l;
    memset(&l, 0, sizeof(l));
    l.type = type;
    l.diffuse = Grey(1.0f);
    l.attenuation0 = 1.0f;
    return l;
}

int main()
{
    const GfxColorValue noAmbient = Grey(0.0f);
    D3DLightMaterial m;
    m.diffuse = Grey(1.0f, 0.5f);
    m.ambient = Grey(1.0f);
    m.emissive = Grey(0.0f);
    const float up[3] = { 0, 1, 0 };
    const float origin[3] = { 0, 0, 0 };
    int on[2] = { 1, 1 };

    // Directional straight down onto an upward face: full diffuse, alpha from
    // the material diffuse.
    GfxLight dir = White(GFX_LIGHT_DIRECTIONAL);
    dir.direction.y = -1;
    GfxColorValue c = D3DLight_Vertex(&dir, on, 1, &noAmbient, &m, origin, up);
    REQUIRE(Near(c.r, 1) && Near(c.a, 0.5f));
    // Facing away: nothing.
    const float down[3] = { 0, -1, 0 };
    c = D3DLight_Vertex(&dir, on, 1, &noAmbient, &m, origin, down);
    REQUIRE(Near(c.r, 0));
    // A disabled light adds nothing.
    int off[1] = { 0 };
    c = D3DLight_Vertex(&dir, off, 1, &noAmbient, &m, origin, up);
    REQUIRE(Near(c.r, 0));

    // The logged car light: point, white ambient and diffuse, a0 30, a2 0.1,
    // range 60. Directly above a face at distance 10: (1 + 1) / (30 + 10).
    GfxLight point = White(GFX_LIGHT_POINT);
    point.ambient = Grey(1.0f);
    point.position.y = 10;
    point.range = 60;
    point.attenuation0 = 30;
    point.attenuation2 = 0.1f;
    c = D3DLight_Vertex(&point, on, 1, &noAmbient, &m, origin, up);
    REQUIRE(Near(c.r, 2.0f / 40.0f));
    // It lights by position, not by a fixed direction: a face beside the
    // light, facing it, is lit; the same face on the far side is not.
    const float side[3] = { 5, 10, 0 };
    const float towardLight[3] = { -1, 0, 0 };
    const float awayFromLight[3] = { 1, 0, 0 };
    const GfxColorValue facing = D3DLight_Vertex(&point, on, 1, &noAmbient, &m, side, towardLight);
    const GfxColorValue away = D3DLight_Vertex(&point, on, 1, &noAmbient, &m, side, awayFromLight);
    REQUIRE(facing.r > away.r);
    // Past the range nothing at all, ambient included.
    const float far[3] = { 0, 10, 70 };
    c = D3DLight_Vertex(&point, on, 1, &noAmbient, &m, far, up);
    REQUIRE(Near(c.r, 0));

    // Spot: inside the inner cone full, outside the outer cone nothing.
    GfxLight spot = White(GFX_LIGHT_SPOT);
    spot.position.y = 10;
    spot.direction.y = -1;
    spot.range = 100;
    spot.theta = 0.2f;
    spot.phi = 1.0f;
    spot.falloff = 1;
    c = D3DLight_Vertex(&spot, on, 1, &noAmbient, &m, origin, up);
    REQUIRE(Near(c.r, 1));
    const float outside[3] = { 50, 0, 0 };
    c = D3DLight_Vertex(&spot, on, 1, &noAmbient, &m, outside, up);
    REQUIRE(Near(c.r, 0));

    // Emissive and global ambient add; the sum clamps at 1.
    m.emissive = Grey(0.8f);
    const GfxColorValue ambient = Grey(0.5f);
    c = D3DLight_Vertex(&dir, on, 0, &ambient, &m, origin, up);
    REQUIRE(Near(c.r, 1));
    m.emissive = Grey(0.25f);
    c = D3DLight_Vertex(&dir, on, 0, &ambient, &m, origin, up);
    REQUIRE(Near(c.r, 0.75f));
    return 0;
}

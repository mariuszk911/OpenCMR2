#include "port/light_range.h"

#include <cmath>
#include <cstdio>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

// Laid out like the renderer's 48-byte vertex: position first.
struct Vertex {
    float x, y, z;
    float rest[9];
};

static bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-4f;
}

int main()
{
    static const float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    Vertex v[4] = {};
    v[0].x = -1; v[0].y = 0; v[0].z = 0;
    v[1].x = 3;  v[1].y = 2; v[1].z = 0;
    v[2].x = 1;  v[2].y = 0; v[2].z = 4;
    v[3].x = 100; v[3].y = 100; v[3].z = 100;   // not indexed below

    LightRangeSphere s;
    REQUIRE(!LightRange_Bounds(&v[0].x, sizeof(Vertex), nullptr, 0, &s));

    // Only the indexed vertices count.
    const uint16_t indices[3] = { 0, 1, 2 };
    REQUIRE(LightRange_Bounds(&v[0].x, sizeof(Vertex), indices, 3, &s));
    REQUIRE(Near(s.x, 1) && Near(s.y, 1) && Near(s.z, 2));
    REQUIRE(Near(s.radius, 0.5f * std::sqrt(16.0f + 4.0f + 16.0f)));

    // Without indices every vertex in the window counts.
    LightRangeSphere all;
    REQUIRE(LightRange_Bounds(&v[0].x, sizeof(Vertex), nullptr, 4, &all));
    REQUIRE(all.radius > s.radius);

    // World transform: translation moves the centre, scale grows the radius.
    const float m[16] = { 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1 };
    const LightRangeSphere w = LightRange_ToWorld(s, m);
    REQUIRE(Near(w.x, 12) && Near(w.y, 21) && Near(w.z, 32));
    REQUIRE(Near(w.radius, s.radius * 2));
    const LightRangeSphere same = LightRange_ToWorld(s, kIdentity);
    REQUIRE(Near(same.x, s.x) && Near(same.radius, s.radius));

    // Nearest distance: the sphere's surface, 0 when the light is inside it.
    const LightRangeSphere far = { 0, 0, 150, 10 };
    REQUIRE(Near(LightRange_NearestDistance(0, 0, 0, far), 140));
    const LightRangeSphere around = { 0, 0, 5, 10 };
    REQUIRE(LightRange_NearestDistance(0, 0, 0, around) == 0.0f);

    // The logged car light: a0 about 30, a2 0.1, range 60. Faint at the
    // light, smaller with distance, 0 past the range.
    const float atLight = LightRange_Attenuation(30, 0, 0.1f, 60, 0);
    REQUIRE(Near(atLight, 1.0f / 30.0f));
    REQUIRE(LightRange_Attenuation(30, 0, 0.1f, 60, 20) < atLight);
    REQUIRE(Near(LightRange_Attenuation(30, 0, 0.1f, 60, 20), 1.0f / 70.0f));
    REQUIRE(LightRange_Attenuation(30, 0, 0.1f, 60, 61) == 0.0f);
    // No attenuation terms: full strength inside the range (the front spot).
    REQUIRE(LightRange_Attenuation(0, 0, 0, 100, 99) == 1.0f);
    REQUIRE(LightRange_Attenuation(0, 0, 0, 100, 101) == 0.0f);
    return 0;
}

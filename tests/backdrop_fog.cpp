#include "port/backdrop_fog.h"

#include <cmath>
#include <cstdio>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); return 1; } } while (0)

// The farthest eye distance the check covers; the sky is drawn with a far
// plane of 150 units (Game_DrawSceneViewport), so this is well beyond it.
static const float kFarthestEyeDistance = 2000.0f;

// GL's linear fog factor, clamped as GL clamps it.
static float LinearFog(float start, float end, float z)
{
    const float f = (end - z) / (end - start);
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

int main()
{
    for (unsigned int alpha = 0; alpha <= 255; ++alpha) {
        float start;
        float end;

        BackdropFog_Limits(alpha, &start, &end);
        REQUIRE(end > start);
        const float wanted = (float)alpha / 255.0f;
        // The factor must not visibly change across the sky: within half an
        // 8-bit step at every distance a backdrop vertex can have.
        for (float z = 0.0f; z <= kFarthestEyeDistance; z += 50.0f)
            REQUIRE(std::fabs(LinearFog(start, end, z) - wanted) < 0.5f / 255.0f + 0.002f);
    }

    /* The two ends are exact: all fog, and no fog. */
    float start;
    float end;
    BackdropFog_Limits(0, &start, &end);
    REQUIRE(LinearFog(start, end, 100.0f) == 0.0f);
    BackdropFog_Limits(255, &start, &end);
    REQUIRE(std::fabs(LinearFog(start, end, 0.0f) - 1.0f) < 1e-5f);
    return 0;
}

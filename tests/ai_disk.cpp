// .ai? CPU-driver tables: every converted region and count bound.
#include "port/ai_disk.h"
#include "port/endian.h"

#include <stdio.h>
#include <string.h>

static void PutLE32(unsigned char *p, unsigned int value)
{
#if defined(OPENCMR2_TEST_FORCE_BIG_ENDIAN) && defined(OPENCMR2_LITTLE_ENDIAN)
    p[0] = (unsigned char)((value >> 24) & 0xff);
    p[1] = (unsigned char)((value >> 16) & 0xff);
    p[2] = (unsigned char)((value >> 8) & 0xff);
    p[3] = (unsigned char)(value & 0xff);
#else
    p[0] = (unsigned char)(value & 0xff);
    p[1] = (unsigned char)((value >> 8) & 0xff);
    p[2] = (unsigned char)((value >> 16) & 0xff);
    p[3] = (unsigned char)((value >> 24) & 0xff);
#endif
}

static unsigned int GetHost32(const unsigned char *p)
{
    unsigned int value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static int Fail(const char *what, int line)
{
    printf("ai_disk: FAIL %s (line %d)\n", what, line);
    return 1;
}

#define CHECK(expr) do { if (!(expr)) return Fail(#expr, __LINE__); } while (0)

static const unsigned int kCarOffset = 0;
static const unsigned int kRouteCountOffset = 0x18;
static const unsigned int kRoutesOffset = 0x1c;
static const unsigned int kDescriptorCountOffset = kRoutesOffset + AI_DISK_ROUTE_STRIDE;
static const unsigned int kDescriptorsOffset = kDescriptorCountOffset + 4;
static const unsigned int kPointCountOffset = kDescriptorsOffset + AI_DISK_DESCRIPTOR_STRIDE;
static const unsigned int kPointsOffset = kPointCountOffset + 4;
static const unsigned int kNetworkCountOffset = kPointsOffset + AI_DISK_POINT_STRIDE;
static const unsigned int kNetworksOffset = kNetworkCountOffset + 4;
static const unsigned int kWeightsOffset = kNetworksOffset + AI_DISK_NETWORK_STRIDE;
static const unsigned int kWeightWords = 9;
static const unsigned int kActivationOffset = kWeightsOffset + kWeightWords * 4;
static const unsigned int kNodesOffset = kActivationOffset + AI_DISK_ACTIVATION_COUNT * 4;
static const unsigned int kReferencesOffset = kNodesOffset + AI_DISK_NODE_COUNT * AI_DISK_NODE_STRIDE;
static const unsigned int kFileSize = kReferencesOffset + AI_DISK_REFERENCE_ROUTE_SIZE;

static void PutCount(unsigned char *p, unsigned int value)
{
    PutLE32(p, value);
}

static void BuildSynthetic(unsigned char *file)
{
    unsigned int i;
    unsigned char *route;
    unsigned char *network;

    memset(file, 0xcc, kFileSize);
    for (i = 0; i < AI_DISK_CAR_COUNT; i++)
        PutLE32(file + kCarOffset + i * 4, i);
    PutCount(file + kRouteCountOffset, 1);
    route = file + kRoutesOffset;
    PutLE32(route + 0x00, 0x11223344);
    PutLE32(route + 0x84, 0x55667788);
    PutCount(file + kDescriptorCountOffset, 1);
    for (i = 0; i < AI_DISK_DESCRIPTOR_STRIDE; i++)
        file[kDescriptorsOffset + i] = (unsigned char)(0x40 + i);
    PutCount(file + kPointCountOffset, 1);
    for (i = 0; i < AI_DISK_POINT_STRIDE; i++)
        file[kPointsOffset + i] = (unsigned char)(0x60 + i);
    PutCount(file + kNetworkCountOffset, 1);
    network = file + kNetworksOffset;
    for (i = 0; i < 8; i++)
        network[i] = (unsigned char)(0x70 + i);
    PutLE32(network + 8, 2);
    network[12] = 2;
    network[13] = 3;
    network[14] = 1;
    network[15] = 0;
    for (i = 0; i < 4; i++)
        PutLE32(network + 16 + i * 4, 0x80000000u + i);
    for (i = 0; i < kWeightWords; i++)
        PutLE32(file + kWeightsOffset + i * 4, 0x3f800000u + i);
    for (i = 0; i < AI_DISK_ACTIVATION_COUNT; i++)
        PutLE32(file + kActivationOffset + i * 4, 0x1000u + i);
    for (i = 0; i < AI_DISK_NODE_COUNT; i++) {
        unsigned char *node = file + kNodesOffset + i * AI_DISK_NODE_STRIDE;
        node[0] = (unsigned char)i;
        node[1] = 0x11;
        node[2] = 0x22;
        node[3] = 0x33;
        PutLE32(node + 4, 0x2000u + i);
        node[8] = (unsigned char)(0x90 + i);
        node[9] = 0x91;
        node[10] = 0x92;
        node[11] = 0x93;
    }
    for (i = 0; i < AI_DISK_NODE_COUNT; i++) {
        PutLE32(file + kReferencesOffset + i * 4, 0x3000u + i);
        PutLE32(file + kReferencesOffset + AI_DISK_NODE_COUNT * 4 + i * 4, 0x4000u + i);
        PutLE32(file + kReferencesOffset + AI_DISK_NODE_COUNT * 8 + i * 4, 0x5000u + i);
        PutLE32(file + kReferencesOffset + AI_DISK_NODE_COUNT * 12 + i * 4, 0x6000u + i);
    }
}

static int CheckSynthetic(const unsigned char *file)
{
    const unsigned char *route = file + kRoutesOffset;
    const unsigned char *network = file + kNetworksOffset;
    unsigned int i;

    for (i = 0; i < AI_DISK_CAR_COUNT; i++)
        CHECK(GetHost32(file + kCarOffset + i * 4) == i);
    CHECK(GetHost32(route + 0x00) == 0x11223344);
    CHECK(GetHost32(route + 0x84) == 0x55667788);
    for (i = 0; i < AI_DISK_DESCRIPTOR_STRIDE; i++)
        CHECK(file[kDescriptorsOffset + i] == (unsigned char)(0x40 + i));
    for (i = 0; i < AI_DISK_POINT_STRIDE; i++)
        CHECK(file[kPointsOffset + i] == (unsigned char)(0x60 + i));
    for (i = 0; i < 8; i++)
        CHECK(network[i] == (unsigned char)(0x70 + i));
    CHECK(GetHost32(network + 8) == 2);
    CHECK(network[12] == 2 && network[13] == 3 && network[14] == 1 && network[15] == 0);
    for (i = 0; i < kWeightWords; i++)
        CHECK(GetHost32(file + kWeightsOffset + i * 4) == 0x3f800000u + i);
    for (i = 0; i < AI_DISK_ACTIVATION_COUNT; i++)
        CHECK(GetHost32(file + kActivationOffset + i * 4) == 0x1000u + i);
    for (i = 0; i < AI_DISK_NODE_COUNT; i++) {
        const unsigned char *node = file + kNodesOffset + i * AI_DISK_NODE_STRIDE;
        CHECK(node[0] == (unsigned char)i);
        CHECK(node[1] == 0x11 && node[2] == 0x22 && node[3] == 0x33);
        CHECK(GetHost32(node + 4) == 0x2000u + i);
        CHECK(node[8] == (unsigned char)(0x90 + i) && node[9] == 0x91);
    }
    for (i = 0; i < AI_DISK_NODE_COUNT; i++) {
        CHECK(GetHost32(file + kReferencesOffset + i * 4) == 0x3000u + i);
        CHECK(GetHost32(file + kReferencesOffset + AI_DISK_NODE_COUNT * 4 + i * 4) == 0x4000u + i);
        CHECK(GetHost32(file + kReferencesOffset + AI_DISK_NODE_COUNT * 8 + i * 4) == 0x5000u + i);
        CHECK(GetHost32(file + kReferencesOffset + AI_DISK_NODE_COUNT * 12 + i * 4) == 0x6000u + i);
    }
    return 0;
}

static int LoadFile(const char *path, unsigned char *buffer, unsigned int capacity, unsigned int *outSize)
{
    FILE *f = fopen(path, "rb");
    size_t got;

    if (f == NULL)
        return 0;
    got = fread(buffer, 1, capacity, f);
    fclose(f);
    *outSize = (unsigned int)got;
    return got > 0;
}

int main(int argc, char **argv)
{
    unsigned char file[kFileSize];
    unsigned char output[0x10000];
    unsigned char inplace[kFileSize];

    BuildSynthetic(file);
    memset(output, 0, sizeof(output));
    CHECK(AiDisk_Decode(output, sizeof(output), file, sizeof(file)) != 0);
    if (CheckSynthetic(output))
        return 1;

    // The four count words are read as bytes by the parser: the decoder must
    // leave them byte-identical.
    CHECK(memcmp(output + kRouteCountOffset, file + kRouteCountOffset, 4) == 0);
    CHECK(memcmp(output + kDescriptorCountOffset, file + kDescriptorCountOffset, 4) == 0);
    CHECK(memcmp(output + kPointCountOffset, file + kPointCountOffset, 4) == 0);
    CHECK(memcmp(output + kNetworkCountOffset, file + kNetworkCountOffset, 4) == 0);
    CHECK(ReadLE32(output + kRouteCountOffset) == 1);
    CHECK(ReadLE32(output + kDescriptorCountOffset) == 1);
    CHECK(ReadLE32(output + kPointCountOffset) == 1);
    CHECK(ReadLE32(output + kNetworkCountOffset) == 1);

    // The stage buffer path decodes in place.
    memcpy(inplace, file, sizeof(file));
    CHECK(AiDisk_Decode(inplace, sizeof(inplace), inplace, sizeof(inplace)) != 0);
    if (CheckSynthetic(inplace))
        return 1;

    // Counts above the native table capacities are rejected.
    BuildSynthetic(file);
    PutCount(file + kRouteCountOffset, 6);
    CHECK(AiDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);
    BuildSynthetic(file);
    PutCount(file + kDescriptorCountOffset, 40);
    CHECK(AiDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);
    BuildSynthetic(file);
    PutCount(file + kPointCountOffset, 9);
    CHECK(AiDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);
    BuildSynthetic(file);
    PutCount(file + kNetworkCountOffset, 10);
    CHECK(AiDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);

    // A network with more layers than pWeights/layerSizes can hold.
    BuildSynthetic(file);
    PutLE32(file + kNetworksOffset + 8, 5);
    CHECK(AiDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);

    // A member shorter than the reference-route section, and NULL inputs.
    BuildSynthetic(file);
    CHECK(AiDisk_Decode(output, sizeof(output), file, sizeof(file) - 1 - AI_DISK_REFERENCE_ROUTE_SIZE) == 0);
    CHECK(AiDisk_Decode(output, sizeof(file) - 1, file, sizeof(file)) == 0);
    CHECK(AiDisk_Decode(NULL, sizeof(output), file, sizeof(file)) == 0);
    CHECK(AiDisk_Decode(output, sizeof(output), NULL, sizeof(file)) == 0);

    // A failed decode must not modify the destination.
    memset(output, 0x5a, sizeof(output));
    BuildSynthetic(file);
    PutCount(file + kRouteCountOffset, 6);
    CHECK(AiDisk_Decode(output, sizeof(output), file, sizeof(file)) == 0);
    CHECK(output[0] == 0x5a);

    if (argc > 1) {
        unsigned char captured[0x10000];
        unsigned int capturedSize = 0;
        unsigned int routesOff = kRoutesOffset;
        unsigned int descriptorCountOff;
        unsigned int descriptorsOff;
        unsigned int pointCountOff;
        unsigned int pointsOff;
        unsigned int networkCountOff;
        unsigned int networksOff;
        unsigned int weightsOff;
        unsigned int activationOff;
        unsigned int nodesOff;
        unsigned int referencesOff;
        unsigned int c1, c2, c0, c3, i;

        CHECK(LoadFile(argv[1], captured, sizeof(captured), &capturedSize));
        memset(output, 0, sizeof(output));
        CHECK(AiDisk_Decode(output, capturedSize, captured, capturedSize) != 0);
        c1 = output[kRouteCountOffset];
        CHECK(c1 == 4);
        CHECK(ReadLE32(output + kRouteCountOffset) == 4);
        descriptorCountOff = routesOff + c1 * AI_DISK_ROUTE_STRIDE;
        c2 = output[descriptorCountOff];
        CHECK(c2 == 1);
        descriptorsOff = descriptorCountOff + 4;
        pointCountOff = descriptorsOff + c2 * AI_DISK_DESCRIPTOR_STRIDE;
        c0 = output[pointCountOff];
        CHECK(c0 == 4);
        pointsOff = pointCountOff + 4;
        networkCountOff = pointsOff + c0 * AI_DISK_POINT_STRIDE;
        c3 = output[networkCountOff];
        CHECK(c3 == 2);
        networksOff = networkCountOff + 4;
        weightsOff = networksOff + c3 * AI_DISK_NETWORK_STRIDE;
        for (i = 0; i < c3; i++) {
            unsigned int layers = GetHost32(output + networksOff + i * AI_DISK_NETWORK_STRIDE + 8);
            unsigned int k;
            for (k = 1; k < layers; k++) {
                unsigned int previous = output[networksOff + i * AI_DISK_NETWORK_STRIDE + 12 + k - 1];
                unsigned int current = output[networksOff + i * AI_DISK_NETWORK_STRIDE + 12 + k];
                weightsOff += (previous + 1) * current * 4;
            }
        }
        activationOff = weightsOff;
        nodesOff = activationOff + AI_DISK_ACTIVATION_COUNT * 4;
        referencesOff = nodesOff + AI_DISK_NODE_COUNT * AI_DISK_NODE_STRIDE;
        CHECK(GetHost32(output + 0x00) == 0);
        CHECK(GetHost32(output + 0x04) == 4);
        CHECK(GetHost32(output + routesOff + 0x00) == 0x105450);
        CHECK(GetHost32(output + routesOff + 0x84) == 1);
        CHECK(GetHost32(output + networksOff + 8) == 3);
        CHECK(GetHost32(output + activationOff + 4) == 4090);
        CHECK(GetHost32(output + nodesOff + 4) == 0);
        CHECK(GetHost32(output + referencesOff + AI_DISK_NODE_COUNT * 4) == 5488475);
        CHECK(GetHost32(output + referencesOff + AI_DISK_NODE_COUNT * 8) == 6379714);
        CHECK(GetHost32(output + referencesOff + AI_DISK_NODE_COUNT * 12) == 125846);
    }

    printf("ai_disk: OK\n");
    return 0;
}

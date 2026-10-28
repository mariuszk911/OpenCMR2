// .ai? CPU-driver table decoder, see port/ai_disk.h.

#include "port/ai_disk.h"

#include "port/endian.h"

#include <stdint.h>
#include <string.h>

namespace {

const size_t kCarsOffset = 0;
const size_t kFirstCountOffset = AI_DISK_CAR_COUNT * 4;
const int kMaxRoutes = 5;          // AIRouteTables.routes[6], entries 1..n
const int kMaxDescriptors = 39;    // AIRouteTables.descriptors[40]
const int kMaxPoints = 8;          // AIRouteTables.points[10] and g_aiReferenceRoutes[9]
const int kMaxNetworks = 9;        // AIRouteTables.networks[10]
const int kMaxLayers = 4;          // AINetwork.pWeights[4] and layerSizes[4]

struct Sections {
    size_t routeCount;
    size_t routes;
    unsigned int routeEntries;
    size_t descriptorCount;
    size_t descriptors;
    unsigned int descriptorEntries;
    size_t pointCount;
    size_t points;
    unsigned int pointEntries;
    size_t networkCount;
    size_t networks;
    unsigned int networkEntries;
    size_t weights;
    size_t activation;
    size_t nodes;
    size_t references;
};

void StoreHost32(void *destination, uint32_t value)
{
    memcpy(destination, &value, sizeof(value));
}

void SwapWord(unsigned char *destination, const unsigned char *source)
{
    StoreHost32(destination, ReadLE32(source));
}

void SwapWords(unsigned char *destination, const unsigned char *source, size_t bytes)
{
    size_t i;

    for (i = 0; i + 4 <= bytes; i += 4)
        SwapWord(destination + i, source + i);
}

int ReadCount(const unsigned char *src, size_t offset, int maximum, unsigned int *out)
{
    int32_t value = (int32_t)ReadLE32(src + offset);

    if (value < 0 || value > maximum)
        return 0;
    *out = (unsigned int)value;
    return 1;
}

bool ParseSections(const unsigned char *src, size_t size, Sections *sections)
{
    size_t offset;

    if (size < kFirstCountOffset)
        return false;
    offset = kFirstCountOffset;

    sections->routeCount = offset;
    if (!ReadCount(src, offset, kMaxRoutes, &sections->routeEntries))
        return false;
    offset += 4;
    sections->routes = offset;
    if ((size - offset) / AI_DISK_ROUTE_STRIDE < sections->routeEntries)
        return false;
    offset += (size_t)sections->routeEntries * AI_DISK_ROUTE_STRIDE;

    sections->descriptorCount = offset;
    if (!ReadCount(src, offset, kMaxDescriptors, &sections->descriptorEntries))
        return false;
    offset += 4;
    sections->descriptors = offset;
    if ((size - offset) / AI_DISK_DESCRIPTOR_STRIDE < sections->descriptorEntries)
        return false;
    offset += (size_t)sections->descriptorEntries * AI_DISK_DESCRIPTOR_STRIDE;

    sections->pointCount = offset;
    if (!ReadCount(src, offset, kMaxPoints, &sections->pointEntries))
        return false;
    offset += 4;
    sections->points = offset;
    if ((size - offset) / AI_DISK_POINT_STRIDE < sections->pointEntries)
        return false;
    offset += (size_t)sections->pointEntries * AI_DISK_POINT_STRIDE;

    sections->networkCount = offset;
    if (!ReadCount(src, offset, kMaxNetworks, &sections->networkEntries))
        return false;
    offset += 4;
    sections->networks = offset;
    if ((size - offset) / AI_DISK_NETWORK_STRIDE < sections->networkEntries)
        return false;
    offset += (size_t)sections->networkEntries * AI_DISK_NETWORK_STRIDE;

    sections->weights = offset;
    {
        unsigned int network;
        for (network = 0; network < sections->networkEntries; network++) {
            const unsigned char *record = src + sections->networks + (size_t)network * AI_DISK_NETWORK_STRIDE;
            int32_t layers = (int32_t)ReadLE32(record + 8);
            int layer;
            size_t bytes;

            if (layers < 0 || layers > kMaxLayers)
                return false;
            for (layer = 1; layer < layers; layer++) {
                unsigned int previous = record[12 + layer - 1];
                unsigned int current = record[12 + layer];
                unsigned int count = (previous + 1) * current;

                if (count > (size - offset) / 4)
                    return false;
                bytes = (size_t)count * 4;
                if (bytes > size - offset)
                    return false;
                offset += bytes;
            }
        }
    }

    sections->activation = offset;
    if (size - offset < (size_t)AI_DISK_ACTIVATION_COUNT * 4)
        return false;
    offset += (size_t)AI_DISK_ACTIVATION_COUNT * 4;

    sections->nodes = offset;
    if (size - offset < (size_t)AI_DISK_NODE_COUNT * AI_DISK_NODE_STRIDE)
        return false;
    offset += (size_t)AI_DISK_NODE_COUNT * AI_DISK_NODE_STRIDE;

    sections->references = offset;
    if (sections->pointEntries > 0 &&
            (size - offset) / AI_DISK_REFERENCE_ROUTE_SIZE < sections->pointEntries)
        return false;
    return true;
}

} // namespace

extern "C" int AiDisk_Decode(void *destination, size_t destinationSize,
                             const void *file, size_t size)
{
    const unsigned char *src = (const unsigned char *)file;
    unsigned char *dst = (unsigned char *)destination;
    Sections sections;
    unsigned int i;

    if (destination == NULL || file == NULL || destinationSize < size)
        return 0;
    for (i = 0; i < AI_DISK_CAR_COUNT; i++) {
        int32_t table = (int32_t)ReadLE32(src + kCarsOffset + (size_t)i * 4);
        if (table < 0 || table > kMaxRoutes)
            return 0;
    }
    if (!ParseSections(src, size, &sections))
        return 0;

    memmove(dst, src, size);

    // Six per-car route table numbers, read as native ints.
    for (i = 0; i < AI_DISK_CAR_COUNT; i++) {
        size_t offset = kCarsOffset + (size_t)i * 4;
        SwapWord(dst + offset, src + offset);
    }

    // Route records: the mask at 0 and the entry count at 0x84 are native
    // ints; the entry table itself stays bytes.
    for (i = 0; i < sections.routeEntries; i++) {
        size_t offset = sections.routes + (size_t)i * AI_DISK_ROUTE_STRIDE;
        SwapWord(dst + offset + 0x00, src + offset + 0x00);
        SwapWord(dst + offset + 0x84, src + offset + 0x84);
    }

    // AINetwork.layerCount; the input ids and layer sizes stay bytes.
    for (i = 0; i < sections.networkEntries; i++) {
        size_t offset = sections.networks + (size_t)i * AI_DISK_NETWORK_STRIDE + 8;
        SwapWord(dst + offset, src + offset);
    }

    SwapWords(dst + sections.weights, src + sections.weights,
              sections.activation - sections.weights);
    SwapWords(dst + sections.activation, src + sections.activation,
              (size_t)AI_DISK_ACTIVATION_COUNT * 4);

    // AIRouteNodeInfo.key at +4; field_0x0, the padding and the wall bytes
    // stay bytes.
    for (i = 0; i < AI_DISK_NODE_COUNT; i++) {
        size_t offset = sections.nodes + (size_t)i * AI_DISK_NODE_STRIDE + 4;
        SwapWord(dst + offset, src + offset);
    }

    SwapWords(dst + sections.references, src + sections.references,
              (size_t)sections.pointEntries * AI_DISK_REFERENCE_ROUTE_SIZE);
    return 1;
}

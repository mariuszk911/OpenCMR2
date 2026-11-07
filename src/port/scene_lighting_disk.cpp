// Stage lighting (.tre) decoder, see port/scene_lighting_disk.h.

#include "port/scene_lighting_disk.h"

#include "port/c3d_payload.h"
#include "port/endian.h"

#include "FileBuffer.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

const unsigned int kHeaderSize = 50;
const unsigned int kZoneSize = 0x14;
const unsigned int kItemSize = 0x30;
const unsigned int kVertexSize = 0x30;
const unsigned int kTriangleSize = 0x4c;

void FreeNative(SceneLightingDisk *out)
{
    CFileBuffer::FreeGenericFileBuffer(out->native);
    out->native = NULL;
}

} // namespace

int SceneLightingDisk_Decode(const unsigned char *tre, unsigned int size,
                             SceneLightingDisk *out)
{
    const unsigned char *pZoneDisk;
    const unsigned char *pItemDisk;
    const unsigned char *pMeshDisk;
    const unsigned char *pVertFile;
    const unsigned char *pTriFile;
    const unsigned char *pLevelFile;
    uint64_t itemCount = 0;
    uint64_t payloadBytes = 0;
    unsigned char *pOut;
    LightZone *pZones;
    LightZoneMesh *pMeshes;
    LightZoneItem *pNextItem;
    unsigned int zoneCount;
    int zone;
    int item;
    int i;

    out->zoneCount = 0;
    out->native = NULL;
    out->payload = NULL;
    if (tre == NULL || size < kHeaderSize)
        return SCENE_LIGHTING_REJECTED;
    zoneCount = ReadLE16(tre + 48);
    pZoneDisk = tre + kHeaderSize;
    if ((uint64_t)zoneCount * kZoneSize > (uint64_t)(size - kHeaderSize))
        return SCENE_LIGHTING_REJECTED;
    out->lightDir[0] = (int)ReadLE32(tre + 0);
    out->lightDir[1] = (int)ReadLE32(tre + 4);
    out->lightDir[2] = (int)ReadLE32(tre + 8);
    for (i = 0; i < 3; i++) {
        out->basis[i][0] = (int)ReadLE32(tre + 12 + i * 12);
        out->basis[i][1] = (int)ReadLE32(tre + 16 + i * 12);
        out->basis[i][2] = (int)ReadLE32(tre + 20 + i * 12);
    }

    for (zone = 0; zone < (int)zoneCount; zone++)
        itemCount += ReadLE16(pZoneDisk + zone * kZoneSize + 2);
    if (itemCount > (uint64_t)(size_t)-1 / sizeof(LightZoneItem))
        return SCENE_LIGHTING_HEADER_ONLY;
    if ((uint64_t)kHeaderSize + (uint64_t)zoneCount * kZoneSize +
            itemCount * kItemSize > (uint64_t)size)
        return SCENE_LIGHTING_HEADER_ONLY;
    out->native = (unsigned char *)CFileBuffer::AllocateLockedBuffer(
        zoneCount * (sizeof(LightZone) + sizeof(LightZoneMesh)) + (size_t)itemCount * sizeof(LightZoneItem) + 1);
    if (out->native == NULL)
        return SCENE_LIGHTING_HEADER_ONLY;
    pZones = (LightZone *)out->native;
    pMeshes = (LightZoneMesh *)(pZones + zoneCount);
    pNextItem = (LightZoneItem *)(pMeshes + zoneCount);
    pItemDisk = pZoneDisk + zoneCount * kZoneSize;
    for (zone = 0; zone < (int)zoneCount; zone++) {
        const unsigned char *zoneFile = pZoneDisk + zone * kZoneSize;
        pZones[zone].sector = (short)ReadLE16(zoneFile + 0);
        pZones[zone].itemCount = ReadLE16(zoneFile + 2);
        pZones[zone].x = (int)ReadLE32(zoneFile + 4);
        pZones[zone].z = (int)ReadLE32(zoneFile + 8);
        pZones[zone].pItems = pNextItem;
        pZones[zone].pMesh = &pMeshes[zone];
        for (item = 0; item < pZones[zone].itemCount; item++, pItemDisk += kItemSize, pNextItem++) {
            const unsigned char *itemFile = pItemDisk;
            memset(pNextItem, 0, sizeof(*pNextItem));
            pNextItem->ownerX = (int)ReadLE32(itemFile + 0x00);
            pNextItem->ownerZ = (int)ReadLE32(itemFile + 0x04);
            pNextItem->x = (int)ReadLE32(itemFile + 0x08);
            pNextItem->z = (int)ReadLE32(itemFile + 0x0c);
            pNextItem->halfSizeRight = (int)ReadLE32(itemFile + 0x10);
            pNextItem->halfSizeForward = (int)ReadLE32(itemFile + 0x14);
            pNextItem->field_0x18 = (int)ReadLE32(itemFile + 0x18);
            pNextItem->field_0x1c = (int)ReadLE32(itemFile + 0x1c);
            pNextItem->intensity = (int)ReadLE32(itemFile + 0x20);
            pNextItem->field_0x24 = (int)ReadLE32(itemFile + 0x24);
            pNextItem->vertexCount = (int)ReadLE32(itemFile + 0x28);
        }
    }
    pMeshDisk = pItemDisk;
    if ((uint64_t)(pMeshDisk - tre) + (uint64_t)zoneCount * kZoneSize > (uint64_t)size)
        return SCENE_LIGHTING_HEADER_ONLY;
    for (zone = 0; zone < (int)zoneCount; zone++) {
        const unsigned char *meshFile = pMeshDisk + zone * kZoneSize;
        pMeshes[zone].triangleCount = (int)ReadLE32(meshFile + 0);
        pMeshes[zone].vertexCount = (int)ReadLE32(meshFile + 4);
        if (pMeshes[zone].triangleCount < 0 || pMeshes[zone].vertexCount < 0) {
            FreeNative(out);
            return SCENE_LIGHTING_HEADER_ONLY;
        }
        payloadBytes += (uint64_t)pMeshes[zone].vertexCount * kVertexSize;
        payloadBytes += (uint64_t)pMeshes[zone].triangleCount * kTriangleSize;
        payloadBytes += (uint64_t)pMeshes[zone].vertexCount * 4;
    }
    if ((uint64_t)(pMeshDisk - tre) + (uint64_t)zoneCount * kZoneSize +
            payloadBytes > (uint64_t)size) {
        FreeNative(out);
        return SCENE_LIGHTING_HEADER_ONLY;
    }
    if (payloadBytes > (uint64_t)(size_t)-1) {
        FreeNative(out);
        return SCENE_LIGHTING_HEADER_ONLY;
    }
    out->payload = (unsigned char *)CFileBuffer::AllocateLockedBuffer(
        payloadBytes != 0 ? (size_t)payloadBytes : 1);
    if (out->payload == NULL) {
        FreeNative(out);
        return SCENE_LIGHTING_HEADER_ONLY;
    }
    pOut = out->payload;
    pVertFile = pMeshDisk + zoneCount * kZoneSize;
    for (zone = 0; zone < (int)zoneCount; zone++) {
        const size_t bytes = (size_t)pMeshes[zone].vertexCount * kVertexSize;
        pMeshes[zone].pVertices = (MeshVertexF *)pOut;
        C3D_DecodeWords(pOut, pVertFile, bytes);
        pOut += bytes;
        pVertFile += bytes;
    }
    pTriFile = pVertFile;
    for (zone = 0; zone < (int)zoneCount; zone++) {
        pMeshes[zone].pTriangles = (MeshTriangle *)pOut;
        for (item = 0; item < pMeshes[zone].triangleCount; item++)
            C3D_DecodeTriangle((unsigned char *)pMeshes[zone].pTriangles + item * kTriangleSize,
                               pTriFile + item * kTriangleSize);
        pOut += (size_t)pMeshes[zone].triangleCount * kTriangleSize;
        pTriFile += (size_t)pMeshes[zone].triangleCount * kTriangleSize;
    }
    pLevelFile = pTriFile;
    for (zone = 0; zone < (int)zoneCount; zone++) {
        const size_t bytes = (size_t)pMeshes[zone].vertexCount * 4;
        pMeshes[zone].pLevels = (int *)pOut;
        C3D_DecodeWords(pOut, pLevelFile, bytes);
        pOut += bytes;
        pLevelFile += bytes;
    }
    out->zoneCount = zoneCount;
    return SCENE_LIGHTING_OK;
}

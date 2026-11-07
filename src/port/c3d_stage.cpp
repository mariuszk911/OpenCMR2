// Stage C3D layout, bounds, payload decode and mesh range check, see
// port/c3d_stage.h.

#include "port/c3d_stage.h"

#include "port/c3d_payload.h"
#include "port/disk_layouts.h"

#include "Mesh.h"

#include <stddef.h>
#include <stdint.h>

int C3DStage_Measure(unsigned char *pData, DWORD size, const C3DStageLimits *limits,
                     C3DHeaderValues *header, C3DStageLayout *layout)
{
    if (!C3DHeader_Decode(pData, header))
        return C3D_STAGE_BAD_HEADER;
    if (size != 0 && size < C3D_HEADER_SIZE)
        return C3D_STAGE_SHORT_MEMBER;
    unsigned int nodeCount = header->nodeCount;
    unsigned int meshCount = header->meshCount;
    unsigned int objectCount = header->objectCount;
    unsigned int sectorCount = header->sectorCount;
    unsigned int recordCount = header->recordCount;
    unsigned int triangleCount = header->triangleCount;
    if (triangleCount > (size_t)-1 / sizeof(MeshTriangle) ||
        header->vertexCount > (size_t)-1 / 0x30 ||
        header->vertexFlagCount > (size_t)-1 / 0x14)
        return C3D_STAGE_ARRAY_OVERFLOW;
    if (nodeCount > limits->nodes || meshCount > limits->meshes ||
            objectCount > limits->objects || recordCount > limits->records ||
            sectorCount > limits->sectors)
        return C3D_STAGE_CAPACITY;
    layout->nodes = pData + C3D_HEADER_SIZE;
    layout->meshes = layout->nodes + nodeCount * g_diskLayout_SceneNode.diskSize;
    layout->objects = layout->meshes + (meshCount + objectCount) * g_diskLayout_Mesh.diskSize;
    layout->sectors = layout->objects + objectCount * g_diskLayout_StageObject.diskSize;
    layout->triangles = layout->sectors + sectorCount * g_diskLayout_Sector.diskSize + header->partCount * 0x1c;
    layout->vertexData = layout->triangles + triangleCount * sizeof(MeshTriangle);
    layout->lightLevels = layout->vertexData + header->vertexCount * 0x30;
    layout->vertexFlags = layout->lightLevels + header->vertexCount * 4;
    layout->records = layout->vertexFlags + header->vertexFlagCount * 0x14;
    layout->end = layout->records + recordCount * g_diskLayout_MeshCollisionRecord.diskSize;
    layout->textureRecords = (char *)pData + header->textureNamesOffset;
    if (size != 0) {
        if ((uint64_t)(layout->end - pData) > (uint64_t)size ||
                header->textureNamesOffset >= size ||
                (uint64_t)header->textureNamesOffset +
                    (uint64_t)header->textureCount * 0x104 > (uint64_t)size ||
                layout->end > (unsigned char *)layout->textureRecords)
            return C3D_STAGE_EXCEEDS_MEMBER;
    }
    layout->triangleBytes = ((uint64_t)triangleCount * sizeof(MeshTriangle) + 7) & ~(uint64_t)7;
    layout->vertexBytes = ((uint64_t)header->vertexCount * 0x30 + 7) & ~(uint64_t)7;
    layout->lightBytes = ((uint64_t)header->vertexCount * 4 + 7) & ~(uint64_t)7;
    layout->flagBytes = ((uint64_t)header->vertexFlagCount * 0x14 + 7) & ~(uint64_t)7;
    layout->payloadBytes = layout->triangleBytes + layout->vertexBytes + layout->lightBytes +
                           layout->flagBytes;
    if (layout->payloadBytes > (uint64_t)(size_t)-1)
        return C3D_STAGE_PAYLOAD_OVERFLOW;
    return C3D_STAGE_OK;
}

const char *C3DStage_StatusText(int status)
{
    switch (status) {
    case C3D_STAGE_BAD_HEADER:
        return "invalid C3D header";
    case C3D_STAGE_SHORT_MEMBER:
        return "C3D member shorter than the header";
    case C3D_STAGE_ARRAY_OVERFLOW:
        return "C3D array size overflows";
    case C3D_STAGE_CAPACITY:
        return "C3D table count exceeds capacity";
    case C3D_STAGE_EXCEEDS_MEMBER:
        return "C3D counts exceed member size";
    case C3D_STAGE_PAYLOAD_OVERFLOW:
        return "C3D payload size overflows";
    }
    return "ok";
}

void C3DStage_DecodePayload(unsigned char *pPayload, const C3DStageLayout *layout,
                            const C3DHeaderValues *header, int textureBase,
                            C3DStageArrays *arrays)
{
    unsigned int i;
    unsigned int k;

    arrays->triangles = (MeshTriangle *)pPayload;
    arrays->vertexData = pPayload + (size_t)layout->triangleBytes;
    arrays->lightLevels = (int *)(pPayload + (size_t)(layout->triangleBytes + layout->vertexBytes));
    arrays->vertexFlags = pPayload + (size_t)(layout->triangleBytes + layout->vertexBytes +
                                              layout->lightBytes);
    for (i = 0; i < header->triangleCount; i++) {
        const unsigned char *pFile = layout->triangles + i * sizeof(MeshTriangle);
        C3D_DecodeTriangle(&arrays->triangles[i], pFile);
        for (k = 0; k < 10; k++) {
            if (arrays->triangles[i].textures[k] != -1)
                arrays->triangles[i].textures[k] += textureBase;
        }
    }
    C3D_DecodeWords(arrays->vertexData, layout->vertexData, (size_t)header->vertexCount * 0x30);
    C3D_DecodeWords(arrays->lightLevels, layout->lightLevels, (size_t)header->vertexCount * 4);
    C3D_DecodeWords(arrays->vertexFlags, layout->vertexFlags, (size_t)header->vertexFlagCount * 0x14);
}

int C3DStage_MeshArraysValid(const Mesh *pMesh, const C3DStageArrays *arrays,
                             const C3DHeaderValues *header)
{
    uintptr_t triBytes = (uintptr_t)pMesh->pTriangles - (uintptr_t)arrays->triangles;
    uintptr_t vertexBytes = (uintptr_t)pMesh->pVertexData - (uintptr_t)arrays->vertexData;
    uintptr_t lightBytes = (uintptr_t)pMesh->pLightLevels - (uintptr_t)arrays->lightLevels;

    return !(pMesh->pTriangles == NULL || pMesh->pVertexData == NULL ||
             pMesh->pLightLevels == NULL ||
             (triBytes % 4) != 0 || (vertexBytes % 4) != 0 || (lightBytes % 4) != 0 ||
             triBytes / 0x4c + (uintptr_t)pMesh->triangleCount > (uintptr_t)header->triangleCount ||
             vertexBytes / 0x30 + (uintptr_t)pMesh->field_0x10 > (uintptr_t)header->vertexCount ||
             lightBytes / 4 + (uintptr_t)pMesh->field_0x10 > (uintptr_t)header->vertexCount);
}

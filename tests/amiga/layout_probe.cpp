/*
 * M0.5 layout probe.
 *
 * Records sizeof/offsetof/alignof for the types that must keep the original
 * 32-bit layout and for the types that cross the game/executor boundary. The
 * host i386 build prints the reference; the 68k build is compiled with
 * -malign-int (Domain A) or without it (Domain B shared-only) and its values
 * are extracted from the generated object/assembly.
 *
 * The source contains no 64-bit division, so it is not affected by the GCC
 * 6.5 -malign-int ICE. It is not a replacement for running the probe on the
 * target; it records compiler ABI facts.
 */
#include <stddef.h>
#include <stdio.h>

#include "port/types.h"
#include "port/gfx.h"
#include "port/disk.h"
#include "port/sys.h"
#include "port/input.h"

#ifndef OPENCMR2_LAYOUT_SHARED_ONLY
#include "SceneNode.h"
#include "Mesh.h"
#include "Sector.h"
#include "MeshCollisionRecord.h"
#include "Car.h"
#include "CarParts.h"
#include "ReplayStream.h"
#include "SavedGame.h"
#include "Font.h"
#endif

#ifndef OPENCMR2_LAYOUT_SHARED_ONLY
#define OPENCMR2_GAME_LAYOUT(X) \
    X(SceneNode_size, sizeof(SceneNode)) \
    X(SceneNode_align, alignof(SceneNode)) \
    X(SceneNode_pNext, offsetof(SceneNode, pNext)) \
    X(SceneNode_pObject, offsetof(SceneNode, pObject)) \
    X(SceneNode_translation, offsetof(SceneNode, translation)) \
    X(SceneNode_angles, offsetof(SceneNode, angles)) \
    X(SceneNode_flags, offsetof(SceneNode, flags)) \
    X(SceneNode_local, offsetof(SceneNode, local)) \
    X(SceneNode_current, offsetof(SceneNode, current)) \
    X(SceneNode_world, offsetof(SceneNode, world)) \
    X(SceneNode_worldF, offsetof(SceneNode, worldF)) \
    X(SceneNode_pNextInSector, offsetof(SceneNode, pNextInSector)) \
    X(SceneNode_type, offsetof(SceneNode, type)) \
    X(SceneNode_colour, offsetof(SceneNode, colour)) \
    X(Mesh_size, sizeof(Mesh)) \
    X(Mesh_align, alignof(Mesh)) \
    X(Mesh_pVertexData, offsetof(Mesh, pVertexData)) \
    X(Mesh_field_0x10, offsetof(Mesh, field_0x10)) \
    X(Mesh_pTriangles, offsetof(Mesh, pTriangles)) \
    X(Mesh_pParts, offsetof(Mesh, pParts)) \
    X(Mesh_partCount, offsetof(Mesh, partCount)) \
    X(Mesh_pCollisionRecords, offsetof(Mesh, pCollisionRecords)) \
    X(Mesh_sizeUnits, offsetof(Mesh, sizeUnits)) \
    X(Mesh_lodIndex, offsetof(Mesh, lodIndex)) \
    X(StageObject_size, sizeof(StageObject)) \
    X(StageObject_align, alignof(StageObject)) \
    X(StageObject_pMesh, offsetof(StageObject, pMesh)) \
    X(StageObject_pNext, offsetof(StageObject, pNext)) \
    X(StageObject_lightLevel, offsetof(StageObject, lightLevel)) \
    X(Sector_size, sizeof(Sector)) \
    X(Sector_align, alignof(Sector)) \
    X(Sector_x, offsetof(Sector, x)) \
    X(Sector_pMesh, offsetof(Sector, pMesh)) \
    X(Sector_pObjects, offsetof(Sector, pObjects)) \
    X(Sector_pFirstNode, offsetof(Sector, pFirstNode)) \
    X(Sector_pFinRecords, offsetof(Sector, pFinRecords)) \
    X(Sector_corners, offsetof(Sector, corners)) \
    X(Sector_bounds, offsetof(Sector, bounds)) \
    X(Sector_viewDistanceSquared, offsetof(Sector, viewDistanceSquared)) \
    X(MeshCollisionRecord_size, sizeof(MeshCollisionRecord)) \
    X(MeshCollisionRecord_align, alignof(MeshCollisionRecord)) \
    X(MeshCollisionRecord_matrix, offsetof(MeshCollisionRecord, matrix)) \
    X(MeshCollisionRecord_width, offsetof(MeshCollisionRecord, width)) \
    X(MeshCollisionRecord_length, offsetof(MeshCollisionRecord, length)) \
    X(MeshCollisionRecord_height, offsetof(MeshCollisionRecord, height)) \
    X(MeshCollisionRecord_pNext, offsetof(MeshCollisionRecord, pNext)) \
    X(Car_size, sizeof(Car)) \
    X(Car_align, alignof(Car)) \
    X(Car_handbrake, offsetof(Car, handbrake)) \
    X(Car_bodySize, offsetof(Car, bodySize)) \
    X(Car_position, offsetof(Car, position)) \
    X(Car_pSceneRoot, offsetof(Car, pSceneRoot)) \
    X(Car_pWorld, offsetof(Car, pWorld)) \
    X(Car_collisionRadius, offsetof(Car, collisionRadius)) \
    X(Car_mass, offsetof(Car, mass)) \
    X(CarPartSet_size, sizeof(CarPartSet)) \
    X(CarPartSet_align, alignof(CarPartSet)) \
    X(CarPartSet_geometry, offsetof(CarPartSet, geometry)) \
    X(CarPartSet_centres, offsetof(CarPartSet, centres)) \
    X(CarPartSet_halfExtents, offsetof(CarPartSet, halfExtents)) \
    X(CarPartSet_damageGrid, offsetof(CarPartSet, damageGrid)) \
    X(CarPartSet_damageValues, offsetof(CarPartSet, damageValues)) \
    X(CarPartSet_count, offsetof(CarPartSet, count)) \
    X(CarPartSet_breakPartIndex, offsetof(CarPartSet, breakPartIndex)) \
    X(CarPartSet_partDamaged, offsetof(CarPartSet, partDamaged)) \
    X(CarPartSet_partBroken, offsetof(CarPartSet, partBroken)) \
    X(ReplayStream_size, sizeof(ReplayStream)) \
    X(ReplayStream_align, alignof(ReplayStream)) \
    X(ReplayStream_steeringModeOption, offsetof(ReplayStream, steeringModeOption)) \
    X(ReplayStream_type, offsetof(ReplayStream, type)) \
    X(ReplayStream_car, offsetof(ReplayStream, car)) \
    X(ReplayStream_pInputs, offsetof(ReplayStream, pInputs)) \
    X(ReplayStream_pStates, offsetof(ReplayStream, pStates)) \
    X(ReplayStream_pose, offsetof(ReplayStream, pose)) \
    X(ReplayStream_headingFrom, offsetof(ReplayStream, headingFrom)) \
    X(ReplayStream_laneCapacity, offsetof(ReplayStream, laneCapacity)) \
    X(ReplayStream_pLaneSamples, offsetof(ReplayStream, pLaneSamples)) \
    X(ReplayStream_field_0x10c, offsetof(ReplayStream, field_0x10c)) \
    X(SaveBlock_size, sizeof(SaveBlock)) \
    X(SaveBlock_align, alignof(SaveBlock)) \
    X(SaveBlock_profiles, offsetof(SaveBlock, profiles)) \
    X(SaveBlock_slots, offsetof(SaveBlock, slots)) \
    X(FontGlyph_size, sizeof(FontGlyph)) \
    X(FontGlyph_align, alignof(FontGlyph)) \
    X(FontGlyph_width, offsetof(FontGlyph, width)) \
    X(FontGlyph_pKern, offsetof(FontGlyph, pKern))
#endif

#define OPENCMR2_SHARED_LAYOUT(X) \
    X(GfxVertex_size, sizeof(GfxVertex)) \
    X(GfxVertex_align, alignof(GfxVertex)) \
    X(GfxVertex_x, offsetof(GfxVertex, x)) \
    X(GfxVertex_diffuse, offsetof(GfxVertex, diffuse)) \
    X(GfxVertex_u1, offsetof(GfxVertex, u1)) \
    X(GfxTLVertex_size, sizeof(GfxTLVertex)) \
    X(GfxTLVertex_align, alignof(GfxTLVertex)) \
    X(GfxTLVertex_sx, offsetof(GfxTLVertex, sx)) \
    X(GfxTLVertex_color, offsetof(GfxTLVertex, color)) \
    X(GfxTLVertex_tu, offsetof(GfxTLVertex, tu)) \
    X(GfxLight_size, sizeof(GfxLight)) \
    X(GfxLight_align, alignof(GfxLight)) \
    X(GfxLight_type, offsetof(GfxLight, type)) \
    X(GfxLight_diffuse, offsetof(GfxLight, diffuse)) \
    X(GfxLight_position, offsetof(GfxLight, position)) \
    X(GfxLight_range, offsetof(GfxLight, range)) \
    X(GfxMatrix_size, sizeof(GfxMatrix)) \
    X(GfxMatrix_align, alignof(GfxMatrix)) \
    X(GfxMatrix__11, offsetof(GfxMatrix, _11)) \
    X(GfxMatrix__41, offsetof(GfxMatrix, _41)) \
    X(GfxViewport_size, sizeof(GfxViewport)) \
    X(GfxViewport_align, alignof(GfxViewport)) \
    X(GfxViewport_x, offsetof(GfxViewport, x)) \
    X(GfxViewport_minZ, offsetof(GfxViewport, minZ)) \
    X(DiskLayout_size, sizeof(DiskLayout)) \
    X(DiskLayout_align, alignof(DiskLayout)) \
    X(DiskLayout_diskSize, offsetof(DiskLayout, diskSize)) \
    X(DiskLayout_runs, offsetof(DiskLayout, runs)) \
    X(DiskLayout_runCount, offsetof(DiskLayout, runCount)) \
    X(DiskLayout_pointers, offsetof(DiskLayout, pointers)) \
    X(SysEvent_size, sizeof(SysEvent)) \
    X(SysEvent_align, alignof(SysEvent)) \
    X(SysEvent_type, offsetof(SysEvent, type)) \
    X(SysEvent_repeat, offsetof(SysEvent, repeat)) \
    X(SysStepClock_size, sizeof(SysStepClock)) \
    X(SysStepClock_align, alignof(SysStepClock)) \
    X(SysStepClock_last, offsetof(SysStepClock, last)) \
    X(SysStepClock_remainder, offsetof(SysStepClock, remainder)) \
    X(SysStepClock_started, offsetof(SysStepClock, started)) \
    X(InputJoystickState_size, sizeof(InputJoystickState)) \
    X(InputJoystickState_align, alignof(InputJoystickState)) \
    X(InputJoystickState_lX, offsetof(InputJoystickState, lX)) \
    X(InputJoystickState_rgdwPOV, offsetof(InputJoystickState, rgdwPOV)) \
    X(InputJoystickState_rgbButtons, offsetof(InputJoystickState, rgbButtons))

#define OPENCMR2_VALUE(name, expression) (unsigned long)(expression),
#define OPENCMR2_NAME(name, expression) #name,

extern "C" const unsigned long opencmr2_layout_values[] = {
#ifndef OPENCMR2_LAYOUT_SHARED_ONLY
    OPENCMR2_GAME_LAYOUT(OPENCMR2_VALUE)
#endif
    OPENCMR2_SHARED_LAYOUT(OPENCMR2_VALUE)
};

extern "C" const char *const opencmr2_layout_names[] = {
#ifndef OPENCMR2_LAYOUT_SHARED_ONLY
    OPENCMR2_GAME_LAYOUT(OPENCMR2_NAME)
#endif
    OPENCMR2_SHARED_LAYOUT(OPENCMR2_NAME)
};

#undef OPENCMR2_VALUE
#undef OPENCMR2_NAME

#ifdef OPENCMR2_LAYOUT_PRINT
int main(void)
{
    const size_t count = sizeof(opencmr2_layout_values) / sizeof(opencmr2_layout_values[0]);
    for (size_t i = 0; i < count; ++i)
        printf("%s\t%lu\n", opencmr2_layout_names[i], opencmr2_layout_values[i]);
    return 0;
}
#endif

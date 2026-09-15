#ifndef GUARD_VOXEL_ATLAS_H
#define GUARD_VOXEL_ATLAS_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>
#include "voxel_world.h"

// The metatile atlas: every metatile of a map's tileset pair composed (bottom
// layer under top layer) into one 16x16 RGBA tile, laid out 32 per row on a
// 512x512 sheet. Metatile m lives at ((m % 32) * 16, (m / 32) * 16).
#define VOXEL_ATLAS_SIZE 512
#define VOXEL_ATLAS_COLS 32
#define VOXEL_ATLAS_TILE 16

struct MapLayout;

// Decode from the tilesets in ROM. `liveMap` (optional) adds the metatiles a
// script-modified live map lays that the ROM layout does not.
// Decode from the tilesets in ROM. Fills `meta` (may be NULL) with the layer
// analysis the world model needs. `rgba` may be NULL to analyse only.
void VoxelAtlas_DecodeFromRom(const struct MapLayout *layout, const uint16_t *liveMap, int liveCount,
                              uint32_t *rgba, VoxelMetaInfo *meta);

// Decode the currently loaded map's tiles straight out of BG VRAM and the
// (possibly fading) palette RAM, so tile animations and screen fades show in
// 3D. Only valid for the map the game has loaded.
void VoxelAtlas_DecodeFromVram(const struct MapLayout *layout, const uint16_t *liveMap, int liveCount, uint32_t *rgba);

// A cheap checksum of BG tile VRAM + BG palettes, to know when to re-decode.
uint32_t VoxelAtlas_VramChecksum(void);

#endif
#endif
#endif // GUARD_VOXEL_ATLAS_H

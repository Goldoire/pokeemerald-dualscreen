#ifndef GUARD_VOXEL_ATLAS_H
#define GUARD_VOXEL_ATLAS_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>
#include "voxel_world.h"

// The metatile atlas: a 1024x1024 sheet holding four 512x512 quadrants, each
// laying out the tileset pair's 1024 metatiles 32 per row at 16x16:
//
//   (0,0)     composed:  bottom layer under top layer, the flat picture
//   (512,0)   ground:    the picture with every silhouette pixel replaced by
//                        the cell's own ground colour (what lies under a
//                        standee)
//   (0,512)   carved:    the silhouette alone, ground carved to alpha 0
//   (512,512) top:       the above-player layer alone, alpha elsewhere
//
// Metatile m sits at ((m % 32) * 16, (m / 32) * 16) inside its quadrant.
#define VOXEL_ATLAS_SIZE 1024
#define VOXEL_ATLAS_QUAD 512
#define VOXEL_ATLAS_COLS 32
#define VOXEL_ATLAS_TILE 16

#define VOXEL_VAR_COMPOSED 0
#define VOXEL_VAR_GROUND   1
#define VOXEL_VAR_CARVED   2
#define VOXEL_VAR_TOP      3

struct MapLayout;

// Decode from the tilesets in ROM into a 1024x1024 buffer: the composed and
// top-layer quadrants. `liveMap` (optional) adds the metatiles a script-
// modified live map lays that the ROM layout does not. Fills `meta` (the
// layer analysis), `topMask` (1024 * 32 bytes: bit per pixel set by the top
// layer) and `used` (which metatiles were decoded); each may be NULL, as may
// `rgba` to analyse only.
void VoxelAtlas_DecodeFromRom(const struct MapLayout *layout, const uint16_t *liveMap, int liveCount,
                              uint32_t *rgba, VoxelMetaInfo *meta, uint8_t *topMask, bool *used);

// Decode the currently loaded map's tiles straight out of BG VRAM and the
// (possibly fading) palette RAM into a 512x512 composed sheet, so tile
// animations and screen fades show in 3D. Only valid for the loaded map.
void VoxelAtlas_DecodeFromVram(const struct MapLayout *layout, const uint16_t *liveMap, int liveCount,
                               uint32_t *rgba512);

// A cheap checksum of BG tile VRAM + BG palettes, to know when to re-decode.
uint32_t VoxelAtlas_VramChecksum(void);

#endif
#endif
#endif // GUARD_VOXEL_ATLAS_H

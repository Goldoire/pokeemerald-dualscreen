#ifndef GUARD_VOXEL_HULL_H
#define GUARD_VOXEL_HULL_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Round hulls (lib/Structures.lua `roundTemplate`, Gen 3 arm; DramaticShapes,
// MIT).
//
// A tree, a bush or a boulder is carved out of its own drawing per pixel:
// every row of the carved silhouette becomes a horizontal disc of THAT row's
// drawn width, cut in whole voxels, so a crown's rows narrow into a dome and
// its trunk is simply the narrow rows. The front and back of every voxel
// column wear the drawing projected straight on, pixel for pixel; the sides,
// tops and undersides wear one texel each. Nothing is lathed or wrapped.
//
// A template is built once per distinct drawing and stamped wherever it
// stands, translated: a forest is one template drawn many times.
// ---------------------------------------------------------------------------

struct VoxelInstanceTag;

// One vertex of a template: local position in world units (cells), x/z
// centred on the hull's axis and y from its foot, atlas UV and a shade.
// (the same layout as the mesh vertex: position, UV, then a grey shade)
typedef struct {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
} VoxelHullVertex;

typedef struct VoxelHullTemplateTag {
    int nx, ny;               // canvas size in pixels
    int metas[9];             // the drawing, row-major top row first (cols = nx/16, rows = ny/16)
    int nMetas;
    VoxelHullVertex *verts;   // six per quad
    int count;
    int bgMeta;               // the ground metatile the drawing stands on, -1 when none matched
    bool blueBg;              // carved with the drawn water as background
    unsigned vbo;             // GL buffer, 0 until drawn
} VoxelHullTemplate;

// Build a template for the drawing `metas` (cols x rows metatiles, top row
// first) of instance `inst` on a canvas nx wide (and deep) by ny tall.
// `ground` lists candidate floor metatiles for the matched background.
// `blueBg`: the drawing stands in water, and its blue is background too
// (a rock in the sea is drawn boulder and ripple together).
bool VoxelHull_Build(VoxelHullTemplate *tpl, const struct VoxelInstanceTag *inst, int nx, int ny,
                     const int *metas, const int *ground, int groundCount, bool blueBg);

void VoxelHull_Free(VoxelHullTemplate *tpl);

#endif
#endif
#endif // GUARD_VOXEL_HULL_H

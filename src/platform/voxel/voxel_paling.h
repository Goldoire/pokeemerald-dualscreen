#ifndef GUARD_VOXEL_PALING_H
#define GUARD_VOXEL_PALING_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Palings (DramaticShapes data/gen3_palings.lua and Structures.lua
// buildGen3Palings; MIT): a fence STATED as posts and rails, not carved out
// of its drawing.
//
// Emerald draws a fence line twice. The east-west arm is an ELEVATION -- its
// drawn rows are height, and a carve reading rows as height gets a rank of
// posts for free. The north-south arm is a PLAN -- its drawn rows are depth,
// the posts overlap on screen into one unbroken bar, and there is no gap in
// it to carve. So the post is authored: its size read off the elevation, its
// sides wearing the elevation's own drawn rows one to one, its lid the plan's
// drawn post head, and every piece of the line lists where its posts stand.
// Nothing here is a threshold; a metatile not listed is not a paling.
// ---------------------------------------------------------------------------

struct MapLayout;

typedef struct {
    int8_t x, z;                // post origin in cell-local pixels
} VoxelPalingPost;

typedef struct {
    char axis;                  // 'x' runs east-west, 'z' north-south
    int8_t lane, from, to;      // cross-axis offset shared with the posts; the span along the axis
} VoxelPalingSpan;

typedef struct {
    uint16_t meta;
    uint8_t nPosts, nSpans;
    VoxelPalingPost posts[3];
    VoxelPalingSpan spans[2];
} VoxelPalingPiece;

typedef struct {
    uint8_t face0, face1;       // drawn rows of the rail's front, in the face metatile
    int8_t top0, top1;          // drawn rows of its lit top (-1: none drawn)
    uint8_t depth;              // stated depth when no top is drawn
} VoxelPalingRail;

typedef struct {
    const char *name;
    uint8_t postW, postD, postH;
    uint16_t faceMeta;          // the elevation: the post's four sides
    uint8_t faceX, faceY0, faceY1;
    uint16_t topMeta;           // the plan: the post's lid
    uint8_t topX, topZ;
    uint8_t nRails;
    VoxelPalingRail rails[2];
    uint8_t nPieces;
    const VoxelPalingPiece *pieces;
} VoxelPalingFigure;

// ---------------------------------------------------------------------------
// Basins (data/gen3_palings.lua `basins`, Structures.lua buildGen3Basins): a
// fountain is a ring in plan, and its plan is what is drawn. The drawing is
// an exact octagon; the kerb, the water and the jet are stated heights, and
// every surface wears the drawing's own texel at its plan position.
// ---------------------------------------------------------------------------
typedef struct {
    const char *name;
    uint8_t w, h;               // the block of metatiles the figure is drawn on
    uint16_t meta[9];           // row-major
    float outerHalf, outerDiag; // the kerb's outer plan: half-extent and L1 limit about the block centre
    float innerHalf, innerDiag; // the water's plan
    float jetRadius;            // the spout's plan
    uint8_t kerb, water, crown; // STATED heights in pixels over the street
    uint8_t faceCol, faceY0, faceY1;    // the moulding profile: one drawn column, bottom-up
    uint8_t lipCol, lipY0, lipY1;       // the inside of the kerb above the water
    uint8_t plumeCol, plumeY0, plumeY1; // the jet's body
} VoxelBasinFigure;

// The basin whose block starts with metatile `m` on this layout's pair, NULL
// if none.
const VoxelBasinFigure *VoxelBasin_ForAnchor(const struct MapLayout *layout, int m);

// The piece metatile `m` is on this layout's tileset pair, NULL if none;
// `fig` receives the figure it belongs to.
const VoxelPalingPiece *VoxelPaling_Find(const struct MapLayout *layout, int m, const VoxelPalingFigure **fig);

// Mark the metatiles a used piece reads its texels from (the face and top
// drawings) as used, so the atlas decodes them.
void VoxelPaling_MarkUsed(const struct MapLayout *layout, bool *used);

#endif
#endif
#endif // GUARD_VOXEL_PALING_H

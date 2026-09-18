#include "voxel_paling.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stddef.h>
#include "global.h"

extern const struct Tileset gTileset_General;
extern const struct Tileset gTileset_Mauville;
extern const struct Tileset gTileset_Rustboro;

// ---------------------------------------------------------------------------
// RUSTBORO CITY'S FOUNTAIN (General + Rustboro, 824..842: laid once each, all
// nine on RustboroCity -- and the same ids are Slateport's moored boat on
// another pair, which is why this is keyed on the pair). The outer plan is
// the drawn silhouette (x 1..46 on rows 12..33, the north edge 11..36); the
// water sits seven stone pixels in; the spout is a disc of radius 6 dead
// centre. Kerb 12, water 9, crown 19 are the four stated numbers.
// ---------------------------------------------------------------------------
static const VoxelBasinFigure sRustboroFountain = {
    "rustboro city fountain",
    3, 3,
    { 824, 825, 826, 832, 833, 834, 840, 841, 842 },
    22.5f, 34.0f,
    15.5f, 25.0f,
    6.0f,
    12, 9, 19,
    24, 35, 45,
    24, 5, 7,
    24, 18, 27,
};

const VoxelBasinFigure *VoxelBasin_ForAnchor(const struct MapLayout *layout, int m)
{
    if (layout && layout->primaryTileset == &gTileset_General && layout->secondaryTileset == &gTileset_Rustboro
        && m == sRustboroFountain.meta[0])
        return &sRustboroFountain;
    return NULL;
}

// ---------------------------------------------------------------------------
// THE MAUVILLE LOG PALING (a SECONDARY fence: General + Mauville, pieces
// 640..698). MauvilleCity's Game Corner and Bike Shop property line, and the
// same fence on Route 111 and Route 117.
//
// The post is read off 657, the east-west arm: its west post drawn in
// columns 1..5 over rows 4..14, five wide and eleven tall. Its depth is read
// off 650, the north-south arm, where each 8px station carries the drawn
// post head over rows 4..7. A north-south lane hugs one side of its cell and
// carries a post at each 8px station (z 4 and 12); an east-west run carries
// posts at x 1 and 9, both at z 12, where 657 draws its feet.
// ---------------------------------------------------------------------------
static const VoxelPalingPiece sLogPieces[] = {
    { 640, 2, 0, { { 1, 12 }, { 9, 12 } } },            // east-west, the ends of a run
    { 642, 2, 0, { { 1, 12 }, { 9, 12 } } },
    { 648, 2, 0, { { 1, 4 }, { 1, 12 } } },             // north-south, lane on the west
    { 650, 2, 0, { { 9, 4 }, { 9, 12 } } },             // north-south, lane on the east
    { 656, 3, 0, { { 1, 4 }, { 1, 12 }, { 9, 12 } } },  // corners: both lines' posts
    { 657, 2, 0, { { 1, 12 }, { 9, 12 } } },            // east-west, through the cell
    { 658, 3, 0, { { 9, 4 }, { 9, 12 }, { 1, 12 } } },
    { 689, 2, 0, { { 9, 4 }, { 9, 12 } } },             // north-south, the south end
    { 690, 2, 0, { { 1, 4 }, { 1, 12 } } },
    { 697, 1, 0, { { 9, 12 } } },                       // north-south, the north end: one head drawn
    { 698, 1, 0, { { 1, 12 } } },
};

static const VoxelPalingFigure sLogPaling = {
    "mauville log paling",
    5, 4, 11,
    657, 1, 4, 14,
    650, 9, 4,
    0, { { 0 } },
    sizeof(sLogPieces) / sizeof(sLogPieces[0]), sLogPieces,
};

// ---------------------------------------------------------------------------
// THE GENERAL WHITE RAIL FENCE (a PRIMARY fence: pieces 306..330 of
// gTileset_General, in every pair built on it). Slateport's promenade,
// Route 123's field boundaries, Route 110's verges, Route 117, the Battle
// Frontier approaches.
//
// The post: columns 2..5 of 329 across, columns 2..5 of 320 through (6..7
// there are the cast shadow), rows 3..14 of 329 tall. The rails: A is face
// rows 7..8 under a lit top at rows 5..6; B is face rows 12..13, its top
// drawn in A's shadow, so its depth is stated (copied from A). A rail span is
// continuous where a post is discrete: its unit is the cell. (Metatile 308,
// the run with a kerb drawn above it, is refused: the kerb is not modelled.)
// ---------------------------------------------------------------------------
static const VoxelPalingPiece sRailPieces[] = {
    { 306, 2, 1, { { 2, 4 }, { 2, 12 } }, { { 'z', 2, 0, 14 } } },     // north-south, the south end
    { 307, 2, 1, { { 10, 4 }, { 10, 12 } }, { { 'z', 10, 0, 14 } } },
    { 312, 2, 2, { { 2, 12 }, { 10, 12 } }, { { 'x', 12, 2, 16 }, { 'z', 2, 12, 16 } } },   // east-west turning south
    { 313, 2, 1, { { 2, 4 }, { 2, 12 } }, { { 'z', 2, 1, 16 } } },     // north-south, the north end
    { 314, 2, 2, { { 2, 12 }, { 10, 12 } }, { { 'x', 12, 0, 14 }, { 'z', 10, 12, 16 } } },
    { 320, 2, 1, { { 2, 4 }, { 2, 12 } }, { { 'z', 2, 0, 16 } } },     // north-south, through the cell
    { 321, 2, 1, { { 10, 4 }, { 10, 12 } }, { { 'z', 10, 1, 16 } } },
    { 322, 2, 1, { { 10, 4 }, { 10, 12 } }, { { 'z', 10, 0, 16 } } },
    { 328, 3, 2, { { 2, 4 }, { 2, 12 }, { 10, 12 } }, { { 'z', 2, 0, 12 }, { 'x', 12, 2, 16 } } },   // corners
    { 329, 2, 1, { { 2, 12 }, { 10, 12 } }, { { 'x', 12, 0, 16 } } },  // east-west, through the cell
    { 330, 3, 2, { { 10, 4 }, { 10, 12 }, { 2, 12 } }, { { 'z', 10, 0, 12 }, { 'x', 12, 0, 14 } } },
};

static const VoxelPalingFigure sRailFence = {
    "general white rail fence",
    4, 4, 12,
    329, 2, 3, 14,
    320, 2, 0,
    2, { { 7, 8, 5, 6, 0 }, { 12, 13, -1, -1, 2 } },
    sizeof(sRailPieces) / sizeof(sRailPieces[0]), sRailPieces,
};

// The figures this layout's pair carries: the pair's own first, then its
// primary's (Structures.lua palingsFor).
static int FiguresFor(const struct MapLayout *layout, const VoxelPalingFigure **out)
{
    int n = 0;
    if (layout == NULL)
        return 0;
    if (layout->primaryTileset == &gTileset_General && layout->secondaryTileset == &gTileset_Mauville)
        out[n++] = &sLogPaling;
    if (layout->primaryTileset == &gTileset_General)
        out[n++] = &sRailFence;
    return n;
}

static const VoxelPalingPiece *PieceIn(const VoxelPalingFigure *f, int m)
{
    int i;
    for (i = 0; i < f->nPieces; i++)
        if (f->pieces[i].meta == m)
            return &f->pieces[i];
    return NULL;
}

const VoxelPalingPiece *VoxelPaling_Find(const struct MapLayout *layout, int m, const VoxelPalingFigure **fig)
{
    const VoxelPalingFigure *figs[2];
    int n = FiguresFor(layout, figs), i;
    for (i = 0; i < n; i++)
    {
        const VoxelPalingPiece *p = PieceIn(figs[i], m);
        if (p)
        {
            if (fig) *fig = figs[i];
            return p;
        }
    }
    return NULL;
}

void VoxelPaling_MarkUsed(const struct MapLayout *layout, bool *used)
{
    const VoxelPalingFigure *figs[2];
    int n = FiguresFor(layout, figs), i, k;
    for (i = 0; i < n; i++)
        for (k = 0; k < figs[i]->nPieces; k++)
            if (used[figs[i]->pieces[k].meta])
            {
                used[figs[i]->faceMeta] = true;
                used[figs[i]->topMeta] = true;
                break;
            }
}

#endif
#endif

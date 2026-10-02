#ifndef GUARD_VOXEL_CAMERA_OPTIONS_H
#define GUARD_VOXEL_CAMERA_OPTIONS_H

// Stored choices are append-only platform settings. Zero angle keeps each
// preset's original framing; old configs default to 100% zoom in sdl2.c.
static inline float VoxelCamera_OptionPitch(int preset, int angle)
{
    static const float degrees[] = {0.0f, 35.0f, 40.0f, 45.0f, 55.0f, 62.0f};
    if (angle <= 0 || (unsigned)angle >= sizeof(degrees) / sizeof(degrees[0]))
        return preset ? 36.0f : 55.0f;
    return degrees[angle];
}

static inline float VoxelCamera_OptionZoom(int zoom)
{
    static const float scale[] = {0.9f, 1.0f, 1.1f, 1.2f};
    if ((unsigned)zoom >= sizeof(scale) / sizeof(scale[0])) return 1.0f;
    return scale[zoom];
}

#endif

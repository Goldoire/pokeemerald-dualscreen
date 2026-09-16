#ifndef GUARD_VOXEL_RENDERER_H
#define GUARD_VOXEL_RENDERER_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>

// Initialize the voxel renderer. Call after the OpenGL context is created.
bool VoxelRenderer_Init(void);

// Render one frame: the 3D overworld, or the flat GBA frame anywhere else.
void VoxelRenderer_RenderFrame(void);

// Shut down the renderer and free resources.
void VoxelRenderer_Shutdown(void);

// The GL context may have been recreated (Android resume): drop every GL
// name and rebuild on the next frame.
void VoxelRenderer_InvalidateGL(void);

// Free-fly camera (desktop debugging).
void VoxelRenderer_HandleMouseMotion(int dx, int dy);

// Runtime debug switches (bit set = feature off): 1 x-ray silhouettes,
// 2 ghosting, 4 billboard tilt, 8 shadows, 16 overlay, 32 live atlas;
// 64 dumps the model to the log once, 128 writes the JSON dump once.
extern unsigned gVoxelDebugFlags;

// Write the next frame to `path` as a binary PPM (debug harness).
void VoxelRenderer_RequestScreenshot(const char *path);

// Write the world model, mesh counts and field effects as JSON (debug harness).
void VoxelRenderer_RequestDumpJson(const char *path);

extern bool gVoxelModeEnabled;
extern bool gVoxelFirstPersonMode;

#endif
#endif
#endif // GUARD_VOXEL_RENDERER_H

#ifndef GUARD_VOXEL_RENDERER_H
#define GUARD_VOXEL_RENDERER_H

#ifdef PLATFORM_SDL2
#ifdef NATIVE_LINUX

#include <stdbool.h>

// Initialize the voxel renderer. Call after OpenGL context is created.
// Returns true on success, false on failure.
bool VoxelRenderer_Init(void);

// Render one frame of the 3D voxel world.
void VoxelRenderer_RenderFrame(void);

// Shut down the voxel renderer and free resources.
void VoxelRenderer_Shutdown(void);

// External flag set by main() from command-line args
extern bool gVoxelModeEnabled;
extern bool gVoxelFirstPersonMode;

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
#endif // GUARD_VOXEL_RENDERER_H

#ifndef GUARD_VOXEL_CAMERA_H
#define GUARD_VOXEL_CAMERA_H

#ifdef PLATFORM_SDL2
#ifdef NATIVE_LINUX

typedef struct {
    float x, y, z;      // Camera position
    float targetX, targetY, targetZ; // Smooth follow target (player world pos)
    float pitch;        // degrees downward
    float yaw;          // horizontal rotation degrees
    float distance;     // distance from target
    float fov;          // field of view degrees
} VoxelCamera;

void VoxelCamera_Init(VoxelCamera *cam);
void VoxelCamera_Update(VoxelCamera *cam, float playerWorldX, float playerWorldZ);
void VoxelCamera_FPSMouseMotion(VoxelCamera *cam, int dx, int dy);
void VoxelCamera_FPSTick(VoxelCamera *cam, float forward, float right, float up);
void VoxelCamera_ApplyProjection(const VoxelCamera *cam, int viewportW, int viewportH);
void VoxelCamera_ApplyView(const VoxelCamera *cam);

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
#endif // GUARD_VOXEL_CAMERA_H

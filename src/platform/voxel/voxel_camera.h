#ifndef GUARD_VOXEL_CAMERA_H
#define GUARD_VOXEL_CAMERA_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

typedef struct {
    float x, y, z;                   // camera position
    float targetX, targetY, targetZ; // smoothed follow target (player)
    float pitch;                     // degrees downward
    float yaw;                       // horizontal rotation, degrees
    float distance;                  // distance from target
    float fov;                       // vertical field of view, degrees
} VoxelCamera;

void VoxelCamera_Init(VoxelCamera *cam);
void VoxelCamera_Update(VoxelCamera *cam, float playerWorldX, float playerWorldY, float playerWorldZ);
float VoxelCamera_TanPitch(const VoxelCamera *cam);
void VoxelCamera_FPSMouseMotion(VoxelCamera *cam, int dx, int dy);
void VoxelCamera_FPSTick(VoxelCamera *cam, float forward, float right, float up);
void VoxelCamera_ApplyProjection(const VoxelCamera *cam, int viewportW, int viewportH);
void VoxelCamera_ApplyView(const VoxelCamera *cam);

#endif
#endif
#endif // GUARD_VOXEL_CAMERA_H

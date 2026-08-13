#include "voxel_camera.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <math.h>
#ifdef __ANDROID__
#include "gles1_compat.h"
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif
#include "voxel_world.h"

void VoxelCamera_Init(VoxelCamera *cam)
{
    cam->x = 0.0f;
    cam->y = 8.0f;
    cam->z = 0.0f;
    cam->targetX = 0.0f;
    cam->targetY = 0.0f;
    cam->targetZ = 0.0f;
    cam->pitch = 40.0f;  // 35-50 degrees
    cam->yaw = 0.0f;
    cam->distance = 9.0f;
    cam->fov = 35.0f; // Tighter framing
}

void VoxelCamera_Update(VoxelCamera *cam, float playerWorldX, float playerWorldZ)
{
    cam->targetX += (playerWorldX - cam->targetX) * 0.15f;
    cam->targetZ += (playerWorldZ - cam->targetZ) * 0.15f;
    
    extern bool gVoxelFirstPersonMode;
    if (gVoxelFirstPersonMode) {
        // Free-fly mode: Update does not snap to player.
        // We calculate target from yaw and pitch, assuming they are updated by mouse.
        float pitchRad = cam->pitch * (3.14159265f / 180.0f);
        float yawRad   = cam->yaw   * (3.14159265f / 180.0f);
        float lookX = sinf(yawRad) * cosf(pitchRad);
        float lookY = sinf(pitchRad);
        float lookZ = cosf(yawRad) * cosf(pitchRad);
        
        cam->targetX = cam->x + lookX;
        cam->targetY = cam->y - lookY; // Pitch positive = looking down
        cam->targetZ = cam->z - lookZ; // Looking towards -Z when yaw=0
        
        return;
    }

    // Adapt distance to map size
    int mapW = 0, mapH = 0;
    VoxelWorld_GetMapDimensions(&mapW, &mapH);
    float baseDist = 8.0f;
    float mapScale = (float)(mapW > mapH ? mapW : mapH) * 0.2f;
    if (mapScale > 5.0f) mapScale = 5.0f;
    cam->distance = baseDist + mapScale;

    float pitchRad = cam->pitch * (3.14159265f / 180.0f);
    float yawRad   = cam->yaw   * (3.14159265f / 180.0f);

    // Camera sits to the SOUTH of the player (+Z) and above, looking NORTH (-Z).
    cam->x = cam->targetX + sinf(yawRad) * cam->distance;
    cam->y = tanf(pitchRad) * cam->distance;
    cam->z = cam->targetZ + cosf(yawRad) * cam->distance;
    cam->targetY = 0.0f;
}

void VoxelCamera_ApplyProjection(const VoxelCamera *cam, int viewportW, int viewportH)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)viewportW / (float)viewportH;
    gluPerspective(cam->fov, aspect, 0.1f, 200.0f);
}

void VoxelCamera_ApplyView(const VoxelCamera *cam)
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(cam->x, cam->y, cam->z,
              cam->targetX, cam->targetY, cam->targetZ,
              0.0f, 1.0f, 0.0f);
}

void VoxelCamera_FPSMouseMotion(VoxelCamera *cam, int dx, int dy)
{
    float sensitivity = 0.2f;
    cam->yaw -= dx * sensitivity;
    cam->pitch += dy * sensitivity;
    
    // Constrain pitch
    if (cam->pitch > 89.0f) cam->pitch = 89.0f;
    if (cam->pitch < -89.0f) cam->pitch = -89.0f;
    
    // Wrap yaw
    if (cam->yaw < 0.0f) cam->yaw += 360.0f;
    if (cam->yaw >= 360.0f) cam->yaw -= 360.0f;
}

void VoxelCamera_FPSTick(VoxelCamera *cam, float forward, float right, float up)
{
    float pitchRad = cam->pitch * (3.14159265f / 180.0f);
    float yawRad   = cam->yaw   * (3.14159265f / 180.0f);
    
    float lookX = sinf(yawRad) * cosf(pitchRad);
    float lookZ = -cosf(yawRad) * cosf(pitchRad); // looking towards -Z when yaw=0
    
    float rightX = cosf(yawRad);
    float rightZ = sinf(yawRad); // right vector
    
    float speed = 0.15f;
    
    cam->x += (lookX * forward + rightX * right) * speed;
    cam->z += (lookZ * forward + rightZ * right) * speed;
    cam->y += up * speed;
}

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2

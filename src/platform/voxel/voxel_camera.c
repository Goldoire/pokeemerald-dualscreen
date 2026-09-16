#include "voxel_camera.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <math.h>
#include <string.h>
#ifdef __ANDROID__
#include "gles1_compat.h"
#else
#include <GL/gl.h>
#endif
#include "voxel_world.h"

#define DEG2RAD (3.14159265f / 180.0f)

// GLU is not linked on either platform; these are its two matrix helpers.
static void Perspective(float fovy, float aspect, float zNear, float zFar)
{
    GLfloat m[16];
    float f = 1.0f / tanf(fovy * DEG2RAD * 0.5f);
    float nf = 1.0f / (zNear - zFar);
    memset(m, 0, sizeof(m));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) * nf;
    m[11] = -1.0f;
    m[14] = 2.0f * zFar * zNear * nf;
    glMultMatrixf(m);
}

static void LookAt(float eyeX, float eyeY, float eyeZ, float cX, float cY, float cZ, float upX, float upY, float upZ)
{
    float fx = cX - eyeX, fy = cY - eyeY, fz = cZ - eyeZ;
    float len = sqrtf(fx * fx + fy * fy + fz * fz);
    float sx, sy, sz, ux, uy, uz, slen;
    GLfloat m[16];
    if (len != 0.0f) { fx /= len; fy /= len; fz /= len; }
    sx = fy * upZ - fz * upY;
    sy = fz * upX - fx * upZ;
    sz = fx * upY - fy * upX;
    slen = sqrtf(sx * sx + sy * sy + sz * sz);
    if (slen != 0.0f) { sx /= slen; sy /= slen; sz /= slen; }
    ux = sy * fz - sz * fy;
    uy = sz * fx - sx * fz;
    uz = sx * fy - sy * fx;
    memset(m, 0, sizeof(m));
    m[0] = sx; m[4] = sy; m[8] = sz;
    m[1] = ux; m[5] = uy; m[9] = uz;
    m[2] = -fx; m[6] = -fy; m[10] = -fz;
    m[15] = 1.0f;
    glMultMatrixf(m);
    glTranslatef(-eyeX, -eyeY, -eyeZ);
}

void VoxelCamera_Init(VoxelCamera *cam)
{
    cam->x = 0.0f;
    cam->y = 8.0f;
    cam->z = 0.0f;
    cam->targetX = 0.0f;
    cam->targetY = 0.0f;
    cam->targetZ = 0.0f;
    cam->yaw = 0.0f;
    cam->preset = -1;
    VoxelCamera_ApplyPreset(cam, 0);
}

void VoxelCamera_ApplyPreset(VoxelCamera *cam, int preset)
{
    cam->preset = preset ? 1 : 0;
    if (cam->preset == 1)
    {
        // The showcase framing: low and far, the route laid out like a model.
        cam->pitch = 36.0f;
        cam->distance = 22.0f;
        cam->fov = 45.0f;
    }
    else
    {
        // Steep enough that a house rarely hides the street behind it,
        // shallow enough that facades read as walls rather than as paint.
        cam->pitch = 50.0f;
        cam->distance = 13.5f;
        cam->fov = 40.0f;
    }
}

void VoxelCamera_Update(VoxelCamera *cam, float playerWorldX, float playerWorldY, float playerWorldZ)
{
    float pitchRad, yawRad;
    extern bool gVoxelFirstPersonMode;

    cam->targetX += (playerWorldX - cam->targetX) * 0.18f;
    cam->targetZ += (playerWorldZ - cam->targetZ) * 0.18f;
    cam->targetY += (playerWorldY - cam->targetY) * 0.12f;

    if (gVoxelFirstPersonMode)
    {
        pitchRad = cam->pitch * DEG2RAD;
        yawRad = cam->yaw * DEG2RAD;
        cam->targetX = cam->x + sinf(yawRad) * cosf(pitchRad);
        cam->targetY = cam->y - sinf(pitchRad);
        cam->targetZ = cam->z - cosf(yawRad) * cosf(pitchRad);
        return;
    }

    pitchRad = cam->pitch * DEG2RAD;
    yawRad = cam->yaw * DEG2RAD;
    // The camera sits south of the player (+Z) and above, looking north.
    cam->x = cam->targetX + sinf(yawRad) * cosf(pitchRad) * cam->distance;
    cam->y = cam->targetY + sinf(pitchRad) * cam->distance;
    cam->z = cam->targetZ + cosf(yawRad) * cosf(pitchRad) * cam->distance;
}

float VoxelCamera_TanPitch(const VoxelCamera *cam)
{
    return tanf(cam->pitch * DEG2RAD);
}

void VoxelCamera_ApplyProjection(const VoxelCamera *cam, int viewportW, int viewportH)
{
    float aspect = (float)viewportW / (float)viewportH;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    Perspective(cam->fov, aspect, 0.5f, 120.0f);
}

void VoxelCamera_ApplyView(const VoxelCamera *cam)
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    LookAt(cam->x, cam->y, cam->z,
           cam->targetX, cam->targetY, cam->targetZ,
           0.0f, 1.0f, 0.0f);
}

void VoxelCamera_FPSMouseMotion(VoxelCamera *cam, int dx, int dy)
{
    float sensitivity = 0.2f;
    cam->yaw -= dx * sensitivity;
    cam->pitch += dy * sensitivity;
    if (cam->pitch > 89.0f) cam->pitch = 89.0f;
    if (cam->pitch < -89.0f) cam->pitch = -89.0f;
    if (cam->yaw < 0.0f) cam->yaw += 360.0f;
    if (cam->yaw >= 360.0f) cam->yaw -= 360.0f;
}

void VoxelCamera_FPSTick(VoxelCamera *cam, float forward, float right, float up)
{
    float pitchRad = cam->pitch * DEG2RAD;
    float yawRad = cam->yaw * DEG2RAD;
    float lookX = sinf(yawRad) * cosf(pitchRad);
    float lookZ = -cosf(yawRad) * cosf(pitchRad);
    float rightX = cosf(yawRad);
    float rightZ = sinf(yawRad);
    float speed = 0.15f;

    cam->x += (lookX * forward + rightX * right) * speed;
    cam->z += (lookZ * forward + rightZ * right) * speed;
    cam->y += up * speed;
}

#endif
#endif

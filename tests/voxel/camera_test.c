#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "voxel_camera.h"

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.0001f);
}

int main(void)
{
    VoxelCamera cam;
    VoxelCamera_Init(&cam);
    VoxelCamera_ApplyOptions(&cam, 0, 0, 1);
    near(cam.pitch, 55.0f);
    near(cam.distance, 10.0f);
    near(cam.fov, 53.13f);
    VoxelCamera_ApplyOptions(&cam, 1, 0, 1);
    near(cam.pitch, 36.0f);
    near(cam.distance, 22.0f);
    for (int frame = 0; frame < 10000; ++frame)
        VoxelCamera_ApplyOptions(&cam, 0, 3, 3);
    near(cam.pitch, 45.0f);
    near(cam.distance, 10.0f / 1.2f);
    VoxelCamera_ApplyOptions(&cam, 1, 1, 0);
    near(cam.pitch, 35.0f);
    near(cam.distance, 22.0f / 0.9f);
    VoxelCamera_ApplyOptions(&cam, 0, -1, -1);
    near(cam.pitch, 55.0f);
    near(cam.distance, 10.0f);
    VoxelCamera_ApplyOptions(&cam, 1, 999, 999);
    near(cam.pitch, 36.0f);
    near(cam.distance, 22.0f);
    puts("PASS: camera defaults, live changes, invalid values, repeated-frame stability");
    return 0;
}

#ifndef VOXEL_MODELS_H
#define VOXEL_MODELS_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#ifdef __ANDROID__
#include "gles1_compat.h"
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

extern GLuint gVoxelModels[1024];

#endif
#endif
#endif

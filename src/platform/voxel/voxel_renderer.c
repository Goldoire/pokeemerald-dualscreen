#include "voxel_renderer.h"

#ifdef PLATFORM_SDL2
#ifdef NATIVE_LINUX

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "fieldmap.h"
#include "palette.h"
#include "decompress.h"
#include <GL/gl.h>
#include <SDL2/SDL.h>
#include "voxel_world.h"
#include "voxel_camera.h"
#include "voxel_mesh.h"
#include "field_player_avatar.h"
#include "constants/global.h"

// Will be defined in sdl2.c
extern SDL_Window *sdlWindow;

static GLuint sVoxelAtlasTex = 0;
static const struct MapLayout *sLastMapLayout = NULL;
bool gVoxelFirstPersonMode = false;

// -----------------------------------------------------------------------
//  Atlas builder
// -----------------------------------------------------------------------
static GLuint BuildVoxelAtlasForHeader(const struct MapHeader *header)
{
    if (!header || !header->mapLayout) return 0;

    uint32_t *atlasPixels = malloc(512 * 512 * 4);
    if (!atlasPixels) return 0;
    memset(atlasPixels, 0, 512 * 512 * 4);

    const struct MapLayout *layout = header->mapLayout;

    uint8_t *primTiles = calloc(1, 65536);
    uint8_t *secTiles  = calloc(1, 65536);

    if (layout->primaryTileset) {
        if (layout->primaryTileset->isCompressed)
            LZDecompressWram(layout->primaryTileset->tiles, primTiles);
        else
            memcpy(primTiles, layout->primaryTileset->tiles, 16384);
    }

    if (layout->secondaryTileset) {
        if (layout->secondaryTileset->isCompressed)
            LZDecompressWram(layout->secondaryTileset->tiles, secTiles);
        else
            memcpy(secTiles, layout->secondaryTileset->tiles, 16384);
    }

    bool usedMetatiles[1024] = {false};
    int mapArea = layout->width * layout->height;
    for (int i = 0; i < mapArea; i++) {
        usedMetatiles[layout->map[i] & 0x3FF] = true;
    }
    for (int i = 0; i < 4; i++) {
        if (layout->border) {
            usedMetatiles[layout->border[i] & 0x3FF] = true;
        }
    }

    for (int m = 0; m < 1024; m++) {
        if (!usedMetatiles[m]) continue;
        
        const u16 *metatileData = NULL;
        if (m < 512) {
            if (layout->primaryTileset && layout->primaryTileset->metatiles)
                metatileData = layout->primaryTileset->metatiles + (m * 8);
        } else {
            if (layout->secondaryTileset && layout->secondaryTileset->metatiles)
                metatileData = layout->secondaryTileset->metatiles + ((m - 512) * 8);
        }

        if (metatileData) {
            uint32_t mtPixels[256];
            memset(mtPixels, 0, sizeof(mtPixels));

            for (int layer = 0; layer < 2; layer++) {
                for (int i = 0; i < 4; i++) {
                    int dstOffsetX = (i % 2) * 8;
                    int dstOffsetY = (i / 2) * 8;
                    u16 entry = metatileData[(layer * 4) + i];

                    uint32_t tileId   = entry & 0x3FF;
                    uint32_t paletteId = (entry >> 12) & 0xF;
                    bool flipX = (entry >> 10) & 1;
                    bool flipY = (entry >> 11) & 1;

                    for (int py = 0; py < 8; py++) {
                        for (int px = 0; px < 8; px++) {
                            int srcX = flipX ? (7 - px) : px;
                            int srcY = flipY ? (7 - py) : py;

                            uint8_t pixelByte = 0;
                            if (tileId < 512)
                                pixelByte = primTiles[tileId * 32 + srcY * 4 + srcX / 2];
                            else
                                pixelByte = secTiles[(tileId - 512) * 32 + srcY * 4 + srcX / 2];

                            uint8_t colorIdx = (srcX % 2 == 0) ? (pixelByte & 0xF) : (pixelByte >> 4);

                            if (layer == 0 || colorIdx != 0) {
                                uint16_t bgr15 = 0;
                                if (layer == 0 && colorIdx == 0) {
                                    if (layout->primaryTileset && layout->primaryTileset->palettes)
                                        bgr15 = layout->primaryTileset->palettes[0][0];
                                } else {
                                    if (paletteId < 6) {
                                        if (layout->primaryTileset && layout->primaryTileset->palettes)
                                            bgr15 = layout->primaryTileset->palettes[paletteId][colorIdx];
                                    } else {
                                        if (layout->secondaryTileset && layout->secondaryTileset->palettes)
                                            bgr15 = layout->secondaryTileset->palettes[paletteId][colorIdx];
                                    }
                                }

                                uint8_t r = ((bgr15 >>  0) & 0x1F) << 3;
                                uint8_t g = ((bgr15 >>  5) & 0x1F) << 3;
                                uint8_t b = ((bgr15 >> 10) & 0x1F) << 3;
                                uint32_t rgba = r | (g << 8) | (b << 16) | (255u << 24);
                                mtPixels[(dstOffsetY + py) * 16 + (dstOffsetX + px)] = rgba;
                            }
                        }
                    }
                }
            }

            int atlasX = (m % 32) * 16;
            int atlasY = (m / 32) * 16;
            for (int py = 0; py < 16; py++)
                for (int px = 0; px < 16; px++)
                    atlasPixels[(atlasY + py) * 512 + (atlasX + px)] = mtPixels[py * 16 + px];
        }
    }

    // Force all pixels fully opaque for now (debug)
    for (int i = 0; i < 512 * 512; i++)
        atlasPixels[i] |= (255u << 24);

    GLuint tex = 0;
    glGenTextures(1, &tex);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlasPixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    free(primTiles);
    free(secTiles);
    free(atlasPixels);
    return tex;
}

// -----------------------------------------------------------------------
//  Sprite texture decoder (kept as-is from previous milestone)
// -----------------------------------------------------------------------
static void GetSpriteDimensions(u8 shape, u8 size, int *w, int *h)
{
    if (shape == ST_OAM_SQUARE) {
        if      (size == 0) { *w = 8;  *h = 8;  }
        else if (size == 1) { *w = 16; *h = 16; }
        else if (size == 2) { *w = 32; *h = 32; }
        else                { *w = 64; *h = 64; }
    } else if (shape == ST_OAM_H_RECTANGLE) {
        if      (size == 0) { *w = 16; *h = 8;  }
        else if (size == 1) { *w = 32; *h = 8;  }
        else if (size == 2) { *w = 32; *h = 16; }
        else                { *w = 64; *h = 32; }
    } else if (shape == ST_OAM_V_RECTANGLE) {
        if      (size == 0) { *w = 8;  *h = 16; }
        else if (size == 1) { *w = 8;  *h = 32; }
        else if (size == 2) { *w = 16; *h = 32; }
        else                { *w = 32; *h = 64; }
    } else {
        *w = 16; *h = 32;
    }
}

static GLuint sObjectEventTex[64] = {0};
static int    sObjectEventW[64] = {16};
static int    sObjectEventH[64] = {32};

static void UpdateObjectSpriteTexture(struct Sprite *sprite, GLuint *texOut, int *wOut, int *hOut)
{
    if (!sprite) return;

    int w, h;
    GetSpriteDimensions(sprite->oam.shape, sprite->oam.size, &w, &h);
    if (wOut) *wOut = w;
    if (hOut) *hOut = h;

    u8 *pixels = malloc(w * h * 4);
    if (!pixels) return;

    u16 tileBase = sprite->oam.tileNum;
    u16 palBase  = sprite->oam.paletteNum;

    extern unsigned char PLTT[];
    u16 *palData = (u16 *)(PLTT + 0x200 + palBase * 32);

    extern unsigned char VRAM_[];
    u8 *vramBase = VRAM_ + 0x10000;

    int tilesX = w / 8;
    int tilesY = h / 8;

    // H/V flip from OAM attribute bits (not matrixNum in normal mode)
    // In non-affine mode, bit 28 of attr0+attr1 encoding: we use the OamData struct fields
    bool hFlip = false;
    bool vFlip = false;
    if (sprite->oam.affineMode == 0) {
        // matrixNum field is repurposed for flip flags in non-affine mode
        hFlip = (sprite->oam.matrixNum & 0x08) != 0;
        vFlip = (sprite->oam.matrixNum & 0x10) != 0;
    }

    for (int ty = 0; ty < tilesY; ty++) {
        for (int tx = 0; tx < tilesX; tx++) {
            int tileIdx   = ty * tilesX + tx;
            int offset = (tileBase + tileIdx) * 32;
            if (offset >= 0x8000) offset = 0; // prevent out-of-bounds
            u8 *tileData  = vramBase + offset;

            for (int py = 0; py < 8; py++) {
                for (int px = 0; px < 8; px++) {
                    u8 colorIdx = tileData[(py * 8 + px) / 2];
                    colorIdx = (px % 2 == 0) ? (colorIdx & 0x0F) : (colorIdx >> 4);

                    int outX = tx * 8 + px;
                    int outY = ty * 8 + py;
                    if (hFlip) outX = w - 1 - outX;
                    if (vFlip) outY = h - 1 - outY;

                    u8 *outPixel = pixels + (outY * w + outX) * 4;

                    if (colorIdx == 0) {
                        outPixel[0] = outPixel[1] = outPixel[2] = outPixel[3] = 0;
                    } else {
                        u16 color15 = palData[colorIdx];
                        outPixel[0] = (color15 & 0x1F) * 255 / 31;
                        outPixel[1] = ((color15 >> 5) & 0x1F) * 255 / 31;
                        outPixel[2] = ((color15 >> 10) & 0x1F) * 255 / 31;
                        outPixel[3] = 255;
                    }
                }
            }
        }
    }

    if (*texOut == 0)
        glGenTextures(1, texOut);

    glBindTexture(GL_TEXTURE_2D, *texOut);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    free(pixels);
}

//    sprite->data[3] (sDirection) → movement direction
//    sprite->data[4] (sSpeed)    → speed class (0=16fr, 1=8fr, 2=6fr, 3=4fr, 4=2fr)
//    sprite->data[5] (sTimer)    → frames elapsed in current step (0..stepLen-1)
//
//  World coordinate system:
//    voxel X = map-local X  (east = +X)
//    voxel Y = height
//    voxel Z = map-local Y  (south = +Z)
//
//  NOTE: sprite->x/y/x2/y2 are SCREEN-SPACE values that include camera offset;
//  they must NOT be used to determine world position.
// -----------------------------------------------------------------------

// Step durations matching sStepTimes[] in event_object_movement.c
static const int sVoxelStepTimes[] = { 16, 8, 6, 4, 2 };

// Returns the sub-tile interpolation factor t in [0.0, 1.0].
// t==0 means at previous tile, t==1 means at current tile.
static float GetMovementProgress(const struct ObjectEvent *obj, const struct Sprite *sprite)
{
    // If not currently taking a step, the sprite is idle at currentCoords.
    // After a step completes, ShiftStillObjectEventCoords sets prev==current,
    // so lerp(0..1) between identical coords is still correct.
    if (!obj->singleMovementActive && !obj->heldMovementActive)
        return 1.0f;

    // data[4] = sSpeed, data[5] = sTimer
    int speed = (int)(u16)sprite->data[4];
    int timer = (int)(u16)sprite->data[5];

    if (speed < 0 || speed > 4)
        return 1.0f;

    int stepLen = sVoxelStepTimes[speed];
    if (stepLen <= 0)
        return 1.0f;

    // timer was already incremented AFTER executing the step,
    // so at the start of a step timer==0 and at completion timer==stepLen.
    float t = (float)timer / (float)stepLen;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

// Get the object's interpolated world position.
// Returns true if position is valid.
static bool GetVoxelObjectWorldPos(struct ObjectEvent *obj, float *outX, float *outZ)
{
    if (!obj || !obj->active) return false;
    if (obj->spriteId >= 64) return false; // MAX_SPRITES = 64
    struct Sprite *sprite = &gSprites[obj->spriteId];

    // Convert backup-layout coords to map-local coords
    float prevX = (float)(obj->previousCoords.x - MAP_OFFSET);
    float prevZ = (float)(obj->previousCoords.y - MAP_OFFSET);
    float curX  = (float)(obj->currentCoords.x  - MAP_OFFSET);
    float curZ  = (float)(obj->currentCoords.y  - MAP_OFFSET);

    float t = GetMovementProgress(obj, sprite);

    *outX = prevX + (curX - prevX) * t;
    *outZ = prevZ + (curZ - prevZ) * t;
    return true;
}

// -----------------------------------------------------------------------
//  Map-change detection
// -----------------------------------------------------------------------
static uint16_t sLastMapLayoutId = 0xFFFF;
static u8 sLastMapGroup = 0xFF;
static u8 sLastMapNum   = 0xFF;
static bool sNeedCameraSnap = true;

// -----------------------------------------------------------------------
//  Camera
// -----------------------------------------------------------------------
static VoxelCamera sCamera;

static bool DetectMapChange(void)
{
    uint16_t currentLayoutId = gMapHeader.mapLayoutId;
    u8 currentGroup = gSaveBlock1Ptr->location.mapGroup;
    u8 currentNum   = gSaveBlock1Ptr->location.mapNum;

    if (currentLayoutId != sLastMapLayoutId ||
        currentGroup != sLastMapGroup ||
        currentNum   != sLastMapNum)
    {
        // Try to find the new map in the OLD instances to compute shift
        bool found = false;
        int shiftX = 0;
        int shiftZ = 0;
        for (int i = 0; i < gVoxelMapInstanceCount; i++) {
            if (gVoxelMapInstances[i].mapGroup == currentGroup && gVoxelMapInstances[i].mapNum == currentNum) {
                shiftX = -gVoxelMapInstances[i].originX;
                shiftZ = -gVoxelMapInstances[i].originY;
                found = true;
                break;
            }
        }
        
        if (found) {
            sCamera.x += shiftX;
            sCamera.z += shiftZ;
            sCamera.targetX += shiftX;
            sCamera.targetZ += shiftZ;
        } else {
            sNeedCameraSnap = true;
        }

        sLastMapLayoutId = currentLayoutId;
        sLastMapGroup    = currentGroup;
        sLastMapNum      = currentNum;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------
//  Main render function
// -----------------------------------------------------------------------
static bool sPrintedOnce = false;
static bool gVoxelDebugFlatMode = false;
static bool gF3WasPressed = false;

void VoxelRenderer_HandleMouseMotion(int dx, int dy)
{
    if (gVoxelFirstPersonMode) {
        VoxelCamera_FPSMouseMotion(&sCamera, dx, dy);
    }
}

void VoxelRenderer_RenderFrame(void)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    if (keys[SDL_SCANCODE_F3]) {
        if (!gF3WasPressed) {
            gVoxelDebugFlatMode = !gVoxelDebugFlatMode;
            gF3WasPressed = true;
        }
    } else {
        gF3WasPressed = false;
    }

    // ---- 2D FALLBACK (title screen, battle, menus) ----
    if (!VoxelWorld_IsMapAvailable()) {
        extern void DrawFrame(uint16_t *pixels);

        static uint16_t gbaImage[240 * 160];
        static uint32_t image[240 * 160];
        static int sFallbackFrames = 0;
        sFallbackFrames++;

        memset(gbaImage, 0, sizeof(gbaImage));
        DrawFrame(gbaImage);

        for (int i = 0; i < 240 * 160; i++) {
            uint16_t color = gbaImage[i];
            uint32_t r = (color & 0x1F) * 255 / 31;
            uint32_t g = ((color >> 5) & 0x1F) * 255 / 31;
            uint32_t b = ((color >> 10) & 0x1F) * 255 / 31;
            image[i] = r | (g << 8) | (b << 16) | (255u << 24);
        }

        REG_VCOUNT = 161;

        static GLuint sScreenTex = 0;
        if (!sScreenTex) {
            glGenTextures(1, &sScreenTex);
            glBindTexture(GL_TEXTURE_2D, sScreenTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }

        int winW, winH;
        SDL_GL_GetDrawableSize(sdlWindow, &winW, &winH);
        glViewport(0, 0, winW, winH);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_TEXTURE_2D);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glBindTexture(GL_TEXTURE_2D, sScreenTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 240, 160, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

        if (sFallbackFrames <= 5)
            printf("[Voxel2D] glGetError after glTexImage2D: %d\n", glGetError());

        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
        glEnd();

        glFlush();
        SDL_GL_SwapWindow(sdlWindow);

        // Any time we fall back to 2D we want a camera snap on next 3D frame
        sNeedCameraSnap = true;
        sPrintedOnce = false;
        return;
    }

    // ---- 3D OVERWORLD ----
    REG_VCOUNT = 161;

    // Detect map change → snap camera
    bool mapChanged = DetectMapChange();
    if (mapChanged) {
        sNeedCameraSnap = true;
        sPrintedOnce = false;
    }

    VoxelWorld_BuildInstances();

    static struct {
        const struct MapLayout *layout;
        GLuint tex;
    } sAtlasCache[MAX_VOXEL_MAP_INSTANCES];
    
    // Check if cache needs reset (if main map layout changed, just clear all for safety)
    static const struct MapLayout *sLastMainLayout = NULL;
    if (sLastMainLayout != gMapHeader.mapLayout) {
        for (int i = 0; i < MAX_VOXEL_MAP_INSTANCES; i++) {
            if (sAtlasCache[i].tex) {
                glDeleteTextures(1, &sAtlasCache[i].tex);
                sAtlasCache[i].tex = 0;
            }
            sAtlasCache[i].layout = NULL;
        }
        sLastMainLayout = gMapHeader.mapLayout;
    }

    // Build/update atlas cache for current instances
    GLuint instanceTextures[MAX_VOXEL_MAP_INSTANCES] = {0};
    for (int i = 0; i < gVoxelMapInstanceCount; i++) {
        const struct MapLayout *layout = gVoxelMapInstances[i].header->mapLayout;
        GLuint tex = 0;
        // Find in cache
        for (int j = 0; j < MAX_VOXEL_MAP_INSTANCES; j++) {
            if (sAtlasCache[j].layout == layout) {
                tex = sAtlasCache[j].tex;
                break;
            }
        }
        // Build if not found
        if (tex == 0) {
            tex = BuildVoxelAtlasForHeader(gVoxelMapInstances[i].header);
            // Store in first empty slot
            for (int j = 0; j < MAX_VOXEL_MAP_INSTANCES; j++) {
                if (sAtlasCache[j].layout == NULL) {
                    sAtlasCache[j].layout = layout;
                    sAtlasCache[j].tex = tex;
                    break;
                }
            }
        }
        instanceTextures[i] = tex;
    }

    int mapW, mapH;
    VoxelWorld_GetMapDimensions(&mapW, &mapH);

    // ---- Get canonical player world position ----
    float playerWorldX = 0.0f, playerWorldZ = 0.0f;
    if (gPlayerAvatar.objectEventId < 16) {
        GetVoxelObjectWorldPos(&gObjectEvents[gPlayerAvatar.objectEventId], &playerWorldX, &playerWorldZ);
    }

    // Billboard is centered on the tile. Add 0.5 so player stands in the middle.
    float billX = playerWorldX + 0.5f;
    float billZ = playerWorldZ + 0.5f;

    // ---- Camera snap on map change / entry ----
    if (sNeedCameraSnap) {
        // Snap both the camera target and position to the player immediately.
        sCamera.targetX = billX;
        sCamera.targetZ = billZ;
        sNeedCameraSnap = false;
    }

    // ---- Print map info once per map load ----
    if (!sPrintedOnce) {
        printf("VOXEL MAP INSTANCES:\n");
        for (int i = 0; i < gVoxelMapInstanceCount; i++) {
            struct VoxelMapInstance *inst = &gVoxelMapInstances[i];
            printf("%d:%d\norigin = (%d,%d)\nw=%d h=%d\n\n",
                   inst->mapGroup, inst->mapNum, inst->originX, inst->originY,
                   inst->header->mapLayout->width, inst->header->mapLayout->height);
        }
        printf("current map: %d:%d\n", gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum);
        printf("number of instances: %d\n", gVoxelMapInstanceCount);
        printf("camera position: %.2f, %.2f, %.2f\n", sCamera.x, sCamera.y, sCamera.z);
        
        sPrintedOnce = true;
    }

    // ---- We update object sprite textures below in the main drawing loop ----

    // ---- Camera smooth-follow ----
    if (gVoxelFirstPersonMode) {
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        float fwd = 0.0f, right = 0.0f, up = 0.0f;
        if (keys[SDL_SCANCODE_W]) fwd = 1.0f;
        if (keys[SDL_SCANCODE_S]) fwd = -1.0f;
        if (keys[SDL_SCANCODE_D]) right = 1.0f;
        if (keys[SDL_SCANCODE_A]) right = -1.0f;
        if (keys[SDL_SCANCODE_SPACE]) up = 1.0f;
        if (keys[SDL_SCANCODE_LSHIFT]) up = -1.0f;
        
        VoxelCamera_FPSTick(&sCamera, fwd, right, up);
        VoxelCamera_Update(&sCamera, billX, billZ);
    } else {
        VoxelCamera_Update(&sCamera, billX, billZ);
    }

    // ---- Viewport + clear ----
    int winW, winH;
    SDL_GL_GetDrawableSize(sdlWindow, &winW, &winH);
    glViewport(0, 0, winW, winH);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);

    // Set a sky blue background color instead of black
    glClearColor(0.53f, 0.81f, 0.92f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    VoxelCamera_ApplyProjection(&sCamera, winW, winH);
    VoxelCamera_ApplyView(&sCamera);

    extern void VoxelStructure_ExtractAll(void);
    VoxelStructure_ExtractAll();

    // ---- Map geometry ----
    glEnable(GL_TEXTURE_2D);
    extern void VoxelMesh_BuildWallsForInstance(int instIdx, GLuint tex);
    
    if (!gVoxelDebugFlatMode) {
        for (int i = 0; i < gVoxelMapInstanceCount; i++) {
            VoxelMesh_BuildWallsForInstance(i, instanceTextures[i]);
        }
        
        extern void VoxelStructure_RenderAll(GLuint atlasTex);
        if (gVoxelMapInstanceCount > 0) {
            VoxelStructure_RenderAll(instanceTextures[0]);
        }
    }

    for (int instIdx = 0; instIdx < gVoxelMapInstanceCount; instIdx++) {
        const struct VoxelMapInstance *inst = &gVoxelMapInstances[instIdx];
        glBindTexture(GL_TEXTURE_2D, instanceTextures[instIdx]);
        
        int w = inst->header->mapLayout->width;
        int h = inst->header->mapLayout->height;
        for (int ly = 0; ly < h; ly++) {
            for (int lx = 0; lx < w; lx++) {
                int x = lx + inst->originX;
                int y = ly + inst->originY;
                
                VoxelVisualShape terrain = VoxelWorld_ClassifyTile(x, y);
                int metatileId = VoxelWorld_GetMetatileId(x, y);
                
                if (gVoxelDebugFlatMode) {
                    // Draw entirely flat for debugging
                    float wx = (float)x;
                    float wz = (float)y;
                    float atlasW = 512.0f;
                    float atlasH = 512.0f;
                    float u0 = (metatileId % 32) * 16.0f / atlasW;
                    float v0 = (metatileId / 32) * 16.0f / atlasH;
                    float u1 = u0 + (16.0f / atlasW);
                    float v1 = v0 + (16.0f / atlasH);
                    
                    glEnable(GL_TEXTURE_2D);
                    glColor3f(1.0f, 1.0f, 1.0f);
                    glBegin(GL_QUADS);
                    glTexCoord2f(u0, v0); glVertex3f(wx,      0.0f, wz);
                    glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, 0.0f, wz);
                    glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, 0.0f, wz+1.0f);
                    glTexCoord2f(u0, v1); glVertex3f(wx,      0.0f, wz+1.0f);
                    glEnd();
                    glDisable(GL_TEXTURE_2D);

                    // Highlight shapes with semi-transparent colors
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glBegin(GL_QUADS);
                    if (terrain == VOXEL_SHAPE_WALL) glColor4f(1.0f, 0.0f, 0.0f, 0.5f);
                    else if (terrain == VOXEL_SHAPE_ROOF) glColor4f(0.0f, 1.0f, 0.0f, 0.5f);
                    else if (terrain == VOXEL_SHAPE_LOW) glColor4f(1.0f, 1.0f, 0.0f, 0.5f);
                    else if (terrain == VOXEL_SHAPE_FLAT) glColor4f(0.0f, 0.0f, 1.0f, 0.2f);
                    else glColor4f(1.0f, 1.0f, 1.0f, 0.1f); // Objects/Furniture

                    glVertex3f(wx,      0.01f, wz);
                    glVertex3f(wx+1.0f, 0.01f, wz);
                    glVertex3f(wx+1.0f, 0.01f, wz+1.0f);
                    glVertex3f(wx,      0.01f, wz+1.0f);
                    glEnd();
                    glDisable(GL_BLEND);
                } else {
                    extern bool VoxelMesh_IsWallConsumed(int x, int y);
                    if (!VoxelMesh_IsWallConsumed(x, y)) {
                        VoxelMesh_DrawTile(x, y, terrain, metatileId);
                    }
                }
            }
        }
    }

    // Movement debug log removed

    // ---- Object Events (NPCs, Player, etc.) ----
    glDisable(GL_TEXTURE_2D);
    
    // Sort object events by Z (depth) so things in front draw last, or just let depth testing handle it.
    // We'll rely on depth testing and alpha testing for now.
    for (int i = 0; i < 16; i++) {
        struct ObjectEvent *obj = &gObjectEvents[i];
        if (!obj->active) continue;
        
        struct Sprite *sprite = &gSprites[obj->spriteId];
        if (sprite->invisible) continue;
        
        UpdateObjectSpriteTexture(sprite, &sObjectEventTex[i], &sObjectEventW[i], &sObjectEventH[i]);
        
        float objX = 0.0f, objZ = 0.0f;
        if (GetVoxelObjectWorldPos(obj, &objX, &objZ)) {
            float bX = objX + 0.5f;
            float bZ = objZ + 0.5f;
            VoxelMesh_DrawPlayerBillboard(bX, 0.0f, bZ,
                                          sCamera.x, sCamera.y, sCamera.z,
                                          sObjectEventTex[i], sObjectEventW[i], sObjectEventH[i]);
        }
    }

    // ---- 2D UI Overlay ----
    extern void DrawFrame(uint16_t *pixels);
    uint16_t oldDispCnt = REG_DISPCNT;
    // Disable BG1, BG2, BG3, and OBJ for the UI overlay to only show text boxes / menus (usually BG0).
    // Start menu cursor is sometimes OBJ, but if we enable OBJ we see the player sprite, so we hide OBJs for now.
    REG_DISPCNT &= ~(0x0200 | 0x0400 | 0x0800 | 0x1000); 

    static uint16_t gbaImage[240 * 160];
    static uint32_t uiImage[240 * 160];
    memset(gbaImage, 0, sizeof(gbaImage));
    DrawFrame(gbaImage);
    REG_DISPCNT = oldDispCnt;

    uint16_t backdrop = *(uint16_t *)PLTT;
    for (int i = 0; i < 240 * 160; i++) {
        uint16_t color = gbaImage[i];
        if (color == backdrop || color == 0) { // 0 is also often transparent
            uiImage[i] = 0; // transparent
        } else {
            uint32_t r = (color & 0x1F) * 255 / 31;
            uint32_t g = ((color >> 5) & 0x1F) * 255 / 31;
            uint32_t b = ((color >> 10) & 0x1F) * 255 / 31;
            uiImage[i] = r | (g << 8) | (b << 16) | (255u << 24);
        }
    }

    static GLuint sUiTex = 0;
    if (!sUiTex) {
        glGenTextures(1, &sUiTex);
        glBindTexture(GL_TEXTURE_2D, sUiTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sUiTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 240, 160, 0, GL_RGBA, GL_UNSIGNED_BYTE, uiImage);

    // Setup 2D ortho projection for UI
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
    glEnd();

    SDL_GL_SwapWindow(sdlWindow);
}

void VoxelRenderer_Shutdown(void)
{
    // Nothing to do for GL immediate mode
}

bool VoxelRenderer_Init(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[Voxel] GL vendor: %s\n", (const char *)glGetString(GL_VENDOR));
    printf("[Voxel] GL renderer: %s\n", (const char *)glGetString(GL_RENDERER));
    printf("[Voxel] GL version: %s\n", (const char *)glGetString(GL_VERSION));

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f); // Sky blue

    VoxelCamera_Init(&sCamera);

    sNeedCameraSnap = true;
    sLastMapLayoutId = 0xFFFF;
    sLastMapGroup = 0xFF;
    sLastMapNum = 0xFF;

    return true;
}

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2

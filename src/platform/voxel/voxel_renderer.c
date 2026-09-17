#include "voxel_renderer.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "global.h"
#include "platform.h"
#include "fieldmap.h"
#include "palette.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "field_effect_helpers.h"
#include "constants/global.h"
#ifdef __ANDROID__
#include "gles1_compat.h"
#include <SDL.h>
#else
#include <GL/gl.h>
#include <SDL2/SDL.h>
#endif
#include "voxel_world.h"
#include "voxel_atlas.h"
#include "voxel_camera.h"
#include "voxel_mesh.h"

extern SDL_Window *sdlWindow;
extern unsigned char PLTT[];
extern unsigned char VRAM_[];
extern void DrawFrame(uint16_t *pixels);

bool gVoxelFirstPersonMode = false;
unsigned gVoxelDebugFlags = 0;
#define DBG_NO_XRAY    1u
#define DBG_NO_GHOST   2u
#define DBG_NO_TILT    4u
#define DBG_NO_SHADOW  8u
#define DBG_NO_OVERLAY 16u
#define DBG_NO_LIVEATLAS 32u
#define DBG_DUMP_ONCE  64u
#define DBG_DUMP_JSON  128u   // write the world/mesh/effects dump (Android: internal storage)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static VoxelCamera sCamera;
static bool sNeedCameraSnap = true;
static u8 sLastGroup = 0xFF, sLastNum = 0xFF;

static GLuint sAtlasTex[MAX_VOXEL_MAP_INSTANCES];
static GLuint sLiveAtlasSpare = 0;        // the current map's atlas alternates with this
static const struct MapLayout *sAtlasLayout[MAX_VOXEL_MAP_INSTANCES];
static VoxelMesh sMeshes[MAX_VOXEL_MAP_INSTANCES];
static uint32_t *sAtlasPixels = NULL;     // 512x512 live scratch (the composed quadrant)
static uint32_t sVramChecksum = 0;
static bool sGlInvalid = true;

static GLuint sScreenTex = 0;             // 2D fallback / UI overlay
static GLuint sOverlayTex[2] = {0, 0};    // ping-pong: never re-specify an in-flight texture
static int sOverlayFlip = 0;
static GLuint sShadowTex = 0;
static GLuint sObjTex[OBJECT_EVENTS_COUNT];
static int sObjW[OBJECT_EVENTS_COUNT], sObjH[OBJECT_EVENTS_COUNT];
static uint32_t sObjHash[OBJECT_EVENTS_COUNT];   // what the texture currently holds

static char sScreenshotPath[512];
static bool sScreenshotPending = false;
static char sDumpPath[512];
static bool sDumpPending = false;
static GLuint sEffTex[MAX_SPRITES];
static int sEffW[MAX_SPRITES], sEffH[MAX_SPRITES];
static uint32_t sEffHash[MAX_SPRITES];
static int sAtlasUploads = 0, sSpriteUploads = 0, sOverlayUploads = 0;   // telemetry

static uint16_t sGbaImage[MAX_RENDER_WIDTH * DISPLAY_HEIGHT];
static uint32_t sRgbaImage[MAX_RENDER_WIDTH * DISPLAY_HEIGHT];

// ---------------------------------------------------------------------------
// Sprites (object events) decoded from OBJ VRAM
// ---------------------------------------------------------------------------
static void GetSpriteDimensions(u8 shape, u8 size, int *w, int *h)
{
    static const int dims[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}},
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
    };
    if (shape > 2) shape = 0;
    *w = dims[shape][size & 3][0];
    *h = dims[shape][size & 3][1];
}

// Re-specifying a texture the GPU is still reading from the previous frame
// stalls the driver, so a sprite is only uploaded when its OAM, palette or
// tile bytes actually changed (animation frames are copied into the same
// VRAM slot, hence the byte hash).
static uint32_t SpriteContentHash(const struct Sprite *sprite, int w, int h)
{
    uint32_t hash = 2166136261u;
    const u8 *tiles = VRAM_ + 0x10000 + sprite->oam.tileNum * 32;
    const u8 *pal = PLTT + 0x200 + sprite->oam.paletteNum * 32;
    int bytes = w * h / 2, i;
    if (sprite->oam.tileNum * 32 + bytes > 0x8000)
        bytes = 0;
    for (i = 0; i < bytes; i += 4)
        hash = (hash ^ *(const uint32_t *)(tiles + i)) * 16777619u;
    for (i = 0; i < 32; i += 4)
        hash = (hash ^ *(const uint32_t *)(pal + i)) * 16777619u;
    hash = (hash ^ sprite->oam.tileNum) * 16777619u;
    hash = (hash ^ sprite->oam.paletteNum) * 16777619u;
    hash = (hash ^ (sprite->oam.matrixNum & ST_OAM_MNUM_FLIP_MASK)) * 16777619u;
    hash = (hash ^ (sprite->oam.shape | (sprite->oam.size << 2))) * 16777619u;
    return hash ? hash : 1;
}

static void UpdateObjectSpriteTexture(const struct Sprite *sprite, GLuint *texOut, int *wOut, int *hOut, uint32_t *hashInOut)
{
    int w, h, tilesX, tilesY, tx, ty, px, py;
    u8 *pixels;
    u16 tileBase = sprite->oam.tileNum;
    u16 palBase = sprite->oam.paletteNum;
    const u16 *palData = (const u16 *)(PLTT + 0x200 + palBase * 32);
    const u8 *vramBase = VRAM_ + 0x10000;
    bool hFlip = false, vFlip = false;
    uint32_t hash;

    GetSpriteDimensions(sprite->oam.shape, sprite->oam.size, &w, &h);
    *wOut = w;
    *hOut = h;
    hash = SpriteContentHash(sprite, w, h);
    if (*texOut != 0 && hash == *hashInOut)
        return;
    *hashInOut = hash;
    sSpriteUploads++;
    pixels = malloc(w * h * 4);
    if (pixels == NULL)
        return;
    if (sprite->oam.affineMode == 0)
    {
        hFlip = (sprite->oam.matrixNum & ST_OAM_HFLIP) != 0;
        vFlip = (sprite->oam.matrixNum & ST_OAM_VFLIP) != 0;
    }
    tilesX = w / 8;
    tilesY = h / 8;
    for (ty = 0; ty < tilesY; ty++)
        for (tx = 0; tx < tilesX; tx++)
        {
            int offset = (tileBase + ty * tilesX + tx) * 32;
            const u8 *tile;
            if (offset >= 0x8000) offset = 0;
            tile = vramBase + offset;
            for (py = 0; py < 8; py++)
                for (px = 0; px < 8; px++)
                {
                    u8 idx = tile[(py * 8 + px) / 2];
                    int ox = tx * 8 + px, oy = ty * 8 + py;
                    u8 *out;
                    idx = (px & 1) ? (idx >> 4) : (idx & 0xF);
                    if (hFlip) ox = w - 1 - ox;
                    if (vFlip) oy = h - 1 - oy;
                    out = pixels + (oy * w + ox) * 4;
                    if (idx == 0)
                    {
                        out[0] = out[1] = out[2] = out[3] = 0;
                    }
                    else
                    {
                        u16 c = palData[idx];
                        out[0] = (c & 0x1F) * 255 / 31;
                        out[1] = ((c >> 5) & 0x1F) * 255 / 31;
                        out[2] = ((c >> 10) & 0x1F) * 255 / 31;
                        out[3] = 255;
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

// Step durations matching sStepTimes[] in event_object_movement.c.
static const int sVoxelStepTimes[] = {16, 8, 6, 4, 2};

static float GetMovementProgress(const struct ObjectEvent *obj, const struct Sprite *sprite)
{
    int speed, timer, stepLen;
    float t;
    if (!obj->singleMovementActive && !obj->heldMovementActive)
        return 1.0f;
    speed = (int)(u16)sprite->data[4];
    timer = (int)(u16)sprite->data[5];
    if (speed < 0 || speed > 4)
        return 1.0f;
    stepLen = sVoxelStepTimes[speed];
    t = (float)timer / (float)stepLen;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

// Interpolated world position of an object, and the height its feet are at.
static bool GetObjectWorldPos(const struct ObjectEvent *obj, float *outX, float *outY, float *outZ)
{
    const struct Sprite *sprite;
    float prevX, prevZ, curX, curZ, t, hPrev, hCur;
    if (obj == NULL || !obj->active || obj->spriteId >= MAX_SPRITES)
        return false;
    sprite = &gSprites[obj->spriteId];
    prevX = (float)(obj->previousCoords.x - MAP_OFFSET);
    prevZ = (float)(obj->previousCoords.y - MAP_OFFSET_Y);
    curX = (float)(obj->currentCoords.x - MAP_OFFSET);
    curZ = (float)(obj->currentCoords.y - MAP_OFFSET_Y);
    t = GetMovementProgress(obj, sprite);
    hPrev = VoxelWorld_WalkHeightAt(obj->previousCoords.x - MAP_OFFSET, obj->previousCoords.y - MAP_OFFSET_Y);
    hCur = VoxelWorld_WalkHeightAt(obj->currentCoords.x - MAP_OFFSET, obj->currentCoords.y - MAP_OFFSET_Y);
    *outX = prevX + (curX - prevX) * t;
    *outZ = prevZ + (curZ - prevZ) * t;
    *outY = hPrev + (hCur - hPrev) * t;
    // ledge hops and bounces ride on the sprite's own y offset
    if (sprite->y2 < 0)
        *outY += (float)(-sprite->y2) / 16.0f;
    return true;
}

// ---------------------------------------------------------------------------
// GL resources
// ---------------------------------------------------------------------------
static GLuint MakeTexture(void)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

static void EnsureShadowTexture(void)
{
    uint8_t px[32 * 32 * 4];
    int x, y;
    if (sShadowTex)
        return;
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++)
        {
            float dx = (x + 0.5f - 16.0f) / 16.0f, dy = (y + 0.5f - 16.0f) / 16.0f;
            float d = sqrtf(dx * dx + dy * dy);
            float a = d >= 1.0f ? 0.0f : (1.0f - d) * 1.6f;
            uint8_t *p = &px[(y * 32 + x) * 4];
            if (a > 1.0f) a = 1.0f;
            p[0] = p[1] = p[2] = 0;
            p[3] = (uint8_t)(a * 255.0f);
        }
    sShadowTex = MakeTexture();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static void DropGlResources(void)
{
    int i;
    for (i = 0; i < MAX_VOXEL_MAP_INSTANCES; i++)
    {
        if (sAtlasTex[i]) glDeleteTextures(1, &sAtlasTex[i]);
        sAtlasTex[i] = 0;
        sAtlasLayout[i] = NULL;
    }
    if (sLiveAtlasSpare) glDeleteTextures(1, &sLiveAtlasSpare);
    sLiveAtlasSpare = 0;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        if (sObjTex[i]) glDeleteTextures(1, &sObjTex[i]);
        sObjTex[i] = 0;
        sObjHash[i] = 0;
    }
    if (sScreenTex) glDeleteTextures(1, &sScreenTex);
    if (sShadowTex) glDeleteTextures(1, &sShadowTex);
    for (i = 0; i < 2; i++)
    {
        if (sOverlayTex[i]) glDeleteTextures(1, &sOverlayTex[i]);
        sOverlayTex[i] = 0;
    }
    for (i = 0; i < MAX_SPRITES; i++)
    {
        if (sEffTex[i]) glDeleteTextures(1, &sEffTex[i]);
        sEffTex[i] = 0;
        sEffHash[i] = 0;
    }
    sScreenTex = 0;
    sShadowTex = 0;
    sVramChecksum = 0;
}

void VoxelRenderer_InvalidateGL(void)
{
    // The context may have been recreated: forget every name we held and
    // rebuild on the next frame.
    int i;
    for (i = 0; i < MAX_VOXEL_MAP_INSTANCES; i++) { sAtlasTex[i] = 0; sAtlasLayout[i] = NULL; }
    sLiveAtlasSpare = 0;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++) { sObjTex[i] = 0; sObjHash[i] = 0; }
    for (i = 0; i < MAX_SPRITES; i++) { sEffTex[i] = 0; sEffHash[i] = 0; }
    for (i = 0; i < MAX_VOXEL_MAP_INSTANCES; i++) VoxelMesh_InvalidateGL(&sMeshes[i]);
    sOverlayTex[0] = sOverlayTex[1] = 0;
    sScreenTex = 0;
    sShadowTex = 0;
    sVramChecksum = 0;
    sGlInvalid = true;
}

// The instance's 1024x1024 sheet (composed, ground, carved and top-layer
// quadrants) goes up whole when the map changes; afterwards only the
// composed quadrant is re-decoded live from VRAM/palette RAM so tile
// animations and fades show, into the spare texture so the upload never
// hits the texture in flight.
static void UploadFullAtlas(GLuint tex, const VoxelInstance *inst)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VOXEL_ATLAS_SIZE, VOXEL_ATLAS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, inst->pixels);
    sAtlasUploads++;
}

static void UploadAtlas(int i, const VoxelInstance *inst, bool fromVram)
{
    const uint16_t *live = NULL;
    int liveCount = 0;
    if (inst->pixels == NULL)
        return;
    if (i == 0 && gBackupMapLayout.map)
    {
        live = gBackupMapLayout.map;
        liveCount = gBackupMapLayout.width * gBackupMapLayout.height;
    }
    if (!fromVram)
    {
        if (sAtlasTex[i] == 0)
            sAtlasTex[i] = MakeTexture();
        UploadFullAtlas(sAtlasTex[i], inst);
        if (i == 0)
        {
            if (sLiveAtlasSpare == 0)
                sLiveAtlasSpare = MakeTexture();
            UploadFullAtlas(sLiveAtlasSpare, inst);
        }
        sAtlasLayout[i] = inst->header->mapLayout;
        return;
    }
    if (sAtlasTex[0] == 0 || sLiveAtlasSpare == 0 || sAtlasLayout[0] != inst->header->mapLayout)
        UploadAtlas(0, inst, false);
    VoxelAtlas_DecodeFromVram(inst->header->mapLayout, live, liveCount, sAtlasPixels);
    sAtlasUploads++;
    {
        GLuint t = sLiveAtlasSpare;
        sLiveAtlasSpare = sAtlasTex[0];
        sAtlasTex[0] = t;
    }
    glBindTexture(GL_TEXTURE_2D, sAtlasTex[0]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, VOXEL_ATLAS_QUAD, VOXEL_ATLAS_QUAD, GL_RGBA, GL_UNSIGNED_BYTE, sAtlasPixels);
}

// ---------------------------------------------------------------------------
// Screen-space quads (2D fallback, UI overlay)
// ---------------------------------------------------------------------------
static void ConvertFrame(int width)
{
    int i, n = width * DISPLAY_HEIGHT;
    for (i = 0; i < n; i++)
    {
        uint16_t c = sGbaImage[i];
        uint32_t r = (c & 0x1F) * 255 / 31;
        uint32_t g = ((c >> 5) & 0x1F) * 255 / 31;
        uint32_t b = ((c >> 10) & 0x1F) * 255 / 31;
        sRgbaImage[i] = r | (g << 8) | (b << 16) | (255u << 24);
    }
}

// The frame's rectangle inside the window, keeping the GBA aspect (the
// widened frame when widescreen is on), in 0..1 window space.
static void FrameRect(int winW, int winH, float *x0, float *y0, float *x1, float *y1)
{
    float winAspect = (float)winW / (float)winH;
    float frameAspect = (float)gRenderWidth / (float)DISPLAY_HEIGHT;
    if (winAspect > frameAspect)
    {
        float wf = frameAspect / winAspect;
        *x0 = (1.0f - wf) * 0.5f; *x1 = *x0 + wf;
        *y0 = 0.0f; *y1 = 1.0f;
    }
    else
    {
        float hf = winAspect / frameAspect;
        *y0 = (1.0f - hf) * 0.5f; *y1 = *y0 + hf;
        *x0 = 0.0f; *x1 = 1.0f;
    }
}

static void Begin2D(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
}

static void ScreenQuad(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, y0);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x1, y0);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x1, y1);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x0, y1);
    glEnd();
}

static void UploadScreenTexture(int width)
{
    if (sScreenTex == 0)
        sScreenTex = MakeTexture();
    glBindTexture(GL_TEXTURE_2D, sScreenTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, DISPLAY_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, sRgbaImage);
}

static void RenderFallback2D(void)
{
    int winW, winH;
    float x0, y0, x1, y1;
    int pixelCount = gRenderWidth * DISPLAY_HEIGHT;

    memset(sGbaImage, 0, pixelCount * sizeof(sGbaImage[0]));
    DrawFrame(sGbaImage);
    REG_VCOUNT = 161;
    ConvertFrame(gRenderWidth);

    SDL_GL_GetDrawableSize(sdlWindow, &winW, &winH);
    glViewport(0, 0, winW, winH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Begin2D();
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    UploadScreenTexture(gRenderWidth);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    FrameRect(winW, winH, &x0, &y0, &x1, &y1);
    ScreenQuad(x0, y0, x1, y1);
}

// The GBA's text layer (BG0: dialogue, menus, the map name) laid over the 3D
// scene, keyed on the backdrop colour.
static void RenderUiOverlay(int winW, int winH)
{
    uint16_t oldDispCnt = REG_DISPCNT;
    uint16_t backdrop = *(uint16_t *)PLTT;
    int i, pixelCount = gRenderWidth * DISPLAY_HEIGHT;
    float x0, y0, x1, y1;
    bool anyVisible = false;
    GLuint tex;

    // BG1-3 and OBJ off: only the text layer.
    REG_DISPCNT &= ~(DISPCNT_BG1_ON | DISPCNT_BG2_ON | DISPCNT_BG3_ON | DISPCNT_OBJ_ON);
    memset(sGbaImage, 0, pixelCount * sizeof(sGbaImage[0]));
    DrawFrame(sGbaImage);
    REG_DISPCNT = oldDispCnt;

    for (i = 0; i < pixelCount; i++)
    {
        uint16_t c = sGbaImage[i];
        if (c == backdrop || c == 0)
            sRgbaImage[i] = 0;
        else
        {
            uint32_t r = (c & 0x1F) * 255 / 31;
            uint32_t g = ((c >> 5) & 0x1F) * 255 / 31;
            uint32_t b = ((c >> 10) & 0x1F) * 255 / 31;
            sRgbaImage[i] = r | (g << 8) | (b << 16) | (255u << 24);
            anyVisible = true;
        }
    }
    if (!anyVisible)
        return;   // nothing on the text layer: no upload, no draw
    Begin2D();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    sOverlayFlip ^= 1;
    if (sOverlayTex[sOverlayFlip] == 0)
        sOverlayTex[sOverlayFlip] = MakeTexture();
    tex = sOverlayTex[sOverlayFlip];
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gRenderWidth, DISPLAY_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, sRgbaImage);
    sOverlayUploads++;
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    FrameRect(winW, winH, &x0, &y0, &x1, &y1);
    ScreenQuad(x0, y0, x1, y1);
    glDisable(GL_BLEND);
}

// The palette fade, as a tint over everything the game did not fade itself
// (connected maps drawn from ROM art, the sky).
static float FadeAmount(void)
{
    return (float)gPaletteFade.y / 16.0f;
}

static void FadeColor(float *r, float *g, float *b)
{
    uint16_t c = gPaletteFade.blendColor;
    *r = (c & 0x1F) / 31.0f;
    *g = ((c >> 5) & 0x1F) / 31.0f;
    *b = ((c >> 10) & 0x1F) / 31.0f;
}

// ---------------------------------------------------------------------------
// Objects: billboards leaning back toward the camera, with a soft shadow
// ---------------------------------------------------------------------------
#define BILLBOARD_TILT 0.42f   // radians the sprite leans back

static void DrawBillboard(float wx, float wy, float wz, GLuint tex, int w, int h, float alpha)
{
    float width = w / 16.0f, height = h / 16.0f, hw = width * 0.5f;
    float tilt = (gVoxelDebugFlags & DBG_NO_TILT) ? 0.0f : BILLBOARD_TILT;
    float topZ = wz - height * sinf(tilt);
    float topY = wy + height * cosf(tilt);
    // Emerald draws an object's feet a few pixels above its cell's bottom
    // edge; a 16px sprite's origin sits at the cell centre.
    float z = wz + 0.5f;
    (void)topZ;
    if (alpha >= 0.0f)
        glColor4f(1.0f, 1.0f, 1.0f, alpha);   // negative: the caller set a tint
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(wx - hw, wy, z);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(wx + hw, wy, z);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(wx + hw, topY, z - height * sinf(tilt));
    glTexCoord2f(0.0f, 0.0f); glVertex3f(wx - hw, topY, z - height * sinf(tilt));
    glEnd();
}

static void DrawShadow(float wx, float wy, float wz, float radius)
{
    glBindTexture(GL_TEXTURE_2D, sShadowTex);
    glColor4f(1.0f, 1.0f, 1.0f, 0.42f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(wx - radius, wy + 0.02f, wz + 0.5f - radius * 0.7f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(wx + radius, wy + 0.02f, wz + 0.5f - radius * 0.7f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(wx + radius, wy + 0.02f, wz + 0.5f + radius * 0.7f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(wx - radius, wy + 0.02f, wz + 0.5f + radius * 0.7f);
    glEnd();
}

typedef struct {
    float x, y, z;
    GLuint tex;
    int w, h;
    bool valid;
} ObjectDraw;

static void CollectObjects(ObjectDraw *draws)
{
    int i;
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        struct ObjectEvent *obj = &gObjectEvents[i];
        struct Sprite *sprite;
        ObjectDraw *d = &draws[i];
        d->valid = false;
        if (!obj->active || obj->invisible || obj->spriteId >= MAX_SPRITES)
            continue;
        sprite = &gSprites[obj->spriteId];
        if (!sprite->inUse || sprite->invisible)
            continue;
        UpdateObjectSpriteTexture(sprite, &sObjTex[i], &sObjW[i], &sObjH[i], &sObjHash[i]);
        if (!GetObjectWorldPos(obj, &d->x, &d->y, &d->z))
            continue;
        d->x += 0.5f;
        d->tex = sObjTex[i];
        d->w = sObjW[i];
        d->h = sObjH[i];
        d->valid = true;
    }
}

// ---------------------------------------------------------------------------
// Field effects: the grass rustle, footprints, ripples, splashes, the surf
// blob -- sprites the game draws at map positions without an object event.
// Reflections (a copy of an object's own tiles drawn under it) are the one
// kind that has no place in 3D; shadows are drawn by the renderer itself.
// ---------------------------------------------------------------------------
typedef struct {
    float x, y, z;
    GLuint tex;
    int w, h;
    int cellX, cellY;
    int sprite;
    bool upright;
    bool valid;
} EffectDraw;

static int sReflectionsSkipped = 0;

static int CollectEffects(EffectDraw *out)
{
    bool isObj[MAX_SPRITES];
    const struct Sprite *ps = NULL;
    const struct ObjectEvent *player = NULL;
    int i, k, n = 0;
    float px = 0.0f, pz = 0.0f, dummyY;

    memset(isObj, 0, sizeof(isObj));
    sReflectionsSkipped = 0;
    for (k = 0; k < OBJECT_EVENTS_COUNT; k++)
        if (gObjectEvents[k].active && gObjectEvents[k].spriteId < MAX_SPRITES)
            isObj[gObjectEvents[k].spriteId] = true;
    if (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT)
    {
        player = &gObjectEvents[gPlayerAvatar.objectEventId];
        if (player->active && player->spriteId < MAX_SPRITES)
            ps = &gSprites[player->spriteId];
        px = (float)(player->currentCoords.x - MAP_OFFSET);
        pz = (float)(player->currentCoords.y - MAP_OFFSET_Y);
    }
    (void)dummyY;
    for (i = 0; i < MAX_SPRITES; i++)
    {
        const struct Sprite *sp = &gSprites[i];
        EffectDraw *d;
        bool reflection = false;
        int cellX, cellY;
        float zBias = -0.05f;
        if (!sp->inUse || sp->invisible || isObj[i] || !sp->coordOffsetEnabled)
            continue;
        for (k = 0; k < OBJECT_EVENTS_COUNT && !reflection; k++)
        {
            const struct ObjectEvent *o = &gObjectEvents[k];
            const struct Sprite *os;
            if (!o->active || o->spriteId >= MAX_SPRITES)
                continue;
            os = &gSprites[o->spriteId];
            if (sp->oam.tileNum == os->oam.tileNum && sp->oam.shape == os->oam.shape && sp->oam.size == os->oam.size)
                reflection = true;
        }
        if (reflection)
        {
            sReflectionsSkipped++;
            continue;
        }
        if (sp->callback == UpdateShadowFieldEffect)
            continue;
        d = &out[n];
        memset(d, 0, sizeof(*d));
        if (sp->callback == UpdateTallGrassFieldEffect || sp->callback == UpdateLongGrassFieldEffect)
        {
            // the effect records its own cell
            cellX = (int)(s16)sp->data[1] - MAP_OFFSET;
            cellY = (int)(s16)sp->data[2] - MAP_OFFSET_Y;
            d->upright = true;
            zBias = 0.12f;      // in front of the walker's feet, as the 2D layer orders it
        }
        else if (ps != NULL)
        {
            // every field effect and every object sprite share one
            // camera-relative frame: read the cell off the player's sprite
            int dxp = (sp->x + sp->x2) - (ps->x + ps->x2);
            int dyp = (sp->y + sp->y2) - (ps->y + ps->y2) + 8;
            cellX = (int)px + (int)floorf(dxp / 16.0f + 0.5f);
            cellY = (int)pz + (int)floorf(dyp / 16.0f + 0.5f);
            d->upright = sp->callback == UpdateSurfBlobFieldEffect || sp->callback == UpdateAshFieldEffect
                      || sp->callback == UpdateBubblesFieldEffect || sp->callback == UpdateSparkleFieldEffect
                      || sp->callback == WaitFieldEffectSpriteAnim || sp->callback == UpdateJumpImpactEffect;
        }
        else
            continue;
        UpdateObjectSpriteTexture(sp, &sEffTex[i], &sEffW[i], &sEffH[i], &sEffHash[i]);
        d->tex = sEffTex[i];
        d->w = sEffW[i];
        d->h = sEffH[i];
        d->cellX = cellX;
        d->cellY = cellY;
        d->sprite = i;
        d->x = (float)cellX + 0.5f;
        d->z = (float)cellY + zBias;
        d->y = VoxelWorld_WalkHeightAt(cellX, cellY);
        if (d->upright && sp->y2 < 0)
            d->y += (float)(-sp->y2) / 16.0f;
        d->valid = true;
        n++;
        if (n >= MAX_SPRITES)
            break;
    }
    return n;
}

// A sprite laid flat on the ground: footprints, ripples, sand.
static void DrawDecal(const EffectDraw *d)
{
    float hw = d->w / 32.0f, hh = d->h / 32.0f;
    float y = d->y + 0.03f;
    float cz = (float)d->cellY + 0.5f;
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, d->tex);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(d->x - hw, y, cz - hh);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(d->x + hw, y, cz - hh);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(d->x + hw, y, cz + hh);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(d->x - hw, y, cz + hh);
    glEnd();
}

// ---------------------------------------------------------------------------
// Screenshots and dumps (debug harness)
// ---------------------------------------------------------------------------
void VoxelRenderer_RequestDumpJson(const char *path)
{
    strncpy(sDumpPath, path, sizeof(sDumpPath) - 1);
    sDumpPath[sizeof(sDumpPath) - 1] = 0;
    sDumpPending = true;
}

static void WriteDumpJson(const EffectDraw *effects, int effectCount, const ObjectDraw *draws, float playerX, float playerY, float playerZ)
{
    FILE *f = fopen(sDumpPath, "w");
    int i;
    sDumpPending = false;
    if (f == NULL)
    {
        SDL_Log("[Voxel] dump: cannot write %s", sDumpPath);
        return;
    }
    fprintf(f, "{\"instances\":[");
    for (i = 0; i < gVoxelInstanceCount; i++)
    {
        fprintf(f, "%s\n{\"world\":", i ? "," : "");
        VoxelWorld_WriteCellsJson(f, &gVoxelInstances[i]);
        fprintf(f, ",\"mesh\":");
        VoxelMesh_WriteStatsJson(f, &sMeshes[i]);
        fprintf(f, "}");
    }
    fprintf(f, "],\n\"player\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f", playerX, playerY, playerZ);
    if (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT)
    {
        const struct ObjectEvent *o = &gObjectEvents[gPlayerAvatar.objectEventId];
        fprintf(f, ",\"cell\":[%d,%d],\"elevation\":%d", o->currentCoords.x - MAP_OFFSET, o->currentCoords.y - MAP_OFFSET_Y, o->currentElevation);
    }
    fprintf(f, "},\n\"effects\":{\"reflectionsSkipped\":%d,\"list\":[", sReflectionsSkipped);
    for (i = 0; i < effectCount; i++)
    {
        const EffectDraw *d = &effects[i];
        const struct Sprite *sp = &gSprites[d->sprite];
        const char *kind = "other";
        if (sp->callback == UpdateTallGrassFieldEffect) kind = "tallGrass";
        else if (sp->callback == UpdateLongGrassFieldEffect) kind = "longGrass";
        else if (sp->callback == UpdateSurfBlobFieldEffect) kind = "surfBlob";
        else if (sp->callback == UpdateFootprintsTireTracksFieldEffect) kind = "footprints";
        else if (sp->callback == UpdateAshFieldEffect) kind = "ash";
        else if (sp->callback == UpdateSandPileFieldEffect) kind = "sandPile";
        else if (sp->callback == WaitFieldEffectSpriteAnim) kind = "burst";
        fprintf(f, "%s{\"sprite\":%d,\"kind\":\"%s\",\"cell\":[%d,%d],\"w\":%d,\"h\":%d,\"upright\":%s,\"tile\":%d}",
                i ? "," : "", d->sprite, kind, d->cellX, d->cellY, d->w, d->h, d->upright ? "true" : "false", sp->oam.tileNum);
    }
    fprintf(f, "]},\n\"objects\":[");
    {
        int n = 0;
        for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
        {
            const struct ObjectEvent *o = &gObjectEvents[i];
            if (!draws[i].valid) continue;
            fprintf(f, "%s{\"i\":%d,\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"w\":%d,\"h\":%d,\"cell\":[%d,%d],\"gfx\":%d,\"invisible\":%d}",
                    n ? "," : "", i, draws[i].x, draws[i].y, draws[i].z, draws[i].w, draws[i].h,
                    o->currentCoords.x - MAP_OFFSET, o->currentCoords.y - MAP_OFFSET_Y, o->graphicsId, o->invisible);
            n++;
        }
    }
    fprintf(f, "],\n\"ghosted\":[");
    if (gVoxelInstanceCount > 0)
    {
        const VoxelMesh *m0 = &sMeshes[0];
        int n = 0;
        for (i = 0; i < m0->rangeCount; i++)
        {
            const VoxelMeshRange *rg = &m0->ranges[i];
            if (!m0->ghost[i]) continue;
            fprintf(f, "%s{\"i\":%d,\"x\":%d,\"z\":%d,\"rows\":%d,\"cols\":%d,\"first\":%d,\"count\":%d,\"top\":%.2f,\"base\":%.2f,\"building\":%d,\"verts\":[",
                    n ? "," : "", i, rg->worldX, rg->worldZ, rg->rows, rg->cols, rg->first, rg->count, rg->top, rg->base, rg->building);
            {
                int k, lim = rg->count < 2000 ? rg->count : 2000;
                for (k = 0; k < lim; k++)
                {
                    const VoxelVertex *vv = &m0->verts[rg->first + k];
                    fprintf(f, "%s[%.3f,%.3f,%.3f,%.4f,%.4f]", k ? "," : "", vv->x, vv->y, vv->z, vv->u, vv->v);
                }
            }
            fprintf(f, "]}");
            n++;
        }
    }
    fprintf(f, "],\n\"gl\":{\"error\":%u},\"camera\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"preset\":%d,\"pitch\":%.2f,\"distance\":%.2f,\"fov\":%.2f,\"sky\":\"%s\"}}\n",
            (unsigned)glGetError(), sCamera.x, sCamera.y, sCamera.z, sCamera.preset, sCamera.pitch, sCamera.distance, sCamera.fov,
            sCamera.preset == 1 ? "warm" : "day");
    fclose(f);
    // the current map's atlas beside it, alpha-0 pixels shown magenta
    if (gVoxelInstanceCount > 0 && gVoxelInstances[0].pixels)
    {
        char ppm[600];
        snprintf(ppm, sizeof(ppm), "%s.atlas.ppm", sDumpPath);
        f = fopen(ppm, "wb");
        if (f)
        {
            const uint32_t *px = gVoxelInstances[0].pixels;
            int n = VOXEL_ATLAS_SIZE * VOXEL_ATLAS_SIZE;
            fprintf(f, "P6\n%d %d\n255\n", VOXEL_ATLAS_SIZE, VOXEL_ATLAS_SIZE);
            for (i = 0; i < n; i++)
            {
                uint8_t rgb[3];
                uint32_t c = px[i];
                if ((c >> 24) == 0) { rgb[0] = 255; rgb[1] = 0; rgb[2] = 255; }
                else { rgb[0] = c & 0xFF; rgb[1] = (c >> 8) & 0xFF; rgb[2] = (c >> 16) & 0xFF; }
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
        }
    }
    SDL_Log("[Voxel] dump written to %s", sDumpPath);
}

void VoxelRenderer_RequestScreenshot(const char *path)
{
    strncpy(sScreenshotPath, path, sizeof(sScreenshotPath) - 1);
    sScreenshotPath[sizeof(sScreenshotPath) - 1] = 0;
    sScreenshotPending = true;
}

static void SaveScreenshot(int winW, int winH)
{
    uint8_t *px = malloc(winW * winH * 4);
    FILE *f;
    int y;
    if (px == NULL)
        return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, winW, winH, GL_RGBA, GL_UNSIGNED_BYTE, px);
    f = fopen(sScreenshotPath, "wb");
    if (f)
    {
        fprintf(f, "P6\n%d %d\n255\n", winW, winH);
        for (y = winH - 1; y >= 0; y--)
        {
            int x;
            for (x = 0; x < winW; x++)
                fwrite(&px[(y * winW + x) * 4], 1, 3, f);
        }
        fclose(f);
        printf("[Voxel] screenshot %s\n", sScreenshotPath);
    }
    free(px);
    sScreenshotPending = false;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void VoxelRenderer_HandleMouseMotion(int dx, int dy)
{
    if (gVoxelFirstPersonMode)
        VoxelCamera_FPSMouseMotion(&sCamera, dx, dy);
}

static void HandleMapChange(void)
{
    u8 group = gSaveBlock1Ptr->location.mapGroup;
    u8 num = gSaveBlock1Ptr->location.mapNum;
    int i;
    bool found = false;

    if (group == sLastGroup && num == sLastNum)
        return;
    // Crossing a connection: the map we walked into was already an instance,
    // so slide the camera by its origin rather than snapping.
    for (i = 0; i < gVoxelInstanceCount; i++)
    {
        if (gVoxelInstances[i].mapGroup == group && gVoxelInstances[i].mapNum == num)
        {
            float sx = (float)-gVoxelInstances[i].originX;
            float sz = (float)-gVoxelInstances[i].originY;
            sCamera.x += sx; sCamera.z += sz;
            sCamera.targetX += sx; sCamera.targetZ += sz;
            found = true;
            break;
        }
    }
    if (!found)
        sNeedCameraSnap = true;
    sLastGroup = group;
    sLastNum = num;
}

// Frame-time telemetry: printed every 600 frames so a device log answers
// whether the port keeps up (SDL_Log reaches logcat; printf does not).
static Uint64 sFrameTicks = 0, sFrameMax = 0, sWallStart = 0;
static int sFrameSamples = 0;

static void NoteFrameTime(Uint64 start)
{
    Uint64 now = SDL_GetPerformanceCounter();
    Uint64 dt = now - start;
    if (sWallStart == 0) sWallStart = start;
    sFrameTicks += dt;
    if (dt > sFrameMax) sFrameMax = dt;
    if (++sFrameSamples == 600)
    {
        double freq = (double)SDL_GetPerformanceFrequency();
        double wall = (double)(now - sWallStart) / freq;
        SDL_Log("[Voxel] render avg %.2f ms, max %.2f ms, %.1f fps; uploads atlas %d sprite %d overlay %d",
                (double)sFrameTicks / sFrameSamples * 1000.0 / freq, (double)sFrameMax * 1000.0 / freq,
                wall > 0.0 ? sFrameSamples / wall : 0.0, sAtlasUploads, sSpriteUploads, sOverlayUploads);
        sFrameTicks = 0; sFrameMax = 0; sFrameSamples = 0; sWallStart = 0;
        sAtlasUploads = sSpriteUploads = sOverlayUploads = 0;
    }
}

void VoxelRenderer_RenderFrame(void)
{
    int winW, winH, i;
    Uint64 frameStart = SDL_GetPerformanceCounter();
    float playerX = 0.0f, playerY = 0.0f, playerZ = 0.0f;
    ObjectDraw draws[OBJECT_EVENTS_COUNT];
    static EffectDraw effects[MAX_SPRITES];
    int effectCount = 0;
    float fade = FadeAmount();
    float fr, fg, fb;
    const Uint8 *keys;

    if (sAtlasPixels == NULL)
        sAtlasPixels = malloc(VOXEL_ATLAS_QUAD * VOXEL_ATLAS_QUAD * 4);

    keys = SDL_GetKeyboardState(NULL);
    (void)keys;

    if (!VoxelWorld_IsMapAvailable())
    {
        RenderFallback2D();
        SDL_GL_GetDrawableSize(sdlWindow, &winW, &winH);
        if (sScreenshotPending) SaveScreenshot(winW, winH);
        SDL_GL_SwapWindow(sdlWindow);
        sNeedCameraSnap = true;
        return;
    }

    REG_VCOUNT = 161;

    // ---- world model + meshes ----
    HandleMapChange();
    if (VoxelWorld_Update() || sGlInvalid)
    {
        if (sGlInvalid)
            DropGlResources();
        for (i = 0; i < MAX_VOXEL_MAP_INSTANCES; i++)
        {
            if (i < gVoxelInstanceCount)
            {
                VoxelMesh_BuildInstance(&sMeshes[i], &gVoxelInstances[i]);
                if (sAtlasLayout[i] != gVoxelInstances[i].header->mapLayout || sAtlasTex[i] == 0)
                    UploadAtlas(i, &gVoxelInstances[i], false);
            }
            else
            {
                VoxelMesh_Clear(&sMeshes[i]);
            }
        }
        sVramChecksum = 0;   // force a live decode of the current map
        sGlInvalid = false;
    }
    // The current map's art comes from VRAM/palette RAM so tile animations
    // and fades show; re-decode only when they changed.
    {
        uint32_t sum = (gVoxelDebugFlags & DBG_NO_LIVEATLAS) ? 1u : VoxelAtlas_VramChecksum();
        if (sum != sVramChecksum && gVoxelInstanceCount > 0)
        {
            UploadAtlas(0, &gVoxelInstances[0], true);
            sVramChecksum = sum;
        }
    }
    EnsureShadowTexture();

    if (gVoxelDebugFlags & DBG_DUMP_JSON)
    {
        gVoxelDebugFlags &= ~DBG_DUMP_JSON;
#ifdef __ANDROID__
        {
            const char *dir = SDL_AndroidGetInternalStoragePath();
            char path[512];
            snprintf(path, sizeof(path), "%s/voxel_dump.json", dir ? dir : "/data/local/tmp");
            VoxelRenderer_RequestDumpJson(path);
        }
#else
        if (!sDumpPending)
            VoxelRenderer_RequestDumpJson("/tmp/voxel_dump.json");
#endif
    }
    if (gVoxelDebugFlags & DBG_DUMP_ONCE)
    {
        gVoxelDebugFlags &= ~DBG_DUMP_ONCE;
        if (gVoxelInstanceCount > 0)
            VoxelWorld_DumpInstance(&gVoxelInstances[0]);
        VoxelWorld_DumpPlayerCell();
        SDL_Log("[Voxel] camera (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f) meshes verts %d ranges %d",
                sCamera.x, sCamera.y, sCamera.z, sCamera.targetX, sCamera.targetY, sCamera.targetZ,
                sMeshes[0].count, sMeshes[0].rangeCount);
    }

    // ---- player + camera ----
    {
        int preset = Platform_GetSetting(PLATFORM_SETTING_VOXEL_CAMERA) ? 1 : 0;
        if (preset != sCamera.preset && !gVoxelFirstPersonMode)
            VoxelCamera_ApplyPreset(&sCamera, preset);
    }
    if (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT)
        GetObjectWorldPos(&gObjectEvents[gPlayerAvatar.objectEventId], &playerX, &playerY, &playerZ);
    playerX += 0.5f;
    if (sNeedCameraSnap)
    {
        sCamera.targetX = playerX;
        sCamera.targetZ = playerZ + 0.5f;
        sCamera.targetY = playerY;
        sNeedCameraSnap = false;
    }
    if (gVoxelFirstPersonMode)
    {
        float fwd = 0.0f, right = 0.0f, up = 0.0f;
        if (keys[SDL_SCANCODE_W]) fwd = 1.0f;
        if (keys[SDL_SCANCODE_S]) fwd = -1.0f;
        if (keys[SDL_SCANCODE_D]) right = 1.0f;
        if (keys[SDL_SCANCODE_A]) right = -1.0f;
        if (keys[SDL_SCANCODE_SPACE]) up = 1.0f;
        if (keys[SDL_SCANCODE_LSHIFT]) up = -1.0f;
        VoxelCamera_FPSTick(&sCamera, fwd, right, up);
    }
    VoxelCamera_Update(&sCamera, playerX, playerY, playerZ + 0.5f);

    // ---- viewport + sky ----
    SDL_GL_GetDrawableSize(sdlWindow, &winW, &winH);
    glViewport(0, 0, winW, winH);
    FadeColor(&fr, &fg, &fb);
    {
        bool indoor = gVoxelInstanceCount > 0 && gVoxelInstances[0].indoor;
        float sr = 0.05f, sg = 0.05f, sb = 0.07f;
        glClearColor(sr + (fr - sr) * fade, sg + (fg - sg) * fade, sb + (fb - sb) * fade, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!indoor)
        {
            // Outdoors the backdrop is a sky: deeper blue above, hazy at the
            // horizon, tinted like everything else by a palette fade. The
            // diorama camera gets the model-railway evening: mauve over peach.
            float tr = 0.36f, tg = 0.62f, tb = 0.92f;
            float br = 0.78f, bg = 0.89f, bb = 0.98f;
            if (sCamera.preset == 1)
            {
                tr = 0.58f; tg = 0.50f; tb = 0.78f;
                br = 0.99f; bg = 0.85f; bb = 0.60f;
            }
            Begin2D();
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_BLEND);
            glBegin(GL_QUADS);
            glColor4f(tr + (fr - tr) * fade, tg + (fg - tg) * fade, tb + (fb - tb) * fade, 1.0f);
            glVertex2f(0.0f, 0.0f); glVertex2f(1.0f, 0.0f);
            glColor4f(br + (fr - br) * fade, bg + (fg - bg) * fade, bb + (fb - bb) * fade, 1.0f);
            glVertex2f(1.0f, 1.0f); glVertex2f(0.0f, 1.0f);
            glEnd();
            glEnable(GL_TEXTURE_2D);
        }
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    VoxelCamera_ApplyProjection(&sCamera, winW, winH);
    VoxelCamera_ApplyView(&sCamera);

    // ---- opaque world ----
    for (i = 0; i < gVoxelInstanceCount; i++)
    {
        VoxelMesh_ComputeGhosts(&sMeshes[i], playerX, playerY, playerZ, VoxelCamera_TanPitch(&sCamera));
        glBindTexture(GL_TEXTURE_2D, sAtlasTex[i]);
        VoxelMesh_DrawOpaque(&sMeshes[i]);
    }

    // ---- objects and field effects ----
    CollectObjects(draws);
    effectCount = CollectEffects(effects);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
        if (draws[i].valid && !(gVoxelDebugFlags & DBG_NO_SHADOW))
            DrawShadow(draws[i].x, draws[i].y, draws[i].z, 0.45f);
    // decals lie on the ground under everything that stands
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    for (i = 0; i < effectCount; i++)
        if (effects[i].valid && !effects[i].upright)
            DrawDecal(&effects[i]);
    glDepthMask(GL_TRUE);
    for (i = 0; i < effectCount; i++)
        if (effects[i].valid && effects[i].upright && effects[i].z < (float)effects[i].cellY + 0.5f)
            DrawBillboard(effects[i].x, effects[i].y, effects[i].z, effects[i].tex, effects[i].w, effects[i].h, 1.0f);
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
        if (draws[i].valid)
            DrawBillboard(draws[i].x, draws[i].y, draws[i].z, draws[i].tex, draws[i].w, draws[i].h, 1.0f);
    for (i = 0; i < effectCount; i++)
        if (effects[i].valid && effects[i].upright && effects[i].z >= (float)effects[i].cellY + 0.5f)
            DrawBillboard(effects[i].x, effects[i].y, effects[i].z, effects[i].tex, effects[i].w, effects[i].h, 1.0f);
    glDisable(GL_ALPHA_TEST);

    // ---- ghosted geometry (what stands between the camera and the player) ----
    glDepthMask(GL_FALSE);
    if (!(gVoxelDebugFlags & DBG_NO_GHOST))
        for (i = 0; i < gVoxelInstanceCount; i++)
        {
            glBindTexture(GL_TEXTURE_2D, sAtlasTex[i]);
            VoxelMesh_DrawGhosted(&sMeshes[i]);
        }
    // x-ray silhouette of the PLAYER through anything still hiding them
    // (the ghosting above usually clears the way; this is the fallback).
    // Other objects are left to the scenery: a walker behind a tree is
    // simply behind a tree, and drawing every occluded sliver of every
    // sprite through a round crown's grazing edge left stray blue lines.
    if (!(gVoxelDebugFlags & DBG_NO_XRAY) && gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT
        && draws[gPlayerAvatar.objectEventId].valid)
    {
        const ObjectDraw *pd = &draws[gPlayerAvatar.objectEventId];
        glDepthFunc(GL_GREATER);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.5f);
        glColor4f(0.6f, 0.8f, 1.0f, 0.35f);
        DrawBillboard(pd->x, pd->y, pd->z, pd->tex, pd->w, pd->h, -1.0f);
        glDisable(GL_ALPHA_TEST);
        glDepthFunc(GL_LEQUAL);
    }
    glDepthMask(GL_TRUE);

    // ---- fade tint for the ROM-drawn neighbours and the sky ----
    if (fade > 0.0f)
    {
        Begin2D();
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glColor4f(fr, fg, fb, fade * 0.85f);
        glBegin(GL_QUADS);
        glVertex2f(0.0f, 0.0f); glVertex2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f); glVertex2f(0.0f, 1.0f);
        glEnd();
        glEnable(GL_TEXTURE_2D);
    }

    // ---- GBA text layer ----
    if (!(gVoxelDebugFlags & DBG_NO_OVERLAY))
        RenderUiOverlay(winW, winH);

    if (sScreenshotPending)
        SaveScreenshot(winW, winH);
    if (sDumpPending)
        WriteDumpJson(effects, effectCount, draws, playerX, playerY, playerZ);
    NoteFrameTime(frameStart);
    SDL_GL_SwapWindow(sdlWindow);
}

void VoxelRenderer_Shutdown(void)
{
    int i;
    DropGlResources();
    for (i = 0; i < MAX_VOXEL_MAP_INSTANCES; i++)
        VoxelMesh_Free(&sMeshes[i]);
    free(sAtlasPixels);
    sAtlasPixels = NULL;
    VoxelWorld_Reset();
}

bool VoxelRenderer_Init(void)
{
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[Voxel] GL vendor: %s\n", (const char *)glGetString(GL_VENDOR));
    printf("[Voxel] GL renderer: %s\n", (const char *)glGetString(GL_RENDERER));
    printf("[Voxel] GL version: %s\n", (const char *)glGetString(GL_VERSION));
    printf("[Voxel] debug flags: notex=%d wire=%d noghost=%d nodepth=%d\n",
           getenv("EMERALD_VOXEL_NOTEX") != NULL, getenv("EMERALD_VOXEL_WIRE") != NULL,
           getenv("EMERALD_VOXEL_NOGHOST") != NULL, getenv("EMERALD_VOXEL_NODEPTH") != NULL);

    for (i = 0; i < MAX_VOXEL_MAP_INSTANCES; i++)
        VoxelMesh_Init(&sMeshes[i]);
    VoxelRenderer_InvalidateGL();
    VoxelCamera_Init(&sCamera);
    sNeedCameraSnap = true;
    sLastGroup = 0xFF;
    sLastNum = 0xFF;
    return true;
}

#endif // NATIVE_LINUX || __ANDROID__
#endif // PLATFORM_SDL2

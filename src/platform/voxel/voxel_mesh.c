#include "voxel_mesh.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#ifdef __ANDROID__
#include "gles1_compat.h"
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "fieldmap.h"

bool *gVoxelWallConsumed = NULL;

bool VoxelMesh_IsWallConsumed(int worldX, int worldY)
{
    // A simple coordinate hash map for consumed walls since the world coordinate space can be negative and large
    if (!gVoxelWallConsumed) return false;
    int hash = ((worldY + 4096) * 8192 + (worldX + 4096)) % (8192 * 8192); // naive hash mapped into a 1D array index (just an offset, assuming a bounded world area)
    // Wait, the coordinates can be from -500 to +500. A 2048x2048 array centered at 1024,1024 is enough!
    int cx = worldX + 1024;
    int cy = worldY + 1024;
    if (cx < 0 || cx >= 2048 || cy < 0 || cy >= 2048) return false;
    return gVoxelWallConsumed[cy * 2048 + cx];
}

void VoxelMesh_SetWallConsumed(int worldX, int worldY, bool consumed)
{
    if (!gVoxelWallConsumed) return;
    int cx = worldX + 1024;
    int cy = worldY + 1024;
    if (cx < 0 || cx >= 2048 || cy < 0 || cy >= 2048) return;
    gVoxelWallConsumed[cy * 2048 + cx] = consumed;
}

void VoxelMesh_BuildWallsForInstance(int instIdx, GLuint tex)
{
    if (gVoxelWallConsumed == NULL) {
        gVoxelWallConsumed = calloc(2048 * 2048, sizeof(bool));
    }

    if (instIdx == 0) {
        // clear array once per frame on the first instance
        memset(gVoxelWallConsumed, 0, 2048 * 2048 * sizeof(bool));
    }

    const struct VoxelMapInstance *inst = &gVoxelMapInstances[instIdx];
    int w = inst->header->mapLayout->width;
    int h = inst->header->mapLayout->height;

    float atlasW = 512.0f;
    float atlasH = 512.0f;

    glBindTexture(GL_TEXTURE_2D, tex);

    for (int ly = 0; ly < h; ly++) {
        for (int lx = 0; lx < w; lx++) {
            int x = lx + inst->originX;
            int y = ly + inst->originY;
            if (VoxelMesh_IsWallConsumed(x, y)) continue;

            extern bool VoxelStructure_IsTileInStructure(int worldX, int worldY);
            if (VoxelStructure_IsTileInStructure(x, y)) continue;

            VoxelVisualShape shape = VoxelWorld_ClassifyTile(x, y);

            if (shape == VOXEL_SHAPE_WALL || shape == VOXEL_SHAPE_BUILDING || shape == VOXEL_SHAPE_TREE) {
                // Find height of this vertical structure
                int heightInTiles = 1;
                while (true) {
                    VoxelVisualShape nShape = VoxelWorld_ClassifyTile(x, y - heightInTiles);
                    if (nShape == VOXEL_SHAPE_WALL || nShape == VOXEL_SHAPE_ROOF) {
                        heightInTiles++;
                        if (nShape == VOXEL_SHAPE_ROOF) break; 
                    } else {
                        break;
                    }
                }
                
                VoxelVisualShape sS = VoxelWorld_ClassifyTile(x, y + 1);
                VoxelVisualShape sN = VoxelWorld_ClassifyTile(x, y - heightInTiles);
                VoxelVisualShape sE = VoxelWorld_ClassifyTile(x + 1, y);
                VoxelVisualShape sW = VoxelWorld_ClassifyTile(x - 1, y);
                
                bool faceSouth = (sS != VOXEL_SHAPE_WALL && sS != VOXEL_SHAPE_ROOF && sS != VOXEL_SHAPE_VOID);
                bool faceNorth = (sN != VOXEL_SHAPE_WALL && sN != VOXEL_SHAPE_ROOF && sN != VOXEL_SHAPE_VOID);
                bool faceEast = (sE != VOXEL_SHAPE_WALL && sE != VOXEL_SHAPE_ROOF && sE != VOXEL_SHAPE_VOID);
                bool faceWest = (sW != VOXEL_SHAPE_WALL && sW != VOXEL_SHAPE_ROOF && sW != VOXEL_SHAPE_VOID);
                
                if (!faceSouth && !faceEast && !faceWest && !faceNorth) faceSouth = true; 
                
                float wx = (float)x;
                float wz = (float)y;
                float totalH = heightInTiles * 1.0f;
                
                float pWX = 0.0f, pWZ = 0.0f;
                VoxelWorld_GetPlayerWorldCoords(&pWX, &pWZ);
                bool isCutaway = (faceSouth && y > pWZ + 0.5f && shape != VOXEL_SHAPE_TREE);
                
                for (int i = 0; i < heightInTiles; i++) {
                    VoxelMesh_SetWallConsumed(x, y - i, true);
                }

                int topTileY = y - heightInTiles + 1;
                
                // Get the texture from the instance's own atlas!
                int topM;
                const struct VoxelMapInstance *topInst = VoxelWorld_GetMetatileIdAndInstance(x, topTileY, &topM);
                
                // If it belongs to a different instance, the atlas coordinates might be wrong.
                // For now, we assume it's in the current instance's atlas or just use the UVs.
                // In a perfect world, we'd bind topInst's texture here.
                float t_u0 = (topM % 32) * 16.0f / atlasW;
                float t_v0 = (topM / 32) * 16.0f / atlasH;
                float t_u1 = t_u0 + (16.0f / atlasW);
                float t_v1 = t_v0 + (16.0f / atlasH);

                glEnable(GL_TEXTURE_2D);
                glColor3f(1.0f, 1.0f, 1.0f);
                
                // TOP FACE (Textured with top-most tile)
                if (!isCutaway) {
                    glBegin(GL_QUADS);
                    glTexCoord2f(t_u0, t_v0); glVertex3f(wx,      totalH, wz);
                    glTexCoord2f(t_u1, t_v0); glVertex3f(wx+1.0f, totalH, wz);
                    glTexCoord2f(t_u1, t_v1); glVertex3f(wx+1.0f, totalH, wz+1.0f);
                    glTexCoord2f(t_u0, t_v1); glVertex3f(wx,      totalH, wz+1.0f);
                    glEnd();
                }

                glBegin(GL_QUADS);
                for (int i = 0; i < heightInTiles; i++) {
                    if (isCutaway && i > 0) continue;
                    int m = VoxelWorld_GetMetatileId(x, y - i);
                    float u0 = (m % 32) * 16.0f / atlasW;
                    float v0 = (m / 32) * 16.0f / atlasH;
                    float u1 = u0 + (16.0f / atlasW);
                    float v1 = v0 + (16.0f / atlasH);
                    
                    float yBottom = i * 1.0f;
                    float yTop = (i + 1) * 1.0f;
                    
                    if (faceSouth) {
                        glTexCoord2f(u0, v1); glVertex3f(wx,      yBottom, wz+1.0f);
                        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, yBottom, wz+1.0f);
                        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, yTop,    wz+1.0f);
                        glTexCoord2f(u0, v0); glVertex3f(wx,      yTop,    wz+1.0f);
                    }
                    if (faceNorth) {
                        glTexCoord2f(u1, v1); glVertex3f(wx,      yBottom, wz);
                        glTexCoord2f(u0, v1); glVertex3f(wx+1.0f, yBottom, wz);
                        glTexCoord2f(u0, v0); glVertex3f(wx+1.0f, yTop,    wz);
                        glTexCoord2f(u1, v0); glVertex3f(wx,      yTop,    wz);
                    }
                    if (faceEast) {
                        glTexCoord2f(u0, v1); glVertex3f(wx+1.0f, yBottom, wz+1.0f);
                        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, yBottom, wz);
                        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, yTop,    wz);
                        glTexCoord2f(u0, v0); glVertex3f(wx+1.0f, yTop,    wz+1.0f);
                    }
                    if (faceWest) {
                        glTexCoord2f(u1, v1); glVertex3f(wx, yBottom, wz+1.0f);
                        glTexCoord2f(u0, v1); glVertex3f(wx, yBottom, wz);
                        glTexCoord2f(u0, v0); glVertex3f(wx, yTop,    wz);
                        glTexCoord2f(u1, v0); glVertex3f(wx, yTop,    wz+1.0f);
                    }
                }
                glEnd();
            }
        }
    }
}

static void VoxelMesh_DrawFurniture(int mapX, int mapY, VoxelVisualShape shape, float u0, float v0, float u1, float v1)
{
    float wx = (float)mapX;
    float wz = (float)mapY;
    
    // Check neighbors for multi-tile furniture connection
    bool n_is_same = VoxelWorld_ClassifyTile(mapX, mapY - 1) == shape;
    bool s_is_same = VoxelWorld_ClassifyTile(mapX, mapY + 1) == shape;
    bool e_is_same = VoxelWorld_ClassifyTile(mapX + 1, mapY) == shape;
    bool w_is_same = VoxelWorld_ClassifyTile(mapX - 1, mapY) == shape;

    if (shape == VOXEL_SHAPE_TABLE) {
        float tHeight = 0.5f;
        float tThick = 0.1f;
        
        // Table top
        glColor3f(1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex3f(wx,      tHeight, wz);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, tHeight, wz);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, tHeight, wz+1.0f);
        glTexCoord2f(u0, v1); glVertex3f(wx,      tHeight, wz+1.0f);
        glEnd();
        
        // Table top sides
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.4f, 0.3f, 0.2f); // Dark wood color
        glBegin(GL_QUADS);
        if (!n_is_same) {
            glVertex3f(wx+1.0f, tHeight-tThick, wz); glVertex3f(wx,      tHeight-tThick, wz);
            glVertex3f(wx,      tHeight,        wz); glVertex3f(wx+1.0f, tHeight,        wz);
        }
        if (!s_is_same) {
            glVertex3f(wx,      tHeight-tThick, wz+1.0f); glVertex3f(wx+1.0f, tHeight-tThick, wz+1.0f);
            glVertex3f(wx+1.0f, tHeight,        wz+1.0f); glVertex3f(wx,      tHeight,        wz+1.0f);
        }
        if (!e_is_same) {
            glVertex3f(wx+1.0f, tHeight-tThick, wz+1.0f); glVertex3f(wx+1.0f, tHeight-tThick, wz);
            glVertex3f(wx+1.0f, tHeight,        wz);      glVertex3f(wx+1.0f, tHeight,        wz+1.0f);
        }
        if (!w_is_same) {
            glVertex3f(wx,      tHeight-tThick, wz);      glVertex3f(wx,      tHeight-tThick, wz+1.0f);
            glVertex3f(wx,      tHeight,        wz+1.0f); glVertex3f(wx,      tHeight,        wz);
        }
        glEnd();
        
        // Legs (only on absolute corners of the connected structure)
        glColor3f(0.3f, 0.2f, 0.15f);
        float lW = 0.1f; // leg width
        glBegin(GL_QUADS);
        // Top-left leg
        if (!n_is_same && !w_is_same) {
            glVertex3f(wx,      0.0f, wz); glVertex3f(wx+lW,   0.0f, wz);
            glVertex3f(wx+lW,   tHeight-tThick, wz); glVertex3f(wx,      tHeight-tThick, wz);
            glVertex3f(wx,      0.0f, wz+lW); glVertex3f(wx+lW,   0.0f, wz+lW);
            glVertex3f(wx+lW,   tHeight-tThick, wz+lW); glVertex3f(wx,      tHeight-tThick, wz+lW);
            glVertex3f(wx+lW,   0.0f, wz); glVertex3f(wx+lW,   0.0f, wz+lW);
            glVertex3f(wx+lW,   tHeight-tThick, wz+lW); glVertex3f(wx+lW,   tHeight-tThick, wz);
        }
        // Top-right leg
        if (!n_is_same && !e_is_same) {
            glVertex3f(wx+1.0f-lW, 0.0f, wz); glVertex3f(wx+1.0f,    0.0f, wz);
            glVertex3f(wx+1.0f,    tHeight-tThick, wz); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz);
            glVertex3f(wx+1.0f-lW, 0.0f, wz+lW); glVertex3f(wx+1.0f,    0.0f, wz+lW);
            glVertex3f(wx+1.0f,    tHeight-tThick, wz+lW); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+lW);
            glVertex3f(wx+1.0f-lW, 0.0f, wz); glVertex3f(wx+1.0f-lW, 0.0f, wz+lW);
            glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+lW); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz);
        }
        // Bottom-left leg
        if (!s_is_same && !w_is_same) {
            glVertex3f(wx,      0.0f, wz+1.0f-lW); glVertex3f(wx+lW,   0.0f, wz+1.0f-lW);
            glVertex3f(wx+lW,   tHeight-tThick, wz+1.0f-lW); glVertex3f(wx,      tHeight-tThick, wz+1.0f-lW);
            glVertex3f(wx,      0.0f, wz+1.0f); glVertex3f(wx+lW,   0.0f, wz+1.0f);
            glVertex3f(wx+lW,   tHeight-tThick, wz+1.0f); glVertex3f(wx,      tHeight-tThick, wz+1.0f);
            glVertex3f(wx+lW,   0.0f, wz+1.0f-lW); glVertex3f(wx+lW,   0.0f, wz+1.0f);
            glVertex3f(wx+lW,   tHeight-tThick, wz+1.0f); glVertex3f(wx+lW,   tHeight-tThick, wz+1.0f-lW);
        }
        // Bottom-right leg
        if (!s_is_same && !e_is_same) {
            glVertex3f(wx+1.0f-lW, 0.0f, wz+1.0f-lW); glVertex3f(wx+1.0f,    0.0f, wz+1.0f-lW);
            glVertex3f(wx+1.0f,    tHeight-tThick, wz+1.0f-lW); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+1.0f-lW);
            glVertex3f(wx+1.0f-lW, 0.0f, wz+1.0f); glVertex3f(wx+1.0f,    0.0f, wz+1.0f);
            glVertex3f(wx+1.0f,    tHeight-tThick, wz+1.0f); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+1.0f);
            glVertex3f(wx+1.0f-lW, 0.0f, wz+1.0f-lW); glVertex3f(wx+1.0f-lW, 0.0f, wz+1.0f);
            glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+1.0f); glVertex3f(wx+1.0f-lW, tHeight-tThick, wz+1.0f-lW);
        }
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    else if (shape == VOXEL_SHAPE_FURNITURE) {
        float bHeight = 0.3f; // base height
        float tHeight = 1.0f; // tv height
        
        // Base / Stand
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.2f, 0.2f, 0.2f);
        glBegin(GL_QUADS);
        // Base top
        glVertex3f(wx,      bHeight, wz);
        glVertex3f(wx+1.0f, bHeight, wz);
        glVertex3f(wx+1.0f, bHeight, wz+1.0f);
        glVertex3f(wx,      bHeight, wz+1.0f);
        // Base front
        glVertex3f(wx,      0.0f, wz+1.0f); glVertex3f(wx+1.0f, 0.0f, wz+1.0f);
        glVertex3f(wx+1.0f, bHeight, wz+1.0f); glVertex3f(wx,      bHeight, wz+1.0f);
        // Base sides
        if (!w_is_same) {
            glVertex3f(wx, 0.0f, wz); glVertex3f(wx, 0.0f, wz+1.0f);
            glVertex3f(wx, bHeight, wz+1.0f); glVertex3f(wx, bHeight, wz);
        }
        if (!e_is_same) {
            glVertex3f(wx+1.0f, 0.0f, wz+1.0f); glVertex3f(wx+1.0f, 0.0f, wz);
            glVertex3f(wx+1.0f, bHeight, wz); glVertex3f(wx+1.0f, bHeight, wz+1.0f);
        }
        glEnd();
        
        // TV Screen (textured front)
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v1); glVertex3f(wx,      bHeight, wz+0.8f);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, bHeight, wz+0.8f);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, tHeight, wz+0.8f);
        glTexCoord2f(u0, v0); glVertex3f(wx,      tHeight, wz+0.8f);
        glEnd();
        
        // TV Body (solid)
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.1f, 0.1f, 0.1f);
        glBegin(GL_QUADS);
        // Top
        glVertex3f(wx,      tHeight, wz+0.3f);
        glVertex3f(wx+1.0f, tHeight, wz+0.3f);
        glVertex3f(wx+1.0f, tHeight, wz+0.8f);
        glVertex3f(wx,      tHeight, wz+0.8f);
        // Sides
        if (!w_is_same) {
            glVertex3f(wx, bHeight, wz+0.3f); glVertex3f(wx, bHeight, wz+0.8f);
            glVertex3f(wx, tHeight, wz+0.8f); glVertex3f(wx, tHeight, wz+0.3f);
        }
        if (!e_is_same) {
            glVertex3f(wx+1.0f, bHeight, wz+0.8f); glVertex3f(wx+1.0f, bHeight, wz+0.3f);
            glVertex3f(wx+1.0f, tHeight, wz+0.3f); glVertex3f(wx+1.0f, tHeight, wz+0.8f);
        }
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    else if (shape == VOXEL_SHAPE_WALL) {
        float sHeight = 1.6f;
        // Textured front
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v1); glVertex3f(wx,      0.0f, wz+1.0f);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, 0.0f, wz+1.0f);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, sHeight, wz+1.0f);
        glTexCoord2f(u0, v0); glVertex3f(wx,      sHeight, wz+1.0f);
        glEnd();
        
        // Solid body
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.3f, 0.2f, 0.1f);
        glBegin(GL_QUADS);
        // Top
        glVertex3f(wx,      sHeight, wz);
        glVertex3f(wx+1.0f, sHeight, wz);
        glVertex3f(wx+1.0f, sHeight, wz+1.0f);
        glVertex3f(wx,      sHeight, wz+1.0f);
        // Sides
        if (!w_is_same) {
            glVertex3f(wx, 0.0f, wz); glVertex3f(wx, 0.0f, wz+1.0f);
            glVertex3f(wx, sHeight, wz+1.0f); glVertex3f(wx, sHeight, wz);
        }
        if (!e_is_same) {
            glVertex3f(wx+1.0f, 0.0f, wz+1.0f); glVertex3f(wx+1.0f, 0.0f, wz);
            glVertex3f(wx+1.0f, sHeight, wz); glVertex3f(wx+1.0f, sHeight, wz+1.0f);
        }
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    else if (shape == VOXEL_SHAPE_FURNITURE) { // Fallback chair logic
        float sHeight = 0.4f;
        // Seat top (textured)
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex3f(wx+0.1f, sHeight, wz+0.1f);
        glTexCoord2f(u1, v0); glVertex3f(wx+0.9f, sHeight, wz+0.1f);
        glTexCoord2f(u1, v1); glVertex3f(wx+0.9f, sHeight, wz+0.9f);
        glTexCoord2f(u0, v1); glVertex3f(wx+0.1f, sHeight, wz+0.9f);
        glEnd();
        
        // Base block
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.4f, 0.4f, 0.4f);
        glBegin(GL_QUADS);
        // Front
        glVertex3f(wx+0.2f, 0.0f, wz+0.8f); glVertex3f(wx+0.8f, 0.0f, wz+0.8f);
        glVertex3f(wx+0.8f, sHeight, wz+0.8f); glVertex3f(wx+0.2f, sHeight, wz+0.8f);
        // Backrest (solid tall part on North side)
        glVertex3f(wx+0.1f, sHeight, wz+0.2f); glVertex3f(wx+0.9f, sHeight, wz+0.2f);
        glVertex3f(wx+0.9f, sHeight+0.4f, wz+0.2f); glVertex3f(wx+0.1f, sHeight+0.4f, wz+0.2f);
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
}


void VoxelMesh_DrawTile(int mapX, int mapY, VoxelVisualShape shape, int metatileId)
{
    if (shape == VOXEL_SHAPE_VOID) return;
    
    // If this tile was consumed by VoxelMesh_BuildWalls (i.e. drawn as a vertical face),
    // we still want to draw it as a flat floor so there's ground underneath/behind it.
    if (VoxelMesh_IsWallConsumed(mapX, mapY)) {
        shape = VOXEL_SHAPE_FLAT;
    }

    float wx = (float)mapX;
    float wz = (float)mapY;
    
    float atlasW = 512.0f;
    float atlasH = 512.0f;
    float u0 = (metatileId % 32) * 16.0f / atlasW;
    float v0 = (metatileId / 32) * 16.0f / atlasH;
    float u1 = u0 + (16.0f / atlasW);
    float v1 = v0 + (16.0f / atlasH);

    float h = 0.0f;
    float bottomH = 0.0f;
    bool isSprite = false;
    float inset = 0.0f;

    switch (shape) {
        case VOXEL_SHAPE_FLAT: h = 0.0f; break;
        case VOXEL_SHAPE_DECAL: h = 0.02f; break; // Slight offset for rugs
        case VOXEL_SHAPE_WATER: h = -0.1f; break; // Slight recess for water
        case VOXEL_SHAPE_LOW: h = 0.4f; break;
        case VOXEL_SHAPE_LEDGE: h = 0.4f; break; // Ledges drawn as half-height block
        case VOXEL_SHAPE_COUNTER: h = 0.7f; break;
        case VOXEL_SHAPE_SIGN: h = 1.0f; isSprite = true; break; // Render sign as a vertical sprite billboard
        case VOXEL_SHAPE_STAIRS: h = 0.2f; break; // Simple stairs representation
        case VOXEL_SHAPE_WALL: h = 0.0f; break; // Handled by VoxelMesh_BuildWalls
        case VOXEL_SHAPE_TREE: h = 0.0f; break; // Handled by VoxelMesh_BuildWalls
        case VOXEL_SHAPE_BUILDING: h = 0.0f; break; // Handled by VoxelMesh_BuildWalls
        case VOXEL_SHAPE_ROOF: h = 0.0f; break; // Handled by VoxelMesh_BuildWalls
        // Furniture
        case VOXEL_SHAPE_TABLE:
        case VOXEL_SHAPE_FURNITURE:
        case VOXEL_SHAPE_BED:
            VoxelMesh_DrawFurniture(mapX, mapY, shape, u0, v0, u1, v1);
            return;
        default: h = 1.0f; break;
    }

    static int frame_count = 0;
    if (frame_count++ < 400 && h > 0.5f) {
        printf("[VoxelGeom] local=(%d,%d) metatile=%d shape=%d height=%.2f\n", mapX, mapY, metatileId, shape, h);
    }

    if (isSprite) {
        glColor3f(1.0f, 1.0f, 1.0f);
        glEnable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v1); glVertex3f(wx,      0.0f, wz + 0.5f);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, 0.0f, wz + 0.5f);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, h,    wz + 0.5f);
        glTexCoord2f(u0, v0); glVertex3f(wx,      h,    wz + 0.5f);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        return;
    }

    bool textureOnFront = false;
    
    // Top face
    if (textureOnFront) {
        glDisable(GL_TEXTURE_2D);
        glColor3f(0.2f, 0.2f, 0.2f); // Dark color
    } else {
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    
    glBegin(GL_QUADS);
    if (textureOnFront) {
        glVertex3f(wx+inset,      h, wz+inset);
        glVertex3f(wx+1.0f-inset, h, wz+inset);
        glVertex3f(wx+1.0f-inset, h, wz+1.0f-inset);
        glVertex3f(wx+inset,      h, wz+1.0f-inset);
    } else {
        glTexCoord2f(u0, v0); glVertex3f(wx+inset,      h, wz+inset);
        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f-inset, h, wz+inset);
        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f-inset, h, wz+1.0f-inset);
        glTexCoord2f(u0, v1); glVertex3f(wx+inset,      h, wz+1.0f-inset);
    }
    glEnd();
    
    if (h > 0.0f) {
        bool cullFront = false, cullBack = false, cullLeft = false, cullRight = false;
        
        if (inset == 0.0f && bottomH == 0.0f) {
            VoxelVisualShape fShape = VoxelWorld_ClassifyTile(mapX, mapY + 1);
            VoxelVisualShape bShape = VoxelWorld_ClassifyTile(mapX, mapY - 1);
            VoxelVisualShape lShape = VoxelWorld_ClassifyTile(mapX - 1, mapY);
            VoxelVisualShape rShape = VoxelWorld_ClassifyTile(mapX + 1, mapY);
            
            if (fShape == VOXEL_SHAPE_VOID) cullFront = true;
            if (bShape == VOXEL_SHAPE_VOID) cullBack = true;
            if (lShape == VOXEL_SHAPE_VOID) cullLeft = true;
            if (rShape == VOXEL_SHAPE_VOID) cullRight = true;
            
            if (shape == fShape) cullFront = true;
            if (shape == bShape) cullBack = true;
            if (shape == lShape) cullLeft = true;
            if (shape == rShape) cullRight = true;
        }

        // Front Face
        if (!cullFront) {
            if (textureOnFront) {
                glEnable(GL_TEXTURE_2D);
                glColor3f(1.0f, 1.0f, 1.0f);
            } else {
                glDisable(GL_TEXTURE_2D);
                glColor3f(0.6f, 0.6f, 0.6f);
            }
            glBegin(GL_QUADS);
            if (textureOnFront) {
                glTexCoord2f(u0, v1); glVertex3f(wx+inset,      bottomH, wz+1.0f-inset);
                glTexCoord2f(u1, v1); glVertex3f(wx+1.0f-inset, bottomH, wz+1.0f-inset);
                glTexCoord2f(u1, v0); glVertex3f(wx+1.0f-inset, h,       wz+1.0f-inset);
                glTexCoord2f(u0, v0); glVertex3f(wx+inset,      h,       wz+1.0f-inset);
            } else {
                glVertex3f(wx+inset,      bottomH, wz+1.0f-inset);
                glVertex3f(wx+1.0f-inset, bottomH, wz+1.0f-inset);
                glVertex3f(wx+1.0f-inset, h,       wz+1.0f-inset);
                glVertex3f(wx+inset,      h,       wz+1.0f-inset);
            }
            glEnd();
        }

        glDisable(GL_TEXTURE_2D);

        // Back Face
        if (!cullBack) {
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_QUADS);
            glVertex3f(wx+1.0f-inset, bottomH, wz+inset);
            glVertex3f(wx+inset,      bottomH, wz+inset);
            glVertex3f(wx+inset,      h,       wz+inset);
            glVertex3f(wx+1.0f-inset, h,       wz+inset);
            glEnd();
        }
        
        // Left Face
        if (!cullLeft) {
            glColor3f(0.65f, 0.65f, 0.65f);
            glBegin(GL_QUADS);
            glVertex3f(wx+inset, bottomH, wz+inset);
            glVertex3f(wx+inset, bottomH, wz+1.0f-inset);
            glVertex3f(wx+inset, h,       wz+1.0f-inset);
            glVertex3f(wx+inset, h,       wz+inset);
            glEnd();
        }
        
        // Right Face
        if (!cullRight) {
            glColor3f(0.55f, 0.55f, 0.55f);
            glBegin(GL_QUADS);
            glVertex3f(wx+1.0f-inset, bottomH, wz+1.0f-inset);
            glVertex3f(wx+1.0f-inset, bottomH, wz+inset);
            glVertex3f(wx+1.0f-inset, h,       wz+inset);
            glVertex3f(wx+1.0f-inset, h,       wz+1.0f-inset);
            glEnd();
        }
    }
}

void VoxelMesh_DrawPlayerBillboard(float wx, float wy, float wz, float camX, float camY, float camZ, GLuint tex, int w, int h)
{
    (void)camX;
    (void)camY;
    (void)camZ;

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    
    if (tex != 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    
    // Scale based on sprite width/height. 16 pixels = 1.0 world unit
    float width_world = w / 16.0f;
    float height_world = h / 16.0f;
    float hw = width_world / 2.0f;

    // Enable alpha testing so transparent pixels don't write to depth buffer
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(wx - hw, wy,     wz);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(wx + hw, wy,     wz);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(wx + hw, wy + height_world, wz);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(wx - hw, wy + height_world, wz);
    glEnd();

    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
}

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2

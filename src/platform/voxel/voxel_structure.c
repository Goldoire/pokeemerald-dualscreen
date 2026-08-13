#include "voxel_structure.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include "global.h"
#include "fieldmap.h"
#include <string.h>
#include "voxel_mesh.h" // For VoxelMesh_GetMetatileId etc

VoxelStructure gVoxelStructures[MAX_VOXEL_STRUCTURES];
int gVoxelStructureCount = 0;

static bool sVisitedTiles[2048 * 2048];

static bool IsVisited(int worldX, int worldY) {
    int cx = worldX + 1024;
    int cy = worldY + 1024;
    if (cx < 0 || cx >= 2048 || cy < 0 || cy >= 2048) return true;
    return sVisitedTiles[cy * 2048 + cx];
}

static void SetVisited(int worldX, int worldY) {
    int cx = worldX + 1024;
    int cy = worldY + 1024;
    if (cx < 0 || cx >= 2048 || cy < 0 || cy >= 2048) return;
    sVisitedTiles[cy * 2048 + cx] = true;
}

static void FloodFillStructure(int startX, int startY, VoxelStructureType type)
{
    if (gVoxelStructureCount >= MAX_VOXEL_STRUCTURES) return;
    VoxelStructure *st = &gVoxelStructures[gVoxelStructureCount];
    st->type = type;
    st->tileCount = 0;
    st->minX = startX;
    st->minY = startY;
    st->maxX = startX;
    st->maxY = startY;
    st->consumed = false;

    // Simple BFS queue
    int queueX[MAX_TILES_PER_STRUCTURE];
    int queueY[MAX_TILES_PER_STRUCTURE];
    int head = 0, tail = 0;

    queueX[tail] = startX;
    queueY[tail] = startY;
    tail++;
    SetVisited(startX, startY);

    while (head < tail && st->tileCount < MAX_TILES_PER_STRUCTURE) {
        int x = queueX[head];
        int y = queueY[head];
        head++;

        st->tiles[st->tileCount].x = x;
        st->tiles[st->tileCount].y = y;
        st->tileCount++;

        if (x < st->minX) st->minX = x;
        if (y < st->minY) st->minY = y;
        if (x > st->maxX) st->maxX = x;
        if (y > st->maxY) st->maxY = y;

        // Check neighbors
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};
        for (int i = 0; i < 4; i++) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            
            if (IsVisited(nx, ny)) continue;
            
            VoxelVisualShape shape = VoxelWorld_ClassifyTile(nx, ny);
            
            bool match = false;
            if (type == STRUCT_TYPE_BUILDING) {
                if (shape == VOXEL_SHAPE_BUILDING || shape == VOXEL_SHAPE_WALL || shape == VOXEL_SHAPE_ROOF) match = true;
                // Wait, trees are also WALL or ROOF right now!
                // We should distinguish them by tileset maybe, or just rely on the shape.
                // For now, if the user classifies them as TREE, we use that.
                if (shape == VOXEL_SHAPE_TREE) match = false;
            } else if (type == STRUCT_TYPE_TREE) {
                if (shape == VOXEL_SHAPE_TREE) match = true;
            }

            if (match) {
                if (tail < MAX_TILES_PER_STRUCTURE) {
                    queueX[tail] = nx;
                    queueY[tail] = ny;
                    tail++;
                    SetVisited(nx, ny);
                }
            }
        }
    }
    
    gVoxelStructureCount++;
}

void VoxelStructure_ExtractAll(void)
{
    memset(sVisitedTiles, 0, sizeof(sVisitedTiles));
    gVoxelStructureCount = 0;

    for (int instIdx = 0; instIdx < gVoxelMapInstanceCount; instIdx++) {
        const struct VoxelMapInstance *inst = &gVoxelMapInstances[instIdx];
        int w = inst->header->mapLayout->width;
        int h = inst->header->mapLayout->height;

        for (int ly = 0; ly < h; ly++) {
            for (int lx = 0; lx < w; lx++) {
                int x = lx + inst->originX;
                int y = ly + inst->originY;

                if (IsVisited(x, y)) continue;

                VoxelVisualShape shape = VoxelWorld_ClassifyTile(x, y);
                if (shape == VOXEL_SHAPE_WALL || shape == VOXEL_SHAPE_BUILDING || shape == VOXEL_SHAPE_ROOF) {
                    // Start building floodfill
                    FloodFillStructure(x, y, STRUCT_TYPE_BUILDING);
                } else if (shape == VOXEL_SHAPE_TREE) {
                    // Start tree floodfill
                    FloodFillStructure(x, y, STRUCT_TYPE_TREE);
                }
            }
        }
    }
}

bool VoxelStructure_IsTileInStructure(int worldX, int worldY)
{
    for (int i = 0; i < gVoxelStructureCount; i++) {
        VoxelStructure *st = &gVoxelStructures[i];
        if (worldX >= st->minX && worldX <= st->maxX && worldY >= st->minY && worldY <= st->maxY) {
            for (int j = 0; j < st->tileCount; j++) {
                if (st->tiles[j].x == worldX && st->tiles[j].y == worldY) return true;
            }
        }
    }
    return false;
}

static bool TileExistsInStruct(VoxelStructure *st, int x, int y)
{
    if (x < st->minX || x > st->maxX || y < st->minY || y > st->maxY) return false;
    for (int j = 0; j < st->tileCount; j++) {
        if (st->tiles[j].x == x && st->tiles[j].y == y) return true;
    }
    return false;
}

void VoxelStructure_RenderAll(GLuint atlasTex)
{
    float atlasW = 512.0f;
    float atlasH = 512.0f;
    float pWX = 0.0f, pWZ = 0.0f;
    VoxelWorld_GetPlayerWorldCoords(&pWX, &pWZ);

    glBindTexture(GL_TEXTURE_2D, atlasTex);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);

    for (int i = 0; i < gVoxelStructureCount; i++) {
        VoxelStructure *st = &gVoxelStructures[i];
        
        // Find maximum height of the structure (simple approach for now)
        int maxH = 1;
        for (int j = 0; j < st->tileCount; j++) {
            int y = st->tiles[j].y;
            int x = st->tiles[j].x;
            
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
            if (heightInTiles > maxH) maxH = heightInTiles;
        }

        if (st->type == STRUCT_TYPE_BUILDING) {
            // Check if structure blocks player (cutaway)
            bool isCutaway = false;
            if (st->maxY > pWZ + 0.5f) {
                isCutaway = true; // Very simple bounds-based cutaway
            }

            float totalH = maxH * 1.0f;
            if (isCutaway) totalH = 1.0f;

            // Render Roof / Top face
            if (!isCutaway) {
                glBegin(GL_QUADS);
                for (int j = 0; j < st->tileCount; j++) {
                    int x = st->tiles[j].x;
                    int y = st->tiles[j].y;
                    
                    int m;
                    VoxelWorld_GetMetatileIdAndInstance(x, y - maxH + 1, &m);
                    
                    float u0 = (m % 32) * 16.0f / atlasW;
                    float v0 = (m / 32) * 16.0f / atlasH;
                    float u1 = u0 + (16.0f / atlasW);
                    float v1 = v0 + (16.0f / atlasH);

                    glColor3f(1.0f, 1.0f, 1.0f); // Top = 1.0 (Directional Shading)
                    
                    float wx = (float)x;
                    float wz = (float)y;
                    glTexCoord2f(u0, v0); glVertex3f(wx,      totalH, wz);
                    glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, totalH, wz);
                    glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, totalH, wz+1.0f);
                    glTexCoord2f(u0, v1); glVertex3f(wx,      totalH, wz+1.0f);
                }
                glEnd();
            }

            // Render exterior walls only
            glBegin(GL_QUADS);
            for (int j = 0; j < st->tileCount; j++) {
                int x = st->tiles[j].x;
                int y = st->tiles[j].y;
                float wx = (float)x;
                float wz = (float)y;

                VoxelVisualShape sShape = VoxelWorld_ClassifyTile(x, y + 1);
                VoxelVisualShape nShape = VoxelWorld_ClassifyTile(x, y - 1);
                VoxelVisualShape eShape = VoxelWorld_ClassifyTile(x + 1, y);
                VoxelVisualShape wShape = VoxelWorld_ClassifyTile(x - 1, y);

                bool faceSouth = !(sShape == VOXEL_SHAPE_WALL || sShape == VOXEL_SHAPE_BUILDING || sShape == VOXEL_SHAPE_ROOF);
                bool faceNorth = !(nShape == VOXEL_SHAPE_WALL || nShape == VOXEL_SHAPE_BUILDING || nShape == VOXEL_SHAPE_ROOF);
                bool faceEast  = !(eShape == VOXEL_SHAPE_WALL || eShape == VOXEL_SHAPE_BUILDING || eShape == VOXEL_SHAPE_ROOF);
                bool faceWest  = !(wShape == VOXEL_SHAPE_WALL || wShape == VOXEL_SHAPE_BUILDING || wShape == VOXEL_SHAPE_ROOF);

                int heightToDraw = isCutaway ? 1 : maxH;

                for (int h = 0; h < heightToDraw; h++) {
                    int m = VoxelWorld_GetMetatileId(x, y - h);
                    float u0 = (m % 32) * 16.0f / atlasW;
                    float v0 = (m / 32) * 16.0f / atlasH;
                    float u1 = u0 + (16.0f / atlasW);
                    float v1 = v0 + (16.0f / atlasH);
                    
                    float yBottom = h * 1.0f;
                    float yTop = (h + 1) * 1.0f;
                    
                    if (faceSouth) {
                        glColor3f(0.85f, 0.85f, 0.85f);
                        glTexCoord2f(u0, v1); glVertex3f(wx,      yBottom, wz+1.0f);
                        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, yBottom, wz+1.0f);
                        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, yTop,    wz+1.0f);
                        glTexCoord2f(u0, v0); glVertex3f(wx,      yTop,    wz+1.0f);
                    }
                    if (faceNorth && !isCutaway) { // Hide inside north wall if cutaway
                        glColor3f(0.90f, 0.90f, 0.90f);
                        glTexCoord2f(u1, v1); glVertex3f(wx,      yBottom, wz);
                        glTexCoord2f(u0, v1); glVertex3f(wx+1.0f, yBottom, wz);
                        glTexCoord2f(u0, v0); glVertex3f(wx+1.0f, yTop,    wz);
                        glTexCoord2f(u1, v0); glVertex3f(wx,      yTop,    wz);
                    }
                    if (faceEast) {
                        glColor3f(0.82f, 0.82f, 0.82f);
                        glTexCoord2f(u0, v1); glVertex3f(wx+1.0f, yBottom, wz+1.0f);
                        glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, yBottom, wz);
                        glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, yTop,    wz);
                        glTexCoord2f(u0, v0); glVertex3f(wx+1.0f, yTop,    wz+1.0f);
                    }
                    if (faceWest) {
                        glColor3f(0.75f, 0.75f, 0.75f);
                        glTexCoord2f(u1, v1); glVertex3f(wx, yBottom, wz+1.0f);
                        glTexCoord2f(u0, v1); glVertex3f(wx, yBottom, wz);
                        glTexCoord2f(u0, v0); glVertex3f(wx, yTop,    wz);
                        glTexCoord2f(u1, v0); glVertex3f(wx, yTop,    wz+1.0f);
                    }
                }
            }
            glEnd();
        } else if (st->type == STRUCT_TYPE_TREE) {
            float totalH = 2.0f; // 2 tiles high for trees
            
            // Canopy Top
            glBegin(GL_QUADS);
            for (int j = 0; j < st->tileCount; j++) {
                int x = st->tiles[j].x;
                int y = st->tiles[j].y;
                
                int m = VoxelWorld_GetMetatileId(x, y - 1); // Usually tree top is 1 tile up
                float u0 = (m % 32) * 16.0f / atlasW;
                float v0 = (m / 32) * 16.0f / atlasH;
                float u1 = u0 + (16.0f / atlasW);
                float v1 = v0 + (16.0f / atlasH);

                glColor3f(1.0f, 1.0f, 1.0f);
                float wx = (float)x;
                float wz = (float)y;
                glTexCoord2f(u0, v0); glVertex3f(wx,      totalH, wz);
                glTexCoord2f(u1, v0); glVertex3f(wx+1.0f, totalH, wz);
                glTexCoord2f(u1, v1); glVertex3f(wx+1.0f, totalH, wz+1.0f);
                glTexCoord2f(u0, v1); glVertex3f(wx,      totalH, wz+1.0f);
            }
            glEnd();

            // Canopy Sides and Trunk
            glBegin(GL_QUADS);
            for (int j = 0; j < st->tileCount; j++) {
                int x = st->tiles[j].x;
                int y = st->tiles[j].y;
                float wx = (float)x;
                float wz = (float)y;

                VoxelVisualShape sShape = VoxelWorld_ClassifyTile(x, y + 1);
                VoxelVisualShape nShape = VoxelWorld_ClassifyTile(x, y - 1);
                VoxelVisualShape eShape = VoxelWorld_ClassifyTile(x + 1, y);
                VoxelVisualShape wShape = VoxelWorld_ClassifyTile(x - 1, y);

                bool faceSouth = !(sShape == VOXEL_SHAPE_TREE);
                bool faceNorth = !(nShape == VOXEL_SHAPE_TREE);
                bool faceEast  = !(eShape == VOXEL_SHAPE_TREE);
                bool faceWest  = !(wShape == VOXEL_SHAPE_TREE);

                for (int h = 0; h < 2; h++) {
                    int m = VoxelWorld_GetMetatileId(x, y - h);
                    float u0 = (m % 32) * 16.0f / atlasW;
                    float v0 = (m / 32) * 16.0f / atlasH;
                    float u1 = u0 + (16.0f / atlasW);
                    float v1 = v0 + (16.0f / atlasH);
                    
                    float yBottom = h * 1.0f;
                    float yTop = (h + 1) * 1.0f;

                    // Make trunk narrower
                    float ix0 = 0.0f, ix1 = 1.0f, iz0 = 0.0f, iz1 = 1.0f;
                    if (h == 0) { // Trunk level
                        ix0 = 0.2f; ix1 = 0.8f;
                        iz0 = 0.2f; iz1 = 0.8f;
                    }

                    if (faceSouth) {
                        glColor3f(0.85f, 0.85f, 0.85f);
                        glTexCoord2f(u0, v1); glVertex3f(wx+ix0, yBottom, wz+iz1);
                        glTexCoord2f(u1, v1); glVertex3f(wx+ix1, yBottom, wz+iz1);
                        glTexCoord2f(u1, v0); glVertex3f(wx+ix1, yTop,    wz+iz1);
                        glTexCoord2f(u0, v0); glVertex3f(wx+ix0, yTop,    wz+iz1);
                    }
                    if (faceNorth) {
                        glColor3f(0.90f, 0.90f, 0.90f);
                        glTexCoord2f(u1, v1); glVertex3f(wx+ix0, yBottom, wz+iz0);
                        glTexCoord2f(u0, v1); glVertex3f(wx+ix1, yBottom, wz+iz0);
                        glTexCoord2f(u0, v0); glVertex3f(wx+ix1, yTop,    wz+iz0);
                        glTexCoord2f(u1, v0); glVertex3f(wx+ix0, yTop,    wz+iz0);
                    }
                    if (faceEast) {
                        glColor3f(0.82f, 0.82f, 0.82f);
                        glTexCoord2f(u0, v1); glVertex3f(wx+ix1, yBottom, wz+iz1);
                        glTexCoord2f(u1, v1); glVertex3f(wx+ix1, yBottom, wz+iz0);
                        glTexCoord2f(u1, v0); glVertex3f(wx+ix1, yTop,    wz+iz0);
                        glTexCoord2f(u0, v0); glVertex3f(wx+ix1, yTop,    wz+iz1);
                    }
                    if (faceWest) {
                        glColor3f(0.75f, 0.75f, 0.75f);
                        glTexCoord2f(u1, v1); glVertex3f(wx+ix0, yBottom, wz+iz1);
                        glTexCoord2f(u0, v1); glVertex3f(wx+ix0, yBottom, wz+iz0);
                        glTexCoord2f(u0, v0); glVertex3f(wx+ix0, yTop,    wz+iz0);
                        glTexCoord2f(u1, v0); glVertex3f(wx+ix0, yTop,    wz+iz1);
                    }
                }
            }
            glEnd();
        }
    }
}

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2

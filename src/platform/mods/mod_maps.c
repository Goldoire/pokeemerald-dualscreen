#include <stdlib.h>
#include "mod_internal.h"
#include "mod_manager.h"
#include "global.fieldmap.h"
#include "overworld.h"
extern void *malloc(unsigned int);
#include <string.h>


typedef struct {
    u8 mapGroup;
    u8 mapNum;
    
    // RAM shadows
    struct MapHeader header;
    struct MapLayout layout;
    struct MapEvents events;
    u16 *mapData;
    
    // Original pointers
    const struct MapHeader *vanillaHeader;
    
    bool8 initialized;
    
    cJSON *patchData; // Keep the cJSON node around to apply patches on demand? No, parse into our own structures or apply immediately.
} MapOverride;

static MapOverride **sMapOverrides = NULL;
static int sMapOverridesCapacity = 0;
static int sNumMapOverrides = 0;

void ModMaps_LoadOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/maps.json", mod->path);
    char *jsonStr = ModManager_ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
        extern void free(void*);
        free(jsonStr);
        return;
    }

    cJSON *maps = cJSON_GetObjectItem(root, "maps");
    if (cJSON_IsArray(maps)) {
        cJSON *mapObj;
        cJSON_ArrayForEach(mapObj, maps) {
            cJSON *groupObj = cJSON_GetObjectItem(mapObj, "map_group");
            cJSON *numObj = cJSON_GetObjectItem(mapObj, "map_num");
            
            if (!cJSON_IsNumber(groupObj) || !cJSON_IsNumber(numObj)) continue;
            
            u8 mapGroup = groupObj->valueint;
            u8 mapNum = numObj->valueint;

            

            // Check priority
            bool8 alreadyOverridden = FALSE;
            for (int i = 0; i < sNumMapOverrides; i++) {
                if (sMapOverrides[i]->mapGroup == mapGroup && sMapOverrides[i]->mapNum == mapNum) {
                    alreadyOverridden = TRUE;
                    break;
                }
            }
            if (alreadyOverridden) continue;

            if (sNumMapOverrides >= sMapOverridesCapacity) {
                sMapOverridesCapacity = sMapOverridesCapacity == 0 ? 64 : sMapOverridesCapacity * 2;
                sMapOverrides = realloc(sMapOverrides, sMapOverridesCapacity * sizeof(MapOverride*));
            }
            MapOverride *ov = malloc(sizeof(MapOverride));
            memset(ov, 0, sizeof(MapOverride));
            sMapOverrides[sNumMapOverrides++] = ov;
            ov->mapGroup = mapGroup;
            ov->mapNum = mapNum;
            ov->initialized = FALSE;
            // Duplicate JSON object and store it for lazy application
            ov->patchData = cJSON_Duplicate(mapObj, 1);

            fprintf(stderr, "[Mods]   Loaded map override %d.%d from %s\n", mapGroup, mapNum, mod->id);
        }
    }

    cJSON_Delete(root);
    extern void free(void*);
    free(jsonStr);
}

static void InitializeMapOverride(MapOverride *ov, const struct MapHeader *vanilla) {
    ov->vanillaHeader = vanilla;
    
    // Copy Header
    memcpy(&ov->header, vanilla, sizeof(struct MapHeader));
    
    // Copy Layout
    if (vanilla->mapLayout) {
        memcpy(&ov->layout, vanilla->mapLayout, sizeof(struct MapLayout));
        ov->header.mapLayout = &ov->layout;
        
        // Copy map data (tiles)
        int numTiles = ov->layout.width * ov->layout.height;
        ov->mapData = malloc(numTiles * sizeof(u16));
        memcpy(ov->mapData, vanilla->mapLayout->map, numTiles * sizeof(u16));
        ov->layout.map = ov->mapData;
    }
    
    // Copy Events (Phase 2 will expand this)
    if (vanilla->events) {
        memcpy(&ov->events, vanilla->events, sizeof(struct MapEvents));
        ov->header.events = &ov->events;
        
        // Deep copy object events to allow patching
        if (vanilla->events->objectEventCount > 0 && vanilla->events->objectEvents) {
            struct ObjectEventTemplate *objEvents = malloc(vanilla->events->objectEventCount * sizeof(struct ObjectEventTemplate));
            memcpy(objEvents, vanilla->events->objectEvents, vanilla->events->objectEventCount * sizeof(struct ObjectEventTemplate));
            ov->events.objectEvents = objEvents;

            for (int j = 0; j < vanilla->events->objectEventCount; j++) {
                const u8 *scriptOverride = ModScripts_GetObjectScript(ov->mapGroup, ov->mapNum, objEvents[j].localId);
                if (scriptOverride) {
                    objEvents[j].script = scriptOverride;
                }
            }
        }
    }
    
    // Apply patches from ov->patchData
    if (ov->patchData) {
        // Tiles patch
        cJSON *tiles = cJSON_GetObjectItem(ov->patchData, "tiles");
        if (cJSON_IsArray(tiles)) {
            cJSON *tile;
            cJSON_ArrayForEach(tile, tiles) {
                cJSON *xObj = cJSON_GetObjectItem(tile, "x");
                cJSON *yObj = cJSON_GetObjectItem(tile, "y");
                cJSON *metatileObj = cJSON_GetObjectItem(tile, "metatile");
                cJSON *collisionObj = cJSON_GetObjectItem(tile, "collision");
                cJSON *elevationObj = cJSON_GetObjectItem(tile, "elevation");
                
                if (!cJSON_IsNumber(xObj) || !cJSON_IsNumber(yObj) || !cJSON_IsNumber(metatileObj)) continue;
                
                int x = xObj->valueint;
                int y = yObj->valueint;
                int metatile = metatileObj->valueint;
                int collision = cJSON_IsNumber(collisionObj) ? collisionObj->valueint : 0;
                int elevation = cJSON_IsNumber(elevationObj) ? elevationObj->valueint : 0;
                
                if (x >= 0 && x < ov->layout.width && y >= 0 && y < ov->layout.height) {
                    // map cell format:
                    // 0-9: metatile id
                    // 10-11: collision
                    // 12-15: elevation
                    u16 cell = (metatile & 0x3FF) | ((collision & 3) << 10) | ((elevation & 15) << 12);
                    ov->mapData[y * ov->layout.width + x] = cell;
                }
            }
        }
        
        
        cJSON *objects = cJSON_GetObjectItem(ov->patchData, "objects");
        if (cJSON_IsArray(objects) && ov->events.objectEvents) {
            cJSON *obj;
            struct ObjectEventTemplate *mutObjEvents = (struct ObjectEventTemplate *)ov->events.objectEvents;
            cJSON_ArrayForEach(obj, objects) {
                cJSON *indexObj = cJSON_GetObjectItem(obj, "index");
                if (!cJSON_IsNumber(indexObj)) continue;
                
                int index = indexObj->valueint;
                if (index >= 0 && index < ov->events.objectEventCount) {
                    struct ObjectEventTemplate *target = &mutObjEvents[index];
                    
                    cJSON *xObj = cJSON_GetObjectItem(obj, "x");
                    if (cJSON_IsNumber(xObj)) target->x = xObj->valueint;
                    
                    cJSON *yObj = cJSON_GetObjectItem(obj, "y");
                    if (cJSON_IsNumber(yObj)) target->y = yObj->valueint;
                    
                    cJSON *graphicsIdObj = cJSON_GetObjectItem(obj, "graphics_id");
                    if (cJSON_IsNumber(graphicsIdObj)) target->graphicsId = graphicsIdObj->valueint;
                    
                    cJSON *movementTypeObj = cJSON_GetObjectItem(obj, "movement_type");
                    if (cJSON_IsNumber(movementTypeObj)) target->movementType = movementTypeObj->valueint;
                }
            }
        }
        
        cJSON_Delete(ov->patchData);
        ov->patchData = NULL;
    }
    
    ov->initialized = TRUE;
}

const struct MapHeader *ModManager_GetMapHeaderByMap(u16 mapGroup, u16 mapNum, const struct MapHeader *vanilla) {
    if (!gModsEnabled) return vanilla;
    
    for (int i = 0; i < sNumMapOverrides; i++) {
        if (sMapOverrides[i]->mapGroup == mapGroup && sMapOverrides[i]->mapNum == mapNum) {
            MapOverride *ov = sMapOverrides[i];
            if (!ov->initialized) {
                InitializeMapOverride(ov, vanilla);
            }
            return &ov->header;
        }
    }
    return vanilla;
}

void ModMaps_Shutdown(void) {
    for (int i = 0; i < sNumMapOverrides; i++) {
        if (sMapOverrides[i]->mapData) {
            extern void free(void*);
            free(sMapOverrides[i]->mapData);
            sMapOverrides[i]->mapData = NULL;
        }
        if (sMapOverrides[i]->events.objectEvents && sMapOverrides[i]->vanillaHeader && sMapOverrides[i]->events.objectEvents != sMapOverrides[i]->vanillaHeader->events->objectEvents) {
            extern void free(void*);
            free((void*)sMapOverrides[i]->events.objectEvents);
            sMapOverrides[i]->events.objectEvents = NULL;
        }
        if (sMapOverrides[i]->patchData) {
            cJSON_Delete(sMapOverrides[i]->patchData);
            sMapOverrides[i]->patchData = NULL;
        }
        sMapOverrides[i]->initialized = FALSE;
    }
    sNumMapOverrides = 0;
}

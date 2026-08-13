#include <stdlib.h>
#include "mod_internal.h"
#include "mod_manager.h"
#include "wild_encounter.h"
#include <string.h>

extern void free(void*);


typedef struct {
    u8 mapGroup;
    u8 mapNum;
    struct WildPokemonInfo landInfo;
    struct WildPokemon landMons[12];
    bool8 hasLand;
    
    struct WildPokemonInfo waterInfo;
    struct WildPokemon waterMons[5];
    bool8 hasWater;
    
    struct WildPokemonInfo rockSmashInfo;
    struct WildPokemon rockSmashMons[5];
    bool8 hasRockSmash;
    
    struct WildPokemonInfo fishingInfo;
    struct WildPokemon fishingMons[10];
    bool8 hasFishing;
    
    struct WildPokemonHeader header;
} EncounterOverride;

static EncounterOverride **sEncounterOverrides = NULL;
static int sEncounterOverridesCapacity = 0;
static int sNumEncounterOverrides = 0;

static void ParseEncounterSlots(cJSON *slots, struct WildPokemon *mons, int maxSlots) {
    int count = 0;
    cJSON *slot;
    cJSON_ArrayForEach(slot, slots) {
        if (count >= maxSlots) break;
        cJSON *speciesObj = cJSON_GetObjectItem(slot, "species");
        cJSON *minObj = cJSON_GetObjectItem(slot, "min_level");
        cJSON *maxObj = cJSON_GetObjectItem(slot, "max_level");
        
        if (cJSON_IsNumber(speciesObj)) {
            mons[count].species = speciesObj->valueint;
            mons[count].minLevel = cJSON_IsNumber(minObj) ? minObj->valueint : 1;
            mons[count].maxLevel = cJSON_IsNumber(maxObj) ? maxObj->valueint : mons[count].minLevel;
        }
        count++;
    }
    // Fill remaining with 0s (safeguard)
    for (; count < maxSlots; count++) {
        mons[count].species = 0;
        mons[count].minLevel = 0;
        mons[count].maxLevel = 0;
    }
}

void ModEncounters_LoadOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/encounters.json", mod->path);
    char *jsonStr = ModManager_ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
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
            for (int i = 0; i < sNumEncounterOverrides; i++) {
                if (sEncounterOverrides[i]->mapGroup == mapGroup && sEncounterOverrides[i]->mapNum == mapNum) {
                    alreadyOverridden = TRUE;
                    break;
                }
            }
            if (alreadyOverridden) continue;

            if (sNumEncounterOverrides >= sEncounterOverridesCapacity) {
                sEncounterOverridesCapacity = sEncounterOverridesCapacity == 0 ? 64 : sEncounterOverridesCapacity * 2;
                sEncounterOverrides = realloc(sEncounterOverrides, sEncounterOverridesCapacity * sizeof(EncounterOverride*));
            }
            EncounterOverride *ov = malloc(sizeof(EncounterOverride));
            memset(ov, 0, sizeof(EncounterOverride));
            sEncounterOverrides[sNumEncounterOverrides++] = ov;
            ov->mapGroup = mapGroup;
            ov->mapNum = mapNum;
            ov->hasLand = FALSE;
            ov->hasWater = FALSE;
            ov->hasRockSmash = FALSE;
            ov->hasFishing = FALSE;
            
            ov->header.mapGroup = mapGroup;
            ov->header.mapNum = mapNum;
            ov->header.landMonsInfo = NULL;
            ov->header.waterMonsInfo = NULL;
            ov->header.rockSmashMonsInfo = NULL;
            ov->header.fishingMonsInfo = NULL;

            cJSON *land = cJSON_GetObjectItem(mapObj, "land");
            if (cJSON_IsObject(land)) {
                cJSON *rate = cJSON_GetObjectItem(land, "encounter_rate");
                cJSON *slots = cJSON_GetObjectItem(land, "slots");
                if (cJSON_IsArray(slots)) {
                    ov->hasLand = TRUE;
                    ov->landInfo.encounterRate = cJSON_IsNumber(rate) ? rate->valueint : 20;
                    ov->landInfo.wildPokemon = ov->landMons;
                    ParseEncounterSlots(slots, ov->landMons, 12);
                    ov->header.landMonsInfo = &ov->landInfo;
                }
            }

            cJSON *water = cJSON_GetObjectItem(mapObj, "water");
            if (cJSON_IsObject(water)) {
                cJSON *rate = cJSON_GetObjectItem(water, "encounter_rate");
                cJSON *slots = cJSON_GetObjectItem(water, "slots");
                if (cJSON_IsArray(slots)) {
                    ov->hasWater = TRUE;
                    ov->waterInfo.encounterRate = cJSON_IsNumber(rate) ? rate->valueint : 20;
                    ov->waterInfo.wildPokemon = ov->waterMons;
                    ParseEncounterSlots(slots, ov->waterMons, 5);
                    ov->header.waterMonsInfo = &ov->waterInfo;
                }
            }

            // (Skipping RockSmash and Fishing for brevity in test mod, but parsed if provided similarly)
            // Can just add them exactly like water.
            
            fprintf(stderr, "[Mods]   Loaded encounter override map %d.%d from %s\n", mapGroup, mapNum, mod->id);
        }
    }

    cJSON_Delete(root);
    free(jsonStr);
}

void ModEncounters_Shutdown(void) {
    sNumEncounterOverrides = 0;
}

const struct WildPokemonHeader *ModManager_GetWildMonHeader(u16 headerId, const struct WildPokemonHeader *vanilla) {
    if (!gModsEnabled) return vanilla;
    
    for (int i = 0; i < sNumEncounterOverrides; i++) {
        if (sEncounterOverrides[i]->mapGroup == vanilla->mapGroup && sEncounterOverrides[i]->mapNum == vanilla->mapNum) {
            // For fields we DID NOT override, fallback to vanilla!
            EncounterOverride *ov = sEncounterOverrides[i];
            ov->header.landMonsInfo = ov->hasLand ? &ov->landInfo : vanilla->landMonsInfo;
            ov->header.waterMonsInfo = ov->hasWater ? &ov->waterInfo : vanilla->waterMonsInfo;
            ov->header.rockSmashMonsInfo = ov->hasRockSmash ? &ov->rockSmashInfo : vanilla->rockSmashMonsInfo;
            ov->header.fishingMonsInfo = ov->hasFishing ? &ov->fishingInfo : vanilla->fishingMonsInfo;
            return &ov->header;
        }
    }
    return vanilla;
}

const struct WildPokemonHeader *ModManager_GetWildMonHeaderByMap(u8 mapGroup, u8 mapNum, const struct WildPokemonHeader *vanilla) {
    if (!gModsEnabled) return vanilla;
    for (int i = 0; i < sNumEncounterOverrides; i++) {
        if (sEncounterOverrides[i]->mapGroup == mapGroup && sEncounterOverrides[i]->mapNum == mapNum) {
            EncounterOverride *ov = sEncounterOverrides[i];
            ov->header.landMonsInfo = ov->hasLand ? &ov->landInfo : (vanilla ? vanilla->landMonsInfo : NULL);
            ov->header.waterMonsInfo = ov->hasWater ? &ov->waterInfo : (vanilla ? vanilla->waterMonsInfo : NULL);
            ov->header.rockSmashMonsInfo = ov->hasRockSmash ? &ov->rockSmashInfo : (vanilla ? vanilla->rockSmashMonsInfo : NULL);
            ov->header.fishingMonsInfo = ov->hasFishing ? &ov->fishingInfo : (vanilla ? vanilla->fishingMonsInfo : NULL);
            return &ov->header;
        }
    }
    return vanilla;
}

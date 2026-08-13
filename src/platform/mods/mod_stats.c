#include "mod_internal.h"
#include "mod_manager.h"
#include "pokemon.h"

void ModStats_LoadOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/species.json", mod->path);
    char *jsonStr = ModManager_ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
        extern void free(void*);
        free(jsonStr);
        return;
    }

    cJSON *speciesList = cJSON_GetObjectItem(root, "species");
    if (cJSON_IsArray(speciesList)) {
        cJSON *specObj;
        cJSON_ArrayForEach(specObj, speciesList) {
            cJSON *idObj = cJSON_GetObjectItem(specObj, "id");
            if (!cJSON_IsNumber(idObj)) continue;
            u16 species = idObj->valueint;

            if (species == 0 || species >= NUM_SPECIES) {
                fprintf(stderr, "[Mods][ERROR] %s: Invalid/unresolved species ID %d; override skipped.\n", mod->id, species);
                continue;
            }

            struct SpeciesInfo *info = &gSpeciesInfo[species];
            
            cJSON *hp = cJSON_GetObjectItem(specObj, "base_hp");
            if (cJSON_IsNumber(hp)) info->baseHP = hp->valueint;
            
            cJSON *atk = cJSON_GetObjectItem(specObj, "base_attack");
            if (cJSON_IsNumber(atk)) info->baseAttack = atk->valueint;
            
            cJSON *def = cJSON_GetObjectItem(specObj, "base_defense");
            if (cJSON_IsNumber(def)) info->baseDefense = def->valueint;
            
            cJSON *spd = cJSON_GetObjectItem(specObj, "base_speed");
            if (cJSON_IsNumber(spd)) info->baseSpeed = spd->valueint;
            
            cJSON *spAtk = cJSON_GetObjectItem(specObj, "base_sp_attack");
            if (cJSON_IsNumber(spAtk)) info->baseSpAttack = spAtk->valueint;
            
            cJSON *spDef = cJSON_GetObjectItem(specObj, "base_sp_defense");
            if (cJSON_IsNumber(spDef)) info->baseSpDefense = spDef->valueint;
            
            fprintf(stderr, "[Mods]   Loaded species override %d from %s\n", species, mod->id);
        }
    }

    cJSON_Delete(root);
    extern void free(void*);
    free(jsonStr);
}

void ModStats_Shutdown(void) {
    // Stats are reset in InitRAMShadows at startup
}

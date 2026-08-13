#include "mod_internal.h"
#include "mod_manager.h"
#include "pokemon.h"

void ModMoves_LoadOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/moves.json", mod->path);
    char *jsonStr = ModManager_ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
        extern void free(void*);
        free(jsonStr);
        return;
    }

    cJSON *movesList = cJSON_GetObjectItem(root, "moves");
    if (cJSON_IsArray(movesList)) {
        cJSON *moveObj;
        cJSON_ArrayForEach(moveObj, movesList) {
            cJSON *idObj = cJSON_GetObjectItem(moveObj, "id");
            if (!cJSON_IsNumber(idObj)) continue;
            u16 move = idObj->valueint;

            // Notice: using 355 as MOVES_COUNT is not strictly needed if we just check against MOVES_COUNT
            // We use MOVES_COUNT which is 355 in pokeemerald
            if (move == 0 || move >= MOVES_COUNT) {
                fprintf(stderr, "[Mods][ERROR] %s: Invalid/unresolved move ID %d; override skipped.\n", mod->id, move);
                continue;
            }

            struct BattleMove *info = &gBattleMoves[move];
            
            cJSON *power = cJSON_GetObjectItem(moveObj, "power");
            if (cJSON_IsNumber(power)) info->power = power->valueint;
            
            cJSON *accuracy = cJSON_GetObjectItem(moveObj, "accuracy");
            if (cJSON_IsNumber(accuracy)) info->accuracy = accuracy->valueint;
            
            cJSON *pp = cJSON_GetObjectItem(moveObj, "pp");
            if (cJSON_IsNumber(pp)) info->pp = pp->valueint;
            
            cJSON *type = cJSON_GetObjectItem(moveObj, "type");
            if (cJSON_IsNumber(type)) info->type = type->valueint;
            
            cJSON *effect = cJSON_GetObjectItem(moveObj, "effect");
            if (cJSON_IsNumber(effect)) info->effect = effect->valueint;

            cJSON *target = cJSON_GetObjectItem(moveObj, "target");
            if (cJSON_IsNumber(target)) info->target = target->valueint;
            
            cJSON *priority = cJSON_GetObjectItem(moveObj, "priority");
            if (cJSON_IsNumber(priority)) info->priority = priority->valueint;

            fprintf(stderr, "[Mods]   Loaded move override %d from %s\n", move, mod->id);
        }
    }

    cJSON_Delete(root);
    extern void free(void*);
    free(jsonStr);
}

void ModMoves_Shutdown(void) {
}

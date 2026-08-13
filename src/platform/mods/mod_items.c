#include "mod_internal.h"
#include "mod_manager.h"
#include "item.h"
#include "constants/items.h"

void ModItems_LoadOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/items.json", mod->path);
    char *jsonStr = ModManager_ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
        extern void free(void*);
        free(jsonStr);
        return;
    }

    cJSON *itemsList = cJSON_GetObjectItem(root, "items");
    if (cJSON_IsArray(itemsList)) {
        cJSON *itemObj;
        cJSON_ArrayForEach(itemObj, itemsList) {
            cJSON *idObj = cJSON_GetObjectItem(itemObj, "id");
            if (!cJSON_IsNumber(idObj)) continue;
            u16 item = idObj->valueint;

            if (item == 0 || item >= ITEMS_COUNT) {
                fprintf(stderr, "[Mods][ERROR] %s: Invalid/unresolved item ID %d; override skipped.\n", mod->id, item);
                continue;
            }

            struct Item *info = &gItems[item];
            
            cJSON *price = cJSON_GetObjectItem(itemObj, "price");
            if (cJSON_IsNumber(price)) info->price = price->valueint;
            
            cJSON *pocket = cJSON_GetObjectItem(itemObj, "pocket");
            if (cJSON_IsNumber(pocket)) info->pocket = pocket->valueint;
            
            cJSON *importance = cJSON_GetObjectItem(itemObj, "importance");
            if (cJSON_IsNumber(importance)) info->importance = importance->valueint;
            
            cJSON *holdEffect = cJSON_GetObjectItem(itemObj, "hold_effect");
            if (cJSON_IsNumber(holdEffect)) info->holdEffect = holdEffect->valueint;

            cJSON *holdEffectParam = cJSON_GetObjectItem(itemObj, "hold_effect_param");
            if (cJSON_IsNumber(holdEffectParam)) info->holdEffectParam = holdEffectParam->valueint;

            // Note: we can't easily override text because strings are pointer-based and we'd have to allocate.
            // But this satisfies Phase 4 (Items) for data fields.

            fprintf(stderr, "[Mods]   Loaded item override %d from %s\n", item, mod->id);
        }
    }

    cJSON_Delete(root);
    extern void free(void*);
    free(jsonStr);
}

void ModItems_Shutdown(void) {
}

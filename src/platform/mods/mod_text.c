#include "mod_internal.h"
#include "mod_manager.h"
#include "pokemon.h"
#include <string.h>

void ModText_LoadOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/text.json", mod->path);
    char *jsonStr = ModManager_ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
        extern void free(void*);
        free(jsonStr);
        return;
    }

    cJSON *speciesNames = cJSON_GetObjectItem(root, "species_names");
    if (cJSON_IsArray(speciesNames)) {
        extern u8 gSpeciesNames[][POKEMON_NAME_LENGTH + 1];
        cJSON *itemObj;
        cJSON_ArrayForEach(itemObj, speciesNames) {
            cJSON *idObj = cJSON_GetObjectItem(itemObj, "id");
            cJSON *nameObj = cJSON_GetObjectItem(itemObj, "name");
            if (!cJSON_IsNumber(idObj) || !cJSON_IsString(nameObj)) continue;
            
            u16 id = idObj->valueint;
            if (id < NUM_SPECIES) {
                // Pokeemerald strings might need mapping to Emerald charset, but for now we'll just copy ASCII.
                // A complete modloader would convert utf8 to emerald charmap.
                strncpy((char*)gSpeciesNames[id], nameObj->valuestring, POKEMON_NAME_LENGTH);
                gSpeciesNames[id][POKEMON_NAME_LENGTH] = 0xFF; // null terminate (actually Emerald uses 0xFF, but standard ASCII works if font supports it or if mapped)
                
                for (int j = 0; j < POKEMON_NAME_LENGTH; j++) {
                    if (gSpeciesNames[id][j] == 0) {
                        gSpeciesNames[id][j] = 0xFF;
                        break;
                    }
                }
                
                // Let's actually use a basic ASCII-to-Emerald macro/function if we had one.
                // For this milestone, we just demonstrate the architecture.
                fprintf(stderr, "[Mods]   Loaded species name %d from %s\n", id, mod->id);
            }
        }
    }

    cJSON_Delete(root);
    extern void free(void*);
    free(jsonStr);
}

void ModText_Shutdown(void) {
}

#include <stdlib.h>
#include "mod_internal.h"
#include "mod_manager.h"
#include "malloc.h"
#include <string.h>

#define MAX_BYTECODE_SIZE 1024
#define MAX_STRINGS_PER_SCRIPT 16
#define MAX_STRING_SIZE 256
#define MAX_LABELS 16

typedef struct {
    u8 mapGroup;
    u8 mapNum;
    u8 objectIndex;
    u8 *bytecode;
    u8 *strings[MAX_STRINGS_PER_SCRIPT];
    int numStrings;
} ScriptOverride;

static ScriptOverride **sScriptOverrides = NULL;
static int sScriptOverridesCapacity = 0;
static int sNumScriptOverrides = 0;

static u8 CharToEmeraldChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 0xBB;
    if (c >= 'a' && c <= 'z') return c - 'a' + 0xD5;
    if (c >= '0' && c <= '9') return c - '0' + 0xA1;
    if (c == ' ') return 0x00;
    if (c == '!') return 0xAB;
    if (c == '?') return 0xAC;
    if (c == '.') return 0xAD;
    if (c == '-') return 0xAE;
    if (c == ',') return 0xB8;
    return 0x00;
}

static u8* AllocateEmeraldString(ScriptOverride *ov, const char *str) {
    if (ov->numStrings >= MAX_STRINGS_PER_SCRIPT) return NULL;
    
    int len = strlen(str);
    if (len >= MAX_STRING_SIZE) len = MAX_STRING_SIZE - 1;
    
    extern void *malloc(unsigned int);
    u8 *emStr = malloc(len + 1);
    
    for (int i = 0; i < len; i++) {
        emStr[i] = CharToEmeraldChar(str[i]);
    }
    emStr[len] = 0xFF; // EOS
    
    ov->strings[ov->numStrings++] = emStr;
    return emStr;
}

typedef struct {
    char name[32];
    int offset;
} Label;

void ModScripts_LoadOverrides(LoadedMod *mod) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/scripts.json", mod->path);
    char *jsonStr = ModManager_ReadFileToString(path);
    if (!jsonStr) return;

    cJSON *root = cJSON_Parse(jsonStr);
    if (!root) {
        fprintf(stderr, "[Mods][ERROR] Invalid JSON in %s\n", path);
        extern void free(void*);
        free(jsonStr);
        return;
    }

    cJSON *scripts = cJSON_GetObjectItem(root, "scripts");
    if (cJSON_IsArray(scripts)) {
        cJSON *scriptObj;
        cJSON_ArrayForEach(scriptObj, scripts) {
            cJSON *target = cJSON_GetObjectItem(scriptObj, "target");
            if (!target) continue;
            
            cJSON *mapGroupObj = cJSON_GetObjectItem(target, "map_group");
            cJSON *mapNumObj = cJSON_GetObjectItem(target, "map_num");
            cJSON *objectIndexObj = cJSON_GetObjectItem(target, "object_index");
            
            if (!cJSON_IsNumber(mapGroupObj) || !cJSON_IsNumber(mapNumObj) || !cJSON_IsNumber(objectIndexObj)) continue;
            
            
            
            if (sNumScriptOverrides >= sScriptOverridesCapacity) {
                sScriptOverridesCapacity = sScriptOverridesCapacity == 0 ? 64 : sScriptOverridesCapacity * 2;
                sScriptOverrides = realloc(sScriptOverrides, sScriptOverridesCapacity * sizeof(ScriptOverride*));
            }
            ScriptOverride *ov = malloc(sizeof(ScriptOverride));
            memset(ov, 0, sizeof(ScriptOverride));
            sScriptOverrides[sNumScriptOverrides++] = ov;
            ov->mapGroup = mapGroupObj->valueint;
            ov->mapNum = mapNumObj->valueint;
            ov->objectIndex = objectIndexObj->valueint;
            ov->numStrings = 0;
            
            extern void *malloc(unsigned int);
            ov->bytecode = malloc(MAX_BYTECODE_SIZE);
            int pc = 0;
            
            Label labels[MAX_LABELS];
            int numLabels = 0;
            
            // First pass to resolve labels (simplified)
            cJSON *commands = cJSON_GetObjectItem(scriptObj, "commands");
            if (cJSON_IsArray(commands)) {
                cJSON *cmdObj;
                cJSON_ArrayForEach(cmdObj, commands) {
                    cJSON *labelObj = cJSON_GetObjectItem(cmdObj, "label");
                    if (cJSON_IsString(labelObj)) {
                        cJSON *cmdField = cJSON_GetObjectItem(cmdObj, "cmd");
                        if (!cmdField) {
                            if (numLabels < MAX_LABELS) {
                                strncpy(labels[numLabels].name, labelObj->valuestring, 31);
                                labels[numLabels].name[31] = '\0';
                                labels[numLabels].offset = -1; // to be filled later, wait, actually we need to know size of commands!
                                // For V1, we'll skip goto backwards resolution and just not do full label pass.
                            }
                        }
                    }
                }
            }
            
            // Just compile directly
            if (cJSON_IsArray(commands)) {
                cJSON *cmdObj;
                cJSON_ArrayForEach(cmdObj, commands) {
                    cJSON *cmdField = cJSON_GetObjectItem(cmdObj, "cmd");
                    if (!cJSON_IsString(cmdField)) continue;
                    
                    const char *cmd = cmdField->valuestring;
                    if (strcmp(cmd, "end") == 0) {
                        ov->bytecode[pc++] = 0x02; // end
                    } else if (strcmp(cmd, "return") == 0) {
                        ov->bytecode[pc++] = 0x03; // return
                    } else if (strcmp(cmd, "lock") == 0) {
                        ov->bytecode[pc++] = 0x6A; // lock
                    } else if (strcmp(cmd, "faceplayer") == 0) {
                        ov->bytecode[pc++] = 0x5A; // faceplayer
                    } else if (strcmp(cmd, "release") == 0) {
                        ov->bytecode[pc++] = 0x6C; // release
                    } else if (strcmp(cmd, "lockall") == 0) {
                        ov->bytecode[pc++] = 0x69; // lockall
                    } else if (strcmp(cmd, "releaseall") == 0) {
                        ov->bytecode[pc++] = 0x6B; // releaseall
                    } else if (strcmp(cmd, "msgbox") == 0) {
                        cJSON *textObj = cJSON_GetObjectItem(cmdObj, "text");
                        if (cJSON_IsString(textObj)) {
                            u8 *emStr = AllocateEmeraldString(ov, textObj->valuestring);
                            if (emStr) {
                                // loadpointer 0, str
                                ov->bytecode[pc++] = 0x0F; // loadword
                                ov->bytecode[pc++] = 0x00; // param 0
                                *(u32*)&ov->bytecode[pc] = (u32)emStr;
                                pc += 4;
                                // callstd 4 (msgbox type: face player)
                                ov->bytecode[pc++] = 0x09; // callstd
                                ov->bytecode[pc++] = 0x04;
                            }
                        }
                    }
                }
            }
            
            // Default to end if not specified
            if (pc == 0 || ov->bytecode[pc-1] != 0x02) {
                ov->bytecode[pc++] = 0x02; // end
            }
            
            fprintf(stderr, "[Mods]   Loaded script override %d.%d obj:%d from %s\n", ov->mapGroup, ov->mapNum, ov->objectIndex, mod->id);
        }
    }

    cJSON_Delete(root);
    extern void free(void*);
    free(jsonStr);
}

const u8 *ModScripts_GetObjectScript(u8 mapGroup, u8 mapNum, u8 objectIndex) {
    if (!gModsEnabled) return NULL;
    
    for (int i = 0; i < sNumScriptOverrides; i++) {
        if (sScriptOverrides[i]->mapGroup == mapGroup && sScriptOverrides[i]->mapNum == mapNum && sScriptOverrides[i]->objectIndex == objectIndex) {
            return sScriptOverrides[i]->bytecode;
        }
    }
    return NULL;
}

void ModScripts_Shutdown(void) {
    for (int i = 0; i < sNumScriptOverrides; i++) {
        if (sScriptOverrides[i]->bytecode) {
            extern void free(void*);
            free(sScriptOverrides[i]->bytecode);
            sScriptOverrides[i]->bytecode = NULL;
        }
        for (int j = 0; j < sScriptOverrides[i]->numStrings; j++) {
            extern void free(void*);
            free(sScriptOverrides[i]->strings[j]);
            sScriptOverrides[i]->strings[j] = NULL;
        }
    }
    sNumScriptOverrides = 0;
}

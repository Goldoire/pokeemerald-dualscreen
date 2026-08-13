#ifndef GUARD_MOD_INTERNAL_H
#define GUARD_MOD_INTERNAL_H

#include "global.h"
#include "cJSON.h"

// Expose LoadedMod so sub-modules can access mod paths and IDs
typedef struct {
    char id[64];
    int priority;
    char path[256];
} LoadedMod;

// Helper to read a file into a newly allocated string (must be free'd)
char* ModManager_ReadFileToString(const char *path);

// Modules should implement these
void ModEncounters_LoadOverrides(LoadedMod *mod);
void ModEncounters_Shutdown(void);

void ModStats_LoadOverrides(LoadedMod *mod);
void ModStats_Shutdown(void);

void ModMoves_LoadOverrides(LoadedMod *mod);
void ModMoves_Shutdown(void);

void ModItems_LoadOverrides(LoadedMod *mod);
void ModItems_Shutdown(void);

void ModText_LoadOverrides(LoadedMod *mod);
void ModMaps_LoadOverrides(LoadedMod *mod);
void ModScripts_LoadOverrides(LoadedMod *mod);
const u8 *ModScripts_GetObjectScript(u8 mapGroup, u8 mapNum, u8 objectIndex);
void ModText_Shutdown(void);
void ModMaps_Shutdown(void);
void ModScripts_Shutdown(void);

void ModAudio_LoadOverrides(LoadedMod *mod);
void ModAudio_Shutdown(void);

void ModMaps_LoadOverrides(LoadedMod *mod);
void ModScripts_LoadOverrides(LoadedMod *mod);
const u8 *ModScripts_GetObjectScript(u8 mapGroup, u8 mapNum, u8 objectIndex);
void ModMaps_Shutdown(void);
void ModScripts_Shutdown(void);

void ModScripts_LoadOverrides(LoadedMod *mod);
void ModScripts_Shutdown(void);

#endif // GUARD_MOD_INTERNAL_H

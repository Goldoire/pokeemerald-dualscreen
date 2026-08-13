#ifndef GUARD_MOD_MANAGER_H
#define GUARD_MOD_MANAGER_H

#include "global.h"
#include "data.h"
#include <stdbool.h>

extern bool8 gModsEnabled;
extern char *gActiveModSelector;

void ModManager_Init(void);
void ModManager_Shutdown(void);
bool8 ModManager_IsEnabled(void);

// Trainer Override API
const struct Trainer *ModManager_GetTrainer(u16 trainerId);

// Graphics Override API
// Returns TRUE if overridden and copies the raw 4bpp uncompressed data into destBuffer.
bool8 ModManager_GetTrainerFrontPicOverride(u16 trainerPicId, void *destBuffer);


// Starter Override API
u16 ModManager_GetStarterSpecies(u8 slot, u16 vanillaSpecies);
u8 ModManager_GetStarterLevel(u8 slot, u8 vanillaLevel);


struct WildPokemonHeader;
const struct WildPokemonHeader *ModManager_GetWildMonHeader(u16 headerId, const struct WildPokemonHeader *vanilla);
const struct WildPokemonHeader *ModManager_GetWildMonHeaderByMap(u8 mapGroup, u8 mapNum, const struct WildPokemonHeader *vanilla);

const struct MapHeader *ModManager_GetMapHeaderByMap(u16 mapGroup, u16 mapNum, const struct MapHeader *vanilla);

#endif // GUARD_MOD_MANAGER_H



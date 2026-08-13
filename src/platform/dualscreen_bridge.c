#ifdef PLATFORM_SDL2
// Second-screen state bridge. Snapshots live game state into a JSON string
// once per frame window (throttled), for the Android bottom-screen UI or a
// desktop debug consumer. Runs on the SDL frame thread at the vblank point;
// all game reads go through the game's own accessors.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include <jni.h>
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif

#include "global.h"
#include "fonts.h"
#include "main.h"
#include "platform.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "battle.h"
#include "battle_anim.h"
#include "data.h"
#include "item.h"
#include "event_data.h"
#include "money.h"
#include "overworld.h"
#include "pokedex.h"
#include "pokemon_summary_screen.h"
#include "region_map.h"
#include "battle_main.h"
#include "platform/dualscreen.h"
#include "constants/battle.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/items.h"
#include "constants/pokemon.h"
#include "constants/region_map_sections.h"
#include "constants/trainers.h"

extern u32 CountPlayerTrainerStars(void);

#define SNAPSHOT_CAPACITY 24576
#define SNAPSHOT_FRAME_INTERVAL 8
#define VIRTUAL_KEY_QUEUE_SIZE 64

static char sBuffers[2][SNAPSHOT_CAPACITY];
static int sFrontBuffer;
static SDL_mutex *sSnapshotMutex;
static u32 sFrameCounter;

static u16 sVirtualKeys[VIRTUAL_KEY_QUEUE_SIZE];
static SDL_SpinLock sVirtualKeyLock;
static int sVirtualKeyHead;
static int sVirtualKeyCount;

#ifdef __ANDROID__
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

// Asset-hole support: distributable builds ship libmain.so with all
// ROM-matching asset bytes zeroed plus a manifest of (vaddr, size,
// romOffset) ranges (see tools/dualscreen/make_asset_holes.py). At first
// launch the Java side extracts those ranges from the user's own ROM into
// assets.bin; this restores them in memory before any game code runs,
// reproducing the original library exactly.
#define BASEROM_SIZE (16 * 1024 * 1024)

void DualScreen_FillAssets(const char *prefPath)
{
    char path[1024];
    FILE *manifest;
    FILE *romFile;
    unsigned char *rom;
    Dl_info info;
    unsigned char *base;
    unsigned char header[24];
    u32 count, i;
    long pageSize = sysconf(_SC_PAGESIZE);

    snprintf(path, sizeof(path), "%sasset_manifest.bin", prefPath);
    manifest = fopen(path, "rb");
    if (manifest == NULL)
        return; // development build: assets are compiled in
    snprintf(path, sizeof(path), "%sbaserom.gba", prefPath);
    romFile = fopen(path, "rb");
    if (romFile == NULL)
    {
        fclose(manifest);
        printf("[Assets] manifest present but baserom.gba missing\n");
        return;
    }
    rom = malloc(BASEROM_SIZE);
    if (rom == NULL || fread(rom, 1, BASEROM_SIZE, romFile) != BASEROM_SIZE
     || fread(header, 1, sizeof(header), manifest) != sizeof(header)
     || dladdr((void *)DualScreen_FillAssets, &info) == 0)
    {
        free(rom);
        fclose(manifest);
        fclose(romFile);
        return;
    }
    base = (unsigned char *)info.dli_fbase;

    count = header[20] | (header[21] << 8) | (header[22] << 16) | ((u32)header[23] << 24);
    for (i = 0; i < count; i++)
    {
        u32 entry[3];
        unsigned char *dest;
        uintptr_t pageStart;
        size_t protLen;
        if (fread(entry, 1, sizeof(entry), manifest) != sizeof(entry))
            break;
        if (entry[2] + entry[1] > BASEROM_SIZE)
            continue;
        dest = base + entry[0];
        pageStart = (uintptr_t)dest & ~(pageSize - 1);
        protLen = ((uintptr_t)dest + entry[1] + pageSize - 1 - pageStart) & ~(pageSize - 1);
        mprotect((void *)pageStart, protLen, PROT_READ | PROT_WRITE);
        memcpy(dest, rom + entry[2], entry[1]);
    }
    printf("[Assets] filled %u asset ranges from baserom\n", (unsigned)count);
    free(rom);
    fclose(manifest);
    fclose(romFile);
}
#endif // __ANDROID__

u32 DualScreen_BattleUiActive(void)
{
    // User preference: keep the classic top-screen battle menus.
    if (Platform_GetSetting(PLATFORM_SETTING_BATTLE_UI_TOP))
        return FALSE;
    // Battles with nonstandard menus (Safari's BALL/BAIT/ROCK, Wally's
    // scripted tutorial) or strict timing (link) keep the classic top-screen
    // UI; the bottom screen then just shows the status cards.
    if (gMain.inBattle
     && (gBattleTypeFlags & (BATTLE_TYPE_SAFARI | BATTLE_TYPE_WALLY_TUTORIAL | BATTLE_TYPE_LINK)))
        return FALSE;
    return TRUE;
}

u16 DualScreen_ConsumeVirtualKeys(void)
{
    u16 keys = 0;
    SDL_AtomicLock(&sVirtualKeyLock);
    if (sVirtualKeyCount > 0)
    {
        keys = sVirtualKeys[sVirtualKeyHead];
        sVirtualKeyHead = (sVirtualKeyHead + 1) % VIRTUAL_KEY_QUEUE_SIZE;
        sVirtualKeyCount--;
    }
    SDL_AtomicUnlock(&sVirtualKeyLock);
    return keys;
}

static void QueueVirtualKeys(u16 keys)
{
    SDL_AtomicLock(&sVirtualKeyLock);
    if (sVirtualKeyCount < VIRTUAL_KEY_QUEUE_SIZE)
    {
        sVirtualKeys[(sVirtualKeyHead + sVirtualKeyCount) % VIRTUAL_KEY_QUEUE_SIZE] = keys;
        sVirtualKeyCount++;
    }
    SDL_AtomicUnlock(&sVirtualKeyLock);
}

// ---------------------------------------------------------------------------
// GBA charset -> ASCII
// ---------------------------------------------------------------------------

static char DecodeGbaChar(u8 c)
{
    if (c == CHAR_SPACE || c == CHAR_SPACER)
        return ' ';
    if (c >= CHAR_0 && c <= CHAR_9)
        return '0' + (c - CHAR_0);
    if (c >= CHAR_A && c <= CHAR_Z)
        return 'A' + (c - CHAR_A);
    if (c >= CHAR_a && c <= CHAR_z)
        return 'a' + (c - CHAR_a);
    switch (c)
    {
    case CHAR_PERIOD:          return '.';
    case CHAR_HYPHEN:          return '-';
    case CHAR_COMMA:           return ',';
    case CHAR_SLASH:           return '/';
    case CHAR_COLON:           return ':';
    case CHAR_EXCL_MARK:       return '!';
    case CHAR_QUESTION_MARK:   return '?';
    case CHAR_SGL_QUOTE_RIGHT: return '\'';
    case CHAR_MALE:            return 'm';
    case CHAR_FEMALE:          return 'f';
    case CHAR_LEFT_PAREN:      return '(';
    case CHAR_RIGHT_PAREN:     return ')';
    case CHAR_ELLIPSIS:        return '~';
    }
    return 0; // unmapped; skipped
}

static void DecodeGbaString(char *dest, int destSize, const u8 *src, int maxSrcLen)
{
    int out = 0;
    int i;

    for (i = 0; src != NULL && i < maxSrcLen && out < destSize - 1; i++)
    {
        u8 c = src[i];
        char decoded;
        if (c == EOS)
            break;
        // Control codes consume following bytes; just stop at them.
        if (c == CHAR_NEWLINE || c == CHAR_PROMPT_SCROLL || c == CHAR_PROMPT_CLEAR
         || c == CHAR_KEYPAD_ICON || c == CHAR_EXTRA_SYMBOL
         || c == EXT_CTRL_CODE_BEGIN || c == PLACEHOLDER_BEGIN || c == CHAR_DYNAMIC)
            break;
        decoded = DecodeGbaChar(c);
        if (decoded != 0)
            dest[out++] = decoded;
    }
    dest[out] = '\0';
}

// ---------------------------------------------------------------------------
// JSON assembly
// ---------------------------------------------------------------------------

struct JsonWriter
{
    char *buffer;
    int length;
    int capacity;
};

static void JsonPut(struct JsonWriter *w, const char *format, ...)
{
    va_list args;
    int written;

    if (w->length >= w->capacity - 1)
        return;
    va_start(args, format);
    written = vsnprintf(w->buffer + w->length, w->capacity - w->length, format, args);
    va_end(args);
    if (written > 0)
    {
        w->length += written;
        if (w->length > w->capacity - 1)
            w->length = w->capacity - 1;
    }
}

// Decoded strings are plain ASCII; only quote and backslash need escaping.
static void JsonPutString(struct JsonWriter *w, const char *s)
{
    JsonPut(w, "\"");
    for (; *s != '\0'; s++)
    {
        if (*s == '"' || *s == '\\')
            JsonPut(w, "\\%c", *s);
        else
            JsonPut(w, "%c", *s);
    }
    JsonPut(w, "\"");
}

static void WritePartyMonJson(struct JsonWriter *w, struct Pokemon *mon)
{
    char text[32];
    u16 species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG);
    bool32 isEgg = (GetMonData(mon, MON_DATA_IS_EGG) != 0) || species == SPECIES_EGG;
    u16 heldItem = GetMonData(mon, MON_DATA_HELD_ITEM);
    u8 ppBonuses = GetMonData(mon, MON_DATA_PP_BONUSES);
    u8 gender = GetMonGender(mon);
    u16 displaySpecies = isEgg ? GetMonData(mon, MON_DATA_SPECIES) : species;
    int i;

    JsonPut(w, "{\"species\":%u,\"dex\":%u,", displaySpecies,
            isEgg ? 0 : SpeciesToNationalPokedexNum(species));

    GetSpeciesName((u8 *)text, species);
    // GetSpeciesName writes in the GBA charset into a u8 buffer.
    {
        char decoded[24];
        DecodeGbaString(decoded, sizeof(decoded), (u8 *)text, POKEMON_NAME_LENGTH + 1);
        JsonPut(w, "\"name\":");
        JsonPutString(w, decoded);
        JsonPut(w, ",");
    }
    {
        u8 nickRaw[POKEMON_NAME_BUFFER_SIZE];
        char nick[24];
        GetMonData(mon, MON_DATA_NICKNAME, nickRaw);
        DecodeGbaString(nick, sizeof(nick), nickRaw, POKEMON_NAME_LENGTH + 1);
        JsonPut(w, "\"nick\":");
        JsonPutString(w, nick);
        JsonPut(w, ",");
    }

    JsonPut(w, "\"level\":%u,\"hp\":%u,\"maxHp\":%u,\"status\":%u,",
            GetMonData(mon, MON_DATA_LEVEL),
            GetMonData(mon, MON_DATA_HP),
            GetMonData(mon, MON_DATA_MAX_HP),
            (unsigned)GetMonData(mon, MON_DATA_STATUS));

    JsonPut(w, "\"item\":%u,\"itemName\":", heldItem);
    if (heldItem != ITEM_NONE && heldItem < ITEMS_COUNT)
    {
        char itemName[24];
        DecodeGbaString(itemName, sizeof(itemName), GetItemName(heldItem), ITEM_NAME_LENGTH);
        JsonPutString(w, itemName);
    }
    else
    {
        JsonPut(w, "\"\"");
    }

    JsonPut(w, ",\"isEgg\":%d,\"gender\":%d,", isEgg ? 1 : 0,
            gender == MON_MALE ? 0 : gender == MON_FEMALE ? 1 : 2);

    if (species < NUM_SPECIES)
        JsonPut(w, "\"types\":[%u,%u],", gSpeciesInfo[species].types[0], gSpeciesInfo[species].types[1]);
    else
        JsonPut(w, "\"types\":[0,0],");

    // Detail-view extras: computed stats, experience progress, nature, ability.
    if (!isEgg && species < NUM_SPECIES)
    {
        u32 exp = GetMonData(mon, MON_DATA_EXP);
        u8 level = GetMonData(mon, MON_DATA_LEVEL);
        u8 growth = gSpeciesInfo[species].growthRate;
        int expPct = 100;
        u8 abilityId;
        char text2[24];

        if (level < MAX_LEVEL)
        {
            u32 base = gExperienceTables[growth][level];
            u32 next = gExperienceTables[growth][level + 1];
            if (next > base)
                expPct = (int)(((u64)(exp - base)) * 100 / (next - base));
            if (expPct < 0) expPct = 0;
            if (expPct > 100) expPct = 100;
        }
        JsonPut(w, "\"expPct\":%d,", expPct);
        JsonPut(w, "\"stats\":[%u,%u,%u,%u,%u],",
                GetMonData(mon, MON_DATA_ATK), GetMonData(mon, MON_DATA_DEF),
                GetMonData(mon, MON_DATA_SPEED),
                GetMonData(mon, MON_DATA_SPATK), GetMonData(mon, MON_DATA_SPDEF));

        DecodeGbaString(text2, sizeof(text2), gNatureNamePointers[GetNature(mon)], 12);
        JsonPut(w, "\"nature\":");
        JsonPutString(w, text2);
        abilityId = GetMonAbility(mon);
        DecodeGbaString(text2, sizeof(text2), gAbilityNames[abilityId], 16);
        JsonPut(w, ",\"ability\":");
        JsonPutString(w, text2);
        JsonPut(w, ",");
    }

    JsonPut(w, "\"moves\":[");
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = GetMonData(mon, MON_DATA_MOVE1 + i);
        char moveName[24];
        if (move == MOVE_NONE || move >= MOVES_COUNT)
            continue;
        if (i > 0 && w->buffer[w->length - 1] == '}')
            JsonPut(w, ",");
        DecodeGbaString(moveName, sizeof(moveName), gMoveNames[move], MOVE_NAME_LENGTH + 1);
        JsonPut(w, "{\"id\":%u,\"name\":", move);
        JsonPutString(w, moveName);
        JsonPut(w, ",\"pp\":%u,\"maxPp\":%u,\"type\":%u}",
                GetMonData(mon, MON_DATA_PP1 + i),
                CalculatePPWithBonus(move, ppBonuses, i),
                gBattleMoves[move].type);
    }
    JsonPut(w, "]}");
}

static void WriteBattleMonJson(struct JsonWriter *w, struct BattlePokemon *mon)
{
    char text[24];
    u16 species = mon->species;
    int i;

    JsonPut(w, "{\"species\":%u,", species);
    if (species < NUM_SPECIES)
    {
        char decoded[24];
        GetSpeciesName((u8 *)text, species);
        DecodeGbaString(decoded, sizeof(decoded), (u8 *)text, POKEMON_NAME_LENGTH + 1);
        JsonPut(w, "\"name\":");
        JsonPutString(w, decoded);
        JsonPut(w, ",");
    }
    else
    {
        JsonPut(w, "\"name\":\"\",");
    }
    {
        char nick[24];
        DecodeGbaString(nick, sizeof(nick), mon->nickname, POKEMON_NAME_LENGTH + 1);
        JsonPut(w, "\"nick\":");
        JsonPutString(w, nick);
        JsonPut(w, ",");
    }
    JsonPut(w, "\"level\":%u,\"hp\":%u,\"maxHp\":%u,\"status\":%u,",
            mon->level, mon->hp, mon->maxHP, (unsigned)mon->status1);
    JsonPut(w, "\"types\":[%u,%u],", mon->types[0], mon->types[1]);
    JsonPut(w, "\"moves\":[");
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        u16 move = mon->moves[i];
        char moveName[24];
        if (move == MOVE_NONE || move >= MOVES_COUNT)
            continue;
        if (i > 0 && w->buffer[w->length - 1] == '}')
            JsonPut(w, ",");
        DecodeGbaString(moveName, sizeof(moveName), gMoveNames[move], MOVE_NAME_LENGTH + 1);
        JsonPut(w, "{\"id\":%u,\"name\":", move);
        JsonPutString(w, moveName);
        JsonPut(w, ",\"pp\":%u,\"maxPp\":%u,\"type\":%u}",
                mon->pp[i],
                CalculatePPWithBonus(move, mon->ppBonuses, i),
                gBattleMoves[move].type);
    }
    JsonPut(w, "]}");
}

// Reads pockets straight from the save block rather than through
// gBagPockets: those runtime pointers are only wired by
// SetBagItemsPointers() and hold garbage before then (caused a crash).
static void WriteBagJson(struct JsonWriter *w)
{
    const struct ItemSlot *pockets[POCKETS_COUNT];
    int capacities[POCKETS_COUNT];
    int pocket;

    pockets[ITEMS_POCKET] = gSaveBlock1Ptr->bagPocket_Items;
    capacities[ITEMS_POCKET] = BAG_ITEMS_COUNT;
    pockets[BALLS_POCKET] = gSaveBlock1Ptr->bagPocket_PokeBalls;
    capacities[BALLS_POCKET] = BAG_POKEBALLS_COUNT;
    pockets[TMHM_POCKET] = gSaveBlock1Ptr->bagPocket_TMHM;
    capacities[TMHM_POCKET] = BAG_TMHM_COUNT;
    pockets[BERRIES_POCKET] = gSaveBlock1Ptr->bagPocket_Berries;
    capacities[BERRIES_POCKET] = BAG_BERRIES_COUNT;
    pockets[KEYITEMS_POCKET] = gSaveBlock1Ptr->bagPocket_KeyItems;
    capacities[KEYITEMS_POCKET] = BAG_KEYITEMS_COUNT;

    JsonPut(w, "\"bag\":[");
    for (pocket = 0; pocket < POCKETS_COUNT; pocket++)
    {
        int pos;
        bool32 first = TRUE;
        if (pocket > 0)
            JsonPut(w, ",");
        JsonPut(w, "[");
        for (pos = 0; pos < capacities[pocket]; pos++)
        {
            u16 itemId = pockets[pocket][pos].itemId;
            u16 quantity;
            char itemName[24];
            if (itemId == ITEM_NONE || itemId >= ITEMS_COUNT)
                continue;
            // Bag quantities are XOR-encrypted with the save's key.
            quantity = pockets[pocket][pos].quantity ^ (u16)gSaveBlock2Ptr->encryptionKey;
            DecodeGbaString(itemName, sizeof(itemName), GetItemName(itemId), ITEM_NAME_LENGTH);
            if (!first)
                JsonPut(w, ",");
            first = FALSE;
            JsonPut(w, "{\"id\":%u,\"n\":", itemId);
            JsonPutString(w, itemName);
            JsonPut(w, ",\"q\":%u}", quantity);
        }
        JsonPut(w, "]");
    }
    JsonPut(w, "],");
}

static bool32 IsInGame(void)
{
    // Party/bag EWRAM holds garbage until a game is actually running (e.g. on
    // the title screen of a save). The play-time counter only ticks in-game,
    // so latch once it has been observed advancing.
    static bool32 sSeenGameplay;
    static u8 sLastPlayTimeSeconds;
    static u8 sPlayTimeTicks;

    if (gSaveBlock1Ptr == NULL || gSaveBlock2Ptr == NULL)
        return FALSE;
    if (gMain.callback2 == CB2_Overworld)
        sSeenGameplay = TRUE;
    if (gSaveBlock2Ptr->playTimeSeconds != sLastPlayTimeSeconds)
    {
        sLastPlayTimeSeconds = gSaveBlock2Ptr->playTimeSeconds;
        if (++sPlayTimeTicks >= 2)
            sSeenGameplay = TRUE;
    }
    if (!sSeenGameplay)
        return FALSE;
    // Read-only checks only: this runs on the frame thread, so nothing here
    // may mutate game state (CalculatePlayerPartyCount writes the count).
    if (gPlayerPartyCount == 0 || gPlayerPartyCount > PARTY_SIZE)
        return FALSE;
    return FlagGet(FLAG_SYS_POKEMON_GET);
}

static void BuildSnapshot(char *buffer, int capacity)
{
    struct JsonWriter writer = { buffer, 0, capacity };
    struct JsonWriter *w = &writer;
    bool32 inGame = IsInGame();
    int i;

    JsonPut(w, "{\"v\":1,\"inGame\":%d,\"inBattle\":%d,", inGame ? 1 : 0,
            (inGame && gMain.inBattle) ? 1 : 0);

    if (inGame)
    {
        char text[32];
        u8 mapsec = gMapHeader.regionMapSectionId;
        int badges = 0;

        for (i = 0; i < NUM_BADGES; i++)
        {
            if (FlagGet(FLAG_BADGE01_GET + i))
                badges++;
        }

        JsonPut(w, "\"player\":{");
        {
            char name[16];
            DecodeGbaString(name, sizeof(name), gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH + 1);
            JsonPut(w, "\"name\":");
            JsonPutString(w, name);
            JsonPut(w, ",");
        }
        {
            int badgeFlags = 0;
            for (i = 0; i < NUM_BADGES; i++)
            {
                if (FlagGet(FLAG_BADGE01_GET + i))
                    badgeFlags |= 1 << i;
            }
            JsonPut(w, "\"badgeFlags\":%d,\"dexSeen\":%u,\"dexCaught\":%u,",
                    badgeFlags, GetHoennPokedexCount(FLAG_GET_SEEN),
                    GetHoennPokedexCount(FLAG_GET_CAUGHT));
            JsonPut(w, "\"trainerId\":%u,\"stars\":%u,",
                    (unsigned)(gSaveBlock2Ptr->playerTrainerId[0]
                             | (gSaveBlock2Ptr->playerTrainerId[1] << 8)),
                    (unsigned)CountPlayerTrainerStars());
        }
        JsonPut(w, "\"gender\":%d,\"money\":%u,\"badges\":%d,",
                gSaveBlock2Ptr->playerGender, (unsigned)GetMoney(&gSaveBlock1Ptr->money), badges);
        JsonPut(w, "\"hours\":%u,\"minutes\":%u,",
                gSaveBlock2Ptr->playTimeHours, gSaveBlock2Ptr->playTimeMinutes);
        JsonPut(w, "\"mapGroup\":%d,\"mapNum\":%d,\"x\":%d,\"y\":%d,\"mapsec\":%u,",
                gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
                gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y, mapsec);
        if (mapsec < MAPSEC_NONE)
        {
            JsonPut(w, "\"rmx\":%u,\"rmy\":%u,\"rmw\":%u,\"rmh\":%u,",
                    gRegionMapEntries[mapsec].x, gRegionMapEntries[mapsec].y,
                    gRegionMapEntries[mapsec].width, gRegionMapEntries[mapsec].height);
        }
        {
            char mapName[24];
            GetMapName((u8 *)text, mapsec, 0);
            DecodeGbaString(mapName, sizeof(mapName), (u8 *)text, MAP_NAME_LENGTH);
            JsonPut(w, "\"mapName\":");
            JsonPutString(w, mapName);
        }
        JsonPut(w, "},");

        JsonPut(w, "\"party\":[");
        for (i = 0; i < PARTY_SIZE && i < gPlayerPartyCount; i++)
        {
            if (i > 0)
                JsonPut(w, ",");
            WritePartyMonJson(w, &gPlayerParty[i]);
        }
        JsonPut(w, "],");

        WriteBagJson(w);

        if (gMain.inBattle && gBattlersCount > 0 && gBattlersCount <= MAX_BATTLERS_COUNT)
        {
            u8 playerBattler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
            u8 enemyBattler = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
            int menu = 0;

            // In doubles the two player mons pick sequentially; show whichever
            // battler the open menu belongs to.
            if (DualScreen_BattleUiActive())
            {
                s32 menuBattler = DualScreen_PlayerMoveBattler();
                if (menuBattler >= 0)
                    menu = 2;
                else if ((menuBattler = DualScreen_PlayerActionBattler()) >= 0)
                    menu = 1;
                if (menuBattler >= 0 && menuBattler < MAX_BATTLERS_COUNT)
                    playerBattler = menuBattler;
            }

            if (playerBattler < MAX_BATTLERS_COUNT && enemyBattler < MAX_BATTLERS_COUNT)
            {
                JsonPut(w, "\"battle\":{\"kind\":%d,",
                        (gBattleTypeFlags & BATTLE_TYPE_TRAINER) ? 1 : 0);
                JsonPut(w, "\"menu\":%d,\"actionCursor\":%d,\"moveCursor\":%d,",
                        menu, gActionSelectionCursor[playerBattler],
                        gMoveSelectionCursor[playerBattler]);
                JsonPut(w, "\"playerMon\":");
                WriteBattleMonJson(w, &gBattleMons[playerBattler]);
                JsonPut(w, ",\"enemyMon\":");
                WriteBattleMonJson(w, &gBattleMons[enemyBattler]);
                JsonPut(w, "},");
            }
        }
    }

    // Trim a trailing comma before closing.
    if (writer.length > 0 && buffer[writer.length - 1] == ',')
        writer.length--;
    buffer[writer.length] = '\0';
    JsonPut(w, "}");
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

void DualScreen_FrameHook(void)
{
    int backBuffer;

    if (sSnapshotMutex == NULL)
    {
        sSnapshotMutex = SDL_CreateMutex();
        strcpy(sBuffers[0], "{\"v\":1,\"inGame\":0,\"inBattle\":0}");
        strcpy(sBuffers[1], sBuffers[0]);
    }

    // While the bottom screen owns the battle menus, keep the top screen's
    // textbox on the plain full-width message layout: the action/move menu
    // art is never shown at all (BG0 scroll selects which band of the battle
    // textbox is visible), and the "What will {x} do?" prompt is printed
    // into the message window by BattlePutTextOnWindow.
    if (DualScreen_BattleUiActive() && gMain.inBattle
     && (DualScreen_PlayerAtMoveSelect() || DualScreen_PlayerAtActionSelect()))
        gBattle_BG0_Y = 0;

    if (++sFrameCounter % SNAPSHOT_FRAME_INTERVAL != 0)
        return;

    backBuffer = sFrontBuffer ^ 1;
    BuildSnapshot(sBuffers[backBuffer], SNAPSHOT_CAPACITY);

    SDL_LockMutex(sSnapshotMutex);
    sFrontBuffer = backBuffer;
    SDL_UnlockMutex(sSnapshotMutex);

#ifndef __ANDROID__
    if (getenv("DUALSCREEN_DEBUG") != NULL && sFrameCounter % 300 == 0)
        printf("DUALSCREEN %s\n", sBuffers[sFrontBuffer]);
#endif
}

const char *DualScreen_GetSnapshotJson(void)
{
    return sBuffers[sFrontBuffer];
}

// ---------------------------------------------------------------------------
// JNI surface
// ---------------------------------------------------------------------------

#ifdef __ANDROID__

// Enqueue synthetic button states, one entry per frame (0 = release).
JNIEXPORT void JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeQueueKeys(JNIEnv *env, jclass clazz, jintArray masks)
{
    jint buffer[VIRTUAL_KEY_QUEUE_SIZE];
    jsize length;
    jsize i;

    if (masks == NULL)
        return;
    length = (*env)->GetArrayLength(env, masks);
    if (length > VIRTUAL_KEY_QUEUE_SIZE)
        length = VIRTUAL_KEY_QUEUE_SIZE;
    (*env)->GetIntArrayRegion(env, masks, 0, length, buffer);
    for (i = 0; i < length; i++)
        QueueVirtualKeys((u16)buffer[i]);
}

JNIEXPORT jstring JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetSnapshotJson(JNIEnv *env, jclass clazz)
{
    jstring result;
    if (sSnapshotMutex == NULL)
        return (*env)->NewStringUTF(env, "{\"v\":1,\"inGame\":0,\"inBattle\":0}");
    SDL_LockMutex(sSnapshotMutex);
    result = (*env)->NewStringUTF(env, sBuffers[sFrontBuffer]);
    SDL_UnlockMutex(sSnapshotMutex);
    return result;
}

// Implemented in region_map.c: access to the (static) region map graphics.
extern const u16 *DualScreen_GetRegionMapPal(void);
extern const u32 *DualScreen_GetRegionMapGfxLZ(void);
extern const u32 *DualScreen_GetRegionMapTilemapLZ(void);
#include "gba/syscall.h"

static jint Bgr555ToArgb(u16 bgr)
{
    int r = (bgr & 0x1F) << 3;
    int g = ((bgr >> 5) & 0x1F) << 3;
    int b = ((bgr >> 10) & 0x1F) << 3;
    return (jint)((0xFFu << 24) | (r << 16) | (g << 8) | b);
}

// The real Pokenav Hoenn map, composed from the game's own tileset/tilemap:
// 240x160 ARGB pixels. The player marker goes at tile (rmx+1, rmy+2).
// The map is an affine BG: one byte per tile on a 64x64 grid, 8bpp tiles,
// with pixel values indexing the palette loaded at bank 7 (offset 112).
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetRegionMapImage(JNIEnv *env, jclass clazz)
{
    static u8 sGfx[16384];
    static u8 sTilemap[4096];
    jintArray result;
    jint *pixels;
    const u16 *pal = DualScreen_GetRegionMapPal();
    int tx, ty, px, py;

    LZ77UnCompWram(DualScreen_GetRegionMapGfxLZ(), sGfx);
    LZ77UnCompWram(DualScreen_GetRegionMapTilemapLZ(), sTilemap);

    pixels = malloc(240 * 160 * sizeof(jint));
    if (pixels == NULL)
        return NULL;

    for (ty = 0; ty < 20; ty++)
    for (tx = 0; tx < 30; tx++)
    {
        u8 tile = sTilemap[ty * 64 + tx];
        const u8 *src = &sGfx[tile * 64];
        if (tile * 64 >= (int)sizeof(sGfx))
            continue;
        for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
        {
            u8 colorIndex = (u8)(src[py * 8 + px] - 112);
            pixels[(ty * 8 + py) * 240 + tx * 8 + px] = Bgr555ToArgb(pal[colorIndex & 0x1F]);
        }
    }

    result = (*env)->NewIntArray(env, 240 * 160);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, 240 * 160, pixels);
    free(pixels);
    return result;
}

// The game's normal Latin font: one glyph per GBA charcode, 16x16 2bpp.
// Returns [width[0..255], then 256*256 color indices (0 bg, 1 fg, 2 shadow)].
#define FONT_GLYPH_COUNT 256
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetFontAtlas(JNIEnv *env, jclass clazz)
{
    jintArray result;
    jint *data;
    int glyph, tile, py, px;
    int total = FONT_GLYPH_COUNT + FONT_GLYPH_COUNT * 16 * 16;

    data = malloc(total * sizeof(jint));
    if (data == NULL)
        return NULL;

    for (glyph = 0; glyph < FONT_GLYPH_COUNT; glyph++)
    {
        const u16 *rows = gFontNormalLatinGlyphs + 0x20 * glyph;
        jint *out = data + FONT_GLYPH_COUNT + glyph * 256;
        data[glyph] = gFontNormalLatinGlyphWidths[glyph];
        memset(out, 0, 256 * sizeof(jint));
        // Four 8x8 tiles: top-left, top-right, bottom-left, bottom-right.
        // Each u16 is one 8-pixel row at 2bpp, pixel 0 in the low bits.
        for (tile = 0; tile < 4; tile++)
        {
            int baseX = (tile & 1) * 8;
            int baseY = (tile >> 1) * 8;
            const u16 *tileRows = rows + tile * 8;
            for (py = 0; py < 8; py++)
            for (px = 0; px < 8; px++)
                out[(baseY + py) * 16 + baseX + px] = (tileRows[py] >> ((7 - px) * 2)) & 0x3;
        }
    }

    result = (*env)->NewIntArray(env, total);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, total, data);
    free(data);
    return result;
}

// Implemented in trainer_card.c: the trainer card badge graphics.
extern const u16 *DualScreen_GetBadgesPal(void);
extern const u32 *DualScreen_GetBadgesGfxLZ(void);

// All eight badge sprites as ARGB: 8 badges x 16x16 pixels, badge-major.
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetBadges(JNIEnv *env, jclass clazz)
{
    static u8 sGfx[2048];
    const u16 *pal = DualScreen_GetBadgesPal();
    jint pixels[8 * 16 * 16];
    jintArray result;
    int badge, tile, py, px;

    LZ77UnCompWram(DualScreen_GetBadgesGfxLZ(), sGfx);

    // 128x16 sheet: 16 tile columns x 2 tile rows; badge i is columns 2i,2i+1.
    for (badge = 0; badge < 8; badge++)
    for (tile = 0; tile < 4; tile++)
    {
        int tileCol = badge * 2 + (tile & 1);
        int tileRow = tile >> 1;
        const u8 *src = &sGfx[(tileRow * 16 + tileCol) * 32];
        jint *out = &pixels[badge * 256];
        for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
        {
            u8 packed = src[py * 4 + px / 2];
            u8 colorIndex = (px & 1) ? (packed >> 4) : (packed & 0xF);
            int x = (tile & 1) * 8 + px;
            int y = tileRow * 8 + py;
            out[y * 16 + x] = colorIndex == 0 ? 0 : Bgr555ToArgb(pal[colorIndex]);
        }
    }

    result = (*env)->NewIntArray(env, 8 * 16 * 16);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, 8 * 16 * 16, pixels);
    return result;
}

// The player's 64x64 trainer front pic as ARGB pixels.
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetTrainerPic(JNIEnv *env, jclass clazz, jint gender)
{
    static u8 sGfx[4096];
    static u16 sPal[16];
    int picId = gender == 1 ? TRAINER_PIC_MAY : TRAINER_PIC_BRENDAN;
    jint *pixels;
    jintArray result;
    int tileCol, tileRow, py, px;

    LZ77UnCompWram(gTrainerFrontPicTable[picId].data, sGfx);
    LZ77UnCompWram(gTrainerFrontPicPaletteTable[picId].data, sPal);

    pixels = malloc(64 * 64 * sizeof(jint));
    if (pixels == NULL)
        return NULL;
    for (tileRow = 0; tileRow < 8; tileRow++)
    for (tileCol = 0; tileCol < 8; tileCol++)
    {
        const u8 *src = &sGfx[(tileRow * 8 + tileCol) * 32];
        for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
        {
            u8 packed = src[py * 4 + px / 2];
            u8 colorIndex = (px & 1) ? (packed >> 4) : (packed & 0xF);
            pixels[(tileRow * 8 + py) * 64 + tileCol * 8 + px] =
                    colorIndex == 0 ? 0 : Bgr555ToArgb(sPal[colorIndex]);
        }
    }

    result = (*env)->NewIntArray(env, 64 * 64);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, 64 * 64, pixels);
    free(pixels);
    return result;
}

// The full region map location table (static game data), for the Map tab.
JNIEXPORT jstring JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetRegionMapJson(JNIEnv *env, jclass clazz)
{
    static char json[16384];
    struct JsonWriter writer = { json, 0, sizeof(json) };
    struct JsonWriter *w = &writer;
    int mapsec;

    JsonPut(w, "[");
    for (mapsec = 0; mapsec < MAPSEC_NONE; mapsec++)
    {
        char name[24];
        DecodeGbaString(name, sizeof(name), gRegionMapEntries[mapsec].name, MAP_NAME_LENGTH);
        if (mapsec > 0)
            JsonPut(w, ",");
        JsonPut(w, "{\"id\":%d,\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u,\"n\":",
                mapsec, gRegionMapEntries[mapsec].x, gRegionMapEntries[mapsec].y,
                gRegionMapEntries[mapsec].width, gRegionMapEntries[mapsec].height);
        JsonPutString(w, name);
        JsonPut(w, "}");
    }
    JsonPut(w, "]");
    return (*env)->NewStringUTF(env, json);
}

JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetMonIcon(JNIEnv *env, jclass clazz, jint species, jint frame)
{
    const u8 *tiles;
    const u16 *palette;
    jintArray result;
    jint pixels[32 * 32];
    int tileX, tileY, y, x;

    if (species <= 0 || species >= NUM_SPECIES)
        return NULL;
    tiles = GetMonIconTiles(species, TRUE);
    palette = GetValidMonIconPalettePtr(species);
    if (tiles == NULL || palette == NULL)
        return NULL;
    if (frame == 1)
        tiles += 0x200; // second animation frame: 32x32 4bpp = 0x200 bytes each

    // First frame only: 32x32 4bpp, 4x4 tiles of 8x8, 32 bytes per tile.
    for (tileY = 0; tileY < 4; tileY++)
    for (tileX = 0; tileX < 4; tileX++)
    for (y = 0; y < 8; y++)
    for (x = 0; x < 8; x++)
    {
        int tileIndex = tileY * 4 + tileX;
        u8 packed = tiles[tileIndex * 32 + y * 4 + x / 2];
        u8 colorIndex = (x & 1) ? (packed >> 4) : (packed & 0xF);
        int px = tileX * 8 + x;
        int py = tileY * 8 + y;
        if (colorIndex == 0)
        {
            pixels[py * 32 + px] = 0;
        }
        else
        {
            u16 bgr = palette[colorIndex];
            int r = (bgr & 0x1F) << 3;
            int g = ((bgr >> 5) & 0x1F) << 3;
            int b = ((bgr >> 10) & 0x1F) << 3;
            pixels[py * 32 + px] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }

    result = (*env)->NewIntArray(env, 32 * 32);
    if (result == NULL)
        return NULL;
    (*env)->SetIntArrayRegion(env, result, 0, 32 * 32, pixels);
    return result;
}

#endif // __ANDROID__

#endif // PLATFORM_SDL2

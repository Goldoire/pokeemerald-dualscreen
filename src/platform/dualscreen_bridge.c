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
#include "graphics.h"
#include "main.h"
#include "platform.h"
#include "pokemon.h"
#include "pokemon_icon.h"
#include "item_icon.h"
#include "item_use.h"
#include "party_menu.h"
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
#include "constants/item_effects.h"
#include "constants/party_menu.h"
#include "constants/pokemon.h"
#include "constants/region_map_sections.h"
#include "constants/trainers.h"

extern u32 CountPlayerTrainerStars(void);

#define SNAPSHOT_CAPACITY 28672
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

// Button presses handed to the bottom screen while a takeover panel is open.
// Sampled from the game's own input rather than intercepted as Android key
// events: the engine is parked in a wait handler that reads nothing then, so
// the buttons are free, and this is the path already proven to carry the
// device's controller - whatever it reports its buttons as. Actions match
// DualScreenView's NAV_* constants.
#define NAV_QUEUE_SIZE 16
#define NAV_UP 0
#define NAV_DOWN 1
#define NAV_LEFT 2
#define NAV_RIGHT 3
#define NAV_CONFIRM 4
#define NAV_CANCEL 5

static u8 sNavQueue[NAV_QUEUE_SIZE];
static SDL_SpinLock sNavLock;
static int sNavHead;
static int sNavCount;
static u32 sVirtualInputFrame; // last frame a queued virtual key was handed out
static u32 sButtonPressCount;  // real button presses during a battle menu, 7 bits

// Battle menu cursor, republished every frame rather than once per snapshot.
// The bottom screen draws the engine's own cursor, and at the snapshot's
// cadence (8 frames, then a 120ms poll) it would trail a d-pad press by a
// quarter second. Written by the frame thread, read by the Java UI thread;
// one word, so a reader only ever sees a whole value, at worst one frame old.
static SDL_atomic_t sBattleCursor;

#ifdef __ANDROID__
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>

// Asset-hole support: distributable builds ship libmain.so with all
// ROM-matching asset bytes zeroed plus a manifest of (vaddr, size,
// romOffset) ranges (see tools/dualscreen/make_asset_holes.py). The user's
// own ROM is kept as baserom.gba; this restores the zeroed ranges in
// memory before any game code runs, reproducing the original library
// exactly. The manifest names the library it was built for by GNU build
// id, so a stale manifest can never corrupt a different build.
#define BASEROM_SIZE (16 * 1024 * 1024)

// Finds this library's GNU build id note; returns length or 0.
static int GetOwnBuildId(unsigned char *out, int outSize)
{
    Dl_info info;
    int i;

    if (dladdr((void *)GetOwnBuildId, &info) == 0)
        return 0;

    // Walk our own program headers via the ELF header at the load base.
    {
        const ElfW(Ehdr) *ehdr = (const ElfW(Ehdr) *)info.dli_fbase;
        const ElfW(Phdr) *phdrs = (const ElfW(Phdr) *)((const char *)info.dli_fbase + ehdr->e_phoff);
        for (i = 0; i < ehdr->e_phnum; i++)
        {
            const char *note = (const char *)info.dli_fbase + phdrs[i].p_vaddr;
            const char *end = note + phdrs[i].p_filesz;
            if (phdrs[i].p_type != PT_NOTE)
                continue;
            while (note + 12 <= end)
            {
                const ElfW(Nhdr) *nhdr = (const ElfW(Nhdr) *)note;
                const char *name = note + sizeof(*nhdr);
                const char *desc = name + ((nhdr->n_namesz + 3) & ~3);
                if (nhdr->n_type == NT_GNU_BUILD_ID
                 && nhdr->n_namesz == 4 && memcmp(name, "GNU", 4) == 0
                 && (int)nhdr->n_descsz <= outSize)
                {
                    memcpy(out, desc, nhdr->n_descsz);
                    return nhdr->n_descsz;
                }
                note = desc + ((nhdr->n_descsz + 3) & ~3);
            }
        }
    }
    return 0;
}

void DualScreen_FillAssets(const char *prefPath)
{
    char path[1024];
    FILE *manifest;
    FILE *romFile;
    unsigned char *rom;
    Dl_info info;
    unsigned char *base;
    unsigned char header[44];
    unsigned char ownBuildId[20];
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

    // Never fill a library the manifest was not generated for.
    if (GetOwnBuildId(ownBuildId, sizeof(ownBuildId)) != 20
     || memcmp(ownBuildId, header + 20, 20) != 0)
    {
        printf("[Assets] manifest build id mismatch; refusing to fill\n");
        free(rom);
        fclose(manifest);
        fclose(romFile);
        return;
    }

    count = header[40] | (header[41] << 8) | (header[42] << 16) | ((u32)header[43] << 24);
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
        sVirtualInputFrame = sFrameCounter;
    }
    SDL_AtomicUnlock(&sVirtualKeyLock);
    return keys;
}

static void PushNav(u8 action)
{
    SDL_AtomicLock(&sNavLock);
    if (sNavCount < NAV_QUEUE_SIZE)
    {
        sNavQueue[(sNavHead + sNavCount) % NAV_QUEUE_SIZE] = action;
        sNavCount++;
    }
    SDL_AtomicUnlock(&sNavLock);
}

static void ClearNavQueue(void)
{
    SDL_AtomicLock(&sNavLock);
    sNavHead = 0;
    sNavCount = 0;
    SDL_AtomicUnlock(&sNavLock);
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

// The debug harness's way in: one frame of button state, or FALSE when the
// queue is full and the caller should wait a frame.
u32 DualScreen_DebugQueueKeys(u16 keys)
{
    u32 queued;
    SDL_AtomicLock(&sVirtualKeyLock);
    queued = sVirtualKeyCount < VIRTUAL_KEY_QUEUE_SIZE;
    if (queued)
    {
        sVirtualKeys[(sVirtualKeyHead + sVirtualKeyCount) % VIRTUAL_KEY_QUEUE_SIZE] = keys;
        sVirtualKeyCount++;
    }
    SDL_AtomicUnlock(&sVirtualKeyLock);
    return queued;
}

// ---------------------------------------------------------------------------
// Battle bag/party takeover: pending-choice mailbox between the bottom
// screen and the player battle controller (see platform/dualscreen.h).
// All state here is zero-initialized runtime .bss: nothing is written before
// DualScreen_FillAssets runs, so it is safe w.r.t. asset-hole packaging.
// ---------------------------------------------------------------------------

// An armed tap expires if no menu opens within this many frames (~4s), so a
// stale arm can never hijack a later, physical-button-driven menu open.
#define DS_TAKEOVER_TTL_FRAMES 240

// The bottom screen counts as live if it fetched a snapshot within this many
// frames (~3s; it polls every 120ms while its Presentation is showing).
#define DS_BOTTOM_LIVE_TTL_FRAMES 180

static SDL_SpinLock sBattleMenuLock;
static u32 sBottomPollFrame;  // frame of the last snapshot poll over JNI; 0 = never
static u32 sBattleArmMode;    // Java's intent: 0 none, 1 bag, 2 party
static u32 sBattleArmFrame;
static u32 sBattleMenuMode;   // controller wait state: 0 closed, 1 bag, 2 party
static u32 sBattleMenuCaseId; // PARTY_ACTION_* for the party wait state
static u32 sBattleMenuBattler;
static u32 sBattleMenuResult; // DS_BMENU_RESULT_* of the last submission
static u32 sBattleMenuSeq;    // bumps on every menu open and result, for the UI
static u32 sBattleChoicePending;
static s32 sBattleChoiceA;
static s32 sBattleChoiceB;

u32 DualScreen_TakeBattleTakeover(u32 mode)
{
    u32 taken = FALSE;
    SDL_AtomicLock(&sBattleMenuLock);
    if (sBattleArmMode == mode && sFrameCounter - sBattleArmFrame < DS_TAKEOVER_TTL_FRAMES)
    {
        taken = TRUE;
        sBattleArmMode = 0;
    }
    SDL_AtomicUnlock(&sBattleMenuLock);
    // Not armed: BAG or POKeMON was chosen with the physical buttons rather
    // than tapped. The bottom screen still owns the choice as long as it is
    // there to show it, which is the rule the forced send-out already uses.
    // This deliberately fell through to the GBA menus back when the panels
    // could only be worked with a finger; now that the buttons drive them
    // too, falling through would just mean the two ways of choosing the same
    // action opened two different UIs. The arm is still what makes a tap
    // engage on the very frame it happens.
    if (!taken)
        taken = DualScreen_BottomScreenLive();
    return taken;
}

void DualScreen_SetBattleMenuOpen(u32 mode, u32 caseId, u32 battler)
{
    SDL_AtomicLock(&sBattleMenuLock);
    sBattleMenuMode = mode;
    sBattleMenuCaseId = caseId;
    sBattleMenuBattler = battler;
    sBattleMenuResult = DS_BMENU_RESULT_NONE;
    sBattleChoicePending = FALSE; // a fresh menu never inherits a stale choice
    ClearNavQueue();              // nor a button press aimed at the last one
    // Bumped on an open as well as on a result, so the bottom screen can tell
    // a brand new wait from the one it just closed and still sees in a stale
    // snapshot. Without that it cannot reopen promptly after a cancel.
    sBattleMenuSeq++;
    SDL_AtomicUnlock(&sBattleMenuLock);
}

void DualScreen_ClearBattleMenu(void)
{
    SDL_AtomicLock(&sBattleMenuLock);
    sBattleMenuMode = 0;
    sBattleMenuCaseId = 0;
    sBattleChoicePending = FALSE;
    ClearNavQueue();
    SDL_AtomicUnlock(&sBattleMenuLock);
}

u32 DualScreen_BattleMenuInfo(u32 *caseId, u32 *battler, u32 *result, u32 *seq)
{
    u32 mode;
    SDL_AtomicLock(&sBattleMenuLock);
    mode = sBattleMenuMode;
    *caseId = sBattleMenuCaseId;
    *battler = sBattleMenuBattler;
    *result = sBattleMenuResult;
    *seq = sBattleMenuSeq;
    SDL_AtomicUnlock(&sBattleMenuLock);
    return mode;
}

u32 DualScreen_TakeBattleChoice(s32 *a, s32 *b)
{
    u32 taken = FALSE;
    SDL_AtomicLock(&sBattleMenuLock);
    if (sBattleChoicePending)
    {
        *a = sBattleChoiceA;
        *b = sBattleChoiceB;
        sBattleChoicePending = FALSE;
        taken = TRUE;
    }
    SDL_AtomicUnlock(&sBattleMenuLock);
    return taken;
}

void DualScreen_SetBattleMenuResult(u32 result)
{
    SDL_AtomicLock(&sBattleMenuLock);
    sBattleMenuResult = result;
    sBattleMenuSeq++;
    SDL_AtomicUnlock(&sBattleMenuLock);
}

u32 DualScreen_BottomScreenLive(void)
{
    u32 live;
    SDL_AtomicLock(&sBattleMenuLock);
    // Only the Android JNI snapshot poll ever stamps sBottomPollFrame, and
    // Java polls only while the companion Presentation is actually showing,
    // so this is FALSE on desktop builds and whenever the bottom display is
    // gone - flows that would otherwise wait forever on a bottom-screen
    // choice must then fall back to the GBA menus.
    live = sBottomPollFrame != 0
        && sFrameCounter - sBottomPollFrame < DS_BOTTOM_LIVE_TTL_FRAMES;
    SDL_AtomicUnlock(&sBattleMenuLock);
    return live;
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
        if (c == CHAR_e_ACUTE && out < destSize - 2)
        {
            // é as UTF-8; the Java font maps it back to the game's glyph.
            dest[out++] = (char)0xC3;
            dest[out++] = (char)0xA9;
            continue;
        }
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
        JsonPut(w, ",\"pp\":%u,\"maxPp\":%u,\"type\":%u,\"pw\":%u,\"ac\":%u}",
                GetMonData(mon, MON_DATA_PP1 + i),
                CalculatePPWithBonus(move, ppBonuses, i),
                gBattleMoves[move].type,
                gBattleMoves[move].power,
                gBattleMoves[move].accuracy);
    }
    JsonPut(w, "]}");
}

// Type-chart multiplier of one attacking type against a defender, in the
// engine's own tenths (TYPE_MUL_NORMAL == 10), straight from
// gTypeEffectiveness (battle_main.c). Dual types multiply; the rows behind
// the TYPE_FORESIGHT marker (the Ghost immunities) are applied like the
// normal ones. Abilities such as Levitate are deliberately not consulted:
// this is a table lookup, not a damage calc.
static int TypeMulAgainst(u8 atkType, const struct BattlePokemon *def)
{
    int mul = TYPE_MUL_NORMAL;
    int i;

    for (i = 0; TYPE_EFFECT_ATK_TYPE(i) != TYPE_ENDTABLE; i += 3)
    {
        if (TYPE_EFFECT_ATK_TYPE(i) == TYPE_FORESIGHT)
            continue; // marker row only; keep the Ghost rows behind it
        if (TYPE_EFFECT_ATK_TYPE(i) != atkType)
            continue;
        if (TYPE_EFFECT_DEF_TYPE(i) == def->types[0])
            mul = mul * TYPE_EFFECT_MULTIPLIER(i) / 10;
        if (def->types[1] != def->types[0] && TYPE_EFFECT_DEF_TYPE(i) == def->types[1])
            mul = mul * TYPE_EFFECT_MULTIPLIER(i) / 10;
    }
    return mul;
}

// Multiplier tiers, so the UI can split 2x from 4x (and 1/2 from 1/4):
// 0 immune, 1 quarter, 2 half, 3 neutral, 4 double, 5 quad.
static int MulTier(int mul)
{
    if (mul == 0)
        return 0;
    if (mul < 5)
        return 1;
    if (mul < TYPE_MUL_NORMAL)
        return 2;
    if (mul == TYPE_MUL_NORMAL)
        return 3;
    if (mul <= 20)
        return 4;
    return 5;
}

// Effectiveness class of a damaging move against one foe: -1 no hint (status
// move or no foe), otherwise a MulTier value.
static int MoveEffClass(u16 move, const struct BattlePokemon *def)
{
    if (def == NULL || move == MOVE_NONE || move >= MOVES_COUNT)
        return -1;
    if (gBattleMoves[move].power == 0)
        return -1; // status moves carry no hint
    return MulTier(TypeMulAgainst(gBattleMoves[move].type, def));
}

// Attacking types this mon takes super-effective damage from, as
// [{"t":type,"m":tier}] with the 4x entries ahead of the 2x ones. Same tier
// numbering as MoveEffClass, so the UI can reuse its markers. TYPE_MYSTERY is
// a filler row in the chart, never a real attacking type.
static void WriteWeaknessesJson(struct JsonWriter *w, const struct BattlePokemon *def)
{
    int tier;
    bool32 first = TRUE;

    JsonPut(w, ",\"weak\":[");
    for (tier = 5; tier >= 4; tier--)
    {
        u8 atkType;
        for (atkType = 0; atkType < NUMBER_OF_MON_TYPES; atkType++)
        {
            if (atkType == TYPE_MYSTERY)
                continue;
            if (MulTier(TypeMulAgainst(atkType, def)) != tier)
                continue;
            if (!first)
                JsonPut(w, ",");
            first = FALSE;
            JsonPut(w, "{\"t\":%u,\"m\":%d}", atkType, tier);
        }
    }
    JsonPut(w, "]");
}

// foes: up to two current opposing battlers ([0] left, [1] right; NULL when
// absent/fainted) for the per-move effectiveness hints, or NULL for none.
// withWeak adds this mon's own weakness list (foe cards only — the player's
// card has no room for it).
static void WriteBattleMonJson(struct JsonWriter *w, struct BattlePokemon *mon,
                               const struct BattlePokemon *const *foes, bool32 withWeak)
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
        JsonPut(w, ",\"pp\":%u,\"maxPp\":%u,\"type\":%u,\"pw\":%u,\"ac\":%u",
                mon->pp[i],
                CalculatePPWithBonus(move, mon->ppBonuses, i),
                gBattleMoves[move].type,
                gBattleMoves[move].power,
                gBattleMoves[move].accuracy);
        if (foes != NULL)
        {
            JsonPut(w, ",\"eff\":[%d,%d]",
                    MoveEffClass(move, foes[0]), MoveEffClass(move, foes[1]));
        }
        JsonPut(w, "}");
    }
    JsonPut(w, "]");
    if (withWeak)
        WriteWeaknessesJson(w, mon);
    JsonPut(w, "}");
}

// Which battler owns the open bottom-screen battle menu, and which menu it
// is: 0 none, 1 action select, 2 move select. In doubles the two player mons
// pick sequentially, so this follows whichever battler the open menu belongs
// to. Shared by the snapshot and the per-frame cursor publish so the cursor
// the bottom screen highlights can never disagree with the rest of the state.
static int ResolveBattleMenu(u8 *battlerOut)
{
    u8 battler = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
    int menu = 0;

    if (DualScreen_BattleUiActive())
    {
        s32 menuBattler = DualScreen_PlayerMoveBattler();
        if (menuBattler >= 0)
            menu = 2;
        else if ((menuBattler = DualScreen_PlayerActionBattler()) >= 0)
            menu = 1;
        if (menuBattler >= 0 && menuBattler < MAX_BATTLERS_COUNT)
            battler = (u8)menuBattler;
    }
    *battlerOut = battler;
    return menu;
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
            u8 playerBattler;
            u8 enemyBattler = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
            int menu = ResolveBattleMenu(&playerBattler);

            if (playerBattler < MAX_BATTLERS_COUNT && enemyBattler < MAX_BATTLERS_COUNT)
            {
                JsonPut(w, "\"battle\":{\"kind\":%d,",
                        (gBattleTypeFlags & BATTLE_TYPE_TRAINER) ? 1 : 0);
                JsonPut(w, "\"menu\":%d,\"actionCursor\":%d,\"moveCursor\":%d,",
                        menu, gActionSelectionCursor[playerBattler],
                        gMoveSelectionCursor[playerBattler]);

                // Battle bag/party takeover state for the bottom screen.
                {
                    u32 subCase, subBattler, subResult, subSeq;
                    u32 sub = DualScreen_BattleMenuInfo(&subCase, &subBattler, &subResult, &subSeq);
                    s32 act0 = -1, act1 = -1;
                    int b;

                    for (b = 0; b < gBattlersCount; b++)
                    {
                        if (GetBattlerSide(b) == B_SIDE_PLAYER && !(gAbsentBattlerFlags & (1 << b)))
                        {
                            if (act0 < 0)
                                act0 = gBattlerPartyIndexes[b];
                            else
                                act1 = gBattlerPartyIndexes[b];
                        }
                    }
                    JsonPut(w, "\"double\":%d,\"canUseItems\":%d,\"canCatch\":%d,",
                            (gBattleTypeFlags & BATTLE_TYPE_DOUBLE) ? 1 : 0,
                            (gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_FRONTIER_NO_PYRAMID
                                                 | BATTLE_TYPE_EREADER_TRAINER | BATTLE_TYPE_RECORDED_LINK
                                                 | BATTLE_TYPE_PYRAMID | BATTLE_TYPE_MULTI)) ? 0 : 1,
                            (gBattleTypeFlags & BATTLE_TYPE_TRAINER) ? 0 : 1);
                    JsonPut(w, "\"sub\":%u,\"subCase\":%u,\"subResult\":%u,\"subSeq\":%u,",
                            sub, subCase, subResult, subSeq);
                    JsonPut(w, "\"active\":[%d,%d],\"prevSel\":%d,", act0, act1,
                            gBattleStruct != NULL ? gBattleStruct->prevSelectedPartySlot : PARTY_SIZE);
                }

                // Current foes, for the per-move effectiveness hints and the
                // doubles cards: [0] the left foe, [1] the right foe (doubles
                // only). Absent or fainted battlers stay NULL so a hint is
                // never shown against a mon that is no longer there.
                {
                    const struct BattlePokemon *foes[2] = {NULL, NULL};
                    s32 playerMon2 = -1;
                    s32 enemyMon2 = -1;

                    if (!(gAbsentBattlerFlags & (1u << enemyBattler))
                     && gBattleMons[enemyBattler].hp > 0)
                        foes[0] = &gBattleMons[enemyBattler];
                    if (gBattleTypeFlags & BATTLE_TYPE_DOUBLE)
                    {
                        u8 playerRight = GetBattlerAtPosition(B_POSITION_PLAYER_RIGHT);
                        u8 enemyRight = GetBattlerAtPosition(B_POSITION_OPPONENT_RIGHT);
                        if (enemyRight < MAX_BATTLERS_COUNT
                         && !(gAbsentBattlerFlags & (1u << enemyRight))
                         && gBattleMons[enemyRight].hp > 0)
                        {
                            foes[1] = &gBattleMons[enemyRight];
                            enemyMon2 = enemyRight;
                        }
                        // The partner of whichever battler owns the open menu.
                        if (playerRight < MAX_BATTLERS_COUNT)
                        {
                            u8 playerLeft = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT);
                            u8 partner = playerBattler == playerRight ? playerLeft : playerRight;
                            if (partner < MAX_BATTLERS_COUNT
                             && !(gAbsentBattlerFlags & (1u << partner)))
                                playerMon2 = partner;
                        }
                    }

                    JsonPut(w, "\"playerMon\":");
                    WriteBattleMonJson(w, &gBattleMons[playerBattler], foes, FALSE);
                    JsonPut(w, ",\"enemyMon\":");
                    WriteBattleMonJson(w, &gBattleMons[enemyBattler], NULL, TRUE);
                    if (playerMon2 >= 0)
                    {
                        JsonPut(w, ",\"playerMon2\":");
                        WriteBattleMonJson(w, &gBattleMons[playerMon2], NULL, FALSE);
                    }
                    if (enemyMon2 >= 0)
                    {
                        JsonPut(w, ",\"enemyMon2\":");
                        WriteBattleMonJson(w, &gBattleMons[enemyMon2], NULL, TRUE);
                    }
                }
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

    // Button input for an open takeover panel, sampled once a frame. Skipped
    // while the virtual key queue is draining: those are the keystrokes a
    // bottom-screen tap injects to walk the engine's cursor, and sampling
    // them here would replay the tap as a button press inside the very panel
    // it just opened. Directions use the game's repeat, so a held d-pad
    // scrolls at the rate its own menus do; confirm and cancel do not repeat.
    if (sBattleMenuMode != 0 && sVirtualKeyCount == 0
     && sFrameCounter - sVirtualInputFrame > 4)
    {
        u16 repeated = gMain.newAndRepeatedKeys;

        if (repeated & DPAD_UP)
            PushNav(NAV_UP);
        else if (repeated & DPAD_DOWN)
            PushNav(NAV_DOWN);
        else if (repeated & DPAD_LEFT)
            PushNav(NAV_LEFT);
        else if (repeated & DPAD_RIGHT)
            PushNav(NAV_RIGHT);

        if (gMain.newKeys & A_BUTTON)
            PushNav(NAV_CONFIRM);
        else if (gMain.newKeys & B_BUTTON)
            PushNav(NAV_CANCEL);
    }

    // Republished every frame, ahead of the snapshot throttle: this is what
    // the bottom screen draws its cursor ring from.
    {
        int packed = -1;
        if (gMain.inBattle && gBattlersCount > 0 && gBattlersCount <= MAX_BATTLERS_COUNT)
        {
            u8 battler;
            int menu = ResolveBattleMenu(&battler);
            if (menu != 0 && battler < MAX_BATTLERS_COUNT)
            {
                // Counts real button presses only - virtual keys are the ones
                // a tap injects, and counting those would make every tap look
                // like button input. The bottom screen shows its cursor when
                // this moves and hides it on touch, the way the DS games flip
                // between the two (TouchscreenListMenu's isTouch).
                if (sVirtualKeyCount == 0 && sFrameCounter - sVirtualInputFrame > 4
                 && (gMain.newKeys & (DPAD_ANY | A_BUTTON | B_BUTTON)))
                    sButtonPressCount = (sButtonPressCount + 1) & 0x7F;
                packed = (sButtonPressCount << 24)
                       | (menu << 16)
                       | (gActionSelectionCursor[battler] << 8)
                       | gMoveSelectionCursor[battler];
            }
        }
        SDL_AtomicSet(&sBattleCursor, packed);
    }

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

// Called by RomGateActivity right after System.loadLibrary, so asset holes
// are filled before anything else in the process can read (and cache) them.
// The fill in main() then re-copies the same bytes, which is harmless.
JNIEXPORT void JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeFillAssets(JNIEnv *env, jclass clazz, jstring filesDir)
{
    const char *dir = (*env)->GetStringUTFChars(env, filesDir, NULL);
    char prefPath[1024];
    if (dir != NULL)
    {
        snprintf(prefPath, sizeof(prefPath), "%s/", dir);
        DualScreen_FillAssets(prefPath);
        (*env)->ReleaseStringUTFChars(env, filesDir, dir);
    }
}

// Enqueue synthetic button states, one entry per frame (0 = release).
JNIEXPORT void JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeSetVoxelDebugFlags(JNIEnv *env, jclass clazz, jint flags)
{
    extern unsigned gVoxelDebugFlags;
    (void)env; (void)clazz;
    gVoxelDebugFlags = (unsigned)flags;
}

JNIEXPORT void JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeDebugWarp(JNIEnv *env, jclass clazz, jint group, jint num, jint x, jint y)
{
    extern void DebugHarness_RequestWarp(int group, int num, int x, int y);
    (void)env; (void)clazz;
    DebugHarness_RequestWarp(group, num, x, y);
}

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

// Arms (or, with mode 0, disarms) the battle bag/party takeover; called
// right before the bottom screen key-walks the action cursor to BAG or
// POKeMON. See DualScreen_TakeBattleTakeover.
JNIEXPORT void JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeBattleArm(JNIEnv *env, jclass clazz, jint mode)
{
    SDL_AtomicLock(&sBattleMenuLock);
    sBattleArmMode = (u32)mode;
    sBattleArmFrame = sFrameCounter;
    SDL_AtomicUnlock(&sBattleMenuLock);
}

// Submits the pending battle choice. Bag wait: a = item id, b = target
// party slot (-1 when the item targets the active mon or needs no target).
// Party wait: a = party slot. a = -1 cancels back to the action menu.
JNIEXPORT void JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeBattleSubmit(JNIEnv *env, jclass clazz, jint a, jint b)
{
    SDL_AtomicLock(&sBattleMenuLock);
    sBattleChoiceA = a;
    sBattleChoiceB = b;
    sBattleChoicePending = TRUE;
    SDL_AtomicUnlock(&sBattleMenuLock);
}

// How an item can be used from the battle bag: 0 not usable, 1 ball,
// 2 medicine (pick a target mon), 3 self/no target (X items, Guard Spec),
// 4 escape item (wild battles only), 5 PP restore (pick a target mon).
// Mirrors the classification DualScreen_UseBattleItem applies.
JNIEXPORT jint JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetItemBattleCategory(JNIEnv *env, jclass clazz, jint itemId)
{
    ItemUseFunc func;

    if (itemId <= ITEM_NONE || itemId >= ITEMS_COUNT || GetItemBattleUsage(itemId) == 0)
        return 0;
    func = GetItemBattleFunc(itemId);
    if (func == ItemUseInBattle_PokeBall)
        return 1;
    if (func == ItemUseInBattle_Medicine)
        return 2;
    if (func == ItemUseInBattle_StatIncrease)
        return 3;
    if (func == ItemUseInBattle_Escape)
        return 4;
    if (func == ItemUseInBattle_PPRecovery)
        return 5;
    if (func == ItemUseInBattle_EnigmaBerry)
    {
        switch (GetItemEffectType(itemId))
        {
        case ITEM_EFFECT_X_ITEM:
            return 3;
        case ITEM_EFFECT_HEAL_PP:
            return 5;
        case ITEM_EFFECT_HEAL_HP:
        case ITEM_EFFECT_CURE_POISON:
        case ITEM_EFFECT_CURE_SLEEP:
        case ITEM_EFFECT_CURE_BURN:
        case ITEM_EFFECT_CURE_FREEZE:
        case ITEM_EFFECT_CURE_PARALYSIS:
        case ITEM_EFFECT_CURE_ALL_STATUS:
        case ITEM_EFFECT_CURE_CONFUSION:
        case ITEM_EFFECT_CURE_INFATUATION:
            return 2;
        default:
            return 0;
        }
    }
    return 0;
}

// One queued button press for an open takeover panel, or -1 when the queue is
// empty. Java drains this in a loop; see the NAV_* codes above.
JNIEXPORT jint JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeDrainNavKey(JNIEnv *env, jclass clazz)
{
    jint action = -1;

    SDL_AtomicLock(&sNavLock);
    if (sNavCount > 0)
    {
        action = sNavQueue[sNavHead];
        sNavHead = (sNavHead + 1) % NAV_QUEUE_SIZE;
        sNavCount--;
    }
    SDL_AtomicUnlock(&sNavLock);
    return action;
}

// The battle menu cursor on its own, so the ring can track a d-pad press
// without waiting for a whole snapshot. Packed (menu << 16) | (action << 8) |
// move; -1 when no bottom-screen battle menu is open. Nothing here touches
// game memory - it reads the word the frame thread last published.
JNIEXPORT jint JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetBattleCursor(JNIEnv *env, jclass clazz)
{
    return SDL_AtomicGet(&sBattleCursor);
}

JNIEXPORT jstring JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetSnapshotJson(JNIEnv *env, jclass clazz)
{
    jstring result;
    // Liveness heartbeat: Java polls here only while the bottom-screen
    // Presentation is showing. See DualScreen_BottomScreenLive.
    SDL_AtomicLock(&sBattleMenuLock);
    sBottomPollFrame = sFrameCounter;
    SDL_AtomicUnlock(&sBattleMenuLock);
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

// Item/menu sprites are 3x3 tiles of 8x8 at 4bpp, with a compressed palette.
static void DecodeItemIconArgb(const void *pic, const void *palette, jint *pixels)
{
    u8 gfx[0x120];
    u16 pal[16];
    int tileX, tileY, y, x;

    LZ77UnCompWram(pic, gfx);
    LZ77UnCompWram(palette, pal);

    for (tileY = 0; tileY < 3; tileY++)
    for (tileX = 0; tileX < 3; tileX++)
    for (y = 0; y < 8; y++)
    for (x = 0; x < 8; x++)
    {
        int tileIndex = tileY * 3 + tileX;
        u8 packed = gfx[tileIndex * 32 + y * 4 + x / 2];
        u8 colorIndex = (x & 1) ? (packed >> 4) : (packed & 0xF);
        int px = tileX * 8 + x;
        int py = tileY * 8 + y;
        pixels[py * 24 + px] = colorIndex == 0 ? 0 : Bgr555ToArgb(pal[colorIndex]);
    }
}

// A 24x24 bag item icon as ARGB pixels, decoded from the game's own data.
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetItemIcon(JNIEnv *env, jclass clazz, jint itemId)
{
    jint pixels[24 * 24];
    jintArray result;

    if (itemId <= ITEM_NONE || itemId >= ITEMS_COUNT)
        return NULL;
    DecodeItemIconArgb(GetItemIconPicOrPalette(itemId, 0), GetItemIconPicOrPalette(itemId, 1), pixels);

    result = (*env)->NewIntArray(env, 24 * 24);
    if (result == NULL)
        return NULL;
    (*env)->SetIntArrayRegion(env, result, 0, 24 * 24, pixels);
    return result;
}

// Match the Android tabs, using the same original-game sprites as the 3DS
// navigation column. Decode from ROM-restored data rather than bundling PNGs.
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetNavigationIcons(JNIEnv *env, jclass clazz)
{
    static const struct { const u32 *pic; const u32 *pal; } icons[] = {
        {gItemIcon_PokeBall, gItemIconPalette_PokeBall},
        {gItemIcon_TownMap, gItemIconPalette_TownMap},
        {gItemIcon_BerryPouch, gItemIconPalette_BerryPouch},
        {gItemIcon_ContestPass, gItemIconPalette_ContestPass},
        {gItemIcon_TeachyTV, gItemIconPalette_TeachyTV},
    };
    jint pixels[ARRAY_COUNT(icons) * 24 * 24];
    jintArray result;
    int i;

    for (i = 0; i < ARRAY_COUNT(icons); i++)
        DecodeItemIconArgb(icons[i].pic, icons[i].pal, pixels + i * 24 * 24);
    result = (*env)->NewIntArray(env, ARRAY_COUNT(pixels));
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, ARRAY_COUNT(pixels), pixels);
    return result;
}

// Item descriptions span multiple lines and use é, so DecodeGbaString (which
// stops at the first control code) won't do: newlines become spaces (the Java
// side re-wraps to fit) and é is emitted as UTF-8.
static void DecodeItemDescription(char *dest, int destSize, const u8 *src)
{
    int out = 0;
    int i;

    for (i = 0; src != NULL && i < 256 && out < destSize - 3; i++)
    {
        u8 c = src[i];
        char decoded;
        if (c == EOS)
            break;
        if (c == CHAR_NEWLINE || c == CHAR_PROMPT_SCROLL || c == CHAR_PROMPT_CLEAR)
        {
            if (out > 0 && dest[out - 1] != ' ')
                dest[out++] = ' ';
            continue;
        }
        if (c == CHAR_KEYPAD_ICON || c == CHAR_EXTRA_SYMBOL
         || c == EXT_CTRL_CODE_BEGIN || c == PLACEHOLDER_BEGIN || c == CHAR_DYNAMIC)
            break;
        if (c == CHAR_e_ACUTE)
        {
            dest[out++] = (char)0xC3;
            dest[out++] = (char)0xA9;
            continue;
        }
        decoded = DecodeGbaChar(c);
        if (decoded != 0)
            dest[out++] = decoded;
    }
    dest[out] = '\0';
}

// The in-game description text for an item, decoded to UTF-8.
JNIEXPORT jstring JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetItemDescription(JNIEnv *env, jclass clazz, jint itemId)
{
    char text[256];

    if (itemId <= ITEM_NONE || itemId >= ITEMS_COUNT)
        return NULL;
    DecodeItemDescription(text, sizeof(text), GetItemDescription(itemId));
    return (*env)->NewStringUTF(env, text);
}

// Implemented in party_menu.c: the party menu slot tilemaps and the
// held-item/mail mark graphics.
extern const u8 *DualScreen_GetPartySlotTilemap(u32 kind, u32 *width, u32 *height);
extern const u32 *DualScreen_GetHeldItemGfx(void);
extern const u16 *DualScreen_GetHeldItemPal(void);

// A party menu slot box as ARGB pixels, composed from the game's own
// tileset/tilemaps. Kinds: 0 main (80x56), 1 main no-HP (eggs), 2 wide
// (144x24), 3 wide no-HP, 4 wide empty. The slot windows use palette bank 3
// of the party bg palette; the fainted recolor swaps the box colors (indices
// 1, 4-8) from bank 5, exactly like LoadPartyBoxPalette does.
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetPartySlot(JNIEnv *env, jclass clazz, jint kind, jint fainted)
{
    static u8 sGfx[2048];
    static u16 sPal[11 * 16];
    u16 bank[16];
    u32 tilesWide, tilesHigh;
    const u8 *tilemap;
    jint *pixels;
    jintArray result;
    int widthPx, tx, ty, py, px, i;

    if (kind < 0 || kind > 4)
        return NULL;
    tilemap = DualScreen_GetPartySlotTilemap(kind, &tilesWide, &tilesHigh);
    LZ77UnCompWram(gPartyMenuBg_Gfx, sGfx);
    LZ77UnCompWram(gPartyMenuBg_Pal, sPal);
    memcpy(bank, &sPal[3 * 16], sizeof(bank));
    if (kind == 4 || fainted == 2)
    {
        // DrawEmptySlot remaps entries 1/11/12 from bank 1
        // (sPartyBoxNoMonPalIds/Offsets); without it the empty slot picks
        // up bank 3's unused colors and renders purple. Mode 2 requests
        // the same remap for other slot kinds drawn as empty.
        bank[1] = sPal[1 * 16 + 1];
        bank[11] = sPal[1 * 16 + 11];
        bank[12] = sPal[1 * 16 + 12];
    }
    else if (fainted == 1)
    {
        bank[1] = sPal[5 * 16 + 1];
        for (i = 4; i <= 8; i++)
            bank[i] = sPal[5 * 16 + i];
    }

    widthPx = tilesWide * 8;
    pixels = malloc(tilesWide * tilesHigh * 64 * sizeof(jint));
    if (pixels == NULL)
        return NULL;
    for (ty = 0; ty < (int)tilesHigh; ty++)
    for (tx = 0; tx < (int)tilesWide; tx++)
    {
        const u8 *src = &sGfx[tilemap[ty * tilesWide + tx] * 32];
        for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
        {
            u8 packed = src[py * 4 + px / 2];
            u8 colorIndex = (px & 1) ? (packed >> 4) : (packed & 0xF);
            pixels[(ty * 8 + py) * widthPx + tx * 8 + px] =
                    colorIndex == 0 ? 0 : Bgr555ToArgb(bank[colorIndex]);
        }
    }

    result = (*env)->NewIntArray(env, tilesWide * tilesHigh * 64);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, tilesWide * tilesHigh * 64, pixels);
    free(pixels);
    return result;
}

// The party menu's full 240x160 background layer (BG1) as ARGB pixels,
// composed exactly from what AllocPartyMenuBgGfx loads: gPartyMenuBg_Gfx
// (LZ 4bpp tiles), gPartyMenuBg_Tilemap (LZ 32x32 u16 text-BG map: tile
// index, flip bits, palette bank) and gPartyMenuBg_Pal (LZ, 11 banks).
// Transparent pixels resolve to the backdrop color (palette entry 0), so
// the composed image is fully opaque.
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetPartyBgImage(JNIEnv *env, jclass clazz)
{
    static u8 sGfx[2048];
    static u16 sTilemap[32 * 32];
    static u16 sPal[11 * 16];
    jint *pixels;
    jintArray result;
    int tx, ty, py, px;

    LZ77UnCompWram(gPartyMenuBg_Gfx, sGfx);
    LZ77UnCompWram(gPartyMenuBg_Tilemap, sTilemap);
    LZ77UnCompWram(gPartyMenuBg_Pal, sPal);

    pixels = malloc(240 * 160 * sizeof(jint));
    if (pixels == NULL)
        return NULL;

    // The layer carries furniture the tab doesn't offer: the message strip
    // (bottom four tile rows, with the CANCEL button well) and the darker
    // frame band along the top row and left two columns. Stamp both with
    // the layer's filler tile (the most frequent map entry) so only the
    // staircase area keeps furniture.
    {
        u16 best = sTilemap[0];
        int bestCount = 0;
        for (ty = 0; ty < 16; ty++)
        for (tx = 0; tx < 30; tx++)
        {
            u16 candidate = sTilemap[ty * 32 + tx];
            int count = 0;
            int i;
            for (i = 0; i < 16 * 32; i++)
                if (sTilemap[i] == candidate)
                    count++;
            if (count > bestCount)
            {
                bestCount = count;
                best = candidate;
            }
        }
        for (ty = 16; ty < 20; ty++)
        for (tx = 0; tx < 30; tx++)
            sTilemap[ty * 32 + tx] = best;
        for (ty = 0; ty < 20; ty++)
        {
            sTilemap[ty * 32 + 0] = best;
            sTilemap[ty * 32 + 1] = best;
        }
        for (tx = 0; tx < 30; tx++)
            sTilemap[0 * 32 + tx] = best;
        sTilemap[1 * 32 + 2] = best; // the frame's rounded corner tile
    }

    for (ty = 0; ty < 20; ty++)
    for (tx = 0; tx < 30; tx++)
    {
        u16 entry = sTilemap[ty * 32 + tx];
        int tile = entry & 0x3FF;
        int bankNum = (entry >> 12) & 0xF;
        const u8 *src = &sGfx[tile * 32];
        const u16 *bank;
        int hflip = (entry & (1 << 10)) != 0;
        int vflip = (entry & (1 << 11)) != 0;

        if (bankNum > 10)
            bankNum = 0;
        bank = &sPal[bankNum * 16];
        for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
        {
            int sx = hflip ? 7 - px : px;
            int sy = vflip ? 7 - py : py;
            u8 colorIndex = 0;
            if (tile * 32 < (int)sizeof(sGfx))
            {
                u8 packed = src[sy * 4 + sx / 2];
                colorIndex = (sx & 1) ? (packed >> 4) : (packed & 0xF);
            }
            pixels[(ty * 8 + py) * 240 + tx * 8 + px] =
                    Bgr555ToArgb(colorIndex == 0 ? sPal[0] : bank[colorIndex]);
        }
    }

    result = (*env)->NewIntArray(env, 240 * 160);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, 240 * 160, pixels);
    free(pixels);
    return result;
}

// The party menu status tags as ARGB: 8 tags x 32x8 pixels, tag-major, in
// sheet order PSN, PAR, SLP, FRZ, BRN, PKRS, FNT, blank.
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetStatusIcons(JNIEnv *env, jclass clazz)
{
    static u8 sGfx[0x400];
    static u16 sPal[16];
    jint pixels[8 * 32 * 8];
    jintArray result;
    int icon, tile, py, px;

    LZ77UnCompWram(gStatusGfx_Icons, sGfx);
    LZ77UnCompWram(gStatusPal_Icons, sPal);

    // 32x64 sheet: each tag is one row of 4 tiles.
    for (icon = 0; icon < 8; icon++)
    for (tile = 0; tile < 4; tile++)
    {
        const u8 *src = &sGfx[(icon * 4 + tile) * 32];
        jint *out = &pixels[icon * 32 * 8];
        for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
        {
            u8 packed = src[py * 4 + px / 2];
            u8 colorIndex = (px & 1) ? (packed >> 4) : (packed & 0xF);
            out[py * 32 + tile * 8 + px] = colorIndex == 0 ? 0 : Bgr555ToArgb(sPal[colorIndex]);
        }
    }

    result = (*env)->NewIntArray(env, 8 * 32 * 8);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, 8 * 32 * 8, pixels);
    return result;
}

// The party menu's held-item marks as ARGB: 2 marks (item, mail) x 8x8.
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetHoldIcons(JNIEnv *env, jclass clazz)
{
    const u8 *gfx = (const u8 *)DualScreen_GetHeldItemGfx();
    const u16 *pal = DualScreen_GetHeldItemPal();
    jint pixels[2 * 8 * 8];
    jintArray result;
    int icon, py, px;

    for (icon = 0; icon < 2; icon++)
    for (py = 0; py < 8; py++)
    for (px = 0; px < 8; px++)
    {
        u8 packed = gfx[icon * 32 + py * 4 + px / 2];
        u8 colorIndex = (px & 1) ? (packed >> 4) : (packed & 0xF);
        pixels[icon * 64 + py * 8 + px] = colorIndex == 0 ? 0 : Bgr555ToArgb(pal[colorIndex]);
    }

    result = (*env)->NewIntArray(env, 2 * 8 * 8);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, 2 * 8 * 8, pixels);
    return result;
}

// Implemented in pokemon_summary_screen.c: which of the three palettes in
// gMoveTypes_Pal each move-type icon uses.
extern const u8 *DualScreen_GetMoveTypePalNums(void);

// The summary screen's move-type icons as ARGB: NUMBER_OF_MON_TYPES icons of
// 32x16 pixels, type-major, decoded from the game's own compressed sheet
// (gMoveTypes_Gfx; each icon is 8 sequential 4bpp tiles, 4 across x 2 down)
// with the per-type palette bank the summary screen assigns.
JNIEXPORT jintArray JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetTypeIcons(JNIEnv *env, jclass clazz)
{
    // The sheet also carries the five contest category icons behind the types.
    static u8 sGfx[(NUMBER_OF_MON_TYPES + 5) * 0x100];
    static u16 sPal[3 * 16];
    const u8 *palNums = DualScreen_GetMoveTypePalNums();
    jint *pixels;
    jintArray result;
    int type, tile, py, px;

    LZ77UnCompWram(gMoveTypes_Gfx, sGfx);
    LZ77UnCompWram(gMoveTypes_Pal, sPal);

    pixels = malloc(NUMBER_OF_MON_TYPES * 32 * 16 * sizeof(jint));
    if (pixels == NULL)
        return NULL;

    for (type = 0; type < NUMBER_OF_MON_TYPES; type++)
    {
        const u16 *pal = &sPal[(palNums[type] - 13) * 16];
        jint *out = &pixels[type * 32 * 16];
        for (tile = 0; tile < 8; tile++)
        {
            const u8 *src = &sGfx[type * 0x100 + tile * 32];
            int baseX = (tile & 3) * 8;
            int baseY = (tile >> 2) * 8;
            for (py = 0; py < 8; py++)
            for (px = 0; px < 8; px++)
            {
                u8 packed = src[py * 4 + px / 2];
                u8 colorIndex = (px & 1) ? (packed >> 4) : (packed & 0xF);
                out[(baseY + py) * 32 + baseX + px] =
                        colorIndex == 0 ? 0 : Bgr555ToArgb(pal[colorIndex]);
            }
        }
    }

    result = (*env)->NewIntArray(env, NUMBER_OF_MON_TYPES * 32 * 16);
    if (result != NULL)
        (*env)->SetIntArrayRegion(env, result, 0, NUMBER_OF_MON_TYPES * 32 * 16, pixels);
    free(pixels);
    return result;
}

#endif // __ANDROID__

#endif // PLATFORM_SDL2

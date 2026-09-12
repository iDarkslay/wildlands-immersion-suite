/* Wildlands Mod Framework / Immersion Suite fork.
 * Original GRW ScriptHook portions Copyright (C) 2026 PhialsBasement.
 * Modifications and additions Copyright (C) 2026 iDarkslay.
 * Modified through 2026-09-12. SPDX-License-Identifier: GPL-3.0-only.
 * See LICENSE, NOTICE.md and UPSTREAM_CHANGES.md.
 */
/* Read-only held-weapon identity discovery.
 *
 * Wildlands publishes the player and weapon skeletons through the same engine
 * entry point. Recording those skeleton identities lets the first-person
 * camera keep a separate learned alignment for each equipped weapon. No pose,
 * transform, game object or user file is changed here.
 */
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define SH_BUILD 1
#include "scripthook.h"

#define PUBLISH_THUNK_RVA 0x0189CE30
#define SKELETON_OWNER    0x010
#define SKELETON_ORIGIN   0x120
#define SKELETON_RIG      0x220
#define RIG_NAME_MAP      0x050
#define RIG_NAME_COUNT    0x05A
#define RIG_BONE_COUNT    0x08A
#define HASH_HEAD         0x07C159A2u

typedef void (__attribute__((ms_abi)) *PublishFn)(uint64_t, uint16_t,
                                                   unsigned char);

typedef struct WeaponSkeletonEntry {
    volatile LONG sequence;
    uint64_t skeleton;
    uint64_t owner;
} WeaponSkeletonEntry;

#define WEAPON_SKELETON_CAP 512
static WeaponSkeletonEntry g_skeletons[WEAPON_SKELETON_CAP];
static volatile LONG g_skeletonCursor;
static PublishFn g_originalPublish;
static int g_weaponDiscoveryReady;
static uint64_t g_cachedPlayer;
static uint64_t g_cachedKey;
static ULONGLONG g_lastResolve;

extern int ShReadableAddr(uint64_t addr, size_t len);
extern int ShReadMem(uint64_t addr, void *out, size_t len);
extern int ShPeekPlayer(ShPlayer *out);

static int ReadU64(uint64_t at, uint64_t *out) {
    return ShReadMem(at, out, sizeof(*out));
}

static int RigHasName(uint64_t rig, uint32_t hash) {
    uint64_t map = 0;
    uint16_t count = 0;
    int lo, hi;

    if (!ReadU64(rig + RIG_NAME_MAP, &map) ||
        !ShReadMem(rig + RIG_NAME_COUNT, &count, sizeof(count)) ||
        !map || count < 1 || count >= 2048)
        return 0;

    lo = 0;
    hi = (int)count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t found = 0;
        if (!ShReadMem(map + (uint64_t)mid * 8, &found, sizeof(found)))
            return 0;
        if (found == hash) return 1;
        if (found < hash) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

static int ReadEntry(LONG logical, WeaponSkeletonEntry *out) {
    WeaponSkeletonEntry *source;
    LONG before, after;

    source = &g_skeletons[(unsigned)logical % WEAPON_SKELETON_CAP];
    before = InterlockedCompareExchange(&source->sequence, 0, 0);
    if (before & 1) return 0;
    MemoryBarrier();
    *out = *source;
    MemoryBarrier();
    after = InterlockedCompareExchange(&source->sequence, 0, 0);
    return before == after && !(after & 1) && out->skeleton;
}

static void __attribute__((ms_abi)) PublishHook(uint64_t skeleton,
                                                uint16_t slot,
                                                unsigned char updateChild) {
    if (skeleton) {
        LONG cursor = InterlockedIncrement(&g_skeletonCursor) - 1;
        WeaponSkeletonEntry *entry =
            &g_skeletons[(unsigned)cursor % WEAPON_SKELETON_CAP];
        LONG sequence = (cursor << 1) | 1;
        uint64_t owner = 0;

        InterlockedExchange(&entry->sequence, sequence);
        ShReadMem(skeleton + SKELETON_OWNER, &owner, sizeof(owner));
        entry->skeleton = skeleton;
        entry->owner = owner;
        MemoryBarrier();
        InterlockedExchange(&entry->sequence, sequence + 1);
    }
    g_originalPublish(skeleton, slot, updateChild);
}

static int InstallPublishHook(void) {
    static const unsigned char signature[] = {
        0x44,0x88,0x44,0x24,0x18,0x55,0x56,0x41,
        0x54,0x41,0x55,0x41,0x56,0x41,0x57
    };
    unsigned char *at;
    unsigned char patch[14];
    int32_t relative;
    DWORD oldProtect;

    at = (unsigned char *)GetModuleHandleA(NULL) + PUBLISH_THUNK_RVA;
    if (!ShReadableAddr((uint64_t)(uintptr_t)at, 16) || at[0] != 0xE9)
        return 0;
    memcpy(&relative, at + 1, sizeof(relative));
    g_originalPublish = (PublishFn)(at + 5 + relative);
    if (!ShReadableAddr((uint64_t)(uintptr_t)g_originalPublish,
                        sizeof(signature)) ||
        memcmp((const void *)(uintptr_t)g_originalPublish,
               signature, sizeof(signature)) != 0)
        return 0;

    patch[0] = 0xFF;
    patch[1] = 0x25;
    memset(patch + 2, 0, 4);
    {
        void *handler = (void *)(uintptr_t)PublishHook;
        memcpy(patch + 6, &handler, sizeof(handler));
    }
    if (!VirtualProtect(at, sizeof(patch), PAGE_EXECUTE_READWRITE,
                        &oldProtect))
        return 0;
    memcpy(at, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), at, sizeof(patch));
    VirtualProtect(at, sizeof(patch), oldProtect, &oldProtect);
    return 1;
}

void ShWeaponStartup(void) {
    g_weaponDiscoveryReady = InstallPublishHook();
}

/* Return the newest skeleton the engine actually published for the player.
 * A player entity can expose more than one skeleton-like component, so taking
 * the first component is not guaranteed to select the pose used for the
 * rendered hands and weapon. This mirrors the validated native BoneWorld
 * selection without retaining a stale pose buffer. */
uint64_t ShWeaponPlayerSkeleton(uint64_t playerEntity) {
    WeaponSkeletonEntry entry;
    LONG cursor;
    int i;

    if (!playerEntity || !g_weaponDiscoveryReady) return 0;
    cursor = InterlockedCompareExchange(&g_skeletonCursor, 0, 0);
    for (i = 0; i < WEAPON_SKELETON_CAP && i < cursor; i++) {
        if (ReadEntry(cursor - 1 - i, &entry) &&
            entry.owner == playerEntity)
            return entry.skeleton;
    }
    return 0;
}

uint64_t ShWeaponKeyNear(const ShVec3 *anchor) {
    ShPlayer player;
    WeaponSkeletonEntry entry;
    uint64_t playerSkeleton = 0;
    uint64_t bestSkeleton = 0;
    uint64_t bestKey = 0;
    uint64_t seen[256];
    int seenCount = 0;
    float bestDistance = 1e9f;
    LONG cursor;
    ULONGLONG now;
    int i, j;

    if (!anchor || !g_weaponDiscoveryReady ||
        !isfinite(anchor->x) || !isfinite(anchor->y) ||
        !isfinite(anchor->z))
        return 0;

    now = GetTickCount64();
    if (g_cachedKey && now - g_lastResolve < 250) return g_cachedKey;
    g_lastResolve = now;

    memset(&player, 0, sizeof(player));
    if (!ShPeekPlayer(&player) || !player.entity) return g_cachedKey;
    if (player.entity != g_cachedPlayer) {
        g_cachedPlayer = player.entity;
        g_cachedKey = 0;
    }

    cursor = InterlockedCompareExchange(&g_skeletonCursor, 0, 0);
    for (i = 0; i < WEAPON_SKELETON_CAP && i < cursor; i++) {
        LONG logical = cursor - 1 - i;
        if (ReadEntry(logical, &entry) && entry.owner == player.entity) {
            playerSkeleton = entry.skeleton;
            break;
        }
    }
    if (!playerSkeleton) return g_cachedKey;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < WEAPON_SKELETON_CAP && i < cursor &&
                seenCount < (int)(sizeof(seen) / sizeof(seen[0])); i++) {
        uint64_t rig = 0;
        uint64_t entity = 0;
        uint64_t node = 0;
        uint16_t bones = 0;
        float origin[3];
        float dx, dy, dz, distance;
        LONG logical = cursor - 1 - i;

        if (!ReadEntry(logical, &entry) ||
            entry.skeleton == playerSkeleton)
            continue;
        for (j = 0; j < seenCount; j++)
            if (seen[j] == entry.skeleton) break;
        if (j < seenCount) continue;
        seen[seenCount++] = entry.skeleton;

        if (!ReadU64(entry.skeleton + SKELETON_RIG, &rig) || !rig ||
            !ShReadMem(rig + RIG_BONE_COUNT, &bones, sizeof(bones)) ||
            bones < 2 || bones > 64 || RigHasName(rig, HASH_HEAD) ||
            !ShReadMem(entry.skeleton + SKELETON_ORIGIN,
                       origin, sizeof(origin)) ||
            !isfinite(origin[0]) || !isfinite(origin[1]) ||
            !isfinite(origin[2]))
            continue;

        dx = origin[0] - anchor->x;
        dy = origin[1] - anchor->y;
        dz = origin[2] - anchor->z;
        distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestSkeleton = entry.skeleton;
            bestKey = entry.skeleton;
            if (ReadU64(entry.skeleton + SKELETON_OWNER, &entity) &&
                entity > 0x10000 && ReadU64(entity + 0x18, &node) && node)
                bestKey = node;
        }
    }

    if (bestSkeleton && bestDistance <= 0.10f) g_cachedKey = bestKey;
    return g_cachedKey;
}

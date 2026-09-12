/* Wildlands Mod Framework / Immersion Suite fork.
 * Original GRW ScriptHook portions Copyright (C) 2026 PhialsBasement.
 * Modifications and additions Copyright (C) 2026 iDarkslay.
 * Modified through 2026-09-12. SPDX-License-Identifier: GPL-3.0-only.
 * See LICENSE, NOTICE.md and UPSTREAM_CHANGES.md.
 */
/* Weather and time via the engine's request record.
 * FINDINGS.md "THE WEATHER FRONT DOOR". No code patches.
 */
#include <windows.h>
#include <string.h>
#include <stdint.h>

#define SH_BUILD 1
#include "scripthook.h"
#include "image.h"

/* Served by the env object each frame. */
#define WX_RECORD   SH_IMG(0x4495E90)
#define REC_ENV     0x00
#define REC_TIME    0x08   /* hours */

#define ENV_VTABLE  SH_IMG(0x39D20B8)
#define ENV_TYPE    0x130
#define TIME_MGR    SH_IMG(0x4B9B4F8)
#define TM_GET      0x270
#define TM_SET      0x274

/* ChangeTimeAndWeather node: its time half. */
#define OBJ_FACTORY   SH_IMG(0xE0E0C70)
#define CTW_OP_DESC   SH_IMG(0x49E2AF0)
#define CTW_DATA_DESC SH_IMG(0x49E2A50)
#define CTW_START     SH_IMG(0x2827B50)
#define WX_SET_DURATION SH_IMG(0xC8F96F0)

#define D_WEATHER_ON 0x60
#define D_MODE       0x70
#define D_TIME_ON    0x71
#define D_HOURS      0x74
#define D_MINUTES    0x78
#define D_FREEZE     0x80

#define CALL_WAIT_MS 1000

extern int ShReadableAddr(uint64_t addr, size_t len);
extern int ShReadMem(uint64_t addr, void *out, size_t len);
extern uint64_t ShReadQ(uint64_t addr);
extern void ShSetError(int err);
extern int ShRequireInGame(void);
extern int ShQueueCall(uint64_t fn, uint64_t a0, uint64_t a1,
                       uint64_t a2, uint64_t a3);
extern int ShQueueResult(uint64_t *outRet);

/* Kernel write, fails clean on a freed object. */
static int WriteMem(uint64_t addr, const void *v, size_t len) {
    SIZE_T put = 0;

    if (!WriteProcessMemory(GetCurrentProcess(),
                            (void *)(uintptr_t)addr, v, len, &put))
        return 0;
    return put == len;
}

/* Weather ids, indexed by SH_WEATHER_*. */
static const uint64_t g_id[6] = {
    520430410077ULL,   /* sunny        */
    283912574176ULL,   /* clouds light */
    520430611713ULL,   /* clouds heavy */
    532369849126ULL,   /* fog          */
    520430672675ULL,   /* rain light   */
    520430431606ULL,   /* rain heavy   */
};

static int Sane(uint64_t p) {
    return p >= 0x10000ULL && p < 0x800000000000ULL;
}

static uint64_t Env(void) {
    uint64_t env;

    env = ShReadQ(WX_RECORD + REC_ENV);
    if (!Sane(env)) return 0;
    if (ShReadQ(env) != ENV_VTABLE) return 0;
    return env;
}

static int QueueWeatherRequest(int type, float seconds, int enabled,
                               int customDuration);

SH_API int ShSetWeatherBlend(int type, float seconds) {
    if (type < 0 || type >= 6 || seconds < 0.0f ||
        seconds > 3600.0f) {
        ShSetError(SH_ERR_BAD_ARG);
        return 0;
    }
    return QueueWeatherRequest(type, seconds, 1, 1);
}

SH_API int ShSetWeather(int type) {
    if (type < 0 || type >= 6) {
        ShSetError(SH_ERR_BAD_ARG);
        return 0;
    }
    return QueueWeatherRequest(type, 0.0f, 1, 0);
}

SH_API int ShReleaseWeather(void) {
    return QueueWeatherRequest(0, 0.0f, 0, 0);
}

SH_API int ShGetWeather(int *out) {
    uint64_t env, id;
    int i;

    if (!out) { ShSetError(SH_ERR_BAD_ARG); return 0; }
    env = Env();
    if (!env) { ShSetError(SH_ERR_NOT_IN_GAME); return 0; }

    id = ShReadQ(env + ENV_TYPE);
    for (i = 0; i < 6; i++) {
        if (id == g_id[i]) {
            *out = i;
            ShSetError(SH_OK);
            return 1;
        }
    }
    ShSetError(SH_ERR_NO_CANDIDATE);
    return 0;
}

SH_API int ShGetTime(float *out) {
    if (!out) { ShSetError(SH_ERR_BAD_ARG); return 0; }
    if (!Env()) { ShSetError(SH_ERR_NOT_IN_GAME); return 0; }
    if (!ShReadMem(WX_RECORD + REC_TIME, out, 4)) {
        ShSetError(SH_ERR_NO_CANDIDATE);
        return 0;
    }
    ShSetError(SH_OK);
    return 1;
}

/* ---- time of day ---- */

typedef struct { uint64_t st, pin, r2, r3; } CtwRes;
typedef uint64_t (*Factory_t)(uint64_t desc, uint64_t existing);
typedef CtwRes *(*CtwStart_t)(CtwRes *out, uint64_t op,
                              uint64_t ctx, uint64_t data);
typedef void (__attribute__((ms_abi)) *WeatherDuration_t)(uint64_t record,
                                                          float seconds);

static uint64_t g_ctwOp, g_ctwData;

static int EnsureCtwObjects(void) {
    if (!g_ctwOp) {
        Factory_t fac = (Factory_t)(uintptr_t)OBJ_FACTORY;
        g_ctwOp = fac(CTW_OP_DESC, 0);
        g_ctwData = fac(CTW_DATA_DESC, 0);
    }
    return g_ctwOp && g_ctwData;
}

/* Game thread only. The engine's ChangeTimeAndWeather node owns the request
 * lifecycle; direct writes to the old request offsets can succeed without
 * causing a visible weather change on the current executable.
 */
static uint64_t WeatherHelper(uint64_t weatherId, uint64_t enabled,
                              uint64_t a2, uint64_t a3) {
    CtwRes res;
    uint8_t *d;
    uint32_t secondsBits = (uint32_t)a2;
    float seconds = 0.0f;

    memcpy(&seconds, &secondsBits, sizeof(seconds));
    if (!EnsureCtwObjects()) return 0;
    d = (uint8_t *)(uintptr_t)g_ctwData;
    d[D_WEATHER_ON] = enabled ? 1 : 0;
    memcpy(d + 0x68, &weatherId, sizeof(weatherId));
    d[D_MODE] = 0;
    d[D_TIME_ON] = 0;
    d[D_FREEZE] = 0;
    ((CtwStart_t)(uintptr_t)CTW_START)(&res, g_ctwOp, 0, g_ctwData);
    /* ShSetWeather keeps the selected preset's native blend. The legacy
     * ShSetWeatherBlend API can still request a custom duration. */
    if (enabled && a3)
        ((WeatherDuration_t)(uintptr_t)WX_SET_DURATION)(WX_RECORD, seconds);
    return res.st == 1;
}

static int QueueWeatherRequest(int type, float seconds, int enabled,
                               int customDuration) {
    uint64_t ret = 0, secondsBits = 0;
    uint32_t bits = 0;
    int waited = 0;

    if (!ShRequireInGame()) return 0;
    if (!Env()) { ShSetError(SH_ERR_NOT_IN_GAME); return 0; }
    memcpy(&bits, &seconds, sizeof(bits));
    secondsBits = bits;
    while (!ShQueueCall((uint64_t)(uintptr_t)WeatherHelper,
                        enabled ? g_id[type] : 0, enabled ? 1 : 0,
                        secondsBits, customDuration ? 1 : 0)) {
        if (++waited > CALL_WAIT_MS) {
            ShSetError(SH_ERR_HOOK_FAILED);
            return 0;
        }
        Sleep(1);
    }
    while (!ShQueueResult(&ret)) {
        if (++waited > CALL_WAIT_MS) {
            ShSetError(SH_ERR_HOOK_FAILED);
            return 0;
        }
        Sleep(1);
    }
    if (!ret) { ShSetError(SH_ERR_NO_CANDIDATE); return 0; }
    ShSetError(SH_OK);
    return 1;
}

/* Game thread only, via the call queue. */
static uint64_t TimeHelper(uint64_t hoursBits, uint64_t a1,
                           uint64_t a2, uint64_t a3) {
    float hours;
    uint32_t bits = (uint32_t)hoursBits;
    CtwRes res;
    uint8_t *d;

    (void)a1; (void)a2; (void)a3;
    memcpy(&hours, &bits, 4);

    if (!EnsureCtwObjects()) return 0;

    d = (uint8_t *)(uintptr_t)g_ctwData;
    d[D_WEATHER_ON] = 0;
    d[D_MODE] = 0;
    d[D_TIME_ON] = 1;
    d[D_FREEZE] = 0;
    memcpy(d + D_HOURS, &hours, 4);
    memset(d + D_MINUTES, 0, 8);

    ((CtwStart_t)(uintptr_t)CTW_START)(&res, g_ctwOp, 0,
                                       g_ctwData);
    return res.st == 1;
}

SH_API int ShSetTime(float hours) {
    uint64_t ret = 0, bits64;
    uint32_t bits;
    int waited = 0;

    if (hours < 0.0f) hours = 0.0f;
    if (hours >= 24.0f)
        hours = hours - 24.0f * (float)(int)(hours / 24.0f);
    if (!ShRequireInGame()) return 0;
    if (!Env()) { ShSetError(SH_ERR_NOT_IN_GAME); return 0; }

    memcpy(&bits, &hours, 4);
    bits64 = bits;
    while (!ShQueueCall((uint64_t)(uintptr_t)TimeHelper,
                        bits64, 0, 0, 0)) {
        if (++waited > CALL_WAIT_MS) {
            ShSetError(SH_ERR_HOOK_FAILED);
            return 0;
        }
        Sleep(1);
    }
    while (!ShQueueResult(&ret)) {
        if (++waited > CALL_WAIT_MS) {
            ShSetError(SH_ERR_HOOK_FAILED);
            return 0;
        }
        Sleep(1);
    }
    if (!ret) { ShSetError(SH_ERR_NO_CANDIDATE); return 0; }
    ShSetError(SH_OK);
    return 1;
}

/* ---- clock rate ---- */

static uint64_t TimeMgr(void) {
    uint64_t mgr = ShReadQ(TIME_MGR);

    if (!Sane(mgr) || !ShReadableAddr(mgr + TM_GET, 8))
        return 0;
    return mgr;
}

SH_API int ShSetTimeSpeed(float multiplier) {
    uint64_t mgr;

    if (multiplier < 0.0f || multiplier > 100000.0f) {
        ShSetError(SH_ERR_BAD_ARG);
        return 0;
    }
    mgr = TimeMgr();
    if (!mgr) { ShSetError(SH_ERR_NOT_IN_GAME); return 0; }
    if (!WriteMem(mgr + TM_SET, &multiplier, 4)) {
        ShSetError(SH_ERR_UNWRITABLE);
        return 0;
    }
    ShSetError(SH_OK);
    return 1;
}

SH_API int ShGetTimeSpeed(float *out) {
    uint64_t mgr;

    if (!out) { ShSetError(SH_ERR_BAD_ARG); return 0; }
    mgr = TimeMgr();
    if (!mgr) { ShSetError(SH_ERR_NOT_IN_GAME); return 0; }
    if (!ShReadMem(mgr + TM_GET, out, 4)) {
        ShSetError(SH_ERR_NO_CANDIDATE);
        return 0;
    }
    ShSetError(SH_OK);
    return 1;
}

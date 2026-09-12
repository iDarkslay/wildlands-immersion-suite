/* Wildlands Immersion Suite component.
 * Copyright (C) 2026 iDarkslay.
 * Modified through 2026-09-12. SPDX-License-Identifier: GPL-3.0-only.
 * See LICENSE and NOTICE.md.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Wildlands Immersion Suite movement feature
 *
 * Independent in-process implementation for GRW build 133.1.0.9840374
 * (Steam build 24669148). No third-party runtime source is included.
 */

#define CONTROL_SITE_RVA 0x13FDB762ULL
#define MAGNITUDE_FN_RVA 0x007D2800ULL
#define CAVE_SIZE 0x1000
#define SCALE_OFFSET 0x40
#define MIN_LEVEL 15
#define MAX_LEVEL 100
#define LEVEL_STEP 5
#include "suite_config.h"
#define SUITE_INI SH_SUITE_CONFIG_REL
#define CFG_SECTION "MovementSpeedControl"

typedef void (*MenuFn)(uint32_t,uint32_t,int,void*);
typedef uint32_t (*MenuCreateFn)(const char*);
typedef int (*MenuToggleFn)(uint32_t,const char*,int,MenuFn,void*);
typedef int (*MenuNumberFn)(uint32_t,const char*,float,float,float,float,MenuFn,void*);
typedef int (*MenuActionFn)(uint32_t,const char*,MenuFn,void*);
typedef int (*MenuStatusFn)(uint32_t,const char*);
typedef int (*MenuDestroyFn)(uint32_t);
typedef int (*MenuSetNumberFn)(uint32_t,const char*,float);
typedef int (*MenuDescribeFn)(uint32_t,const char*,const char*);

static volatile LONG g_stop;
static volatile LONG g_enabled = 1;
static HANDLE g_thread;
static HHOOK g_mouseHook;
static DWORD g_threadId;
static uint8_t *g_cave;
static uint8_t g_original[5];
static uint8_t *g_controlSite;
static float *g_scale;
static int g_level = 100;
static uint32_t g_menu;
static MenuStatusFn g_menuStatus;
static MenuDestroyFn g_menuDestroy;
static MenuSetNumberFn g_menuSetNumber;
static MenuDescribeFn g_menuDescribe;
static int g_hookInstalled;
static DWORD g_mouseHookError;



static void LoadMovementConfig(void){
    int enabled=GetPrivateProfileIntA(CFG_SECTION,"Enabled",1,SUITE_INI);
    int level=GetPrivateProfileIntA(CFG_SECTION,"SpeedPercent",100,SUITE_INI);
    if(level<MIN_LEVEL)level=MIN_LEVEL;if(level>MAX_LEVEL)level=MAX_LEVEL;
    InterlockedExchange(&g_enabled,enabled?1:0);g_level=level;
}

void ImmersiveMovementSaveConfig(void){
    char value[32];
    snprintf(value,sizeof(value),"%ld",(long)InterlockedCompareExchange(&g_enabled,0,0));
    WritePrivateProfileStringA(CFG_SECTION,"Enabled",value,SUITE_INI);
    snprintf(value,sizeof(value),"%d",g_level);
    WritePrivateProfileStringA(CFG_SECTION,"SpeedPercent",value,SUITE_INI);
    ((void)0);
}

static int Rel32(uint8_t *from, uint8_t *to, int32_t *out) {
    int64_t d = (int64_t)(to - (from + 5));
    if (d < INT32_MIN || d > INT32_MAX) return 0;
    *out = (int32_t)d;
    return 1;
}

static int WriteCode(void *dst, const void *src, SIZE_T size) {
    DWORD oldProtect;
    if (!VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldProtect)) return 0;
    memcpy(dst, src, size);
    FlushInstructionCache(GetCurrentProcess(), dst, size);
    VirtualProtect(dst, size, oldProtect, &oldProtect);
    return 1;
}

static void *AllocateNear(void *origin, SIZE_T size) {
    SYSTEM_INFO si;
    uintptr_t base = (uintptr_t)origin;
    uintptr_t gran, start;
    unsigned i;
    GetSystemInfo(&si);
    gran = si.dwAllocationGranularity;
    start = base & ~(gran - 1);
    for (i = 1; i < 0x7000; ++i) {
        uintptr_t delta = (uintptr_t)i * gran;
        void *p = NULL;
        if (start >= delta) p = VirtualAlloc((void *)(start - delta), size,
                                              MEM_COMMIT | MEM_RESERVE,
                                              PAGE_EXECUTE_READWRITE);
        if (!p && start + delta > start)
            p = VirtualAlloc((void *)(start + delta), size,
                             MEM_COMMIT | MEM_RESERVE,
                             PAGE_EXECUTE_READWRITE);
        if (p) return p;
    }
    return NULL;
}

static void UpdateScale(void) {
    if (g_scale) *g_scale = InterlockedCompareExchange(&g_enabled,0,0)
        ? (float)g_level / 100.0f : 1.0f;
}

static void UpdateStatus(void) {
    char text[96];
    if (!g_menu || !g_menuStatus) return;
    if(g_menuSetNumber)g_menuSetNumber(g_menu,"Current movement speed %",(float)g_level);
    snprintf(text,sizeof(text),"%s | %d%% | engine %s | mouse %s",
             InterlockedCompareExchange(&g_enabled,0,0)?"on":"off",g_level,
             g_hookInstalled?"ready":"unsupported",
             g_mouseHook?"ready":(g_mouseHookError?"failed":"starting"));
    g_menuStatus(g_menu,text);
}

static int GameForeground(void) {
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    if (!w) return 0;
    GetWindowThreadProcessId(w,&pid);
    return pid == GetCurrentProcessId();
}

static int MovementKeyDown(void) {
    return (GetAsyncKeyState('W') & 0x8000) ||
           (GetAsyncKeyState('A') & 0x8000) ||
           (GetAsyncKeyState('S') & 0x8000) ||
           (GetAsyncKeyState('D') & 0x8000);
}

static LRESULT CALLBACK MouseProc(int code, WPARAM msg, LPARAM data) {
    if (code == HC_ACTION && msg == WM_MOUSEWHEEL &&
        InterlockedCompareExchange(&g_enabled,0,0) &&
        GameForeground() && MovementKeyDown()) {
        const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)data;
        short wheel = (short)HIWORD(m->mouseData);
        g_level += wheel > 0 ? LEVEL_STEP : -LEVEL_STEP;
        if (g_level < MIN_LEVEL) g_level = MIN_LEVEL;
        if (g_level > MAX_LEVEL) g_level = MAX_LEVEL;
        UpdateScale();
        UpdateStatus();
        return 1; /* Do not also cycle weapons while adjusting speed. */
    }
    return CallNextHookEx(g_mouseHook,code,msg,data);
}

static int InstallMovementHook(void) {
    uint8_t expected[5], redirect[5];
    uint8_t *image = (uint8_t *)GetModuleHandleA(NULL);
    uint8_t *target;
    int32_t rel;
    if (!image) return 0;
    g_controlSite = image + CONTROL_SITE_RVA;
    target = image + MAGNITUDE_FN_RVA;
    expected[0] = 0xE8;
    if (!Rel32(g_controlSite,target,&rel)) return 0;
    memcpy(expected+1,&rel,4);
    if (memcmp(g_controlSite,expected,5) != 0) return 0;
    memcpy(g_original,g_controlSite,5);
    g_cave = (uint8_t *)AllocateNear(g_controlSite,CAVE_SIZE);
    if (!g_cave) return 0;
    memset(g_cave,0xCC,CAVE_SIZE);
    g_cave[0] = 0xE8;
    if (!Rel32(g_cave,target,&rel)) return 0;
    memcpy(g_cave+1,&rel,4);
    g_cave[5]=0xF3; g_cave[6]=0x0F; g_cave[7]=0x59; g_cave[8]=0x05;
    rel = (int32_t)((g_cave + SCALE_OFFSET) - (g_cave + 13));
    memcpy(g_cave+9,&rel,4);
    g_cave[13]=0xC3;
    g_scale=(float *)(g_cave+SCALE_OFFSET);
    UpdateScale();
    /* The selected scale is intentionally live data in this allocation.
     * Keep the private cave writable while installed; making the whole page
     * RX caused an access violation on the first mouse-wheel adjustment. */
    redirect[0]=0xE8;
    if (!Rel32(g_controlSite,g_cave,&rel)) return 0;
    memcpy(redirect+1,&rel,4);
    if (!WriteCode(g_controlSite,redirect,5)) return 0;
    return memcmp(g_controlSite,redirect,5)==0;
}

static void RemoveMovementHook(void) {
    if (g_controlSite && g_original[0]) WriteCode(g_controlSite,g_original,5);
    g_controlSite=NULL;
    if (g_cave) VirtualFree(g_cave,0,MEM_RELEASE);
    g_cave=NULL;
    g_scale=NULL;
}

static void OnEnabled(uint32_t menu,uint32_t item,int value,void *user) {
    (void)menu;(void)item;(void)user;
    InterlockedExchange(&g_enabled,value?1:0);
    UpdateScale(); UpdateStatus();
}

static void OnLevel(uint32_t menu,uint32_t item,int value,void *user) {
    (void)menu;(void)item;(void)user;
    if (value<MIN_LEVEL)value=MIN_LEVEL;
    if (value>MAX_LEVEL)value=MAX_LEVEL;
    g_level=value; UpdateScale(); UpdateStatus();
}

static void OnReset(uint32_t menu,uint32_t item,int value,void *user) {
    (void)menu;(void)item;(void)value;(void)user;
    g_level=100; UpdateScale(); UpdateStatus();
}

static void OnSave(uint32_t menu,uint32_t item,int value,void *user){
    (void)menu;(void)item;(void)value;(void)user;
    ImmersiveMovementSaveConfig();UpdateStatus();
}

static void CreateMenuItems(uint32_t parent, int createRoot) {
    HMODULE h=GetModuleHandleA("dinput8.dll");
    union{FARPROC p;MenuCreateFn f;}create;
    union{FARPROC p;MenuToggleFn f;}toggle;
    union{FARPROC p;MenuNumberFn f;}number;
    union{FARPROC p;MenuActionFn f;}action;
    union{FARPROC p;MenuStatusFn f;}status;
    union{FARPROC p;MenuDestroyFn f;}destroy;
    union{FARPROC p;MenuSetNumberFn f;}setNumber;
    union{FARPROC p;MenuDescribeFn f;}describe;
    if(!h)return;
    create.p=GetProcAddress(h,"ShMenuCreate"); toggle.p=GetProcAddress(h,"ShMenuToggle");
    number.p=GetProcAddress(h,"ShMenuNumber"); action.p=GetProcAddress(h,"ShMenuAction");
    status.p=GetProcAddress(h,"ShMenuStatus"); destroy.p=GetProcAddress(h,"ShMenuDestroy");
    setNumber.p=GetProcAddress(h,"ShMenuSetNumber");
    describe.p=GetProcAddress(h,"ShMenuDescribe");
    g_menuStatus=status.f;g_menuDestroy=destroy.f;g_menuSetNumber=setNumber.f;g_menuDescribe=describe.f;
    if(!toggle.f||!number.f||!action.f)return;
    if(createRoot){if(!create.f)return;g_menu=create.f("Immersive Movement [EXPERIMENTAL]");}
    else g_menu=parent;
    toggle.f(g_menu,"Enable movement control",1,OnEnabled,NULL);
    number.f(g_menu,"Current movement speed %",100,MIN_LEVEL,MAX_LEVEL,LEVEL_STEP,OnLevel,NULL);
    action.f(g_menu,"Reset to 100%",OnReset,NULL);
    action.f(g_menu,"Save movement configuration",OnSave,NULL);
    if(g_menuDescribe){
        g_menuDescribe(g_menu,"Enable movement control","Enable mouse-wheel movement-speed control while a movement key is held.");
        g_menuDescribe(g_menu,"Current movement speed %","Sets the active movement-speed multiplier from 15% to 100%. Hold a movement key and use the mouse wheel, or adjust this value directly.");
        g_menuDescribe(g_menu,"Reset to 100%","Restore the normal game movement speed without disabling the module.");
        g_menuDescribe(g_menu,"Save movement configuration","Save the enabled state and selected movement speed to Fmods/immersion_suite.ini.");
    }
    UpdateStatus();
}

static DWORD WINAPI Worker(LPVOID unused) {
    MSG msg;HMODULE self=NULL;(void)unused;
    g_threadId=GetCurrentThreadId();
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(uintptr_t)&MouseProc,&self);
    g_mouseHook=SetWindowsHookExA(WH_MOUSE_LL,MouseProc,self,0);
    g_mouseHookError=g_mouseHook?0:GetLastError();
    ((void)0);
    UpdateStatus();
    while(!InterlockedCompareExchange(&g_stop,0,0) && GetMessageA(&msg,NULL,0,0)>0){
        TranslateMessage(&msg); DispatchMessageA(&msg);
    }
    if(g_mouseHook){UnhookWindowsHookEx(g_mouseHook);g_mouseHook=NULL;}
    return 0;
}

int ImmersiveMovementInit(void) {
    g_stop=0; g_level=100; g_original[0]=0;g_hookInstalled=0;g_mouseHookError=0;
    LoadMovementConfig();
    if(!InstallMovementHook()){((void)0);return 0;}
    g_hookInstalled=1;((void)0);
    g_thread=CreateThread(NULL,0,Worker,NULL,0,NULL);
    if(!g_thread){RemoveMovementHook();return 0;}
    return 1;
}

void ImmersiveMovementAddToSuiteMenu(uint32_t suiteRoot) {
    HMODULE h=GetModuleHandleA("dinput8.dll");
    typedef uint32_t (*MenuSubFn)(uint32_t,const char*);
    union{FARPROC p;MenuSubFn f;}sub;
    sub.p=h?GetProcAddress(h,"ShMenuSub"):NULL;
    if(sub.f)CreateMenuItems(sub.f(suiteRoot,"Movement Speed Control"),0);
}

int ImmersiveMovementShutdown(void) {
    InterlockedExchange(&g_stop,1);
    if(g_threadId)PostThreadMessageA(g_threadId,WM_QUIT,0,0);
    if(g_thread){
        if(WaitForSingleObject(g_thread,3000)!=WAIT_OBJECT_0)return 0;
        CloseHandle(g_thread);g_thread=NULL;
    }
    g_menu=0; RemoveMovementHook();g_hookInstalled=0;((void)0);return 1;
}

#ifndef IMMERSIVE_MOVEMENT_EMBEDDED
__declspec(dllexport) int GrwModInit(void) {
    if(!ImmersiveMovementInit())return 0;
    CreateMenuItems(0,1);
    return 1;
}
__declspec(dllexport) int GrwModShutdown(void) {return ImmersiveMovementShutdown();}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
    (void)reserved;
    if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);
    return TRUE;
}
#endif

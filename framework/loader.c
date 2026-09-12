/* Wildlands Mod Framework / Immersion Suite fork.
 * Original GRW ScriptHook portions Copyright (C) 2026 PhialsBasement.
 * Modifications and additions Copyright (C) 2026 iDarkslay.
 * Modified through 2026-09-12. SPDX-License-Identifier: GPL-3.0-only.
 * See LICENSE, NOTICE.md and UPSTREAM_CHANGES.md.
 */
#include <windows.h>
#include <stdio.h>
#include "log.h"
#include "scripthook.h"

typedef HRESULT (WINAPI *DirectInput8Create_t)(
    HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT (WINAPI *DllCanUnloadNow_t)(void);
typedef HRESULT (WINAPI *DllGetClassObject_t)(REFCLSID, REFIID, LPVOID *);
typedef HRESULT (WINAPI *DllRegisterServer_t)(void);
typedef HRESULT (WINAPI *DllUnregisterServer_t)(void);

static HMODULE g_realDinput8 = NULL;

static DirectInput8Create_t   p_DirectInput8Create;
static DllCanUnloadNow_t      p_DllCanUnloadNow;
static DllGetClassObject_t    p_DllGetClassObject;
static DllRegisterServer_t    p_DllRegisterServer;
static DllUnregisterServer_t  p_DllUnregisterServer;

static void LoadRealDinput8(void) {
    char sysdir[MAX_PATH];
    GetSystemDirectoryA(sysdir, MAX_PATH);
    strcat(sysdir, "\\dinput8.dll");

    g_realDinput8 = LoadLibraryA(sysdir);
    if (!g_realDinput8) {
        ((void)0);
        return;
    }
    ((void)0);

    p_DirectInput8Create = (DirectInput8Create_t)
        GetProcAddress(g_realDinput8, "DirectInput8Create");
    p_DllCanUnloadNow = (DllCanUnloadNow_t)
        GetProcAddress(g_realDinput8, "DllCanUnloadNow");
    p_DllGetClassObject = (DllGetClassObject_t)
        GetProcAddress(g_realDinput8, "DllGetClassObject");
    p_DllRegisterServer = (DllRegisterServer_t)
        GetProcAddress(g_realDinput8, "DllRegisterServer");
    p_DllUnregisterServer = (DllUnregisterServer_t)
        GetProcAddress(g_realDinput8, "DllUnregisterServer");
}

extern void ShStateStartup(void);

extern void ShHotModStartup(void);
extern void ShWeaponStartup(void);

/* Keep legacy compatibility with the original ScriptHook layout while also
 * supporting the cleaner Fmods directory. Fmods wins when the same
 * filename exists in both locations. */
#define MAX_ASI_PLUGINS 256
static char g_loadedAsi[MAX_ASI_PLUGINS][MAX_PATH];
static int g_loadedAsiCount;

static int AsiAlreadyLoaded(const char *name) {
    int i;
    for(i=0;i<g_loadedAsiCount;i++)
        if(_stricmp(g_loadedAsi[i],name)==0)return 1;
    return 0;
}

static void RememberAsi(const char *name) {
    if(g_loadedAsiCount>=MAX_ASI_PLUGINS)return;
    strncpy(g_loadedAsi[g_loadedAsiCount],name,MAX_PATH-1);
    g_loadedAsi[g_loadedAsiCount][MAX_PATH-1]=0;
    g_loadedAsiCount++;
}

static void LoadASIDirectory(const char *dir,const char *source) {
    char pat[MAX_PATH],full[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof(pat), "%s*.asi", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        ((void)0);
        return;
    }
    do {
        HMODULE mod;
        if(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)continue;
        if(AsiAlreadyLoaded(fd.cFileName)){
            ((void)0);
            continue;
        }
        snprintf(full, sizeof(full), "%s%s", dir, fd.cFileName);
        ((void)0);
        mod = LoadLibraryA(full);
        if (mod) {
            RememberAsi(fd.cFileName);
            ((void)0);
        } else
            ((void)0);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* Resolve paths beside GRW.exe, never from the process working directory. */
static void LoadASIPlugins(void) {
    char game[MAX_PATH],fmods[MAX_PATH];
    char *slash;
    if(!GetModuleFileNameA(NULL,game,MAX_PATH)){((void)0);return;}
    slash=strrchr(game,'\\');
    if(slash)slash[1]=0;else game[0]=0;
    strncpy(fmods,game,sizeof(fmods)-1);fmods[sizeof(fmods)-1]=0;
    strncat(fmods,"Fmods\\",sizeof(fmods)-strlen(fmods)-1);
    CreateDirectoryA(fmods,NULL);
    g_loadedAsiCount=0;
    LoadASIDirectory(fmods,"Fmods");
    LoadASIDirectory(game,"game directory");
}

/* Plugins load here, not in DllMain. LoadLibrary blocks on
 * the loader lock until DllMain returns, so a plugin binds
 * against a fully initialised DLL and may import it. */
static DWORD WINAPI LoaderThread(LPVOID p) {
    (void)p;
    ShWeaponStartup();
    LoadASIPlugins();
    ShHotModStartup();
    return 0;
}

static BOOL IsGRW(void) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char *name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    return (_stricmp(name, "GRW.exe") == 0);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        if (!IsGRW()) return TRUE;
        ((void)0);
        ((void)0);
        ((void)0);
        
        
        LoadRealDinput8();
        /* Watch state before plugins, so the world is
         * resolved by the time any of them ask.
         */
        ShStateStartup();
        CreateThread(NULL, 0, LoaderThread, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        ((void)0);
        ((void)0);
        if (g_realDinput8) FreeLibrary(g_realDinput8);
    }
    return TRUE;
}

extern void ShWrapDirectInput(void *di);

__declspec(dllexport) HRESULT WINAPI DirectInput8Create(
    HINSTANCE inst, DWORD ver, REFIID iid, LPVOID *out, LPUNKNOWN outer)
{
    HRESULT hr;
    if (!p_DirectInput8Create) return E_FAIL;
    hr = p_DirectInput8Create(inst, ver, iid, out, outer);
    ((void)0);
    if (SUCCEEDED(hr) && out && *out) ShWrapDirectInput(*out);
    return hr;
}

__declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void) {
    if (p_DllCanUnloadNow) return p_DllCanUnloadNow();
    return S_FALSE;
}

__declspec(dllexport) HRESULT WINAPI DllGetClassObject(
    REFCLSID clsid, REFIID iid, LPVOID *out)
{
    if (p_DllGetClassObject)
        return p_DllGetClassObject(clsid, iid, out);
    return E_FAIL;
}

__declspec(dllexport) HRESULT WINAPI DllRegisterServer(void) {
    if (p_DllRegisterServer) return p_DllRegisterServer();
    return E_FAIL;
}

__declspec(dllexport) HRESULT WINAPI DllUnregisterServer(void) {
    if (p_DllUnregisterServer) return p_DllUnregisterServer();
    return E_FAIL;
}

/* Wildlands Mod Framework / Immersion Suite fork.
 * Original GRW ScriptHook portions Copyright (C) 2026 PhialsBasement.
 * Modifications and additions Copyright (C) 2026 iDarkslay.
 * Modified through 2026-09-12. SPDX-License-Identifier: GPL-3.0-only.
 * See LICENSE, NOTICE.md and UPSTREAM_CHANGES.md.
 */
/* Generic reloadable-mod host. Hot mods are cooperative DLLs exporting:
 *   int GrwModInit(void);
 *   int GrwModShutdown(void);  -- must stop/join every worker before success
 * Source files are name.grwmod.dll. Builders may publish name.grwmod.next.dll;
 * reload promotes next atomically, then loads a unique shadow copy so the
 * source file remains replaceable while the game runs.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#define SH_BUILD
#include "scripthook.h"
#include "scripthook_config.h"
#include "log.h"

#define HOT_MAX 32
typedef int (*ModFn)(void);
typedef struct {char name[MAX_PATH];char source[MAX_PATH];char shadow[MAX_PATH];HMODULE module;ModFn init;ModFn shutdown;unsigned generation;} HotSlot;
static HotSlot g_hot[HOT_MAX];static int g_hotCount;static CRITICAL_SECTION g_hotLock;static int g_hotReady;static volatile LONG g_hotBusy;static uint32_t g_hotHud;
static int g_reloadKey=VK_F9,g_noticeMs=1800;

static void GameDir(char *out,size_t cap){char *slash;GetModuleFileNameA(NULL,out,(DWORD)cap);slash=strrchr(out,'\\');if(slash)slash[1]=0;else out[0]=0;strncat(out,"Fmods\\",cap-strlen(out)-1);CreateDirectoryA(out,NULL);}
static int Ends(const char *s,const char *tail){size_t a=strlen(s),b=strlen(tail);return a>=b&&!_stricmp(s+a-b,tail);}
static void BaseName(const char *file,char *out,size_t cap){size_t n=strlen(file);const char *tail=".grwmod.dll";if(n>strlen(tail)&&Ends(file,tail))n-=strlen(tail);if(n>=cap)n=cap-1;memcpy(out,file,n);out[n]=0;}
static HotSlot *Slot(const char *name,int create){int i;for(i=0;i<g_hotCount;i++)if(!_stricmp(g_hot[i].name,name))return &g_hot[i];if(!create||g_hotCount>=HOT_MAX)return NULL;ZeroMemory(&g_hot[g_hotCount],sizeof(g_hot[0]));strncpy(g_hot[g_hotCount].name,name,sizeof(g_hot[0].name)-1);return &g_hot[g_hotCount++];}

/* A loaded DLL is locked by Windows, hence each generation needs a temporary
 * shadow file. Normal reload removes its previous generation immediately.
 * Files left by a crash or forced process exit are safe to remove at the next
 * framework startup, before any hot module has been loaded. */
static void CleanupStaleShadows(void){
    char dir[MAX_PATH],pattern[MAX_PATH],path[MAX_PATH];WIN32_FIND_DATAA fd;HANDLE h;int removed=0;
    GameDir(dir,sizeof(dir));snprintf(pattern,sizeof(pattern),"%s*.grwmod.loaded.*.dll",dir);
    h=FindFirstFileA(pattern,&fd);if(h==INVALID_HANDLE_VALUE)return;
    do{if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){
        snprintf(path,sizeof(path),"%s%s",dir,fd.cFileName);
        if(DeleteFileA(path)){removed++;}else ((void)0);
    }}while(FindNextFileA(h,&fd));FindClose(h);
    if(removed)((void)0);
}

static int Promote(const char *dir,const char *name){char live[MAX_PATH],next[MAX_PATH];snprintf(live,sizeof(live),"%s%s.grwmod.dll",dir,name);snprintf(next,sizeof(next),"%s%s.grwmod.next.dll",dir,name);if(GetFileAttributesA(next)==INVALID_FILE_ATTRIBUTES)return GetFileAttributesA(live)!=INVALID_FILE_ATTRIBUTES;return MoveFileExA(next,live,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;}
static int HasNext(const char *dir,const char *name){char next[MAX_PATH];snprintf(next,sizeof(next),"%s%s.grwmod.next.dll",dir,name);return GetFileAttributesA(next)!=INVALID_FILE_ATTRIBUTES;}
static int LoadSlot(HotSlot *s,const char *dir){char source[MAX_PATH],shadow[MAX_PATH];union{FARPROC p;ModFn f;}a,b;DWORD pid=GetCurrentProcessId();snprintf(source,sizeof(source),"%s%s.grwmod.dll",dir,s->name);s->generation++;snprintf(shadow,sizeof(shadow),"%s.%s.grwmod.loaded.%lu.%u.dll",dir,s->name,(unsigned long)pid,s->generation);if(!CopyFileA(source,shadow,FALSE)){((void)0);return 0;}s->module=LoadLibraryA(shadow);if(!s->module){((void)0);DeleteFileA(shadow);return 0;}a.p=GetProcAddress(s->module,"GrwModInit");b.p=GetProcAddress(s->module,"GrwModShutdown");s->init=a.f;s->shutdown=b.f;if(!s->init||!s->shutdown||!s->init()){((void)0);if(s->shutdown)s->shutdown();FreeLibrary(s->module);DeleteFileA(shadow);s->module=NULL;return 0;}strncpy(s->source,source,sizeof(s->source)-1);strncpy(s->shadow,shadow,sizeof(s->shadow)-1);((void)0);return 1;}
static int UnloadSlot(HotSlot *s){HMODULE m=s->module;int tries;if(!m)return 1;if(!s->shutdown||!s->shutdown()){((void)0);return 0;}s->module=NULL;s->init=NULL;s->shutdown=NULL;FreeLibrary(m);if(s->shadow[0]){for(tries=0;tries<5&&!DeleteFileA(s->shadow);tries++)Sleep(10);}s->shadow[0]=0;return 1;}

static int DiscoverAndReload(const char *only,int force){char dir[MAX_PATH],pat[MAX_PATH],name[MAX_PATH];WIN32_FIND_DATAA fd;HANDLE h;int ok=1,seen=0;GameDir(dir,sizeof(dir));snprintf(pat,sizeof(pat),"%s*.grwmod*.dll",dir);h=FindFirstFileA(pat,&fd);if(h==INVALID_HANDLE_VALUE)return only?0:1;do{if(Ends(fd.cFileName,".grwmod.next.dll")){size_t n=strlen(fd.cFileName)-strlen(".grwmod.next.dll");if(n>=sizeof(name))n=sizeof(name)-1;memcpy(name,fd.cFileName,n);name[n]=0;}else if(Ends(fd.cFileName,".grwmod.dll"))BaseName(fd.cFileName,name,sizeof(name));else continue;if(only&&_stricmp(name,only))continue;{HotSlot *s=Slot(name,1);int hasNext;if(!s){ok=0;continue;}hasNext=HasNext(dir,name);/* F9 only consumes newly published builds; an explicit one-mod reload may restart the live DLL in place. */if(s->module&&!hasNext&&!force){seen++;continue;}if(s->module&&!UnloadSlot(s)){ok=0;continue;}if(!Promote(dir,name)||!LoadSlot(s,dir))ok=0;seen++;}}while(FindNextFileA(h,&fd));FindClose(h);return seen>0&&ok;}

SH_API int ShHotReloadAll(void){int ok;if(!g_hotReady)return 0;if(InterlockedCompareExchange(&g_hotBusy,1,0))return 0;EnterCriticalSection(&g_hotLock);ok=DiscoverAndReload(NULL,0);LeaveCriticalSection(&g_hotLock);InterlockedExchange(&g_hotBusy,0);return ok;}
SH_API int ShHotReloadOne(const char *name){int ok;if(!g_hotReady||!name||!name[0]||strchr(name,'\\')||strchr(name,'/'))return 0;if(InterlockedCompareExchange(&g_hotBusy,1,0))return 0;EnterCriticalSection(&g_hotLock);ok=DiscoverAndReload(name,1);LeaveCriticalSection(&g_hotLock);InterlockedExchange(&g_hotBusy,0);return ok;}
typedef struct {char name[MAX_PATH];DWORD delay;} DeferredReload;
static DWORD WINAPI DeferredReloadThread(LPVOID p){DeferredReload*r=(DeferredReload*)p;int ok;char text[96];Sleep(r->delay);ok=ShHotReloadOne(r->name);((void)0);if(!g_hotHud)g_hotHud=ShHudCreate("scripthook-hotmods",SH_HUD_TOPLEFT,-110);snprintf(text,sizeof(text),"%s %s",r->name,ok?"RELOADED":"RELOAD FAILED");if(g_hotHud){ShHudColour(g_hotHud,ok?0x75FF9A:0xFF7070);ShHudSet(g_hotHud,text);Sleep((DWORD)g_noticeMs);ShHudSet(g_hotHud,"");}HeapFree(GetProcessHeap(),0,r);return 0;}
SH_API int ShHotReloadDeferred(const char *name,int delayMs){DeferredReload*r;HANDLE t;if(!name||!name[0]||strchr(name,'\\')||strchr(name,'/'))return 0;r=(DeferredReload*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*r));if(!r)return 0;strncpy(r->name,name,sizeof(r->name)-1);r->delay=(DWORD)(delayMs<100?100:delayMs>10000?10000:delayMs);t=CreateThread(NULL,0,DeferredReloadThread,r,0,NULL);if(!t){HeapFree(GetProcessHeap(),0,r);return 0;}((void)0);CloseHandle(t);return 1;}
SH_API int ShHotModCount(void){return g_hotCount;}
SH_API int ShHotModName(int index,char *out,int cap){if(index<0||index>=g_hotCount||!out||cap<1)return 0;strncpy(out,g_hot[index].name,(size_t)cap-1);out[cap-1]=0;return 1;}

static DWORD WINAPI HotThread(LPVOID unused){int latch=0,initialOk;(void)unused;Sleep(1000);initialOk=ShHotReloadAll();if(!g_hotHud)g_hotHud=ShHudCreate("scripthook-hotmods",SH_HUD_TOPLEFT,-110);if(g_hotHud){ShHudColour(g_hotHud,initialOk?0x75FF9A:0xFF7070);ShHudSet(g_hotHud,initialOk?"WILDLANDS MOD FRAMEWORK LOADED SUCCESSFULLY":"WILDLANDS MOD FRAMEWORK LOAD FAILED");Sleep((DWORD)g_noticeMs);ShHudSet(g_hotHud,"");}for(;;){int down=(GetAsyncKeyState(g_reloadKey)&0x8000)!=0;if(down&&!latch){int ok;latch=1;char text[96];ok=ShHotReloadAll();if(!g_hotHud)g_hotHud=ShHudCreate("scripthook-hotmods",SH_HUD_TOPLEFT,-110);snprintf(text,sizeof(text),"HOT MODS %s",ok?"RELOADED":"RELOAD FAILED");if(g_hotHud){ShHudColour(g_hotHud,ok?0x75FF9A:0xFF7070);ShHudSet(g_hotHud,text);Sleep((DWORD)g_noticeMs);ShHudSet(g_hotHud,"");}}if(!down)latch=0;Sleep(50);}return 0;}
void ShHotModStartup(void){HANDLE t;if(g_hotReady)return;CleanupStaleShadows();g_reloadKey=ShCfgKey("HotMods","ReloadKey",VK_F9);g_noticeMs=ShCfgInt("HotMods","NotificationMs",1800,250,10000);InitializeCriticalSection(&g_hotLock);g_hotReady=1;t=CreateThread(NULL,0,HotThread,NULL,0,NULL);if(t)CloseHandle(t);}

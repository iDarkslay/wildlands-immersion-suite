/* Wildlands Immersion Suite component.
 * Copyright (C) 2026 iDarkslay.
 * Modified through 2026-09-12. SPDX-License-Identifier: GPL-3.0-only.
 * See LICENSE and NOTICE.md.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#include "suite_config.h"
#define SUITE_INI SH_SUITE_CONFIG_REL
#define CFG_SECTION "BallisticsControl"

typedef void (*MenuFn)(uint32_t,uint32_t,int,void*);
typedef uint32_t (*MenuSubFn)(uint32_t,const char*);
typedef int (*MenuToggleFn)(uint32_t,const char*,int,MenuFn,void*);
typedef int (*MenuNumberFn)(uint32_t,const char*,float,float,float,float,MenuFn,void*);
typedef int (*MenuActionFn)(uint32_t,const char*,MenuFn,void*);
typedef int (*MenuStatusFn)(uint32_t,const char*);
typedef int (*MenuDescribeFn)(uint32_t,const char*,const char*);
typedef int (*MenuSetNumberFn)(uint32_t,const char*,float);
typedef int (*MenuSetToggleFn)(uint32_t,const char*,int);
typedef int (*SetMultiplierFn)(float);
typedef int (*InstallHookFn)(void);
typedef float (*GetMultiplierFn)(void);
typedef uint32_t (*GetHookCountFn)(void);

static int g_velocityEnabled=1,g_dropEnabled,g_velocityPercent=300,g_dropPercent=100;
static uint32_t g_menu;
static int g_ready;
static MenuStatusFn g_status;static MenuDescribeFn g_describe;static MenuSetNumberFn g_setNumber;static MenuSetToggleFn g_setToggle;
static SetMultiplierFn g_setVelocity,g_setDrop;static GetMultiplierFn g_getVelocity,g_getDrop;
static GetHookCountFn g_getCalls,g_getTrailCalls;
static InstallHookFn g_install;

static void WriteInt(const char*key,int value){char text[24];snprintf(text,sizeof(text),"%d",value);WritePrivateProfileStringA(CFG_SECTION,key,text,SUITE_INI);}
static void Load(void){
    g_velocityEnabled=GetPrivateProfileIntA(CFG_SECTION,"VelocityEnabled",1,SUITE_INI)?1:0;
    g_dropEnabled=GetPrivateProfileIntA(CFG_SECTION,"DropEnabled",0,SUITE_INI)?1:0;
    g_velocityPercent=GetPrivateProfileIntA(CFG_SECTION,"VelocityPercent",300,SUITE_INI);
    g_dropPercent=GetPrivateProfileIntA(CFG_SECTION,"DropPercent",100,SUITE_INI);
    if(g_velocityPercent<10)g_velocityPercent=10;if(g_velocityPercent>300)g_velocityPercent=300;
    if(g_dropPercent<0)g_dropPercent=0;if(g_dropPercent>500)g_dropPercent=500;
}
static int EnsureHooks(void){if(g_ready)return 1;if(!g_install)return 0;g_ready=g_install()?1:0;return g_ready;}
static void Apply(void){if(g_setVelocity)g_setVelocity(g_ready&&g_velocityEnabled?g_velocityPercent/100.0f:1.0f);if(g_setDrop)g_setDrop(1.0f);}
static void Update(void){char text[224];if(g_setNumber)g_setNumber(g_menu,"Global bullet velocity %",(float)g_velocityPercent);if(g_setToggle)g_setToggle(g_menu,"Enable global bullet velocity",g_velocityEnabled);if(g_status){snprintf(text,sizeof(text),"velocity/tracer hook: %s | tracer initializations: %lu | live velocity %.2fx",g_ready?"ready":"inactive",(unsigned long)(g_getTrailCalls?g_getTrailCalls():0),g_getVelocity?g_getVelocity():-1.0f);g_status(g_menu,text);}}
void ImmersiveBallisticsSaveConfig(void){WriteInt("VelocityEnabled",g_velocityEnabled);WriteInt("VelocityPercent",g_velocityPercent);WriteInt("DropEnabled",g_dropEnabled);WriteInt("DropPercent",g_dropPercent);}
static void OnVelocityEnabled(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_velocityEnabled=v?1:0;if(g_velocityEnabled&&!EnsureHooks())g_velocityEnabled=0;Apply();Update();}
static void OnVelocity(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;if(v<10)v=10;if(v>300)v=300;g_velocityPercent=v;Apply();Update();}
static void OnReset(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;g_velocityEnabled=0;g_dropEnabled=0;g_velocityPercent=100;g_dropPercent=100;Apply();Update();}
static void OnSave(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;ImmersiveBallisticsSaveConfig();Update();}

int ImmersiveBallisticsInit(void){HMODULE h=GetModuleHandleA("dinput8.dll");if(!h)return 0;*(FARPROC*)&g_setVelocity=GetProcAddress(h,"ShSetProjectileVelocityMultiplier");*(FARPROC*)&g_setDrop=GetProcAddress(h,"ShSetProjectileDropMultiplier");*(FARPROC*)&g_getVelocity=GetProcAddress(h,"ShGetProjectileVelocityMultiplier");*(FARPROC*)&g_getDrop=GetProcAddress(h,"ShGetProjectileDropMultiplier");*(FARPROC*)&g_getCalls=GetProcAddress(h,"ShGetProjectileVelocityHookCount");*(FARPROC*)&g_getTrailCalls=GetProcAddress(h,"ShGetProjectileTrailHookCount");*(FARPROC*)&g_install=GetProcAddress(h,"ShBallisticsHookInstall");Load();if(g_velocityEnabled&&!EnsureHooks())g_velocityEnabled=0;Apply();return g_ready;}
void ImmersiveBallisticsAddToSuiteMenu(uint32_t root){HMODULE h=GetModuleHandleA("dinput8.dll");MenuSubFn sub=NULL;MenuToggleFn toggle=NULL;MenuNumberFn number=NULL;MenuActionFn action=NULL;if(!h)return;*(FARPROC*)&sub=GetProcAddress(h,"ShMenuSub");*(FARPROC*)&toggle=GetProcAddress(h,"ShMenuToggle");*(FARPROC*)&number=GetProcAddress(h,"ShMenuNumber");*(FARPROC*)&action=GetProcAddress(h,"ShMenuAction");*(FARPROC*)&g_status=GetProcAddress(h,"ShMenuStatus");*(FARPROC*)&g_describe=GetProcAddress(h,"ShMenuDescribe");*(FARPROC*)&g_setNumber=GetProcAddress(h,"ShMenuSetNumber");*(FARPROC*)&g_setToggle=GetProcAddress(h,"ShMenuSetToggle");if(!sub||!toggle||!number||!action)return;g_menu=sub(root,"Ballistics Control");toggle(g_menu,"Enable global bullet velocity",g_velocityEnabled,OnVelocityEnabled,NULL);number(g_menu,"Global bullet velocity %",(float)g_velocityPercent,10,300,10,OnVelocity,NULL);action(g_menu,"Reset ballistics to vanilla",OnReset,NULL);action(g_menu,"Save ballistics configuration",OnSave,NULL);if(g_describe){g_describe(g_menu,"Enable global bullet velocity","Enable global projectile-velocity scaling for newly fired bullets, including shots fired in Native ADS.");g_describe(g_menu,"Global bullet velocity %","Set projectile velocity from 10% to 300%. 100% is the original game value. The visible tracer follows the scaled trajectory but may arrive slightly after the actual hit at high multipliers.");g_describe(g_menu,"Reset ballistics to vanilla","Disable velocity scaling and restore the requested value to the original 100% setting.");g_describe(g_menu,"Save ballistics configuration","Save the enabled state and velocity value to Fmods/immersion_suite.ini.");}Update();}
void ImmersiveBallisticsShutdown(void){g_velocityEnabled=0;g_dropEnabled=0;Apply();}

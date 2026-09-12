/* Wildlands Mod Framework / Immersion Suite fork.
 * Modified by iDarkslay through 2026-09-09; public release preparation 2026-09-09.
 * GPL-3.0; see LICENSE and NOTICE.md for upstream attribution and changes.
 */
/* First person. The camera sits at the player's eye and the
 * head is hidden, so the body and weapon stay drawn.
 */
/* Binds late by choice. Plugins may import the ScriptHook
 * directly instead, since the loader loads them from a
 * thread rather than from DllMain. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>

#include "scripthook.h"
#include "suite_config.h"

/* Optional clean-room KBM movement module, linked into this suite build. */
int ImmersiveMovementInit(void);
void ImmersiveMovementAddToSuiteMenu(uint32_t suiteRoot);
void ImmersiveMovementSaveConfig(void);
#ifdef IMMERSIVE_BALLISTICS_EMBEDDED
int ImmersiveBallisticsInit(void);
void ImmersiveBallisticsAddToSuiteMenu(uint32_t suiteRoot);
void ImmersiveBallisticsSaveConfig(void);
void ImmersiveBallisticsShutdown(void);
#endif
int ImmersiveMovementShutdown(void);

/* The mode switches on a keypress, so the walk has to keep
 * up with it. 250 left the camera held far too long. */
#define TICK_MS     60
#define MAX_PARTS   64

/* The player position is already at head height, so forward
 * is the only offset needed to clear the face.
 */
#define FWD_DEF     15.0f
#define FWD_MIN     0.0f
#define FWD_MAX     60.0f
#define FWD_STEP    5.0f
#define UP_DEF      0.0f
#define UP_MIN      -30.0f
#define UP_MAX      30.0f
#define UP_STEP     5.0f

/* How long the aim has to hold before the camera is handed
 * over, so the zoom into the body is never on screen. */
#define SETTLE_DEF  600.0f
#define SETTLE_MIN  0.0f
#define SETTLE_MAX  2000.0f
#define SETTLE_STEP 50.0f

typedef int (*IsInGame_t)(void);
typedef int (*GameState_t)(void);
typedef int (*GetPlayer_t)(ShPlayer *);
typedef int (*GetEntityTransform_t)(uint64_t,ShVec3*,float*,float*,float*);
typedef int (*QueueTransform_t)(uint64_t,const ShVec3*,float,float,float);
typedef int (*GetComponents_t)(uint64_t, ShComponent *, int);
typedef uint64_t (*FindComponent_t)(uint64_t, uint32_t);
typedef int (*IsInVehicle_t)(void);
typedef int (*GetOccupiedVehicle_t)(ShOccupiedVehicle*);
typedef int (*FirstPerson_t)(float, float);
typedef int (*FirstPersonLean_t)(float, float, float, float);
typedef int (*FirstPersonAdvanced_t)(float, float, float, float, float);
typedef int (*FirstPersonProfile_t)(float,float,float,float,float,float);
typedef int (*FirstPersonWeaponProfile_t)(float,float,float,float,float,float,float,float,float,int);
typedef void (*CameraAdsHandoff_t)(void);
typedef int (*VehicleClass_t)(void);
typedef int (*GetCamera_t)(ShCamera *);
typedef int (*CameraApply_t)(const ShCameraOverride *);
typedef int (*CameraOrbit_t)(float,float);
typedef int (*CameraOrbitAdvanced_t)(float,float,float);
typedef int (*MenuList_t)(uint32_t,const char*,const char**,int,int,ShMenuFn,void*);
typedef void (*Release_t)(uint32_t);
typedef int (*SetBlur_t)(int);
typedef int (*HeadNodes_t)(uint64_t, uint64_t *, int);
typedef void (*HeadNodesInvalidate_t)(void);
typedef int (*SetVisible_t)(uint64_t, uint64_t, int, int);
typedef uint32_t (*MenuCreate_t)(const char *);
typedef uint32_t (*MenuSub_t)(uint32_t, const char *);
typedef int (*MenuToggle_t)(uint32_t, const char *, int,
                            ShMenuFn, void *);
typedef int (*MenuNumber_t)(uint32_t, const char *, float, float,
                            float, float, ShMenuFn, void *);
typedef int (*MenuStatus_t)(uint32_t, const char *);
typedef int (*MenuAction_t)(uint32_t, const char *, ShMenuFn, void *);
typedef int (*MenuDescribe_t)(uint32_t,const char*,const char*);
typedef int (*HotReloadDeferred_t)(const char*,int);
typedef int (*CameraResetRuntime_t)(void);
typedef uint32_t (*HudCreate_t)(const char*,int,int);
typedef int (*HudSet_t)(uint32_t,const char*);
typedef int (*HudColour_t)(uint32_t,uint32_t);
typedef int (*HudDestroy_t)(uint32_t);
typedef int (*MenuDestroy_t)(uint32_t);
typedef void (*MenuOpen_t)(int);
typedef int (*SceneCount_t)(void);
typedef uint64_t (*SceneAt_t)(int);
typedef uint64_t (*SceneRoot_t)(uint64_t);
typedef int (*ChildCount_t)(uint64_t);
typedef uint64_t (*ChildAt_t)(uint64_t, int);
typedef int (*WidgetClass_t)(uint64_t, char *, int);
typedef int (*WidgetGetS_t)(uint64_t, uint32_t, char *, int);
typedef int (*SceneName_t)(uint64_t, char *, int);
typedef int (*WidgetPropType_t)(uint64_t, uint32_t);
typedef int (*SetWeather_t)(int);
typedef int (*ReleaseWeather_t)(void);
typedef int (*GetWeather_t)(int*);
typedef int (*SetTime_t)(float);
typedef int (*GetTime_t)(float*);
typedef int (*SetTimeSpeed_t)(float);
typedef int (*GetTimeSpeed_t)(float*);
typedef int (*IsSwimming_t)(void);
typedef int (*SwimmingFirstPerson_t)(int);
typedef int (*GameHudShow_t)(int);
typedef int (*SetSuperAccuracy_t)(int);
typedef void (*SetSuperAccuracyActive_t)(int);

static IsInGame_t   g_inGame;
static GameState_t  g_state;
static GetPlayer_t  g_getPlayer;
static GetEntityTransform_t g_getEntityTransform;
static QueueTransform_t g_queueTransform;
static GetComponents_t g_getComponents;
static FindComponent_t g_findComponent;
static IsInVehicle_t g_inVehicle;
static GetOccupiedVehicle_t g_getOccupiedVehicle;
static IsSwimming_t g_isSwimming;
static SwimmingFirstPerson_t g_swimmingFirstPerson;
static FirstPerson_t g_fp;
static FirstPersonLean_t g_fpLean;
static FirstPersonAdvanced_t g_fpAdvanced;
static FirstPersonProfile_t g_fpProfile;
static FirstPersonWeaponProfile_t g_fpWeaponProfile;
static CameraAdsHandoff_t g_adsHandoff;
static VehicleClass_t g_vehicleClass;
static GetCamera_t g_getCamera;
static CameraApply_t g_cameraApply;
static CameraOrbit_t g_cameraOrbit;
static CameraOrbitAdvanced_t g_cameraOrbitAdvanced;
static Release_t    g_release;
static HeadNodes_t  g_headNodes;
static HeadNodesInvalidate_t g_headNodesInvalidate;
static SetVisible_t g_setVisible;
static MenuStatus_t g_status;
static SetBlur_t    g_setBlur;
static SceneCount_t  g_sceneCount;
static SceneAt_t     g_sceneAt;
static SceneRoot_t   g_sceneRoot;
static ChildCount_t  g_childCount;
static ChildAt_t     g_childAt;
static WidgetClass_t g_widgetClass;
static WidgetGetS_t  g_widgetGetS;
static SceneName_t   g_sceneName;
static WidgetPropType_t g_widgetPropType;
static SetWeather_t g_setWeather;
static ReleaseWeather_t g_releaseWeather;
static GetWeather_t g_getWeather;
static SetTime_t g_setTime;
static GetTime_t g_getTime;
static SetTimeSpeed_t g_setTimeSpeed;
static GetTimeSpeed_t g_getTimeSpeed;

static uint32_t g_menu = 0;
static volatile int   g_on = 0;
static volatile int   g_wantHide = 1;
static volatile float g_fwd = FWD_DEF;
static volatile float g_up = UP_DEF;
static volatile int   g_settleMs = (int)SETTLE_DEF;
static volatile int   g_vehicleProfile = 1;
static volatile int   g_blurOff = 1;
static volatile int   g_fovEnabled = 0;
static volatile float g_fovDeg = 57.0f;
static volatile int g_maintainHolsteredCamera = 1;
static volatile int g_customHolsteredCamera = 0;
static volatile float g_holsteredBack=3.2f,g_holsteredUp=2.2f,g_holsteredSide=0.0f;
static float g_capturedHolsteredBack=3.2f,g_capturedHolsteredUp=2.2f,g_capturedHolsteredSide=0.0f;
static int g_capturedHolsteredValid=0;
static volatile int g_holstered = 0;
static uint64_t g_holsterCandidateHandler = 0;
static ULONGLONG g_holsterCandidateSince = 0;
static volatile float g_fovDefaultRad = 0.815f;
static volatile int g_cameraMode = 0;
static volatile int g_pendingCameraMode = 0;
static volatile float g_customBack = 4.0f,g_customHeight = 1.7f,g_customSide=0.0f;
static const char *g_cameraModeNames[]={"Default Camera (First Person)","Close shoulder","Tactical third person","Wide cinematic","Custom third person","SOCOM"};
static volatile float g_vehicleFwd = 5.0f;
static volatile float g_vehicleUp = 8.0f;
static volatile float g_vehicleSide = 0.0f;
static volatile float g_airFwd = 5.0f, g_airUp = 8.0f, g_airSide = 0.0f;
static volatile float g_waterFwd = 5.0f, g_waterUp = 8.0f, g_waterSide = 0.0f;
static volatile float g_hipFwd = 15.0f, g_hipSide = 0.0f, g_hipUp = 0.0f;
static volatile int g_weaponFineForward10=0,g_weaponFineSide10=0,g_weaponFineUp10=0;
static volatile float g_sprintLift = 15.0f;
static volatile float g_sprintBlendSpeed = 0.08f;
static volatile float g_sprintCurrentLift = 0.0f;
static volatile float g_sprintSpeedThreshold = 5.5f;
static int g_sprintDetected;
static ShVec3 g_sprintLastPos;
static ULONGLONG g_sprintLastSample;
static float g_sprintMeasuredSpeed;
static int g_sprintPositionValid;
static ULONGLONG g_sprintCandidateSince;
static int g_sprintCandidateState=-1;
static ULONGLONG g_sprintBlendLastUpdate;
static volatile float g_reloadLift = 35.0f;
static volatile float g_reloadCurrentLift = 0.0f;
static volatile float g_reloadBlendSpeed = 0.18f;
static volatile int g_reloadDetected;
static volatile float g_stabRight=1.00f,g_stabForward=1.00f,g_stabUp=1.00f;
static volatile float g_hipStabRight=1.00f,g_hipStabForward=1.00f,g_hipStabUp=1.00f;
static volatile int g_nativeAdsDetected = 0;
static volatile int g_bodyFollow = 0;
static float g_bodyFollowLastYaw;
static float g_bodyFollowLastBodyYaw;
static float g_bodyFollowOffset;
static int g_bodyFollowTracking;
static volatile LONG64 g_aimNeutralHandler = 0;
static volatile LONG64 g_aimHipHandler = 0;
static volatile LONG64 g_aimAdsHandler = 0;
static volatile int   g_rearView = 0;
static volatile int   g_toggleAltAll = 1;
static volatile int   g_gameHudVisible = 1;
static int g_hudToggleKey = VK_CAPITAL;
static int g_perspectiveToggleKey = VK_MENU;
static volatile int g_superAccuracy = 0;
static volatile int g_accuracyDefaultHip = 1;
static volatile int g_accuracyNativeAds = 1;
static volatile int g_stockViewActive = 0;
static volatile float g_side = 0.0f;
static volatile float g_headSmoothingXY = 0.0f;
static volatile float g_headSmoothingZ = 0.0f;
static volatile float g_vehicleSmoothXY = 0.0f,g_vehicleSmoothZ = 0.0f;
static volatile float g_airSmoothXY = 0.0f,g_airSmoothZ = 0.0f;
static volatile float g_waterSmoothXY = 0.0f,g_waterSmoothZ = 0.0f;
static uint32_t g_vehicleProfilesMenu;
static uint64_t g_lastOccupiedVehicle;
static int g_toggleKey = VK_NUMPAD0;
static int g_altToggleKey = '0';
static int g_autoEnable = 1;
static int g_swimFirstPerson = 0;
static uint32_t g_hud=0;
static HudSet_t g_hudSet;
static HudColour_t g_hudColour;
static HudDestroy_t g_hudDestroy;
static volatile ULONGLONG g_noticeUntil=0;
static MenuDestroy_t g_menuDestroy;
static MenuOpen_t g_menuOpen;
static HotReloadDeferred_t g_hotReloadDeferred;
static CameraResetRuntime_t g_cameraResetRuntime;
static GameHudShow_t g_gameHudShow;
static SetSuperAccuracy_t g_setSuperAccuracy;
static SetSuperAccuracyActive_t g_setSuperAccuracyActive;
static volatile LONG g_headRefreshRequested;
static volatile LONG g_stop;
static HANDLE g_bindThreadHandle;
static HANDLE g_tickThreadHandle;
static HANDLE g_bodyThreadHandle;
static HANDLE g_inputThreadHandle;
/* -1: no queued switch, 0: First Person requested, 1: Third Person requested. */
static volatile LONG g_altToggleRequest=-1;
static volatile LONG g_weatherLocked=0,g_weatherCycle=0,g_selectedWeather=0;
static volatile LONG g_weatherCycleSeconds=20,g_timeSpeed=100;
static ULONGLONG g_nextWeather;
static const char *g_weatherNames[]={"Sunny","Light clouds","Heavy clouds","Fog","Light rain","Heavy rain / storm"};
static const char *g_timeSpeedNames[]={"0x - paused","0.1x","0.5x","1x - normal","2x","5x","10x","25x","50x","100x","500x","1000x"};
static const float g_timeSpeedValues[]={0,.1f,.5f,1,2,5,10,25,50,100,500,1000};
static void ApplyBlur(void);
static void LearnFovDefault(void);
static int PushFov(void);
static void BodyFollowOff(void);
static int ApplyWeather(void);

static void Notice(const char*text,uint32_t colour){if(!g_hud||!g_hudSet)return;if(g_hudColour)g_hudColour(g_hud,colour);g_hudSet(g_hud,text);g_noticeUntil=GetTickCount64()+2200;}

static void ConfigPath(char *out, size_t cap) {
    char *slash;
    GetModuleFileNameA(NULL, out, (DWORD)cap);
    slash = strrchr(out, '\\');
    if (slash) slash[1] = 0;
    strncat(out, SH_SUITE_CONFIG_REL, cap-strlen(out)-1);
}

static int CfgInt(const char *key, int def, int lo, int hi) {
    char path[MAX_PATH], d[24], value[64]; long v; char *end;
    ConfigPath(path, sizeof(path));
    snprintf(d, sizeof(d), "%d", def);
    GetPrivateProfileStringA("UltimateFirstPerson", key, d, value, sizeof(value), path);
    v = strtol(value, &end, 0);
    if (end == value) v = def;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (int)v;
}

static int CfgSectionInt(const char *section,const char *key,int def,int lo,int hi){
    char path[MAX_PATH],d[24],value[64],*end;long v;
    ConfigPath(path,sizeof(path));snprintf(d,sizeof(d),"%d",def);
    GetPrivateProfileStringA(section,key,d,value,sizeof(value),path);
    v=strtol(value,&end,0);if(end==value)v=def;if(v<lo)v=lo;if(v>hi)v=hi;return (int)v;
}
static void PutSectionCfg(const char*section,const char*key,int value){char path[MAX_PATH],text[32];ConfigPath(path,sizeof(path));snprintf(text,sizeof(text),"%d",value);WritePrivateProfileStringA(section,key,text,path);}
static void EnsureActiveCfg(const char*key,int value){char path[MAX_PATH],text[32];ConfigPath(path,sizeof(path));GetPrivateProfileStringA("UltimateFirstPerson",key,"",text,sizeof(text),path);if(!text[0])PutSectionCfg("UltimateFirstPerson",key,value);}

static int SuiteNamedKey(const char *name) {
    struct Pair { const char *name; int vk; };
    static const struct Pair keys[] = {
        {"NONE",0},{"UP",VK_UP},{"DOWN",VK_DOWN},{"LEFT",VK_LEFT},{"RIGHT",VK_RIGHT},
        {"ENTER",VK_RETURN},{"RETURN",VK_RETURN},{"ESC",VK_ESCAPE},{"ESCAPE",VK_ESCAPE},
        {"BACKSPACE",VK_BACK},{"SPACE",VK_SPACE},{"TAB",VK_TAB},{"PAGEUP",VK_PRIOR},
        {"PAGEDOWN",VK_NEXT},{"HOME",VK_HOME},{"END",VK_END},{"INSERT",VK_INSERT},{"DELETE",VK_DELETE},
        {"ALT",VK_MENU},{"LEFTALT",VK_LMENU},{"RIGHTALT",VK_RMENU},
        {"CTRL",VK_CONTROL},{"LEFTCTRL",VK_LCONTROL},{"RIGHTCTRL",VK_RCONTROL},
        {"SHIFT",VK_SHIFT},{"LEFTSHIFT",VK_LSHIFT},{"RIGHTSHIFT",VK_RSHIFT},
        {"CAPSLOCK",VK_CAPITAL},{"NUMPAD0",VK_NUMPAD0},{"NUMPAD1",VK_NUMPAD1},
        {"NUMPAD2",VK_NUMPAD2},{"NUMPAD3",VK_NUMPAD3},{"NUMPAD4",VK_NUMPAD4},
        {"NUMPAD5",VK_NUMPAD5},{"NUMPAD6",VK_NUMPAD6},{"NUMPAD7",VK_NUMPAD7},
        {"NUMPAD8",VK_NUMPAD8},{"NUMPAD9",VK_NUMPAD9}
    };
    char upper[32]; size_t i,n;
    if(!name)return -1;n=strlen(name);if(n>=sizeof(upper))return -1;
    for(i=0;i<=n;i++)upper[i]=(char)toupper((unsigned char)name[i]);
    if(n==1&&((upper[0]>='A'&&upper[0]<='Z')||(upper[0]>='0'&&upper[0]<='9')))return upper[0];
    if(upper[0]=='F'&&n<=3){int f=atoi(upper+1);if(f>=1&&f<=24)return VK_F1+f-1;}
    for(i=0;i<sizeof(keys)/sizeof(keys[0]);i++)if(!_stricmp(upper,keys[i].name))return keys[i].vk;
    return -1;
}
static int CfgSectionKey(const char*section,const char*key,int def,int allowNone){
    char path[MAX_PATH],value[64],*end;long numeric;int vk;
    ConfigPath(path,sizeof(path));GetPrivateProfileStringA(section,key,"",value,sizeof(value),path);
    vk=SuiteNamedKey(value);if(vk>=0&&(vk||allowNone))return vk;
    numeric=strtol(value,&end,0);while(*end&&isspace((unsigned char)*end))end++;
    if(end!=value&&!*end&&numeric>=(allowNone?0:1)&&numeric<=255)return (int)numeric;
    return def;
}
static int CfgKey(const char*key,int def,int allowNone){return CfgSectionKey("UltimateFirstPerson",key,def,allowNone);}
static const char* SuiteKeyName(int vk,char out[24]){
    struct Pair { int vk; const char *name; }; static const struct Pair names[]={
        {0,"None"},{VK_MENU,"Alt"},{VK_LMENU,"LeftAlt"},{VK_RMENU,"RightAlt"},
        {VK_CONTROL,"Ctrl"},{VK_LCONTROL,"LeftCtrl"},{VK_RCONTROL,"RightCtrl"},
        {VK_SHIFT,"Shift"},{VK_LSHIFT,"LeftShift"},{VK_RSHIFT,"RightShift"},{VK_CAPITAL,"CapsLock"},
        {VK_RETURN,"Enter"},{VK_ESCAPE,"Escape"},{VK_BACK,"Backspace"},{VK_SPACE,"Space"},{VK_TAB,"Tab"}
    };size_t i;
    for(i=0;i<sizeof(names)/sizeof(names[0]);i++)if(names[i].vk==vk)return names[i].name;
    if(vk>='A'&&vk<='Z'){out[0]=(char)vk;out[1]=0;return out;}if(vk>='0'&&vk<='9'){out[0]=(char)vk;out[1]=0;return out;}
    if(vk>=VK_F1&&vk<=VK_F24){snprintf(out,24,"F%d",vk-VK_F1+1);return out;}
    if(vk>=VK_NUMPAD0&&vk<=VK_NUMPAD9){snprintf(out,24,"Numpad%d",vk-VK_NUMPAD0);return out;}
    snprintf(out,24,"%d",vk);return out;
}
static void PutSectionKeyCfg(const char*section,const char*key,int value){char path[MAX_PATH],text[24];ConfigPath(path,sizeof(path));WritePrivateProfileStringA(section,key,SuiteKeyName(value,text),path);}
static void PutKeyCfg(const char*key,int value){PutSectionKeyCfg("UltimateFirstPerson",key,value);}
static void EnsureActiveKeyCfg(const char*key,int value,int allowNone){char path[MAX_PATH],text[64];int parsed;ConfigPath(path,sizeof(path));GetPrivateProfileStringA("UltimateFirstPerson",key,"",text,sizeof(text),path);parsed=CfgKey(key,value,allowNone);if(!text[0]||SuiteNamedKey(text)<0)PutKeyCfg(key,parsed);}

/* This section is intentionally separate from the active profile. It is a
 * readable, editable factory-profile template and is never overwritten by
 * the normal Save actions. */
static void EnsureModDefaults(void){
#define DEF(k,v) do{char p[MAX_PATH],x[8];ConfigPath(p,sizeof(p));GetPrivateProfileStringA("ModDefaults",k,"",x,sizeof(x),p);if(!x[0])PutSectionCfg("ModDefaults",k,v);}while(0)
    DEF("AutoEnable",1);DEF("CameraMode",1);DEF("ThirdPersonCustomDistanceCm",400);DEF("ThirdPersonCustomHeightCm",170);DEF("ThirdPersonCustomSideCm",0);
    DEF("DisableCameraBlur",1);DEF("FirstPersonWhileSwimming",0);DEF("HideHead",1);
    DEF("VerticalFovDegrees",57);
    DEF("OnFootForwardCm",15);DEF("OnFootHeightCm",0);DEF("OnFootSideCm",0);DEF("HipAimForwardCm",15);DEF("HipAimSideCm",0);DEF("HipAimHeightCm",0);
    DEF("WeaponAlignmentForwardTenthCm",0);DEF("WeaponAlignmentSideTenthCm",0);DEF("WeaponAlignmentHeightTenthCm",0);
    DEF("SprintCameraLiftCm",15);DEF("SprintTransitionPercent",8);DEF("SprintSpeedThresholdTenths",55);DEF("ReloadCameraLiftCm",35);DEF("ReloadTransitionPercent",18);
    DEF("NeutralStabilizeRightPercent",100);DEF("NeutralStabilizeForwardPercent",100);DEF("NeutralStabilizeUpPercent",100);DEF("HipStabilizeRightPercent",100);DEF("HipStabilizeForwardPercent",100);DEF("HipStabilizeUpPercent",100);
    DEF("BodyFollowsCameraDefault",1);DEF("AdsSettleMs",600);DEF("HeadSmoothingXYPercent",0);DEF("HeadSmoothingZPercent",0);
    DEF("VehicleProfile",1);DEF("VehicleForwardCm",5);DEF("VehicleHeightCm",8);DEF("VehicleSideCm",0);DEF("VehicleSmoothingXYPercent",0);DEF("VehicleSmoothingZPercent",0);
    DEF("WaterForwardCm",5);DEF("WaterHeightCm",8);DEF("WaterSideCm",0);DEF("WaterSmoothingXYPercent",0);DEF("WaterSmoothingZPercent",0);
    DEF("AirForwardCm",5);DEF("AirHeightCm",8);DEF("AirSideCm",0);DEF("AirSmoothingXYPercent",0);DEF("AirSmoothingZPercent",0);
    DEF("MaintainCameraWhenHolstered",0);DEF("UseCustomHolsteredCamera",0);DEF("HolsteredDistanceCm",320);DEF("HolsteredHeightCm",220);DEF("HolsteredSideCm",0);
    DEF("HoldAltRearView",1);DEF("ToggleAltStockViewEverywhere",1);
    DEF("GameHudVisible",1);
    DEF("SuperAccuracy",1);
    DEF("SuperAccuracyDefaultHip",1);DEF("SuperAccuracyNativeAds",1);
    PutSectionCfg("ModDefaults","SuperAccuracyDefaultHip",1);
#undef DEF
    {char p[MAX_PATH],x[64];int v;ConfigPath(p,sizeof(p));
#define DEFKEY(k,d,n) do{GetPrivateProfileStringA("ModDefaults",k,"",x,sizeof(x),p);v=CfgSectionKey("ModDefaults",k,d,n);if(!x[0]||SuiteNamedKey(x)<0)PutSectionKeyCfg("ModDefaults",k,v);}while(0)
    DEFKEY("ToggleKey",VK_NUMPAD0,0);DEFKEY("AlternateToggleKey",'0',1);DEFKEY("HudToggleKey",VK_CAPITAL,0);DEFKEY("PerspectiveToggleKey",VK_LMENU,0);
#undef DEFKEY
    }
    /* Keep the user-facing controls discoverable in the active INI as well.
     * Existing custom values are never overwritten. */
    EnsureActiveKeyCfg("ToggleKey",VK_NUMPAD0,0);EnsureActiveKeyCfg("AlternateToggleKey",'0',1);
    EnsureActiveKeyCfg("PerspectiveToggleKey",VK_LMENU,0);EnsureActiveKeyCfg("HudToggleKey",VK_CAPITAL,0);
}

static void LoadModDefaults(void){
#define D(k,v,lo,hi) CfgSectionInt("ModDefaults",k,v,lo,hi)
    g_autoEnable=D("AutoEnable",1,0,1);g_cameraMode=D("CameraMode",1,0,6);if(g_cameraMode==0)g_cameraMode=1;g_pendingCameraMode=g_cameraMode;
    g_customBack=(float)D("ThirdPersonCustomDistanceCm",400,50,1500)/100.0f;g_customHeight=(float)D("ThirdPersonCustomHeightCm",170,-200,800)/100.0f;g_customSide=(float)D("ThirdPersonCustomSideCm",0,-500,500)/100.0f;
    g_toggleKey=CfgSectionKey("ModDefaults","ToggleKey",VK_NUMPAD0,0);g_altToggleKey=CfgSectionKey("ModDefaults","AlternateToggleKey",'0',1);g_blurOff=D("DisableCameraBlur",1,0,1);g_swimFirstPerson=D("FirstPersonWhileSwimming",0,0,1);g_wantHide=D("HideHead",1,0,1);
    g_fovDeg=(float)D("VerticalFovDegrees",57,30,140);
    g_fwd=(float)D("OnFootForwardCm",15,0,60);g_up=(float)D("OnFootHeightCm",0,-30,30);g_side=(float)D("OnFootSideCm",0,-40,40);g_hipFwd=(float)D("HipAimForwardCm",15,0,60);g_hipSide=(float)D("HipAimSideCm",0,-40,40);g_hipUp=(float)D("HipAimHeightCm",0,-30,30);
    g_weaponFineForward10=D("WeaponAlignmentForwardTenthCm",0,-300,300);g_weaponFineSide10=D("WeaponAlignmentSideTenthCm",0,-300,300);g_weaponFineUp10=D("WeaponAlignmentHeightTenthCm",0,-300,300);
    g_sprintLift=(float)D("SprintCameraLiftCm",15,0,80);g_sprintBlendSpeed=(float)D("SprintTransitionPercent",8,1,100)/100.0f;g_sprintSpeedThreshold=(float)D("SprintSpeedThresholdTenths",55,10,120)/10.0f;g_reloadLift=(float)D("ReloadCameraLiftCm",35,0,80);g_reloadBlendSpeed=(float)D("ReloadTransitionPercent",18,1,100)/100.0f;
    g_stabRight=(float)D("NeutralStabilizeRightPercent",100,0,100)/100.0f;g_stabForward=(float)D("NeutralStabilizeForwardPercent",100,0,100)/100.0f;g_stabUp=(float)D("NeutralStabilizeUpPercent",100,0,100)/100.0f;g_hipStabRight=(float)D("HipStabilizeRightPercent",100,0,100)/100.0f;g_hipStabForward=(float)D("HipStabilizeForwardPercent",100,0,100)/100.0f;g_hipStabUp=(float)D("HipStabilizeUpPercent",100,0,100)/100.0f;
    g_bodyFollow=D("BodyFollowsCameraDefault",1,0,1);g_settleMs=D("AdsSettleMs",600,0,2000);g_headSmoothingXY=(float)D("HeadSmoothingXYPercent",0,0,95)/100.0f;g_headSmoothingZ=(float)D("HeadSmoothingZPercent",0,0,95)/100.0f;
    g_vehicleProfile=D("VehicleProfile",1,0,1);g_vehicleFwd=(float)D("VehicleForwardCm",5,-60,60);g_vehicleUp=(float)D("VehicleHeightCm",8,-60,60);g_vehicleSide=(float)D("VehicleSideCm",0,-60,60);g_vehicleSmoothXY=(float)D("VehicleSmoothingXYPercent",0,0,95)/100.0f;g_vehicleSmoothZ=(float)D("VehicleSmoothingZPercent",0,0,95)/100.0f;
    g_waterFwd=(float)D("WaterForwardCm",5,-60,60);g_waterUp=(float)D("WaterHeightCm",8,-60,60);g_waterSide=(float)D("WaterSideCm",0,-60,60);g_waterSmoothXY=(float)D("WaterSmoothingXYPercent",0,0,95)/100.0f;g_waterSmoothZ=(float)D("WaterSmoothingZPercent",0,0,95)/100.0f;
    g_airFwd=(float)D("AirForwardCm",5,-60,60);g_airUp=(float)D("AirHeightCm",8,-60,60);g_airSide=(float)D("AirSideCm",0,-60,60);g_airSmoothXY=(float)D("AirSmoothingXYPercent",0,0,95)/100.0f;g_airSmoothZ=(float)D("AirSmoothingZPercent",0,0,95)/100.0f;
    g_maintainHolsteredCamera=D("MaintainCameraWhenHolstered",0,0,1);g_customHolsteredCamera=D("UseCustomHolsteredCamera",0,0,1);g_holsteredBack=(float)D("HolsteredDistanceCm",320,50,1500)/100.0f;g_holsteredUp=(float)D("HolsteredHeightCm",220,-200,800)/100.0f;g_holsteredSide=(float)D("HolsteredSideCm",0,-500,500)/100.0f;
    g_rearView=0;g_toggleAltAll=D("ToggleAltStockViewEverywhere",1,0,1);
    g_gameHudVisible=D("GameHudVisible",1,0,1);g_hudToggleKey=CfgSectionKey("ModDefaults","HudToggleKey",VK_CAPITAL,0);g_perspectiveToggleKey=CfgSectionKey("ModDefaults","PerspectiveToggleKey",VK_LMENU,0);
    g_superAccuracy=D("SuperAccuracy",1,0,1);
    g_accuracyDefaultHip=D("SuperAccuracyDefaultHip",1,0,1);g_accuracyNativeAds=D("SuperAccuracyNativeAds",1,0,1);
#undef D
}

static void LoadConfig(void) {
    g_autoEnable = CfgInt("AutoEnable",1,0,1);
    g_toggleKey = CfgKey("ToggleKey",VK_NUMPAD0,0);
    g_altToggleKey = CfgKey("AlternateToggleKey",'0',1);
    g_fwd = (float)CfgInt("OnFootForwardCm",15,0,60);
    g_up = (float)CfgInt("OnFootHeightCm",0,-30,30);
    g_side = (float)CfgInt("OnFootSideCm",0,-40,40);
    g_vehicleFwd = (float)CfgInt("VehicleForwardCm",5,-60,60);
    g_vehicleUp = (float)CfgInt("VehicleHeightCm",8,-60,60);
    g_vehicleSide = (float)CfgInt("VehicleSideCm",0,-60,60);
    g_waterFwd = (float)CfgInt("WaterForwardCm",5,-60,60);
    g_waterUp = (float)CfgInt("WaterHeightCm",8,-60,60);
    g_waterSide = (float)CfgInt("WaterSideCm",0,-60,60);
    g_airFwd = (float)CfgInt("AirForwardCm",5,-60,60);
    g_airUp = (float)CfgInt("AirHeightCm",8,-60,60);
    g_airSide = (float)CfgInt("AirSideCm",0,-60,60);
    g_hipFwd = (float)CfgInt("HipAimForwardCm",15,0,60);
    g_hipSide = (float)CfgInt("HipAimSideCm",0,-40,40);
    g_hipUp = (float)CfgInt("HipAimHeightCm",0,-30,30);
    g_weaponFineForward10=CfgInt("WeaponAlignmentForwardTenthCm",0,-300,300);
    g_weaponFineSide10=CfgInt("WeaponAlignmentSideTenthCm",0,-300,300);
    g_weaponFineUp10=CfgInt("WeaponAlignmentHeightTenthCm",0,-300,300);
    g_sprintLift=(float)CfgInt("SprintCameraLiftCm",15,0,80);
    g_sprintBlendSpeed=(float)CfgInt("SprintTransitionPercent",8,1,100)/100.0f;
    g_sprintSpeedThreshold=(float)CfgInt("SprintSpeedThresholdTenths",55,10,120)/10.0f;
    g_reloadLift=(float)CfgInt("ReloadCameraLiftCm",35,0,80);
    g_reloadBlendSpeed=(float)CfgInt("ReloadTransitionPercent",18,1,100)/100.0f;
    g_stabRight=(float)CfgInt("NeutralStabilizeRightPercent",100,0,100)/100.0f;
    g_stabForward=(float)CfgInt("NeutralStabilizeForwardPercent",100,0,100)/100.0f;
    g_stabUp=(float)CfgInt("NeutralStabilizeUpPercent",100,0,100)/100.0f;
    g_hipStabRight=(float)CfgInt("HipStabilizeRightPercent",100,0,100)/100.0f;
    g_hipStabForward=(float)CfgInt("HipStabilizeForwardPercent",100,0,100)/100.0f;
    g_hipStabUp=(float)CfgInt("HipStabilizeUpPercent",100,0,100)/100.0f;
    g_bodyFollow=CfgInt("BodyFollowsCameraDefault",1,0,1);
    g_settleMs = CfgInt("AdsSettleMs",600,0,2000);
    g_wantHide = CfgInt("HideHead",1,0,1);
    g_vehicleProfile = CfgInt("VehicleProfile",1,0,1);
    g_blurOff = CfgInt("DisableCameraBlur",1,0,1);
    g_swimFirstPerson = CfgInt("FirstPersonWhileSwimming",0,0,1);
    g_fovEnabled = 1;
    g_fovDeg = (float)CfgInt("VerticalFovDegrees",57,30,140);
    g_maintainHolsteredCamera = CfgInt("MaintainCameraWhenHolstered",0,0,1);
    g_customHolsteredCamera = CfgInt("UseCustomHolsteredCamera",0,0,1);
    g_holsteredBack=(float)CfgInt("HolsteredDistanceCm",320,50,1500)/100.0f;
    g_holsteredUp=(float)CfgInt("HolsteredHeightCm",220,-200,800)/100.0f;
    g_holsteredSide=(float)CfgInt("HolsteredSideCm",0,-500,500)/100.0f;
    g_cameraMode = CfgInt("CameraMode",1,0,6);
    /* Camera mode 0 used to be the selectable Stock Camera. It is now reserved
       for emergency release only, so migrate old saved profiles to the mod's
       primary First Person camera. */
    if(g_cameraMode==0)g_cameraMode=1;
    g_customBack = (float)CfgInt("ThirdPersonCustomDistanceCm",400,50,1500)/100.0f;
    g_customHeight = (float)CfgInt("ThirdPersonCustomHeightCm",170,-200,800)/100.0f;
    g_customSide = (float)CfgInt("ThirdPersonCustomSideCm",0,-500,500)/100.0f;
    g_rearView = 0;
    g_toggleAltAll = CfgInt("ToggleAltStockViewEverywhere",1,0,1);
    g_gameHudVisible = CfgInt("GameHudVisible",1,0,1);
    g_hudToggleKey = CfgKey("HudToggleKey",VK_CAPITAL,0);
    g_perspectiveToggleKey = CfgKey("PerspectiveToggleKey",VK_LMENU,0);
    g_superAccuracy = CfgInt("SuperAccuracy",1,0,1);
    g_accuracyDefaultHip=CfgInt("SuperAccuracyDefaultHip",1,0,1);
    g_accuracyNativeAds=CfgInt("SuperAccuracyNativeAds",1,0,1);
    /* Weather and time are session controls and deliberately never restored
     * from the persistent camera profile. */
    g_selectedWeather=0;g_weatherLocked=0;
    g_weatherCycle=0;g_weatherCycleSeconds=20;g_timeSpeed=100;
    g_headSmoothingXY = (float)CfgInt("HeadSmoothingXYPercent",0,0,95)/100.0f;
    g_headSmoothingZ = (float)CfgInt("HeadSmoothingZPercent",0,0,95)/100.0f;
    g_vehicleSmoothXY=(float)CfgInt("VehicleSmoothingXYPercent",0,0,95)/100.0f;
    g_vehicleSmoothZ=(float)CfgInt("VehicleSmoothingZPercent",0,0,95)/100.0f;
    g_waterSmoothXY=(float)CfgInt("WaterSmoothingXYPercent",0,0,95)/100.0f;
    g_waterSmoothZ=(float)CfgInt("WaterSmoothingZPercent",0,0,95)/100.0f;
    g_airSmoothXY=(float)CfgInt("AirSmoothingXYPercent",0,0,95)/100.0f;
    g_airSmoothZ=(float)CfgInt("AirSmoothingZPercent",0,0,95)/100.0f;
}

static void PutCfg(const char*key,int value){char path[MAX_PATH],text[32];ConfigPath(path,sizeof(path));snprintf(text,sizeof(text),"%d",value);WritePrivateProfileStringA("UltimateFirstPerson",key,text,path);}
static void SaveConfig(void){
    PutCfg("AutoEnable",g_autoEnable);PutCfg("CameraMode",g_cameraMode);
    PutCfg("ThirdPersonCustomDistanceCm",(int)(g_customBack*100));PutCfg("ThirdPersonCustomHeightCm",(int)(g_customHeight*100));PutCfg("ThirdPersonCustomSideCm",(int)(g_customSide*100));
    PutKeyCfg("ToggleKey",g_toggleKey);PutKeyCfg("AlternateToggleKey",g_altToggleKey);PutCfg("DisableCameraBlur",g_blurOff);PutCfg("FirstPersonWhileSwimming",g_swimFirstPerson);
    PutCfg("VerticalFovDegrees",(int)g_fovDeg);PutCfg("MaintainCameraWhenHolstered",g_maintainHolsteredCamera);PutCfg("UseCustomHolsteredCamera",g_customHolsteredCamera);PutCfg("HolsteredDistanceCm",(int)(g_holsteredBack*100));PutCfg("HolsteredHeightCm",(int)(g_holsteredUp*100));PutCfg("HolsteredSideCm",(int)(g_holsteredSide*100));PutCfg("HideHead",g_wantHide);
    PutCfg("OnFootForwardCm",(int)g_fwd);PutCfg("OnFootHeightCm",(int)g_up);PutCfg("OnFootSideCm",(int)g_side);
    PutCfg("HipAimForwardCm",(int)g_hipFwd);PutCfg("HipAimSideCm",(int)g_hipSide);PutCfg("HipAimHeightCm",(int)g_hipUp);
    PutCfg("SprintCameraLiftCm",(int)g_sprintLift);PutCfg("SprintTransitionPercent",(int)(g_sprintBlendSpeed*100.0f));PutCfg("SprintSpeedThresholdTenths",(int)(g_sprintSpeedThreshold*10.0f));
    PutCfg("ReloadCameraLiftCm",(int)g_reloadLift);PutCfg("ReloadTransitionPercent",(int)(g_reloadBlendSpeed*100.0f));PutCfg("BodyFollowsCameraDefault",g_bodyFollow);
    PutCfg("AdsSettleMs",g_settleMs);PutCfg("HeadSmoothingXYPercent",(int)(g_headSmoothingXY*100.0f));PutCfg("HeadSmoothingZPercent",(int)(g_headSmoothingZ*100.0f));
    PutCfg("VehicleProfile",g_vehicleProfile);PutCfg("VehicleForwardCm",(int)g_vehicleFwd);PutCfg("VehicleHeightCm",(int)g_vehicleUp);PutCfg("VehicleSideCm",(int)g_vehicleSide);
    PutCfg("VehicleSmoothingXYPercent",(int)(g_vehicleSmoothXY*100));PutCfg("VehicleSmoothingZPercent",(int)(g_vehicleSmoothZ*100));
    PutCfg("WaterForwardCm",(int)g_waterFwd);PutCfg("WaterHeightCm",(int)g_waterUp);PutCfg("WaterSideCm",(int)g_waterSide);PutCfg("WaterSmoothingXYPercent",(int)(g_waterSmoothXY*100));PutCfg("WaterSmoothingZPercent",(int)(g_waterSmoothZ*100));
    PutCfg("AirForwardCm",(int)g_airFwd);PutCfg("AirHeightCm",(int)g_airUp);PutCfg("AirSideCm",(int)g_airSide);PutCfg("AirSmoothingXYPercent",(int)(g_airSmoothXY*100));PutCfg("AirSmoothingZPercent",(int)(g_airSmoothZ*100));
    PutCfg("ToggleAltStockViewEverywhere",g_toggleAltAll);
    PutCfg("GameHudVisible",g_gameHudVisible);PutKeyCfg("HudToggleKey",g_hudToggleKey);PutKeyCfg("PerspectiveToggleKey",g_perspectiveToggleKey);
    PutCfg("SuperAccuracy",g_superAccuracy);
    PutCfg("SuperAccuracyDefaultHip",g_accuracyDefaultHip);PutCfg("SuperAccuracyNativeAds",g_accuracyNativeAds);
}
static void SaveStabilizationConfig(void){PutCfg("NeutralStabilizeRightPercent",(int)(g_stabRight*100));PutCfg("NeutralStabilizeForwardPercent",(int)(g_stabForward*100));PutCfg("NeutralStabilizeUpPercent",(int)(g_stabUp*100));PutCfg("HipStabilizeRightPercent",(int)(g_hipStabRight*100));PutCfg("HipStabilizeForwardPercent",(int)(g_hipStabForward*100));PutCfg("HipStabilizeUpPercent",(int)(g_hipStabUp*100));}
static void SaveEnvironmentConfig(void){PutCfg("WeatherType",g_selectedWeather);PutCfg("WeatherLocked",g_weatherLocked);PutCfg("WeatherCycleEnabled",g_weatherCycle);PutCfg("WeatherCycleSeconds",g_weatherCycleSeconds);PutCfg("TimeSpeedPercent",g_timeSpeed);}
static void SaveFovConfig(void){PutCfg("VerticalFovDegrees",(int)g_fovDeg);PutCfg("AdsSettleMs",g_settleMs);}
static void SaveFineConfig(void){PutCfg("OnFootForwardCm",(int)g_fwd);PutCfg("OnFootHeightCm",(int)g_up);PutCfg("OnFootSideCm",(int)g_side);PutCfg("HipAimForwardCm",(int)g_hipFwd);PutCfg("HipAimSideCm",(int)g_hipSide);PutCfg("HipAimHeightCm",(int)g_hipUp);PutCfg("SprintCameraLiftCm",(int)g_sprintLift);PutCfg("SprintTransitionPercent",(int)(g_sprintBlendSpeed*100.0f));PutCfg("SprintSpeedThresholdTenths",(int)(g_sprintSpeedThreshold*10.0f));PutCfg("ReloadCameraLiftCm",(int)g_reloadLift);PutCfg("ReloadTransitionPercent",(int)(g_reloadBlendSpeed*100.0f));PutCfg("BodyFollowsCameraDefault",g_bodyFollow);PutCfg("VehicleProfile",g_vehicleProfile);PutCfg("VehicleForwardCm",(int)g_vehicleFwd);PutCfg("VehicleHeightCm",(int)g_vehicleUp);PutCfg("VehicleSideCm",(int)g_vehicleSide);PutCfg("VehicleSmoothingXYPercent",(int)(g_vehicleSmoothXY*100));PutCfg("VehicleSmoothingZPercent",(int)(g_vehicleSmoothZ*100));PutCfg("WaterForwardCm",(int)g_waterFwd);PutCfg("WaterHeightCm",(int)g_waterUp);PutCfg("WaterSideCm",(int)g_waterSide);PutCfg("WaterSmoothingXYPercent",(int)(g_waterSmoothXY*100));PutCfg("WaterSmoothingZPercent",(int)(g_waterSmoothZ*100));PutCfg("AirForwardCm",(int)g_airFwd);PutCfg("AirHeightCm",(int)g_airUp);PutCfg("AirSideCm",(int)g_airSide);PutCfg("AirSmoothingXYPercent",(int)(g_airSmoothXY*100));PutCfg("AirSmoothingZPercent",(int)(g_airSmoothZ*100));PutCfg("MaintainCameraWhenHolstered",g_maintainHolsteredCamera);PutCfg("UseCustomHolsteredCamera",g_customHolsteredCamera);PutCfg("HolsteredDistanceCm",(int)(g_holsteredBack*100));PutCfg("HolsteredHeightCm",(int)(g_holsteredUp*100));PutCfg("HolsteredSideCm",(int)(g_holsteredSide*100));SaveStabilizationConfig();}
static void OnSaveGroundVehicleConfig(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;PutCfg("VehicleProfile",g_vehicleProfile);PutCfg("VehicleForwardCm",(int)g_vehicleFwd);PutCfg("VehicleHeightCm",(int)g_vehicleUp);PutCfg("VehicleSideCm",(int)g_vehicleSide);PutCfg("VehicleSmoothingXYPercent",(int)(g_vehicleSmoothXY*100));PutCfg("VehicleSmoothingZPercent",(int)(g_vehicleSmoothZ*100));Notice("IMMERSION SUITE | GROUND VEHICLE PROFILE SAVED",0x75FF9A);}
static void OnSaveWaterVehicleConfig(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;PutCfg("VehicleProfile",g_vehicleProfile);PutCfg("WaterForwardCm",(int)g_waterFwd);PutCfg("WaterHeightCm",(int)g_waterUp);PutCfg("WaterSideCm",(int)g_waterSide);PutCfg("WaterSmoothingXYPercent",(int)(g_waterSmoothXY*100));PutCfg("WaterSmoothingZPercent",(int)(g_waterSmoothZ*100));Notice("IMMERSION SUITE | WATER VEHICLE PROFILE SAVED",0x75FF9A);}
static void OnSaveAirVehicleConfig(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;PutCfg("VehicleProfile",g_vehicleProfile);PutCfg("AirForwardCm",(int)g_airFwd);PutCfg("AirHeightCm",(int)g_airUp);PutCfg("AirSideCm",(int)g_airSide);PutCfg("AirSmoothingXYPercent",(int)(g_airSmoothXY*100));PutCfg("AirSmoothingZPercent",(int)(g_airSmoothZ*100));Notice("IMMERSION SUITE | AIR VEHICLE PROFILE SAVED",0x75FF9A);}
static void OnSaveConfig(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;SaveConfig();SaveStabilizationConfig();ImmersiveMovementSaveConfig();
#ifdef IMMERSIVE_BALLISTICS_EMBEDDED
ImmersiveBallisticsSaveConfig();
#endif
if(g_status)g_status(g_menu,"All persistent settings saved to Fmods/immersion_suite.ini (weather and time excluded)");Notice("IMMERSION SUITE | ALL SETTINGS SAVED",0x75FF9A);}
static void OnSaveCameraModeConfig(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;PutCfg("CameraMode",g_cameraMode);PutCfg("ThirdPersonCustomDistanceCm",(int)(g_customBack*100));PutCfg("ThirdPersonCustomHeightCm",(int)(g_customHeight*100));PutCfg("ThirdPersonCustomSideCm",(int)(g_customSide*100));Notice("IMMERSION SUITE | CAMERA MODE SETTINGS SAVED",0x75FF9A);}
static void OnSaveGeneralConfig(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;PutCfg("AutoEnable",g_autoEnable);PutCfg("DisableCameraBlur",g_blurOff);PutCfg("FirstPersonWhileSwimming",g_swimFirstPerson);PutCfg("HideHead",g_wantHide);PutCfg("ToggleAltStockViewEverywhere",g_toggleAltAll);PutCfg("GameHudVisible",g_gameHudVisible);PutKeyCfg("HudToggleKey",g_hudToggleKey);PutKeyCfg("PerspectiveToggleKey",g_perspectiveToggleKey);Notice("IMMERSION SUITE | GENERAL SETTINGS SAVED",0x75FF9A);}
static void OnGameHudVisible(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_gameHudVisible=v?1:0;if(g_gameHudShow)g_gameHudShow(g_gameHudVisible);PutCfg("GameHudVisible",g_gameHudVisible);}
static void OnSuperAccuracy(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;if(g_setSuperAccuracy&&g_setSuperAccuracy(v?1:0)){g_superAccuracy=v?1:0;PutCfg("SuperAccuracy",g_superAccuracy);if(g_status)g_status(g_menu,g_superAccuracy?"Super Accuracy enabled for the player":"Super Accuracy disabled and original code restored");}else if(g_status)g_status(g_menu,"Super Accuracy unavailable: executable bytes/build did not match");}
static void OnAccuracyDefaultHip(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_accuracyDefaultHip=v?1:0;}
static void OnAccuracyNativeAds(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_accuracyNativeAds=v?1:0;}
static void OnSaveWeaponConfig(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;PutCfg("SuperAccuracy",g_superAccuracy);PutCfg("SuperAccuracyDefaultHip",g_accuracyDefaultHip);PutCfg("SuperAccuracyNativeAds",g_accuracyNativeAds);Notice("IMMERSION SUITE | WEAPON SETTINGS SAVED",0x75FF9A);}
static void OnSaveFineConfig(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;SaveFineConfig();Notice("IMMERSION SUITE | CAMERA TUNING SAVED",0x75FF9A);}
static void OnSaveFovConfig(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;SaveFovConfig();Notice("IMMERSION SUITE | FOV SETTINGS SAVED",0x75FF9A);}

/* Counted so the status line can say where the walk got
 * to, rather than only whether it matched. */
static volatile int g_nScenes, g_nWidgets, g_nLabels, g_nText;
static volatile int g_nSights;

static int Aiming(void);
static int Playing(void);

static uint64_t g_root = 0;
static uint64_t g_hideRoot = 0;
static uint64_t g_parts[MAX_PARTS];
static int      g_nparts = 0;

static uint64_t PlayerRoot(void) {
    ShPlayer p;

    memset(&p, 0, sizeof(p));
    if (!g_getPlayer || !g_getPlayer(&p)) return 0;
    /* Visibility components belong to the streamed player entity.  The root
     * may survive a world/map transition and then points at the old body. */
    return p.entity ? p.entity : p.root;
}

/* The eye follows the head bone inside the engine's own
 * frame, so nothing here runs per frame.
 */
static void PushCamera(void) {
    int vehicle = g_inVehicle && g_inVehicle();
    int cls=SH_VEHICLE_NONE;
    ShOccupiedVehicle occupied;
    int sprintLift=!vehicle&&g_sprintCurrentLift>0.05f;
    float fwd=g_fwd,up=g_up,side=g_side,sxy=g_headSmoothingXY,sz=g_headSmoothingZ;
    float sr=g_stabRight,sf=g_stabForward,su=g_stabUp;
    ZeroMemory(&occupied,sizeof(occupied));
    if(vehicle){
        if(g_getOccupiedVehicle&&g_getOccupiedVehicle(&occupied))cls=occupied.vehicleClass;
        if(cls==SH_VEHICLE_UNKNOWN||cls==SH_VEHICLE_NONE)
            cls=(g_vehicleClass&&g_vehicleClass()==2)?SH_VEHICLE_AIR:SH_VEHICLE_GROUND;
        if(occupied.entity!=g_lastOccupiedVehicle){
            char status[192];const char*group=cls==SH_VEHICLE_AIR?"AIR":cls==SH_VEHICLE_WATER?"WATER":"GROUND";
            g_lastOccupiedVehicle=occupied.entity;
            snprintf(status,sizeof(status),"Detected: %s | %s | ID 0x%08X | %s",group,occupied.identified?"catalogue match":"fallback",occupied.id,occupied.name[0]?occupied.name:"Unknown vehicle");
            if(g_status&&g_vehicleProfilesMenu)g_status(g_vehicleProfilesMenu,status);
        }
    }else if(g_lastOccupiedVehicle){
        g_lastOccupiedVehicle=0;
        if(g_status&&g_vehicleProfilesMenu)g_status(g_vehicleProfilesMenu,"On foot | vehicle profiles idle");
    }
    /* One authoritative on-foot position.  Hip aim no longer switches to a
     * second, detector-dependent set of offsets. */
    if(!vehicle&&(g_nativeAdsDetected==0||sprintLift))
        up+=g_sprintCurrentLift+(g_nativeAdsDetected==0?g_reloadCurrentLift:0.0f);
    /* The learned weapon-height correction otherwise cancels any temporary
     * camera lift. During sprint the animation may move freely vertically;
     * horizontal/depth stabilization and all non-sprint states stay intact. */
    if(!vehicle&&g_nativeAdsDetected==0&&g_reloadCurrentLift>0.05f)su=0.0f;
    /* Sprint only: weapon stabilization corrects the eye along camera-local
     * right/forward/up axes. At steep pitch that correction can pull the eye
     * backwards into the body. Keep the already working ordinary/hip/reload
     * paths untouched and suspend all three corrections only while lifted. */
    /* Keep stabilization suspended for the complete lift envelope, including
     * the blend-out after Shift is released. Re-enabling it as soon as the
     * logical sprint state ended caused a one-frame position snap while the
     * camera was still descending. */
    if(sprintLift)
        sr=sf=su=0.0f;
    if(vehicle&&g_vehicleProfile){if(cls==SH_VEHICLE_AIR){fwd=g_airFwd;up=g_airUp;side=g_airSide;sxy=g_airSmoothXY;sz=g_airSmoothZ;}else if(cls==SH_VEHICLE_WATER){fwd=g_waterFwd;up=g_waterUp;side=g_waterSide;sxy=g_waterSmoothXY;sz=g_waterSmoothZ;}else{fwd=g_vehicleFwd;up=g_vehicleUp;side=g_vehicleSide;sxy=g_vehicleSmoothXY;sz=g_vehicleSmoothZ;}}
    if(g_fpWeaponProfile&&!vehicle)g_fpWeaponProfile(fwd/100.0f,side/100.0f,up/100.0f,0.0f,sxy,sz,sr,sf,su,sprintLift?0:g_nativeAdsDetected==1);
    else if(g_fpProfile)g_fpProfile(fwd/100.0f,side/100.0f,up/100.0f,0.0f,sxy,sz);
    else if(g_fpAdvanced)g_fpAdvanced(fwd/100.0f,side/100.0f,
                         up/100.0f,0.0f,(sxy+sz)*0.5f);
    else if(g_fpLean)g_fpLean(fwd/100.0f,side/100.0f,up/100.0f,0.0f);
    else if (g_fp) g_fp(fwd / 100.0f, up / 100.0f);
}

static uint64_t PlayerVisualNode(void) {
    ShPlayer p;
    memset(&p, 0, sizeof(p));
    if (!g_getPlayer || !g_getPlayer(&p)) return 0;
    return p.node;
}

/* The hook reapplies the eye every frame until it is given
 * back, so a screen the player opens has to release it. The
 * drone owns its own camera and fights us for it. */
static volatile int g_held = 0;

static void Hold(int want) {
    if (want == g_held) return;
    g_held = want;
    if (want) PushCamera();
    else if (g_release) g_release(SH_CAM_POS);
}

/* Entity wide show: releases every hold on the root and
 * unhides all nodes in one deferred call, applied on the
 * game thread against the live node list. */
static void ShowHead(void) {
    if (g_setVisible && g_hideRoot)
        g_setVisible(g_hideRoot, 0, 1, 0);
    g_nparts = 0;
    g_hideRoot = 0;
}

/* The aim group that names the head parts appears the first
 * time the player aims, so this keeps trying until it does.
 */
static int HideHead(uint64_t root) {
    int n, i;

    if (!g_headNodes || !g_setVisible) return 0;
    n = g_headNodes(root, g_parts, MAX_PARTS);
    if (n <= 0) return 0;

    for (i = 0; i < n; i++)
        g_setVisible(root, g_parts[i], 0, 1);
    g_nparts = n;
    g_hideRoot = root;
    return n;
}

static void Report(void) {
    /* Runtime status is kept out of the release menu. */
}

static void OnToggle(uint32_t menu, uint32_t item, int value,
                     void *user) {
    (void)menu; (void)item; (void)user;

    if (value) {
        uint64_t root = PlayerRoot();

        g_on = 1;
        g_cameraMode = 1;
        if(g_swimmingFirstPerson)g_swimmingFirstPerson(g_swimFirstPerson&&g_isSwimming&&g_isSwimming());
        Hold(1);
        ApplyBlur();
        if (root && g_wantHide) {
            g_root = root;
            HideHead(root);
        }
    } else {
        g_on = 0;
        g_cameraMode = 0;
        if(g_swimmingFirstPerson)g_swimmingFirstPerson(0);
        ShowHead();
        ApplyBlur();
        Hold(0);
    }
    Report();
    Notice(value?"IMMERSIVE CAMERA | FIRST PERSON ON":"IMMERSIVE CAMERA | FIRST PERSON OFF",value?0x75FF9A:0xFFD070);
}

static void OnHide(uint32_t menu, uint32_t item, int value,
                   void *user) {
    (void)menu; (void)item; (void)user;
    g_wantHide = value;
    if (value) {
        uint64_t root = PlayerRoot();
        if (g_on && root) HideHead(root);
    } else {
        ShowHead();
    }
    Report();
}

static void OnForward(uint32_t menu, uint32_t item, int value,
                      void *user) {
    (void)menu; (void)item; (void)user;
    g_fwd = (float)value;
    /* Only while we already own it, or adjusting a slider
     * would take the camera back during a screen. */
    if (g_on && g_held) PushCamera();
}

static void OnUp(uint32_t menu, uint32_t item, int value,
                 void *user) {
    (void)menu; (void)item; (void)user;
    g_up = (float)value;
    if (g_on && g_held) PushCamera();
}

/* 0 hands the camera over the instant iron sights come up,
 * which shows the eye flying into the body. */
static void OnSettle(uint32_t menu, uint32_t item, int value,
                     void *user) {
    (void)menu; (void)item; (void)user;
    g_settleMs = value;
}

static void HoldForAds(int want) {
    if(want){Hold(1);return;}
    if(!g_held)return;
    g_held=0;
    if(g_adsHandoff)g_adsHandoff();
    else if(g_release)g_release(SH_CAM_POS);
}

static void PushThirdPersonOrbit(void){static const float back[]={0,0,1.8f,3.5f,7.0f,0,3.2f};static const float up[]={0,0,1.45f,1.75f,2.6f,0,2.2f};float b,h,s=0.0f;if(g_cameraMode<2||g_cameraMode>6)return;b=g_cameraMode==5?g_customBack:back[g_cameraMode];h=g_cameraMode==5?g_customHeight:up[g_cameraMode];if(g_cameraMode==5)s=g_customSide;if(g_cameraOrbitAdvanced)g_cameraOrbitAdvanced(b,s,h);else if(g_cameraOrbit)g_cameraOrbit(b,h);}
static void CapturePreHolsterCamera(void){ShPlayer p;ShCamera c;ShVec3 pos;float yaw,pitch,roll,dx,dy,dz;if(g_on)return;ZeroMemory(&p,sizeof(p));ZeroMemory(&c,sizeof(c));if(!g_getPlayer||!g_getCamera||!g_getEntityTransform||!g_getPlayer(&p)||!g_getCamera(&c))return;if(!g_getEntityTransform(p.root?p.root:p.entity,&pos,&yaw,&pitch,&roll))return;dx=c.pos.x-pos.x;dy=c.pos.y-pos.y;dz=c.pos.z-pos.z;g_capturedHolsteredBack=-(dx*c.forward.x+dy*c.forward.y+dz*c.forward.z);g_capturedHolsteredSide=dx*c.right.x+dy*c.right.y+dz*c.right.z;g_capturedHolsteredUp=dx*c.up.x+dy*c.up.y+dz*c.up.z;if(g_capturedHolsteredBack>0.3f&&g_capturedHolsteredBack<20.0f)g_capturedHolsteredValid=1;}
static void PushHolsteredCamera(void){float b,s,u;if(!g_maintainHolsteredCamera||g_on)return;if(g_customHolsteredCamera){b=g_holsteredBack;s=g_holsteredSide;u=g_holsteredUp;}else if(g_capturedHolsteredValid){b=g_capturedHolsteredBack;s=g_capturedHolsteredSide;u=g_capturedHolsteredUp;}else{PushThirdPersonOrbit();return;}if(g_cameraOrbitAdvanced)g_cameraOrbitAdvanced(b,s,u);else if(g_cameraOrbit)g_cameraOrbit(b,u);}
static void ApplyCameraMode(int mode){if(mode<0||mode>6)mode=0;if(mode==1){g_fovEnabled=1;OnToggle(0,0,1,NULL);PushFov();return;}g_cameraMode=mode;g_on=0;ShowHead();Hold(0);if(mode>=2)PushThirdPersonOrbit();else if(g_release){g_release(SH_CAM_POS);g_release(SH_CAM_FOV);}if(mode>=2)PushFov();ApplyBlur();Report();Notice(mode?"IMMERSIVE CAMERA | THIRD PERSON PRESET":"IMMERSIVE CAMERA | STOCK MODE",mode?0x75FF9A:0xFFD070);}
static void OnCameraMode(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_pendingCameraMode=v+1;}
static void OnApplyCameraMode(uint32_t m,uint32_t i,int v,void*u){int mode=g_pendingCameraMode;(void)m;(void)i;(void)v;(void)u;ApplyCameraMode(mode);}
static void OnCustomBack(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_customBack=(float)v/100.0f;if(g_cameraMode==5)PushThirdPersonOrbit();}
static void OnCustomHeight(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_customHeight=(float)v/100.0f;if(g_cameraMode==5)PushThirdPersonOrbit();}
static void OnCustomSide(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_customSide=(float)v/100.0f;if(g_cameraMode==5)PushThirdPersonOrbit();}
static void OnSide(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_side=(float)v;if(g_on&&g_held)PushCamera();}
static void OnHipFwd(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_hipFwd=(float)v;if(g_on&&g_held&&g_nativeAdsDetected==1)PushCamera();}
static void OnHipSide(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_hipSide=(float)v;if(g_on&&g_held&&g_nativeAdsDetected==1)PushCamera();}
static void OnHipUp(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_hipUp=(float)v;if(g_on&&g_held&&g_nativeAdsDetected==1)PushCamera();}
static int WeaponSnapshotCommand(int command){return g_fpWeaponProfile?g_fpWeaponProfile(0,0,0,0,0,0,0,0,0,command):0;}
static void OnWeaponSnapshotCapture(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;/* Fine-tune command 6 already persists the visible offsets. Saving must only enable that exact result, never recapture a corrected frame and jump elsewhere. */if(WeaponSnapshotCommand(4)){if(g_status)g_status(g_menu,"Current visible alignment saved globally");Notice("IMMERSIVE CAMERA | GLOBAL WEAPON ALIGNMENT SAVED",0x75FF9A);}else if(WeaponSnapshotCommand(2)){if(g_status)g_status(g_menu,"Base alignment captured globally; fine-tune it now");Notice("IMMERSIVE CAMERA | GLOBAL WEAPON ALIGNMENT CAPTURED",0x75FF9A);}else if(g_status)g_status(g_menu,"Hip aim once, then save again");}
static void OnWeaponSnapshotClear(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;WeaponSnapshotCommand(3);if(g_status)g_status(g_menu,"Global alignment deleted; automatic learning active");}
static void ApplyWeaponFine(void){if(g_fpWeaponProfile)g_fpWeaponProfile((float)g_weaponFineForward10/1000.0f,(float)g_weaponFineSide10/1000.0f,(float)g_weaponFineUp10/1000.0f,0,0,0,0,0,0,6);}
static void OnWeaponFineForward(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_weaponFineForward10=v;PutCfg("WeaponAlignmentForwardTenthCm",v);ApplyWeaponFine();}
static void OnWeaponFineSide(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_weaponFineSide10=v;PutCfg("WeaponAlignmentSideTenthCm",v);ApplyWeaponFine();}
static void OnWeaponFineUp(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_weaponFineUp10=v;PutCfg("WeaponAlignmentHeightTenthCm",v);ApplyWeaponFine();}
static void OnWeaponFineReset(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;g_weaponFineForward10=0;g_weaponFineSide10=0;g_weaponFineUp10=0;PutCfg("WeaponAlignmentForwardTenthCm",0);PutCfg("WeaponAlignmentSideTenthCm",0);PutCfg("WeaponAlignmentHeightTenthCm",0);ApplyWeaponFine();if(g_status)g_status(g_menu,"All weapon-alignment offsets reset to zero");/* Rebuild the menu after the callback so all three displayed values immediately return to 0. */if(g_hotReloadDeferred)g_hotReloadDeferred("immersion_suite",250);}
static void OnWeaponDefaultLoad(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;g_weaponFineForward10=0;g_weaponFineSide10=0;g_weaponFineUp10=0;PutCfg("WeaponAlignmentForwardTenthCm",0);PutCfg("WeaponAlignmentSideTenthCm",0);PutCfg("WeaponAlignmentHeightTenthCm",0);if(WeaponSnapshotCommand(7)){if(g_status)g_status(g_menu,"Release default weapon position loaded and saved");Notice("IMMERSIVE CAMERA | DEFAULT WEAPON POSITION LOADED",0x75FF9A);}else if(g_status)g_status(g_menu,"Could not load default weapon position");if(g_hotReloadDeferred)g_hotReloadDeferred("immersion_suite",250);}
static void OnSprintLift(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_sprintLift=(float)v;}
static void OnSprintTransition(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_sprintBlendSpeed=(float)v/100.0f;}
static void OnSprintThreshold(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_sprintSpeedThreshold=(float)v/10.0f;}



static void OnStabilize(uint32_t m,uint32_t i,int v,void*u){volatile float *target=(volatile float*)u;(void)m;(void)i;if(target)*target=(float)v/100.0f;if(g_on&&g_held)PushCamera();}

static void OnVehicleProfile(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_vehicleProfile=v;if(g_on)PushCamera();}
static void OnVehicleForward(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_vehicleFwd=(float)v;if(g_on)PushCamera();}
static void OnVehicleUp(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_vehicleUp=(float)v;if(g_on)PushCamera();}
static void OnVehicleSide(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_vehicleSide=(float)v;if(g_on)PushCamera();}
static void OnWaterForward(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_waterFwd=(float)v;if(g_on)PushCamera();}
static void OnWaterUp(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_waterUp=(float)v;if(g_on)PushCamera();}
static void OnWaterSide(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_waterSide=(float)v;if(g_on)PushCamera();}
static void OnAirForward(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_airFwd=(float)v;if(g_on)PushCamera();}
static void OnAirUp(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_airUp=(float)v;if(g_on)PushCamera();}
static void OnAirSide(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_airSide=(float)v;if(g_on)PushCamera();}
static void OnRearView(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_rearView=v;}
static void OnToggleAltAll(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_toggleAltAll=v?1:0;PutCfg("ToggleAltStockViewEverywhere",g_toggleAltAll);}
static void OnAutoEnable(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_autoEnable=v;}
static void OnSwimmingFirstPerson(uint32_t m,uint32_t i,int v,void*u){int swimming=g_isSwimming&&g_isSwimming();(void)m;(void)i;(void)u;g_swimFirstPerson=v;if(g_swimmingFirstPerson)g_swimmingFirstPerson(v&&g_on&&swimming);if(v&&g_on&&swimming)PushCamera();}
static void OnEmergency(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;g_on=0;g_cameraMode=0;ShowHead();ApplyBlur();Hold(0);if(g_release)g_release(SH_CAM_POS);Report();Notice("IMMERSIVE CAMERA | CAMERA RELEASED",0xFFD070);}
static void OnReloadConfig(uint32_t m,uint32_t i,int v,void*u){int mode;(void)m;(void)i;(void)v;(void)u;LoadConfig();mode=g_cameraMode;ApplyBlur();if(g_fovEnabled)PushFov();else if(g_release)g_release(SH_CAM_FOV);ApplyCameraMode(mode);Report();Notice("ULTIMATE CAMERA | PROFIL NEU GELADEN",0x75D7FF);}
static void OnLoadAllModDefaults(uint32_t m,uint32_t i,int v,void*u){int mode;(void)m;(void)i;(void)v;(void)u;EnsureModDefaults();LoadModDefaults();mode=g_cameraMode;ApplyBlur();ApplyWeaponFine();if(g_fovEnabled)PushFov();ApplyCameraMode(mode);Report();if(g_status)g_status(g_menu,"All values loaded from the INI [ModDefaults] profile; use Save ALL to make them persistent");Notice("IMMERSION SUITE | ALL MOD DEFAULTS LOADED",0x75FF9A);if(g_hotReloadDeferred)g_hotReloadDeferred("immersion_suite",250);}
static void OnReinitialize(uint32_t m,uint32_t i,int v,void*u){int mode=g_on?1:g_cameraMode;(void)m;(void)i;(void)v;(void)u;ShowHead();BodyFollowOff();Hold(0);if(g_release){g_release(SH_CAM_POS);g_release(SH_CAM_FOV);}if(g_cameraResetRuntime)g_cameraResetRuntime();g_root=0;g_hideRoot=0;g_nparts=0;g_held=0;g_sprintCurrentLift=0;g_reloadCurrentLift=0;g_reloadDetected=0;g_sprintDetected=0;g_sprintCandidateState=-1;g_sprintCandidateSince=0;g_sprintBlendLastUpdate=0;g_nativeAdsDetected=-1;InterlockedExchange64(&g_aimNeutralHandler,0);InterlockedExchange64(&g_aimHipHandler,0);InterlockedExchange64(&g_aimAdsHandler,0);ApplyBlur();ApplyCameraMode(mode);InterlockedExchange(&g_headRefreshRequested,1);Notice("IMMERSIVE CAMERA | CAMERA RECAPTURED",0x75FF9A);}
static void OnModInfo(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;Notice("WILDLANDS IMMERSION SUITE | BY IDARKSLAY",0x75D7FF);}
static void OnReloadWholeMod(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;if(g_hotReloadDeferred&&g_hotReloadDeferred("immersion_suite",250)){if(g_menuOpen)g_menuOpen(0);}else if(g_status)g_status(g_menu,"Full mod reload could not be scheduled");}
static void OnSmoothXY(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_headSmoothingXY=(float)v/100.0f;if(g_on&&g_held)PushCamera();}
static void OnSmoothZ(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_headSmoothingZ=(float)v/100.0f;if(g_on&&g_held)PushCamera();}
static void OnVehSmoothXY(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_vehicleSmoothXY=(float)v/100.0f;if(g_on&&g_held)PushCamera();}
static void OnVehSmoothZ(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_vehicleSmoothZ=(float)v/100.0f;if(g_on&&g_held)PushCamera();}
static void OnWaterSmoothXY(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_waterSmoothXY=(float)v/100.0f;if(g_on&&g_held)PushCamera();}
static void OnWaterSmoothZ(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_waterSmoothZ=(float)v/100.0f;if(g_on&&g_held)PushCamera();}
static void OnAirSmoothXY(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_airSmoothXY=(float)v/100.0f;if(g_on&&g_held)PushCamera();}
static void OnAirSmoothZ(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_airSmoothZ=(float)v/100.0f;if(g_on&&g_held)PushCamera();}
static void ApplyBlur(void){if(g_setBlur)g_setBlur(g_blurOff?0:1);}
static void OnBlurOff(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_blurOff=v;ApplyBlur();Notice(v?"IMMERSIVE CAMERA | CAMERA BLUR OFF":"IMMERSIVE CAMERA | CAMERA BLUR DEFAULT",v?0x75FF9A:0xFFD070);}
static void LearnFovDefault(void){ShCamera c;if(!g_fovEnabled&&g_getCamera&&g_getCamera(&c)&&c.fov>0.05f&&c.fov<3.0f)g_fovDefaultRad=c.fov;}
static int PushFov(void){ShCameraOverride o;if(!g_cameraApply)return 0;if(!g_on&&g_cameraMode==0&&!Playing()){if(g_release)g_release(SH_CAM_FOV);return 1;}ZeroMemory(&o,sizeof(o));o.apply=SH_CAM_FOV;o.fov=g_fovDeg*0.01745329252f;return g_cameraApply(&o);}
static void OnFovEnabled(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;if(v){LearnFovDefault();g_fovEnabled=1;PushFov();}else{g_fovEnabled=0;if(g_release)g_release(SH_CAM_FOV);}Notice(v?"ULTIMATE CAMERA | FOV OVERRIDE AN":"ULTIMATE CAMERA | FOV ORIGINAL",v?0x75FF9A:0xFFD070);}
static void OnFovValue(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_fovDeg=(float)v;PushFov();}
static void OnMaintainHolsteredCamera(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_maintainHolsteredCamera=v;PutCfg("MaintainCameraWhenHolstered",v);if(v){if(g_holstered)PushHolsteredCamera();}else{/* Immediately restore normal third-person ownership instead of leaving the last experimental transform latched. */if(g_release)g_release(SH_CAM_POS);if(g_cameraMode>=2)PushThirdPersonOrbit();}}
static void OnCustomHolsteredCamera(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_customHolsteredCamera=v;PutCfg("UseCustomHolsteredCamera",v);if(g_holstered)PushHolsteredCamera();}
static void OnHolsteredBack(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_holsteredBack=(float)v/100.0f;PutCfg("HolsteredDistanceCm",v);if(g_holstered&&g_customHolsteredCamera)PushHolsteredCamera();}
static void OnHolsteredUp(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_holsteredUp=(float)v/100.0f;PutCfg("HolsteredHeightCm",v);if(g_holstered&&g_customHolsteredCamera)PushHolsteredCamera();}
static void OnHolsteredSide(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_holsteredSide=(float)v/100.0f;PutCfg("HolsteredSideCm",v);if(g_holstered&&g_customHolsteredCamera)PushHolsteredCamera();}
static void OnFovReset(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;g_fovDeg=g_fovDefaultRad*57.2957795f;PushFov();Notice("IMMERSIVE CAMERA | GAME FOV RESTORED",0x75D7FF);}
static void OnRestoreFirstPersonDefaults(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;g_fovDeg=57.0f;SaveFovConfig();PushFov();Notice("IMMERSION SUITE | VANILLA FOV DEFAULT RESTORED",0x75FF9A);if(g_hotReloadDeferred)g_hotReloadDeferred("immersion_suite",250);}
static void WeatherNotice(const char *prefix,int weather){char b[160];if(weather<0||weather>5)weather=0;snprintf(b,sizeof(b),"%s: %s",prefix,g_weatherNames[weather]);Notice(b,0x75FF9A);}
static int ApplyWeather(void){return g_setWeather&&g_setWeather((int)g_selectedWeather);}
static void OnWeatherSelect(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;InterlockedExchange(&g_selectedWeather,v);if(g_weatherLocked){if(ApplyWeather())WeatherNotice("Weather locked",v);else Notice("WEATHER | APPLY FAILED",0xFF7070);}}
static void OnWeatherApply(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;if(ApplyWeather())WeatherNotice("Weather applied",g_selectedWeather);else Notice("WEATHER | APPLY FAILED",0xFF7070);}
static void OnWeatherLock(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;InterlockedExchange(&g_weatherLocked,v);if(v){InterlockedExchange(&g_weatherCycle,0);if(ApplyWeather())WeatherNotice("Weather locked",g_selectedWeather);else Notice("WEATHER | LOCK FAILED",0xFF7070);}else if(g_releaseWeather&&g_releaseWeather())Notice("WEATHER | DYNAMIC GAME WEATHER",0x75FF9A);}
static void OnWeatherRelease(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;(void)u;InterlockedExchange(&g_weatherLocked,0);InterlockedExchange(&g_weatherCycle,0);if(g_releaseWeather&&g_releaseWeather())Notice("WEATHER | DYNAMIC GAME WEATHER",0x75FF9A);}
static void OnWeatherCycleSeconds(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;InterlockedExchange(&g_weatherCycleSeconds,v);}
static void OnWeatherCycle(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;InterlockedExchange(&g_weatherCycle,v);InterlockedExchange(&g_weatherLocked,0);g_nextWeather=0;if(!v&&g_releaseWeather)g_releaseWeather();Notice(v?"WEATHER | CYCLE ON":"WEATHER | CYCLE OFF",v?0x75FF9A:0xFFD070);}
static void OnTimeHour(uint32_t m,uint32_t i,int v,void*u){char b[96];(void)m;(void)i;(void)u;if(g_setTime&&g_setTime((float)(v%24))){snprintf(b,sizeof(b),"TIME | %02d:00",v%24);Notice(b,0x75FF9A);}else Notice("TIME | SET FAILED",0xFF7070);}
static void OnTimePreset(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)v;OnTimeHour(0,0,(int)(intptr_t)u,NULL);}
static void OnTimeSpeedPreset(uint32_t m,uint32_t i,int v,void*u){char b[96];float speed;(void)m;(void)i;(void)u;if(v<0||v>=12)return;speed=g_timeSpeedValues[v];InterlockedExchange(&g_timeSpeed,(LONG)(speed*100.0f));if(g_setTimeSpeed&&g_setTimeSpeed(speed)){snprintf(b,sizeof(b),"TIME SPEED | %.1fx",speed);Notice(b,0x75FF9A);}else Notice("TIME SPEED | FAILED",0xFF7070);}
static void OnCustomTimeSpeed(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;InterlockedExchange(&g_timeSpeed,v);}
static void OnApplyTimeSpeed(uint32_t m,uint32_t i,int v,void*u){char b[96];float speed=(float)g_timeSpeed/100.0f;(void)m;(void)i;(void)v;(void)u;if(g_setTimeSpeed&&g_setTimeSpeed(speed)){snprintf(b,sizeof(b),"TIME SPEED | %.2fx",speed);Notice(b,0x75FF9A);}else Notice("TIME SPEED | FAILED",0xFF7070);}
static void OnEnvironmentState(uint32_t m,uint32_t i,int v,void*u){char b[180];float hour=-1,speed=-1;int weather=-1;(void)m;(void)i;(void)v;(void)u;if(g_getTime)g_getTime(&hour);if(g_getTimeSpeed)g_getTimeSpeed(&speed);if(g_getWeather)g_getWeather(&weather);snprintf(b,sizeof(b),"Time %.2f | %.2fx | %s",hour,speed,weather>=0&&weather<6?g_weatherNames[weather]:"dynamic / unknown");Notice(b,0x75D7FF);}
static void UpdateWeatherCycle(void){if(g_weatherCycle&&Playing()){ULONGLONG now=GetTickCount64();if(now>=g_nextWeather){int next=(g_selectedWeather+1)%6;InterlockedExchange(&g_selectedWeather,next);ApplyWeather();g_nextWeather=now+(ULONGLONG)g_weatherCycleSeconds*1000;}}}
static void BodyFollowOff(void){g_bodyFollowTracking=0;g_bodyFollowOffset=0;}
static void PushBodyFollow(void){
    ShCamera c;ShPlayer p;ShVec3 pos;float yaw,bodyYaw,pitch,roll,cameraDelta,bodyDelta;int moving;
    if(!g_bodyFollow||!g_on||g_stockViewActive||g_nativeAdsDetected!=0||(g_inVehicle&&g_inVehicle())||!g_getCamera||!g_getPlayer||!g_getEntityTransform||!g_queueTransform||!g_getCamera(&c)||!g_getPlayer(&p)||!g_getEntityTransform(p.entity,&pos,&bodyYaw,&pitch,&roll)){BodyFollowOff();return;}
    yaw=atan2f(c.forward.y,c.forward.x);
    moving=(GetAsyncKeyState('W')&0x8000)||(GetAsyncKeyState('A')&0x8000)||(GetAsyncKeyState('S')&0x8000)||(GetAsyncKeyState('D')&0x8000);
    /* Re-queuing the complete entity transform while locomotion owns it
     * causes tiny velocity stalls.  Native movement already aligns the real
     * controller, so relinquish all writes until the movement keys are up. */
    if(moving){
        
        g_bodyFollowTracking=0;g_bodyFollowOffset=0.0f;
        return;
    }
    if(!g_bodyFollowTracking){
        g_bodyFollowTracking=1;g_bodyFollowLastYaw=yaw;g_bodyFollowLastBodyYaw=bodyYaw;g_bodyFollowOffset=0.0f;
        
        return;
    }
    cameraDelta=yaw-g_bodyFollowLastYaw;
    bodyDelta=bodyYaw-g_bodyFollowLastBodyYaw;
    while(cameraDelta>3.14159265f)cameraDelta-=6.28318531f;
    while(cameraDelta<-3.14159265f)cameraDelta+=6.28318531f;
    while(bodyDelta>3.14159265f)bodyDelta-=6.28318531f;
    while(bodyDelta<-3.14159265f)bodyDelta+=6.28318531f;
    /* Camera and entity yaw use opposite handedness in Wildlands. */
    g_bodyFollowOffset+=-cameraDelta-bodyDelta;
    while(g_bodyFollowOffset>3.14159265f)g_bodyFollowOffset-=6.28318531f;
    while(g_bodyFollowOffset<-3.14159265f)g_bodyFollowOffset+=6.28318531f;
    g_bodyFollowLastYaw=yaw;g_bodyFollowLastBodyYaw=bodyYaw;
    /* Drive the real entity/controller facing instead of only twisting its
     * render pose.  Preserve position, pitch and roll; queue only the yaw
     * target on the frame path so gameplay state and minimap can follow. */
    
    g_queueTransform(p.entity,&pos,bodyYaw+g_bodyFollowOffset,pitch,roll);
}
static void OnBodyFollow(uint32_t m,uint32_t i,int v,void*u){(void)m;(void)i;(void)u;g_bodyFollow=v;if(!v)BodyFollowOff();Notice(v?"BODY FOLLOW | ON":"BODY FOLLOW | OFF",v?0x75D7FF:0xFFD070);}

/* The weapon prompt names the mode Alt switches TO, not
 * the one you are in. So a label reading OVER THE SHOULDER
 * means iron sights are up right now. */
/* Iron sights are the engine's own aim camera. Holding the
 * eye there rips the scope glass off the weapon and smears
 * the scenery through the temporal upscaler. */
/* The label holds a localisation key, not the drawn text:
 * [AIMMODE_PC_OTS] is what renders as OVER THE SHOULDER.
 * The literal is kept for a build that resolves inline. */
#define WANT_KEY     "AIMMODE_PC_OTS"
#define WANT_LABEL   "OVER THE SHOULDER"
#define WANT_SCENE   "HUD_WeaponItemDisplay"
#define WALK_DEPTH   10
#define WALK_BUDGET  600

static int Contains(const char *hay, const char *needle) {
    int i, j;

    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j]; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'a' && a <= 'z') a = (char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (char)(b - 32);
            if (a != b) break;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static int LabelSays(uint64_t w, int depth, int *budget) {
    char cls[32], txt[160];
    int n, i;

    if (!w || depth > WALK_DEPTH) return 0;
    if (--*budget < 0) return 0;
    g_nWidgets++;

    if (g_widgetClass(w, cls, sizeof(cls)) && Contains(cls, "Label")) {
        g_nLabels++;
        if (g_widgetGetS(w, SH_P_TEXT, txt, sizeof(txt))) {
            g_nText++;
            if (Contains(txt, WANT_KEY) || Contains(txt, WANT_LABEL))
                return 1;
        }
    }

    n = g_childCount(w);
    for (i = 0; i < n; i++)
        if (LabelSays(g_childAt(w, i), depth + 1, budget)) return 1;
    return 0;
}

/* The weapon prompt stays up after ADS ends, so the aim
 * mode alone would leave the camera released for good.
 * Our own poll: the hook only blocks the GAME's reads. */
/* Native on-foot aim classifier discovered from the player's 0x4428E933
 * component. The state objects are relocated/recreated between launches, so
 * they are learned per session instead of matching hard-coded addresses. */
static uint64_t NativeAimHandler(void) {
    ShPlayer player; ShComponent components[128]; MEMORY_BASIC_INFORMATION mi;
    int count, i; uint64_t handler;
    if (!g_getPlayer || !g_getComponents) return 0;
    ZeroMemory(&player, sizeof(player));
    if (!g_getPlayer(&player) || !player.entity) return 0;
    count = g_getComponents(player.entity, components, 128);
    for (i = 0; i < count; i++) {
        if (components[i].classHash != 0x4428E933u ||
            !components[i].component) continue;
        if (!VirtualQuery((void *)(uintptr_t)(components[i].component + 0x58),
                          &mi, sizeof(mi)) || mi.State != MEM_COMMIT ||
            (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return 0;
        memcpy(&handler, (void *)(uintptr_t)(components[i].component + 0x58),
               sizeof(handler));
        return handler;
    }
    return 0;
}

static int NativeAimState(void) {
    uint64_t handler=NativeAimHandler();
    if (!handler) return -1;
    if (handler==(uint64_t)InterlockedCompareExchange64(&g_aimNeutralHandler,0,0)) return 0;
    if (handler==(uint64_t)InterlockedCompareExchange64(&g_aimHipHandler,0,0)) return 1;
    if (handler==(uint64_t)InterlockedCompareExchange64(&g_aimAdsHandler,0,0)) return 2;
    return -1;
}

static int Aiming(void) {
    static int lastAimedState=1;
    static ULONGLONG neutralSince=0;
    int down=(GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    int state;
    if(!down){
        uint64_t h=NativeAimHandler();
        uint64_t neutral=(uint64_t)InterlockedCompareExchange64(&g_aimNeutralHandler,0,0);
        if(h&&!neutral){InterlockedExchange64(&g_aimNeutralHandler,(LONG64)h);g_holstered=0;}
        else if(h==neutral){if(g_holstered){g_holstered=0;PushFov();}g_holsterCandidateHandler=0;g_holsterCandidateSince=0;}
        else if(h){ULONGLONG now=GetTickCount64();if(h!=g_holsterCandidateHandler){g_holsterCandidateHandler=h;g_holsterCandidateSince=now;CapturePreHolsterCamera();}else if(!g_holstered&&now-g_holsterCandidateSince>=350){g_holstered=1;PushHolsteredCamera();}}
        state=0;neutralSince=0;
    }
    else {
        state=NativeAimState();
        if(state==1||state==2){if(g_holstered){g_holstered=0;PushFov();}lastAimedState=state;neutralSince=0;}
        else if(lastAimedState&&(state==0||state<0)){
            ULONGLONG now=GetTickCount64();
            if(!neutralSince)neutralSince=now;
            if(now-neutralSince<300)state=lastAimedState;
        }
    }
    g_nativeAdsDetected = state;
    if (state >= 0) return state != 0;
    return down;
}

/* Walks the game's own UI, so it needs a ScriptHook that
 * exposes the widget tree. Older ones keep the camera. */
static int IronSights(void) {
    int i, n, result=0, budget = WALK_BUDGET;
    uint64_t handler;

    if (g_nativeAdsDetected == 2) return 1;

    if (!g_sceneCount || !g_sceneAt || !g_sceneRoot ||
        !g_childCount || !g_childAt || !g_widgetClass ||
        !g_widgetGetS)
        return 0;

    g_nWidgets = 0; g_nLabels = 0; g_nText = 0;
    n = g_sceneCount();
    g_nScenes = n;
    for (i = 0; i < n; i++) {
        uint64_t s = g_sceneAt(i), root;
        char name[64];

        /* Only the weapon display carries the prompt, and
         * walking all thirteen scenes every tick is work
         * for nothing. */
        if (g_sceneName && g_sceneName(s, name, sizeof(name)) &&
            !Contains(name, WANT_SCENE))
            continue;
        root = g_sceneRoot(s);
        if (!root) continue;
        if (LabelSays(root, 0, &budget)) { result=1; break; }
    }
    g_nSights = result;
    handler=NativeAimHandler();
    if(handler){
        if(result){InterlockedExchange64(&g_aimAdsHandler,(LONG64)handler);g_nativeAdsDetected=2;if(g_fovEnabled)PushFov();}
        else InterlockedExchange64(&g_aimHipHandler,(LONG64)handler);
    }
    return result;
}







/* Paused counts as in game, but the player lookup falls
 * back to a heap scan while a menu is up. Nothing here is
 * urgent enough to pay for that, so the tick waits. */
static int Playing(void) {
    if (g_state) return g_state() == SH_STATE_INGAME;
    return g_inGame && g_inGame();
}











static void UpdateSprintCamera(void){
    ShPlayer player;ShVec3 pos;float yaw,pitch,roll,target,diff,dt,rate,alpha;int wanted,keyForward,keySprint,aimInput,havePosition=0,vehicle=g_inVehicle&&g_inVehicle();ULONGLONG now=GetTickCount64();
    ZeroMemory(&player,sizeof(player));ZeroMemory(&pos,sizeof(pos));
    if(g_getPlayer&&g_getEntityTransform&&g_getPlayer(&player)&&g_getEntityTransform(player.entity,&pos,&yaw,&pitch,&roll))havePosition=1;
    if(havePosition&&(!g_sprintLastSample||now-g_sprintLastSample>=16)){
        if(g_sprintPositionValid){
            float dt=(float)(now-g_sprintLastSample)/1000.0f;
            float dx=pos.x-g_sprintLastPos.x,dy=pos.y-g_sprintLastPos.y;
            float dist2=dx*dx+dy*dy;
            float instant=(dt>0.001f&&dist2<25.0f)?sqrtf(dist2)/dt:0.0f;
            if(dist2>2500.0f)InterlockedExchange(&g_headRefreshRequested,1);
            g_sprintMeasuredSpeed+=(instant-g_sprintMeasuredSpeed)*0.28f;
        }else g_sprintPositionValid=1;
        g_sprintLastPos=pos;g_sprintLastSample=now;
    }else if(!havePosition){g_sprintPositionValid=0;g_sprintMeasuredSpeed=0.0f;g_sprintLastSample=now;}
    {
    /* Walking peaked at roughly 5.33 units/s while sprinting stayed around
     * 6.9-7.1.  Keep separate enter/leave thresholds so uneven terrain and
     * the speed EMA cannot make the camera lift chatter at the boundary. */
    keyForward=(GetAsyncKeyState('W')&0x8000)!=0;
    keySprint=keyForward&&((GetAsyncKeyState(VK_SHIFT)&0x8000)!=0);
    aimInput=((GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0)||
             ((GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0);
    if(!g_on||!g_held||vehicle)wanted=0;
    /* With keyboard input, Shift+W is fully authoritative. Requiring a
     * second transform/speed confirmation made later sprints fail whenever
     * entity sampling paused for a transition frame. If W is not used (for
     * example a controller), retain the measured-speed fallback. */
    else if(keyForward)wanted=keySprint;
    else if(g_nativeAdsDetected!=0)wanted=0;
    else if(g_sprintDetected)wanted=g_sprintMeasuredSpeed>=g_sprintSpeedThreshold-0.45f;
    else wanted=g_sprintMeasuredSpeed>g_sprintSpeedThreshold+0.30f;
    /* Uneven ground and animation root corrections can move the measured
     * speed across a threshold for one sample. Require a short stable
     * candidate before changing camera state, so the lift never chatters. */
    if(wanted==g_sprintDetected){g_sprintCandidateState=-1;g_sprintCandidateSince=0;}
    else if(g_sprintCandidateState!=wanted){g_sprintCandidateState=wanted;g_sprintCandidateSince=now;}
    else if(now-g_sprintCandidateSince>=(wanted?25u:50u)){g_sprintDetected=wanted;g_sprintCandidateState=-1;g_sprintCandidateSince=0;}
    {
    int sprinting=g_sprintDetected;
    target=sprinting?g_sprintLift:0.0f;
    /* Hip Aim and direct fire must never inherit a partially blended sprint
     * height. Wildlands enters a semi-hip/fire state on LMB even without RMB,
     * so either physical mouse action collapses only the sprint contribution
     * before the slower aim-state classifier catches up. */
    if(aimInput&&!keySprint){target=0.0f;g_sprintDetected=0;g_sprintCandidateState=-1;g_sprintCandidateSince=0;if(g_sprintCurrentLift!=0.0f){g_sprintCurrentLift=0.0f;if(g_on&&g_held)PushCamera();}}
    diff=target-g_sprintCurrentLift;
    if(!g_sprintBlendLastUpdate)g_sprintBlendLastUpdate=now;
    dt=(float)(now-g_sprintBlendLastUpdate)/1000.0f;g_sprintBlendLastUpdate=now;
    if(dt<0.001f)dt=0.001f;if(dt>0.050f)dt=0.050f;
    if(fabsf(diff)<0.02f){
        if(g_sprintCurrentLift!=target){g_sprintCurrentLift=target;if(g_on&&g_held)PushCamera();}
    }else{
        /* Convert the menu percentage into a response rate and integrate it
         * using real elapsed time. This produces the same curve at 60, 120,
         * 240 Hz and during occasional Windows scheduler delays. */
        rate=0.6f+55.0f*g_sprintBlendSpeed;
        if(target<g_sprintCurrentLift)rate=fmaxf(rate,10.0f);
        alpha=1.0f-expf(-rate*dt);
        g_sprintCurrentLift+=diff*alpha;
        if(g_on&&g_held)PushCamera();
    }
    }
    }
}

/* Body yaw needs render-like cadence.  Running it on the general 60 ms
 * maintenance tick made the character visibly chase the camera at only
 * about 16 Hz. The lightweight facing target now runs at about 480 Hz while
 * sprint/reload camera blending keeps its established roughly 240 Hz rate. */
static DWORD WINAPI BodyFollowThread(LPVOID p) {
    int cameraPhase=0;
    (void)p;
    while (!InterlockedCompareExchange(&g_stop,0,0)) {
        if((cameraPhase++&1)==0){
            ((void)0);
            UpdateSprintCamera();
        }
        PushBodyFollow();
        Sleep(2);
    }
    return 0;
}

/* A new body means the old parts are gone, so the hold is
 * dropped and armed again on the new one.
 */
static void QueueAltPerspectiveSwitch(void){
    LONG queued=InterlockedCompareExchange(&g_altToggleRequest,-1,-1);
    int currentlyThird=queued>=0?(queued!=0):(g_stockViewActive!=0);
    int targetThird=!currentlyThird;
    InterlockedExchange(&g_altToggleRequest,targetThird?1:0);
    Notice(targetThird?"SWITCHING TO THIRD PERSON...":"SWITCHING TO FIRST PERSON...",targetThird?0x75D7FF:0x75FF9A);
}




/* Input must not share the camera/character scanning thread. Some native
 * discovery calls can take long enough to miss a short key press. */
static DWORD WINAPI InputThread(LPVOID p){
    int altLatch=0,hudLatch=0,queuedDuringAds=0;
    (void)p;
    while(!InterlockedCompareExchange(&g_stop,0,0)){
        SHORT perspective=GetAsyncKeyState(g_perspectiveToggleKey),hud=GetAsyncKeyState(g_hudToggleKey);
        int altDown=(perspective&0x8000)!=0;
        int hudDown=(hud&0x8000)!=0;
        /* Stable fallback: any held aim input queues the perspective switch.
         * Distinguishing hip aim from native ADS needs a separate detector
         * pass; do not risk switching inside the native sight camera. */
        int nativeAdsActive=(GetAsyncKeyState(VK_RBUTTON)&0x8000)!=0;
        if(g_toggleAltAll&&g_on&&Playing()&&altDown&&!altLatch){
            ((void)0);
            if(nativeAdsActive){
                queuedDuringAds=1;
                Notice("PERSPECTIVE SWITCH QUEUED - RELEASE AIM TO APPLY",0xFFD070);
            }else QueueAltPerspectiveSwitch();
        }
        if(queuedDuringAds&&!nativeAdsActive){
            if(g_toggleAltAll&&g_on&&Playing())QueueAltPerspectiveSwitch();
            queuedDuringAds=0;
        }
        if(hudDown&&!hudLatch){
            g_gameHudVisible=!g_gameHudVisible;
            if(g_gameHudShow)g_gameHudShow(g_gameHudVisible);
            PutCfg("GameHudVisible",g_gameHudVisible);
        }
        altLatch=altDown;hudLatch=hudDown;
        Sleep(2);
    }
    return 0;
}

static DWORD WINAPI TickThread(LPVOID p) {
    int said = 0, settle = 0, keyLatch = 0,hideRefresh=0,fovRefresh=0,lastAim=-2,altToggleActive=0,lastStockView=0,wasPlaying=0,lastSwimming=0;
    ULONGLONG headReacquireUntil=0,nextHeadReacquire=0;
    uint64_t lastVisualNode=0;
    (void)p;

    while (!InterlockedCompareExchange(&g_stop,0,0)) {
        uint64_t root;

        Sleep(TICK_MS);
        if(g_noticeUntil&&GetTickCount64()>=g_noticeUntil){g_noticeUntil=0;if(g_hudSet&&g_hud)g_hudSet(g_hud,"");}
        {
            int nativeAim=NativeAimState();
            g_nativeAdsDetected=nativeAim;
            if(g_setSuperAccuracyActive)g_setSuperAccuracyActive(g_superAccuracy&&((nativeAim==2)?g_accuracyNativeAds:g_accuracyDefaultHip));
            if(nativeAim!=lastAim){lastAim=nativeAim;if(g_fovEnabled)PushFov();Report();}
        }
        UpdateWeatherCycle();
        if(g_blurOff)ApplyBlur();
        fovRefresh+=TICK_MS;if(fovRefresh>=500){if(g_fovEnabled)PushFov();else LearnFovDefault();fovRefresh=0;}
        {
            DWORD foregroundPid=0;
            int focused;
            GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
            focused=foregroundPid==GetCurrentProcessId();
            int down=focused&&((GetAsyncKeyState(g_toggleKey)&0x8000)!=0);
            if(g_altToggleKey) down|=(GetAsyncKeyState(g_altToggleKey)&0x8000)!=0;
            if(!focused) down=0;
            if(down&&!keyLatch){
                keyLatch=1;g_on=!g_on;
                g_cameraMode=g_on?1:0;
                if(g_swimmingFirstPerson)g_swimmingFirstPerson(g_on&&g_swimFirstPerson&&g_isSwimming&&g_isSwimming());
                if(!g_on){ShowHead();ApplyBlur();Hold(0);if(g_release)g_release(SH_CAM_POS);}
                else{
                    uint64_t root=PlayerRoot();
                    ApplyBlur();
                    PushCamera();
                    g_held=1;
                    if(root&&g_wantHide){g_root=root;HideHead(root);}
                }
                Report();
                Notice(g_on?"IMMERSIVE CAMERA | FIRST PERSON ON":"IMMERSIVE CAMERA | FIRST PERSON OFF",g_on?0x75FF9A:0xFFD070);
            }
            if(!down)keyLatch=0;
        }
        /* Give the camera back on every screen, not just on
         * the toggle, or the drone never gets it. */
        if (!Playing()) {
            wasPlaying=0;InterlockedExchange(&g_headRefreshRequested,1);
            g_root=0;g_hideRoot=0;g_nparts=0;
            Hold(0);
            continue;
        }
        if(!wasPlaying){wasPlaying=1;InterlockedExchange(&g_headRefreshRequested,1);}
        {
            int swimming=g_isSwimming&&g_isSwimming();
            if(g_swimmingFirstPerson)g_swimmingFirstPerson(g_on&&g_swimFirstPerson&&swimming);
            if(g_on&&g_swimFirstPerson&&swimming!=lastSwimming){
                /* Swimming swaps the native gameplay camera. Reacquire our
                 * first-person ownership immediately on both transitions. */
                if(g_cameraResetRuntime)g_cameraResetRuntime();
                Hold(0);PushCamera();Hold(1);
                InterlockedExchange(&g_headRefreshRequested,1);
            }
            lastSwimming=swimming;
        }
        /* Holstered detection is also required while the native/third-person
         * camera owns position, so sample its state before that branch. */
        if(!g_on)Aiming();
        if(!g_on){
            Hold(0);
            /* Every custom third-person preset yields position ownership to
             * the game's native ADS camera. This prevents a high/wide preset
             * from dragging iron sights to the preset's world position. */
            if(g_holstered&&g_maintainHolsteredCamera)PushHolsteredCamera();else if(g_cameraMode>=2){if(g_nativeAdsDetected==2){if(g_release)g_release(SH_CAM_POS);}else PushThirdPersonOrbit();}
            continue;
        }
        {
            SHORT altLeft=GetAsyncKeyState(VK_LMENU),altRight=GetAsyncKeyState(VK_RMENU);
            int altDown=((altLeft&0x8000)||(altRight&0x8000));
            int stockView=0;
            LONG requested=InterlockedExchange(&g_altToggleRequest,-1);
            if(requested>=0)altToggleActive=requested?1:0;
            /* Block the camera switch only for the exact time native ADS is
             * held. There is no post-ADS lockout or timer. */
            {
            int adsHeld=(GetAsyncKeyState(VK_RBUTTON)&0x8000)&&(g_nativeAdsDetected==2||IronSights());
            if(!g_toggleAltAll)altToggleActive=0;
            if(g_toggleAltAll&&altToggleActive)stockView=1;
            }
            g_stockViewActive=stockView;
            if(stockView){
                BodyFollowOff();ShowHead();
                /* Hold may already be zero because native ADS handed camera
                 * ownership away. Release explicitly so its handoff watcher
                 * cannot restore First Person after the aim button is let go. */
                Hold(0);if(g_release)g_release(SH_CAM_POS);
                lastStockView=1;continue;
            }
            if(lastStockView){
                uint64_t fpRoot;
                lastStockView=0;settle=0;g_nativeAdsDetected=-1;
                /* A normal Alt toggle must reuse the already captured First
                 * Person runtime. Resetting it here caused a visible/input
                 * dead period while the camera was learned again. Full
                 * recapture remains available through the recovery menu. */
                PushCamera();g_held=1;
                fpRoot=PlayerRoot();if(fpRoot&&g_wantHide){g_root=fpRoot;g_nparts=0;HideHead(fpRoot);}
                InterlockedExchange(&g_headRefreshRequested,1);
            }
        }
        /* The engine's aim camera owns iron sights, but
         * only while the player is actually aiming. */
        /* The wait is on the AIM, not on the mode: raising
         * the weapon zooms the eye into the body, while
         * Alt switches mode with no transition at all. */
        /* So an already settled aim hands over the moment
         * the mode changes. Taking it back is immediate. */
        {
        int aiming=Aiming();
        if (aiming) {
            if (settle < g_settleMs) settle += TICK_MS;
        } else {
            settle = 0;
        }
        }
        HoldForAds(!(settle >= g_settleMs && IronSights()));
        if (!g_held) continue;

        root = PlayerRoot();
        if (!root) continue;
        {
            uint64_t visualNode=PlayerVisualNode();
            if(visualNode&&lastVisualNode&&visualNode!=lastVisualNode)
                InterlockedExchange(&g_headRefreshRequested,1);
            if(visualNode)lastVisualNode=visualNode;
        }
        if(InterlockedExchange(&g_headRefreshRequested,0)){
            ULONGLONG now=GetTickCount64();
            ShowHead();if(g_cameraResetRuntime)g_cameraResetRuntime();g_root=0;g_nparts=0;PushCamera();
            headReacquireUntil=now+15000;nextHeadReacquire=now+500;
        }
        /* Streaming completion has no single stable notification. During the
         * short post-load/respawn window, invalidate discovery repeatedly so
         * an old controller that remains alive cannot pin the cache forever. */
        if(headReacquireUntil){
            ULONGLONG now=GetTickCount64();
            if(now>=headReacquireUntil)headReacquireUntil=0;
            else if(now>=nextHeadReacquire){
                if(g_headNodesInvalidate)g_headNodesInvalidate();
                g_nparts=0;root=PlayerRoot();if(root&&g_wantHide)HideHead(root);
                nextHeadReacquire=now+500;
            }
        }
        if (root != g_root) {
            g_root = root;
            g_nparts = 0;
            said = 0;
            PushCamera();
        }
        hideRefresh+=TICK_MS;
        if(g_wantHide&&hideRefresh>=600){HideHead(root);hideRefresh=0;}
        if (g_wantHide && g_nparts == 0 && HideHead(root)) {
            Report();
            said = 0;
        } else if (!said) {
            Report();
            said = 1;
        }
    }
    return 0;
}

static DWORD WINAPI BindThread(LPVOID p) {
    HMODULE m = NULL;
    MenuCreate_t menuCreate;
    MenuSub_t menuSub;
    MenuToggle_t menuToggle;
    MenuNumber_t menuNumber;
    MenuList_t menuList;
    MenuAction_t menuAction;
    MenuDescribe_t menuDescribe;
    HudCreate_t hudCreate;
    (void)p;

    while (!m && !InterlockedCompareExchange(&g_stop,0,0)) {
        m = GetModuleHandleA("dinput8.dll");
        if (!m) Sleep(500);
    }
    if (!m || InterlockedCompareExchange(&g_stop,0,0)) return 0;
    *(FARPROC *)&g_inGame = GetProcAddress(m, "ShIsInGame");
    *(FARPROC *)&g_state = GetProcAddress(m, "ShGetGameState");
    *(FARPROC *)&g_getPlayer = GetProcAddress(m, "ShGetPlayer");
    *(FARPROC *)&g_getEntityTransform = GetProcAddress(m, "ShGetEntityTransform");
    *(FARPROC *)&g_queueTransform = GetProcAddress(m, "ShQueueTransform");
    *(FARPROC *)&g_getComponents = GetProcAddress(m, "ShGetComponents");
    *(FARPROC *)&g_findComponent = GetProcAddress(m, "ShFindComponent");
    *(FARPROC *)&g_inVehicle = GetProcAddress(m, "ShIsInVehicle");
    *(FARPROC *)&g_getOccupiedVehicle = GetProcAddress(m, "ShGetOccupiedVehicle");
    *(FARPROC *)&g_isSwimming = GetProcAddress(m, "ShIsSwimming");
    *(FARPROC *)&g_swimmingFirstPerson = GetProcAddress(m, "ShCameraSwimmingFirstPerson");
    *(FARPROC *)&g_fp = GetProcAddress(m, "ShCameraFirstPerson");
    *(FARPROC *)&g_fpLean = GetProcAddress(m, "ShCameraFirstPersonLean");
    *(FARPROC *)&g_fpAdvanced = GetProcAddress(m, "ShCameraFirstPersonAdvanced");
    *(FARPROC *)&g_fpProfile = GetProcAddress(m, "ShCameraFirstPersonProfile");
    *(FARPROC *)&g_fpWeaponProfile = GetProcAddress(m, "ShCameraFirstPersonWeaponProfile");
    *(FARPROC *)&g_adsHandoff = GetProcAddress(m, "ShCameraAdsHandoff");
    *(FARPROC *)&g_vehicleClass = GetProcAddress(m, "ShCameraVehicleClass");
    *(FARPROC *)&g_getCamera = GetProcAddress(m, "ShGetCamera");
    *(FARPROC *)&g_cameraApply = GetProcAddress(m, "ShCameraApply");
    *(FARPROC *)&g_cameraOrbit = GetProcAddress(m, "ShCameraOrbit");
    *(FARPROC *)&g_cameraOrbitAdvanced = GetProcAddress(m,"ShCameraOrbitAdvanced");
    *(FARPROC *)&g_release =
        GetProcAddress(m, "ShCameraReleaseFields");
    *(FARPROC *)&g_headNodes = GetProcAddress(m, "ShGetHeadNodes");
    *(FARPROC *)&g_headNodesInvalidate = GetProcAddress(m, "ShHeadNodesInvalidate");
    *(FARPROC *)&g_setVisible = GetProcAddress(m, "ShSetVisible");
    /* Optional: an older dinput8 just keeps the blur. */
    *(FARPROC *)&g_setBlur = GetProcAddress(m, "ShSetCameraBlur");
    /* Optional: without the widget tree the camera is held
     * through iron sights, which is the old behaviour. */
    *(FARPROC *)&g_sceneCount = GetProcAddress(m, "ShGameSceneCount");
    *(FARPROC *)&g_sceneAt = GetProcAddress(m, "ShGameSceneAt");
    *(FARPROC *)&g_sceneRoot = GetProcAddress(m, "ShSceneRoot");
    *(FARPROC *)&g_childCount = GetProcAddress(m, "ShWidgetChildCount");
    *(FARPROC *)&g_childAt = GetProcAddress(m, "ShWidgetChildAt");
    *(FARPROC *)&g_widgetClass = GetProcAddress(m, "ShWidgetClass");
    *(FARPROC *)&g_widgetGetS = GetProcAddress(m, "ShWidgetGetS");
    *(FARPROC *)&g_sceneName = GetProcAddress(m, "ShGameSceneName");
    *(FARPROC *)&g_widgetPropType =
        GetProcAddress(m, "ShWidgetPropType");
    *(FARPROC *)&g_setWeather = GetProcAddress(m,"ShSetWeather");
    *(FARPROC *)&g_releaseWeather = GetProcAddress(m,"ShReleaseWeather");
    *(FARPROC *)&g_getWeather = GetProcAddress(m,"ShGetWeather");
    *(FARPROC *)&g_setTime = GetProcAddress(m,"ShSetTime");
    *(FARPROC *)&g_getTime = GetProcAddress(m,"ShGetTime");
    *(FARPROC *)&g_setTimeSpeed = GetProcAddress(m,"ShSetTimeSpeed");
    *(FARPROC *)&g_getTimeSpeed = GetProcAddress(m,"ShGetTimeSpeed");
    *(FARPROC *)&menuCreate = GetProcAddress(m, "ShMenuCreate");
    *(FARPROC *)&menuSub = GetProcAddress(m, "ShMenuSub");
    *(FARPROC *)&menuToggle = GetProcAddress(m, "ShMenuToggle");
    *(FARPROC *)&menuNumber = GetProcAddress(m, "ShMenuNumber");
    *(FARPROC *)&menuList = GetProcAddress(m, "ShMenuList");
    *(FARPROC *)&g_status = GetProcAddress(m, "ShMenuStatus");
    *(FARPROC *)&g_menuDestroy = GetProcAddress(m, "ShMenuDestroy");
    *(FARPROC *)&g_menuOpen = GetProcAddress(m, "ShMenuOpen");
    *(FARPROC *)&menuAction = GetProcAddress(m, "ShMenuAction");
    *(FARPROC *)&menuDescribe = GetProcAddress(m,"ShMenuDescribe");
    *(FARPROC *)&g_hotReloadDeferred = GetProcAddress(m,"ShHotReloadDeferred");
    *(FARPROC *)&g_cameraResetRuntime = GetProcAddress(m,"ShCameraResetRuntime");
    *(FARPROC *)&g_gameHudShow = GetProcAddress(m,"ShGameHudShow");
    *(FARPROC *)&g_setSuperAccuracy = GetProcAddress(m,"ShSetSuperAccuracy");
    *(FARPROC *)&g_setSuperAccuracyActive = GetProcAddress(m,"ShSetSuperAccuracyActive");
    *(FARPROC *)&hudCreate = GetProcAddress(m,"ShHudCreate");
    *(FARPROC *)&g_hudSet = GetProcAddress(m,"ShHudSet");
    *(FARPROC *)&g_hudColour = GetProcAddress(m,"ShHudColour");
    *(FARPROC *)&g_hudDestroy = GetProcAddress(m,"ShHudDestroy");
    if (!g_inGame || !g_getPlayer || !g_fp || !g_release) return 1;
    if (!g_headNodes || !g_setVisible) return 1;
    if (!menuCreate || !menuSub || !menuToggle || !menuNumber || !menuList || !g_status)
        return 1;

    EnsureModDefaults();
    LoadConfig();
    if(g_gameHudShow)g_gameHudShow(g_gameHudVisible);
    if(g_setSuperAccuracy&&g_superAccuracy&&!g_setSuperAccuracy(1))g_superAccuracy=0;
    if(g_weatherLocked)ApplyWeather();
    if(g_setTimeSpeed)g_setTimeSpeed((float)g_timeSpeed/100.0f);
    g_pendingCameraMode=g_cameraMode;
    ApplyBlur();
    if(g_swimmingFirstPerson)g_swimmingFirstPerson(g_swimFirstPerson&&g_on&&g_isSwimming&&g_isSwimming());
    if(g_fovEnabled)PushFov();else LearnFovDefault();
    g_menu = menuCreate("Wildlands Immersion Suite");
    if(hudCreate)g_hud=hudCreate("immersion-suite-notice",SH_HUD_TOPLEFT,-120);
    ImmersiveMovementAddToSuiteMenu(g_menu);
#ifdef IMMERSIVE_BALLISTICS_EMBEDDED
    ImmersiveBallisticsAddToSuiteMenu(g_menu);
#endif
    {
    uint32_t modes=menuSub(g_menu,"Camera mode"),general=menuSub(g_menu,"General"),weapon=menuSub(g_menu,"Weapon handling"),fine=menuSub(g_menu,"Camera fine tuning"),position=menuSub(fine,"Camera position [FP only]"),alignment=menuSub(position,"Weapon alignment [FP only]"),movement=menuSub(fine,"Movement [FP only]"),vehicleProfiles=menuSub(fine,"Vehicle camera profiles [EXPERIMENTAL]"),vehicle=menuSub(vehicleProfiles,"Ground vehicles"),water=menuSub(vehicleProfiles,"Water vehicles"),air=menuSub(vehicleProfiles,"Air vehicles"),holstered=menuSub(fine,"Holstered camera [EXPERIMENTAL]"),fov=menuSub(g_menu,"Field of view"),weather=menuSub(g_menu,"Weather"),time=menuSub(g_menu,"Time"),profiles=menuSub(g_menu,"Profiles & recovery"),info=menuSub(profiles,"Mod information");
    g_vehicleProfilesMenu=vehicleProfiles;
    menuList(modes,"Selected camera mode",g_cameraModeNames,6,(g_pendingCameraMode>=1&&g_pendingCameraMode<=6)?g_pendingCameraMode-1:0,OnCameraMode,NULL);
    if(menuAction)menuAction(modes,"Apply selection",OnApplyCameraMode,NULL);
    menuNumber(modes,"Custom distance cm",g_customBack*100,50,1500,10,OnCustomBack,NULL);
    menuNumber(modes,"Custom height cm",g_customHeight*100,-200,800,10,OnCustomHeight,NULL);
    menuNumber(modes,"Custom side cm",g_customSide*100,-500,500,5,OnCustomSide,NULL);
    if(menuAction)menuAction(modes,"Save camera-mode configuration",OnSaveCameraModeConfig,NULL);
    menuToggle(general,"Start in Default Camera",g_autoEnable,OnAutoEnable,NULL);
    menuToggle(general,"Disable camera blur",g_blurOff,OnBlurOff,NULL);
    menuToggle(general,"First Person while swimming [EXPERIMENTAL]",g_swimFirstPerson,OnSwimmingFirstPerson,NULL);
    menuToggle(general, "Hide head", g_wantHide, OnHide, NULL);
    menuToggle(general,"Toggle Alt: third person",g_toggleAltAll,OnToggleAltAll,NULL);
    menuToggle(general,"Show game HUD",g_gameHudVisible,OnGameHudVisible,NULL);
    if(menuAction)menuAction(general,"Save general configuration",OnSaveGeneralConfig,NULL);
    menuToggle(weapon,"Super Accuracy",g_superAccuracy,OnSuperAccuracy,NULL);
    menuToggle(weapon,"Accuracy in Default / Hip Aim",g_accuracyDefaultHip,OnAccuracyDefaultHip,NULL);
    menuToggle(weapon,"Accuracy in Native ADS",g_accuracyNativeAds,OnAccuracyNativeAds,NULL);
    if(menuAction)menuAction(weapon,"Save weapon configuration",OnSaveWeaponConfig,NULL);
    if(menuAction)menuAction(fine,"Save all camera fine tuning",OnSaveFineConfig,NULL);
    menuNumber(alignment,"Depth x0.1 cm",g_weaponFineForward10,-300,300,1,OnWeaponFineForward,NULL);
    menuNumber(alignment,"Horizontal x0.1 cm",g_weaponFineSide10,-300,300,1,OnWeaponFineSide,NULL);
    menuNumber(alignment,"Vertical x0.1 cm",g_weaponFineUp10,-300,300,1,OnWeaponFineUp,NULL);
    if(menuAction){menuAction(alignment,"Save current alignment globally",OnWeaponSnapshotCapture,NULL);menuAction(alignment,"Load default weapon position",OnWeaponDefaultLoad,NULL);menuAction(alignment,"Reset all weapon offsets",OnWeaponFineReset,NULL);menuAction(alignment,"Clear global alignment / use auto",OnWeaponSnapshotClear,NULL);}
    menuNumber(movement,"Sprint camera lift cm",g_sprintLift,0,80,1,OnSprintLift,NULL);
    menuNumber(movement,"Sprint lift response speed %",g_sprintBlendSpeed*100,1,100,1,OnSprintTransition,NULL);
    menuNumber(movement,"Controller sprint threshold x0.1",g_sprintSpeedThreshold*10,10,120,1,OnSprintThreshold,NULL);
    
    menuToggle(movement,"Body follows camera (default)",g_bodyFollow,OnBodyFollow,NULL);
    menuNumber(fov,"Vanilla FOV",g_fovDeg,30,140,1,OnFovValue,NULL);
    menuToggle(holstered,"Enable experimental holstered camera",g_maintainHolsteredCamera,OnMaintainHolsteredCamera,NULL);
    menuToggle(holstered,"Use custom holstered position",g_customHolsteredCamera,OnCustomHolsteredCamera,NULL);
    menuNumber(holstered,"Distance cm",g_holsteredBack*100,50,1500,5,OnHolsteredBack,NULL);
    menuNumber(holstered,"Height cm",g_holsteredUp*100,-200,800,5,OnHolsteredUp,NULL);
    menuNumber(holstered,"Side cm",g_holsteredSide*100,-500,500,5,OnHolsteredSide,NULL);
    menuNumber(fov,"Native ADS transition settle ms",g_settleMs,SETTLE_MIN,SETTLE_MAX,SETTLE_STEP,OnSettle,NULL);
    menuToggle(vehicleProfiles,"Enable vehicle camera profiles",g_vehicleProfile,OnVehicleProfile,NULL);
    menuNumber(vehicle,"Forward cm",g_vehicleFwd,-60,60,2,OnVehicleForward,NULL);
    menuNumber(vehicle,"Height cm",g_vehicleUp,-60,60,2,OnVehicleUp,NULL);
    menuNumber(vehicle,"Side cm",g_vehicleSide,-60,60,1,OnVehicleSide,NULL);
    menuNumber(vehicle,"Lateral smoothing %",g_vehicleSmoothXY*100,0,95,1,OnVehSmoothXY,NULL);
    menuNumber(vehicle,"Vertical smoothing %",g_vehicleSmoothZ*100,0,95,1,OnVehSmoothZ,NULL);
    if(menuAction)menuAction(vehicle,"Save ground-vehicle configuration",OnSaveGroundVehicleConfig,NULL);
    menuNumber(water,"Forward cm",g_waterFwd,-60,60,2,OnWaterForward,NULL);
    menuNumber(water,"Height cm",g_waterUp,-60,60,2,OnWaterUp,NULL);
    menuNumber(water,"Side cm",g_waterSide,-60,60,1,OnWaterSide,NULL);
    menuNumber(water,"Lateral smoothing %",g_waterSmoothXY*100,0,95,1,OnWaterSmoothXY,NULL);
    menuNumber(water,"Vertical smoothing %",g_waterSmoothZ*100,0,95,1,OnWaterSmoothZ,NULL);
    if(menuAction)menuAction(water,"Save water-vehicle configuration",OnSaveWaterVehicleConfig,NULL);
    menuNumber(air,"Forward cm",g_airFwd,-60,60,2,OnAirForward,NULL);
    menuNumber(air,"Height cm",g_airUp,-60,60,2,OnAirUp,NULL);
    menuNumber(air,"Side cm",g_airSide,-60,60,1,OnAirSide,NULL);
    menuNumber(air,"Lateral smoothing %",g_airSmoothXY*100,0,95,1,OnAirSmoothXY,NULL);
    menuNumber(air,"Vertical smoothing %",g_airSmoothZ*100,0,95,1,OnAirSmoothZ,NULL);
    if(menuAction)menuAction(air,"Save air-vehicle configuration",OnSaveAirVehicleConfig,NULL);
    if(g_setWeather&&g_releaseWeather){menuList(weather,"Weather type",g_weatherNames,6,g_selectedWeather,OnWeatherSelect,NULL);if(menuAction)menuAction(weather,"Apply selected weather",OnWeatherApply,NULL);menuToggle(weather,"Lock weather",g_weatherLocked,OnWeatherLock,NULL);if(menuAction)menuAction(weather,"Release to game weather",OnWeatherRelease,NULL);menuNumber(weather,"Cycle interval seconds",g_weatherCycleSeconds,2,300,1,OnWeatherCycleSeconds,NULL);menuToggle(weather,"Weather cycle",g_weatherCycle,OnWeatherCycle,NULL);}
    if(g_setTime&&g_setTimeSpeed){menuNumber(time,"Hour",12,0,23,1,OnTimeHour,NULL);if(menuAction){menuAction(time,"Dawn 05:00",OnTimePreset,(void*)(intptr_t)5);menuAction(time,"Morning 08:00",OnTimePreset,(void*)(intptr_t)8);menuAction(time,"Noon 12:00",OnTimePreset,(void*)(intptr_t)12);menuAction(time,"Evening 18:00",OnTimePreset,(void*)(intptr_t)18);menuAction(time,"Night 21:00",OnTimePreset,(void*)(intptr_t)21);menuAction(time,"Midnight 00:00",OnTimePreset,(void*)(intptr_t)0);}menuList(time,"Speed preset",g_timeSpeedNames,12,3,OnTimeSpeedPreset,NULL);menuNumber(time,"Custom speed x0.01",g_timeSpeed,0,100000,10,OnCustomTimeSpeed,NULL);if(menuAction){menuAction(time,"Apply custom speed",OnApplyTimeSpeed,NULL);menuAction(time,"Show current state",OnEnvironmentState,NULL);}}
    {
        if (menuAction)
            {menuAction(profiles,"Recapture current camera",OnReinitialize,NULL);menuAction(profiles,"Unload and reload entire mod",OnReloadWholeMod,NULL);menuAction(profiles,"Save ALL persistent settings to INI",OnSaveConfig,NULL);menuAction(profiles,"Reload ALL settings from INI",OnReloadConfig,NULL);menuAction(profiles,"Load all mod default values",OnLoadAllModDefaults,NULL);menuAction(profiles,"Emergency restore camera/body",OnEmergency,NULL);menuAction(fov,"Save FOV",OnSaveFovConfig,NULL);menuAction(fov,"Restore vanilla FOV default",OnRestoreFirstPersonDefaults,NULL);menuAction(fov,"Back to game default",OnFovReset,NULL);menuAction(info,"Wildlands Immersion Suite",OnModInfo,NULL);menuAction(info,"Author: iDarkslay",NULL,NULL);menuAction(info,"First Person-focused gameplay mod",NULL,NULL);menuAction(info,"Powered by Wildlands Mod Framework",NULL,NULL);menuAction(info,"Modified GRW ScriptHook fork",NULL,NULL);menuAction(info,"Original ScriptHook: PhialsBasement",NULL,NULL);menuAction(info,"Camera research: Firejumper93",NULL,NULL);menuAction(info,"Human-directed, AI-assisted",NULL,NULL);menuAction(info,"Licensed under GPL-3.0",NULL,NULL);}
    }
    if(menuDescribe){
#define DESC(menu,label,text) menuDescribe(menu,label,text)
    DESC(g_menu,"Movement Speed Control","Adjust movement speed from 15% to 100% with the menu or mouse wheel while holding a movement key.");
    DESC(g_menu,"Ballistics Control","Configure global projectile velocity up to 300%, including Native ADS shots.");
    DESC(g_menu,"Camera mode","Choose and apply a camera preset. Everything besides First Person is currently considered experimental.");
    DESC(g_menu,"General","Global visibility, blur, startup, and temporary third-person behavior.");
    DESC(g_menu,"Weapon handling","Optional player weapon behavior. No Recoil is intentionally not included.");
    DESC(g_menu,"Camera fine tuning","Grouped camera-position controls. Each submenu is explicitly marked FP only or TP only.");
    DESC(g_menu,"Field of view","Manage the single vertical FOV value used by the camera mod. Changes apply immediately.");
    DESC(g_menu,"Weather","Select, blend, lock, release, or cycle world weather.");
    DESC(g_menu,"Time","Set world time and simulation speed.");
    DESC(g_menu,"Profiles & recovery","Save, reload, reinitialize, or safely release all camera ownership.");
    DESC(fine,"Camera position [FP only]","First-person weapon-position controls. These do not affect Third Person.");DESC(fine,"Movement [FP only]","First-person sprint, reload, and physical body-facing behavior.");DESC(fine,"Vehicle camera profiles [EXPERIMENTAL]","EXPERIMENTAL First Person feature. Ground, Water, and Air vehicle detection is available, but custom position offsets are not yet confirmed working and may behave inconsistently.");DESC(fine,"Holstered camera [EXPERIMENTAL]","EXPERIMENTAL Third Person feature. Attempts to maintain or customize the camera while weapons are holstered; behavior is not yet fully reliable.");
    DESC(position,"Weapon alignment [FP only]","Fine-tune only the fixed First Person weapon position in 0.1 cm steps; this does not move the eye camera.");
    DESC(modes,"Selected camera mode","Default Camera is the mod's primary First Person mode and loads automatically at startup. SOCOM uses 320 cm distance and 220 cm height. Custom uses the three values below.");
    DESC(modes,"Apply selection","Activate the selected camera mode without closing the menu.");
    DESC(modes,"Custom distance cm","Distance behind the player for Custom third person.");
    DESC(modes,"Custom height cm","Vertical offset for Custom third person.");
    DESC(modes,"Custom side cm","EXPERIMENTAL: intended to move Custom third person left or right; not confirmed working yet.");
    DESC(modes,"Save camera-mode configuration","Save only camera-mode selection and custom third-person values to the INI.");
    DESC(general,"Start in Default Camera","Automatically enter the mod's Default First Person camera when the plugin initializes.");
    DESC(general,"Disable camera blur","Disables the game's camera blur while leaving other post-processing intact.");
    DESC(general,"First Person while swimming [EXPERIMENTAL]","Experimental and not fully working yet. Idle swimming can remain in First Person, but normal or fast swimming may briefly or continuously switch back to Third Person. Disabled by default and only applies while First Person is active.");
    DESC(general,"Hide head","Hides head geometry in first person to prevent clipping.");
    DESC(general,"Toggle Alt: third person","Press the configured perspective key to switch between First and Third Person. If pressed during Native ADS, the request waits until aim is released.");
    DESC(general,"Show game HUD","Shows or hides the gameplay HUD. Caps Lock toggles it instantly; the state persists in immersion_suite.ini.");
    DESC(general,"Save general configuration","Save only startup, visibility, blur, and Alt behavior to the INI.");
    DESC(weapon,"Super Accuracy","Player-only perfect accuracy with guarded pointer validation. Enabled by default with First Person. No Recoil is not included.");
    DESC(weapon,"Accuracy in Default / Hip Aim","Apply Super Accuracy in the normal First Person and hip-aim states.");
    DESC(weapon,"Accuracy in Native ADS","Apply Super Accuracy only while the native aim-down-sights state is active.");
    DESC(weapon,"Save weapon configuration","Save the current Super Accuracy state to immersion_suite.ini for future game sessions.");
    DESC(fine,"Save all camera fine tuning","Save all First Person, movement, vehicle, and experimental holstered-camera tuning to the INI.");
    DESC(alignment,"Depth x0.1 cm","First Person only: moves the fixed weapon toward or away from the camera in 0.1 cm steps.");DESC(alignment,"Horizontal x0.1 cm","First Person only: moves the fixed weapon left or right in 0.1 cm steps.");DESC(alignment,"Vertical x0.1 cm","First Person only: moves the fixed weapon up or down in 0.1 cm steps.");DESC(alignment,"Save current alignment globally","Keeps the exact currently visible fine-tuned position, enables it globally, and reloads it on every game restart and every later First Person activation.");DESC(alignment,"Load default weapon position","Loads this release's built-in First Person weapon position, resets all fine offsets to zero, and saves it globally.");DESC(alignment,"Reset all weapon offsets","Resets Depth, Horizontal, and Vertical fine offsets to zero together without deleting the captured global base.");DESC(alignment,"Clear global alignment / use auto","Deletes the persistent manual alignment and explicitly returns to automatic per-weapon learning.");
    DESC(movement,"Sprint camera lift cm","Raises the First Person camera during sprinting. Adjust this live to find the best height, then save the camera fine-tuning configuration.");DESC(movement,"Sprint lift response speed %","Controls the time-based sprint-camera transition. Higher values react faster; its update cadence remains approximately 240 Hz.");DESC(movement,"Controller sprint threshold x0.1","Controller-only sprint-detection threshold. Increase it if ordinary controller movement incorrectly activates the sprint camera lift.");DESC(movement,"Body follows camera (default)","Turns the physical player body toward the camera while standing still in First Person. Updates at approximately 480 Hz and yields to native movement while walking.");
    DESC(fov,"Vanilla FOV","One global vertical FOV value for the complete camera mod. No separate First Person, Third Person, Hip Aim, or ADS values are used.");DESC(fov,"Native ADS transition settle ms","Camera-transition timing only; this does not define another FOV value.");DESC(fov,"Save FOV","Save the single Vanilla FOV value and ADS transition timing to the INI.");DESC(fov,"Restore vanilla FOV default","Restore and save the mod default of 57 vertical FOV.");DESC(fov,"Back to game default","Capture and use the game's current default FOV as the single value.");
    DESC(holstered,"Enable experimental holstered camera","EXPERIMENTAL and disabled by default. Third Person only: prevents the large holstered zoom-out by holding or overriding camera position.");DESC(holstered,"Use custom holstered position","Use the Distance, Height, and Side values below instead of the automatically captured pre-holster camera position. Requires the experimental toggle above.");DESC(holstered,"Distance cm","Third-person distance while holstered. Used only when both experimental and custom holstered position are enabled.");DESC(holstered,"Height cm","Third-person height while holstered. Used only when both experimental and custom holstered position are enabled.");DESC(holstered,"Side cm","Third-person horizontal offset while holstered. Used only when both experimental and custom holstered position are enabled.");
    DESC(vehicleProfiles,"Enable vehicle camera profiles","Enable experimental vehicle-specific First Person offsets. Detection works; final rendered position control is still under development.");DESC(vehicleProfiles,"Ground vehicles","Experimental offsets for cars, bikes, and other ground vehicles.");DESC(vehicleProfiles,"Water vehicles","Experimental offsets for boats, dinghies, and yachts.");DESC(vehicleProfiles,"Air vehicles","Experimental offsets for helicopters and aircraft.");
    DESC(vehicle,"Forward cm","Ground-vehicle camera depth offset.");DESC(vehicle,"Height cm","Ground-vehicle camera vertical offset.");DESC(vehicle,"Side cm","Ground-vehicle camera lateral offset.");DESC(vehicle,"Lateral smoothing %","Reduces side-to-side vehicle camera motion.");DESC(vehicle,"Vertical smoothing %","Reduces vertical vehicle camera motion.");DESC(vehicle,"Save ground-vehicle configuration","Save only the Ground vehicle profile to immersion_suite.ini.");
    DESC(water,"Forward cm","Water-vehicle camera depth offset.");DESC(water,"Height cm","Water-vehicle camera vertical offset.");DESC(water,"Side cm","Water-vehicle camera lateral offset.");DESC(water,"Lateral smoothing %","Reduces side-to-side boat camera motion.");DESC(water,"Vertical smoothing %","Reduces vertical boat camera motion.");DESC(water,"Save water-vehicle configuration","Save only the Water vehicle profile to immersion_suite.ini.");
    DESC(air,"Forward cm","Air-vehicle camera depth offset.");DESC(air,"Height cm","Air-vehicle camera vertical offset.");DESC(air,"Side cm","Air-vehicle camera lateral offset.");DESC(air,"Lateral smoothing %","Reduces side-to-side aircraft camera motion.");DESC(air,"Vertical smoothing %","Reduces vertical aircraft camera motion.");DESC(air,"Save air-vehicle configuration","Save only the Air vehicle profile to immersion_suite.ini.");
    DESC(weather,"Weather type","Select one of the discovered native weather presets.");DESC(weather,"Apply selected weather","Apply the selected preset once using its native transition.");DESC(weather,"Lock weather","Keep the selected weather active.");DESC(weather,"Release to game weather","Return weather control to the game.");DESC(weather,"Cycle interval seconds","Seconds between automatic weather changes.");DESC(weather,"Weather cycle","Automatically cycle through available weather presets.");
    DESC(time,"Hour","Set the world clock to a full hour.");DESC(time,"Dawn 05:00","Set world time to dawn at 05:00.");DESC(time,"Morning 08:00","Set world time to morning at 08:00.");DESC(time,"Noon 12:00","Set world time to noon at 12:00.");DESC(time,"Evening 18:00","Set world time to evening at 18:00.");DESC(time,"Night 21:00","Set world time to night at 21:00.");DESC(time,"Midnight 00:00","Set world time to midnight.");DESC(time,"Speed preset","Choose a predefined world-time speed.");DESC(time,"Custom speed x0.01","Custom time multiplier stored in hundredths.");DESC(time,"Apply custom speed","Apply the custom time multiplier.");DESC(time,"Show current state","Display current game time, speed, and weather.");
    DESC(profiles,"Recapture current camera","Release the current camera, clear learned head/aim state, and freshly reacquire the active camera position.");DESC(profiles,"Unload and reload entire mod","Safely unload and initialize the complete hot-mod DLL after this menu callback returns.");DESC(profiles,"Save ALL persistent settings to INI","Save every persistent camera, FOV, control, and tuning value to Fmods/immersion_suite.ini. Weather and time are intentionally excluded.");DESC(profiles,"Reload ALL settings from INI","Reload every saved persistent setting and reapply the selected mode.");DESC(profiles,"Load all mod default values","Load the complete editable [ModDefaults] profile from immersion_suite.ini into the running mod. This does not overwrite your saved profile until you choose Save ALL persistent settings.");DESC(profiles,"Emergency restore camera/body","Release camera and body control immediately.");DESC(profiles,"Mod information","Show the mod name, author, framework origin, primary credits, development disclosure, and GPL-3.0 license.");
#undef DESC
    }
    }
    /* Keep gameplay unobstructed. Feedback is shown inside the F1 menu;
     * this mod deliberately does not allocate a persistent HUD row. */
    Report();
    if(g_menuOpen)g_menuOpen(0);

    if(g_cameraMode)ApplyCameraMode(g_cameraMode);else{g_on=0;ApplyBlur();Report();}

    g_tickThreadHandle=CreateThread(NULL, 0, TickThread, NULL, 0, NULL);
    g_bodyThreadHandle=CreateThread(NULL,0,BodyFollowThread,NULL,0,NULL);
    g_inputThreadHandle=CreateThread(NULL,0,InputThread,NULL,0,NULL);
    return 0;
}

#ifdef BUILD_HOTMOD
__declspec(dllexport) int GrwModInit(void) {
    InterlockedExchange(&g_stop,0);
    InterlockedExchange64(&g_aimNeutralHandler,0);
    InterlockedExchange64(&g_aimHipHandler,0);
    InterlockedExchange64(&g_aimAdsHandler,0);
    g_nativeAdsDetected=-1;g_holstered=0;g_holsterCandidateHandler=0;g_holsterCandidateSince=0;g_menu=0;g_hud=0;g_root=0;g_hideRoot=0;g_nparts=0;
    g_reloadDetected=0;g_reloadInputLatch=0;g_reloadPulseValid=0;g_reloadCurrentLift=0.0f;
    g_bodyFollowTracking=0;g_bodyFollowOffset=0;
    InterlockedExchange(&g_headRefreshRequested,1);
    /* Movement is optional: an unsupported executable build must not stop
     * the camera/weather suite from loading. */
    ImmersiveMovementInit();
#ifdef IMMERSIVE_BALLISTICS_EMBEDDED
    ImmersiveBallisticsInit();
#endif
    g_bindThreadHandle=CreateThread(NULL,0,BindThread,NULL,0,NULL);
    return g_bindThreadHandle!=NULL;
}

__declspec(dllexport) int GrwModShutdown(void) {
    InterlockedExchange(&g_stop,1);
    if(g_gameHudShow)g_gameHudShow(1);
    if(g_setSuperAccuracy)g_setSuperAccuracy(0);
#ifdef IMMERSIVE_BALLISTICS_EMBEDDED
    ImmersiveBallisticsShutdown();
#endif
    if(!ImmersiveMovementShutdown())return 0;
    if(g_bindThreadHandle){
        if(WaitForSingleObject(g_bindThreadHandle,5000)!=WAIT_OBJECT_0)return 0;
        CloseHandle(g_bindThreadHandle);g_bindThreadHandle=NULL;
    }
    if(g_tickThreadHandle){
        if(WaitForSingleObject(g_tickThreadHandle,5000)!=WAIT_OBJECT_0)return 0;
        CloseHandle(g_tickThreadHandle);g_tickThreadHandle=NULL;
    }
    if(g_bodyThreadHandle){
        if(WaitForSingleObject(g_bodyThreadHandle,5000)!=WAIT_OBJECT_0)return 0;
        CloseHandle(g_bodyThreadHandle);g_bodyThreadHandle=NULL;
    }
    if(g_inputThreadHandle){
        if(WaitForSingleObject(g_inputThreadHandle,5000)!=WAIT_OBJECT_0)return 0;
        CloseHandle(g_inputThreadHandle);g_inputThreadHandle=NULL;
    }
    BodyFollowOff();
    g_on=0;g_cameraMode=0;ShowHead();Hold(0);
    if(g_releaseWeather)g_releaseWeather();
    if(g_setTimeSpeed)g_setTimeSpeed(1.0f);
    if(g_release){g_release(SH_CAM_POS);g_release(SH_CAM_FOV);}
    if(g_menu&&g_menuDestroy)g_menuDestroy(g_menu);
    if(g_hud&&g_hudDestroy)g_hudDestroy(g_hud);
    g_menu=0;g_hud=0;
    return 1;
}
#else
static DWORD WINAPI DirectInitThread(LPVOID p){
    (void)p;
    ImmersiveMovementInit();
#ifdef IMMERSIVE_BALLISTICS_EMBEDDED
    ImmersiveBallisticsInit();
#endif
    return BindThread(NULL);
}
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        CreateThread(NULL, 0, DirectInitThread, NULL, 0, NULL);
    }
    return TRUE;
}
#endif

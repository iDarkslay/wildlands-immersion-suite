/* Wildlands Mod Framework / Immersion Suite fork.
 * Original GRW ScriptHook portions Copyright (C) 2026 PhialsBasement.
 * Modifications and additions Copyright (C) 2026 iDarkslay.
 * Modified through 2026-09-12. SPDX-License-Identifier: GPL-3.0-only.
 * See LICENSE, NOTICE.md and UPSTREAM_CHANGES.md.
 */
/* Camera access. The engine rebuilds the transform every
 * frame, so an override is applied per call from inside
 * the engine's own call chain, never from a thread. */
/* Struct offsets, the write authority of +0x000 and the
 * yaw/pitch convention are Firejumper93's findings, MIT.
 */
/* https://github.com/Firejumper93/GhostReconWildlandsVR */
#include <windows.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SH_BUILD 1
#include "scripthook.h"
#include "suite_config.h"
#include "image.h"

/* The projection selector. It takes the camera in RCX and
 * runs every frame, which is what makes it hookable.
 */
#define CAM_THUNK   SH_IMG(0x13781B0)
#define CAM_IMPL    SH_IMG(0xD7C0610)

/* Verified live: +0x2B0 is vertical fov in radians, planes
 * beside it. +0x2BC is an ASPECT multiplier, which the
 * selector scales by width over height. */
#define CAM_POSE    0x000
#define CAM_MODE    0x290
#define CAM_WORLD_OVERRIDE 0x2A0
#define CAM_FOV     0x2B0
#define CAM_NEAR    0x2B4
#define CAM_FAR     0x2B8
#define CAM_ASPECT  0x2BC
#define CAM_SKEWX   0x2C4
#define CAM_SKEWY   0x2C8

/* Nine derived matrices, 0x40 apart, view and projection
 * and their inverses. Read only in practice.
 */
#define CAM_MATS    0x420
#define CAM_MAT_N   9

/* The camera manager, one frame ahead of the camera build.
 * The behaviour's transform lands here first, so an override
 * placed here reaches culling and the matrices together. */
#define MGR_SITE    SH_IMG(0x7E888FE)
#define MGR_LEN     5
#define MGR_NEXT    SH_IMG(0x10D8890)

/* Verified live in gameplay: the mode at +0x6C reads 3, so
 * consumers take the position from +0x170 while the render
 * camera takes the rows at +0x190. Both are restated. */
#define MGR_MODE    0x6C
#define MGR_POS     0x170
#define MGR_FOV     0x180
#define MGR_XFORM   0x190

/* Ownership is per field, so two plugins can hold different
 * parts of the camera at once. Orbit is a private bit: it
 * owns position, but derives it instead of storing it. */
#define CAM_ORBIT_BIT 0x100u
#define CAM_HEAD_BIT  0x200u
#define CAM_LEAN_BIT  0x400u
#define CAM_ADS_HANDOFF 0x800u
#define CAM_DERIVED   (CAM_ORBIT_BIT | CAM_HEAD_BIT | CAM_LEAN_BIT)

static volatile uint64_t g_cam = 0;
static volatile uint64_t g_calls = 0;
static volatile uint64_t g_writes = 0;

/* Special views run mode 0 like the main
 * camera (measured: the drone), so mode gates nothing.
 */
static volatile uint64_t g_otherAt = 0;
static volatile int g_otherMode = 0;

/* The pause menu never leaves the Playing game state, but
 * its camera is a template: a bit exact identity basis,
 * which no steered camera ever holds. Measured live. */
static volatile uint64_t g_uiAt = 0;

static ShVec3 g_absPos;
static volatile float g_back = 0.0f;
static volatile float g_up = 0.0f;
static volatile float g_right = 0.0f;
static volatile float g_leanRoll = 0.0f;
static volatile float g_headSmoothXY = 0.0f;
static volatile float g_headSmoothZ = 0.0f;
static volatile float g_weaponStabRight=0.0f,g_weaponStabForward=0.0f,g_weaponStabUp=0.0f;
typedef struct WeaponReference {
    uint64_t key;int profile;float right,forward,up;unsigned age;int valid;
} WeaponReference;
#define WEAPON_REFERENCE_CAP 256
static WeaponReference g_weaponReferences[WEAPON_REFERENCE_CAP];
static unsigned g_weaponReferenceAge=0;
static volatile int g_weaponProfile=0;
static volatile int g_weaponSnapshotValid=0,g_weaponSnapshotEnabled=0;
static volatile int g_lastHipWeaponValid=0;
static float g_weaponSnapshotRight,g_weaponSnapshotForward,g_weaponSnapshotUp;
static float g_weaponSnapshotOffsetRight,g_weaponSnapshotOffsetForward,g_weaponSnapshotOffsetUp;
static float g_lastHipWeaponRight,g_lastHipWeaponForward,g_lastHipWeaponUp;
static volatile int g_weaponSnapshotLoaded=0;
static volatile int g_adsNativeFrames=0;
static ShVec3 g_filteredHead;
static volatile int g_filteredValid = 0;
static volatile int g_vehicleClass = 0;
static volatile float g_yaw = 0.0f;
static volatile float g_pitch = 0.0f;
static volatile float g_roll = 0.0f;
static volatile float g_fov = 0.0f;
static volatile float g_skewX = 0.0f;
static volatile float g_skewY = 0.0f;
static volatile int g_modeSet = 0;
static volatile LONG g_swimmingFirstPerson = 0;
static volatile uint32_t g_apply = 0;

static uint8_t *g_camStub = NULL;
static uint8_t  g_thunkOrig[5];

static uint8_t *g_mgrStub = NULL;
static int      g_mgrHooked = 0;

extern void ShSetError(int err);
extern void ShVisibilityPump(void);
extern int ShGunrootCached(ShVec3 *out);
extern uint64_t ShGunrootKeyCached(void);

static void WeaponSnapshotPath(char *out,size_t cap){char *slash;GetModuleFileNameA(NULL,out,(DWORD)cap);slash=strrchr(out,'\\');if(slash)slash[1]=0;strncat(out,SH_SUITE_CONFIG_REL,cap-strlen(out)-1);}
static float SnapshotFloat(const char*path,const char*key){char v[64];GetPrivateProfileStringA("ManualWeaponAlignment",key,"0",v,sizeof(v),path);return(float)strtod(v,NULL);}
#define DEFAULT_WEAPON_RIGHT   (-0.0781079652f)
#define DEFAULT_WEAPON_FORWARD ( 0.0787448280f)
#define DEFAULT_WEAPON_UP      ( 0.0727031296f)
static void WeaponSnapshotUseDefault(void){g_weaponSnapshotRight=DEFAULT_WEAPON_RIGHT;g_weaponSnapshotForward=DEFAULT_WEAPON_FORWARD;g_weaponSnapshotUp=DEFAULT_WEAPON_UP;g_weaponSnapshotOffsetRight=0;g_weaponSnapshotOffsetForward=0;g_weaponSnapshotOffsetUp=0;g_weaponSnapshotValid=1;g_weaponSnapshotEnabled=1;}
static void WeaponSnapshotLoad(void){char path[MAX_PATH],enabled[16];if(g_weaponSnapshotLoaded)return;g_weaponSnapshotLoaded=1;WeaponSnapshotPath(path,sizeof(path));GetPrivateProfileStringA("ManualWeaponAlignment","Enabled","",enabled,sizeof(enabled),path);if(!enabled[0]){WeaponSnapshotUseDefault();return;}if(!atoi(enabled))return;g_weaponSnapshotRight=SnapshotFloat(path,"Right");g_weaponSnapshotForward=SnapshotFloat(path,"Forward");g_weaponSnapshotUp=SnapshotFloat(path,"Up");g_weaponSnapshotOffsetRight=SnapshotFloat(path,"OffsetRight");g_weaponSnapshotOffsetForward=SnapshotFloat(path,"OffsetForward");g_weaponSnapshotOffsetUp=SnapshotFloat(path,"OffsetUp");g_weaponSnapshotValid=1;g_weaponSnapshotEnabled=1;}
static void PutSnapshotFloat(const char*path,const char*key,float value){char v[64];snprintf(v,sizeof(v),"%.9g",value);WritePrivateProfileStringA("ManualWeaponAlignment",key,v,path);}
static void WeaponSnapshotSave(void){char path[MAX_PATH];WeaponSnapshotPath(path,sizeof(path));PutSnapshotFloat(path,"Right",g_weaponSnapshotRight);PutSnapshotFloat(path,"Forward",g_weaponSnapshotForward);PutSnapshotFloat(path,"Up",g_weaponSnapshotUp);PutSnapshotFloat(path,"OffsetRight",g_weaponSnapshotOffsetRight);PutSnapshotFloat(path,"OffsetForward",g_weaponSnapshotOffsetForward);PutSnapshotFloat(path,"OffsetUp",g_weaponSnapshotOffsetUp);WritePrivateProfileStringA("ManualWeaponAlignment","Enabled","1",path);}
extern void ShTransformPump(void);
extern void ShDominoPump(void);
extern void ShHeadPump(void);
extern void ShHeadWant(void);
extern int ShFovSet(float radians);
extern void ShFovClear(void);
extern int ShHeadCached(ShVec3 *out);
extern int ShReadableAddr(uint64_t addr, size_t len);
extern void *ShAllocNear(uint64_t target);



/* Row 3 is the translation, rows 0 to 2 the basis: row 0
 * right, row 1 forward, row 2 up. Engine owns the basis.
 */
/* The engine consumes this with no checks of its own, so a
 * NaN or a runaway value crashes it far from here. Refuse
 * the write and keep the engine's frame instead. */
static void WritePos(float *m, float x, float y, float z) {
    if (x != x || y != y || z != z) return;
    if (fabsf(x) > 1e6f || fabsf(y) > 1e6f || fabsf(z) > 1e6f)
        return;
    m[12] = x;
    m[13] = y;
    m[14] = z;
    m[15] = 1.0f;
    g_writes++;
}

/* Derived from the player and the engine's own basis, never
 * from the slot we write, so nothing can accumulate.
 */
static void ApplyOrbit(float *m) {
    ShVec3 p;

    if (!ShGetPlayerPosition(&p)) return;
    WritePos(m,
             p.x - m[4] * g_back,
             p.y - m[5] * g_back,
             p.z - m[6] * g_back + g_up);
}

/* Game basis: x right, y forward, z up. Rebuilt absolutely
 * from yaw and pitch, so roll is dropped.
 */
static void WriteRot(float *m) {
    float cy = cosf(g_yaw), sy = sinf(g_yaw);
    float cp = cosf(g_pitch), sp = sinf(g_pitch);
    float r[3], f[3], u[3];
    float cr, sr, i;

    r[0] = cy;       r[1] = -sy;      r[2] = 0.0f;
    f[0] = sy * cp;  f[1] = cy * cp;  f[2] = sp;
    u[0] = -sy * sp; u[1] = -cy * sp; u[2] = cp;

    /* Roll turns right and up about the forward axis, so
     * the view tilts without changing where it looks.
     */
    if (g_roll != 0.0f) {
        cr = cosf(g_roll);
        sr = sinf(g_roll);
        for (i = 0; i < 3; i += 1.0f) {
            int k = (int)i;
            float rr = r[k] * cr + u[k] * sr;
            float uu = -r[k] * sr + u[k] * cr;
            r[k] = rr;
            u[k] = uu;
        }
    }

    m[0] = r[0]; m[1] = r[1]; m[2] = r[2];
    m[4] = f[0]; m[5] = f[1]; m[6] = f[2];
    m[8] = u[0]; m[9] = u[1]; m[10] = u[2];
}

/* Iron sight ADS pulls the engine's hidden aim camera in
 * next to the head. Free roam keeps it metres back, which
 * is what separates the two cases below. */
#define CAM_ADS_NEAR 1.0f
#define CAM_ADS_FAR  1.7f

/* Iron sights run their ray through the eye. Over the
 * shoulder aim keeps it a fist or more to the right, and
 * that one should feel like hip fire, so it stays put. */
#define CAM_ADS_PMIN 0.12f
#define CAM_ADS_PMAX 0.22f

/* Firejumper93's gates. An engine camera beyond a chase
 * arm of the head is a drone or a remote view, and a
 * zoomed one is a scope drawing its own overlay. */
#define CAM_ARM_MAX  4.0f
#define CAM_ZOOM_FOV 0.30f

/* Vehicles run longer chase arms, so the head pump feeds
 * this hint on its own slow cadence. The frame path must
 * stay call free: player lookups here crashed the menu. */
static volatile int g_vehHint = 0;

void ShCameraVehicleHint(int inVehicle) {
    g_vehHint = inVehicle;
}

/* First person. The eye is the head bone, nudged forward
 * along the engine's own view axis to clear the face.
 */
static void ApplyHead(float *m, float fov) {
    ShVec3 h,gun;
    float fx = m[4], fy = m[5], fz = m[6];
    float ex = m[12], ey = m[13], ez = m[14];
    float gx = fx, gy = fy, gl, len;
    float px, py, pz, d;

    if (!ShHeadCached(&h)) return;

    if(g_headSmoothXY>0.0f||g_headSmoothZ>0.0f){
        float dx=h.x-g_filteredHead.x,dy=h.y-g_filteredHead.y,dz=h.z-g_filteredHead.z;
        float d2=dx*dx+dy*dy+dz*dz,alphaXY=1.0f-g_headSmoothXY,alphaZ=1.0f-g_headSmoothZ;
        if(!g_filteredValid||d2>4.0f){g_filteredHead=h;g_filteredValid=1;}
        else{g_filteredHead.x+=dx*alphaXY;g_filteredHead.y+=dy*alphaXY;g_filteredHead.z+=dz*alphaZ;}
        h=g_filteredHead;
    }else{g_filteredHead=h;g_filteredValid=1;}

    /* A zoomed camera is a scope. The mask and reticle
     * anchor to the engine's own view, so moving the eye
     * displaces them. Skip and let it render. */
    if (fov < CAM_ZOOM_FOV) return;

    d = sqrtf((ex - h.x) * (ex - h.x) + (ey - h.y) * (ey - h.y)
              + (ez - h.z) * (ez - h.z));
    g_vehicleClass=g_vehHint?(d>12.0f?2:1):0;

    /* On foot, an engine camera beyond a chase arm is not
     * looking through the soldier: a drone, a cutscene, a
     * tacmap. Vehicles keep longer arms, so they pass. */
    if (d > CAM_ARM_MAX && !g_vehHint) return;

    /* Flattened: nudging along a downward view would drop
     * the eye to the chest.
     */
    gl = sqrtf(gx * gx + gy * gy);
    if (gl > 0.01f) { gx /= gl; gy /= gl; }
    else { gx = 0.0f; gy = 1.0f; }
    px = h.x + gx * g_back + m[0] * g_right;
    py = h.y + gy * g_back + m[1] * g_right;
    pz = h.z + g_up       + m[2] * g_right;

    /* ADS only: ease the eye the few cm onto the engine's
     * aim ray, at head depth, so the sights and the
     * bullets pass through screen center. */
    len = sqrtf(fx * fx + fy * fy + fz * fz);
    /* A seated vehicle camera is naturally close to the head as well, but it
     * is not Native ADS. Letting the sight-ray alignment run here gives it a
     * weight of one and cancels every Ground/Water/Air position offset. */
    if (!g_vehHint && d < CAM_ADS_FAR && len > 0.01f) {
        float t, w, perp;

        fx /= len; fy /= len; fz /= len;
        t = (h.x - ex) * fx + (h.y - ey) * fy + (h.z - ez) * fz;

        /* How far the aim ray misses the head. Small is a
         * sight line, large is the shoulder camera.
         */
        perp = d * d - t * t;
        perp = (perp > 0.0f) ? sqrtf(perp) : 0.0f;

        if (perp < CAM_ADS_PMAX) {
            w = (CAM_ADS_FAR - d) / (CAM_ADS_FAR - CAM_ADS_NEAR);
            if (w > 1.0f) w = 1.0f;
            if (perp > CAM_ADS_PMIN)
                w *= (CAM_ADS_PMAX - perp)
                   / (CAM_ADS_PMAX - CAM_ADS_PMIN);
            t += g_back;
            px += (ex + fx * t - px) * w;
            py += (ey + fy * t - py) * w;
            pz += (ez + fz * t - pz) * w;
        }
    }
    WeaponSnapshotLoad();
    if((g_weaponStabRight>0.0f||g_weaponStabForward>0.0f||g_weaponStabUp>0.0f)&&ShGunrootCached(&gun)){
        float dx=px-gun.x,dy=py-gun.y,dz=pz-gun.z;
        float cr=dx*m[0]+dy*m[1]+dz*m[2];
        float cf=dx*m[4]+dy*m[5]+dz*m[6];
        float cu=dx*m[8]+dy*m[9]+dz*m[10];
        uint64_t key=ShGunrootKeyCached();
        WeaponReference *ref=NULL,*oldest=&g_weaponReferences[0];
        float targetRight,targetForward,targetUp;
        unsigned i;
        if(g_weaponProfile){
            g_lastHipWeaponRight=cr;g_lastHipWeaponForward=cf;g_lastHipWeaponUp=cu;
            InterlockedExchange((volatile LONG*)&g_lastHipWeaponValid,1);
        }
        if(!key)key=1; /* fallback for a missing native rig identity */
        for(i=0;i<WEAPON_REFERENCE_CAP;i++){
            WeaponReference *it=&g_weaponReferences[i];
            if(it->valid&&it->key==key){ref=it;break;}
            if(!it->valid){oldest=it;break;}
            if(it->age<oldest->age)oldest=it;
        }
        if(!ref){
            ref=oldest;ref->key=key;ref->profile=g_weaponProfile?1:0;
            ref->right=cr;ref->forward=cf;ref->up=cu;ref->valid=1;
        }else if(g_weaponProfile&&!ref->profile){
            /* Hip is the chosen master position. Share it with Default so
             * changing aim or stance cannot shift the weapon on screen. */
            ref->right=cr;ref->forward=cf;ref->up=cu;ref->profile=1;
        }
        ref->age=++g_weaponReferenceAge;
        if(g_weaponSnapshotEnabled&&g_weaponSnapshotValid){
            targetRight=g_weaponSnapshotRight+g_weaponSnapshotOffsetRight;targetForward=g_weaponSnapshotForward+g_weaponSnapshotOffsetForward;targetUp=g_weaponSnapshotUp+g_weaponSnapshotOffsetUp;
        }else{
            targetRight=ref->right;targetForward=ref->forward;targetUp=ref->up;
        }
        {
         /* Keep Wildlands' native body -> hands -> weapon attachment chain
          * completely untouched.  To hold the weapon at its learned screen
          * position, move the eye by the inverse camera-to-gun error instead
          * of fighting the animation system with a second weapon pose writer.
          * This is why the native third-person presentation stays coherent. */
         float sr=(targetRight-cr)*g_weaponStabRight;
         float sf=(targetForward-cf)*g_weaponStabForward;
         float su=(targetUp-cu)*g_weaponStabUp;
         px+=m[0]*sr+m[4]*sf+m[8]*su;
         py+=m[1]*sr+m[5]*sf+m[9]*su;
         pz+=m[2]*sr+m[6]*sf+m[10]*su;
         ((void)0);
        }
    }else ((void)0);
    WritePos(m, px, py, pz);
}

/* Roll around the engine's current forward basis. This is deliberately
 * additive: yaw and pitch remain native and therefore keep ADS exact. */
static void ApplyLean(float *m) {
    float roll=g_leanRoll,cr,sr,r0,r1,r2,u0,u1,u2;
    if(fabsf(roll)<0.00001f)return;
    cr=cosf(roll);sr=sinf(roll);
    r0=m[0];r1=m[1];r2=m[2];u0=m[8];u1=m[9];u2=m[10];
    m[0]=r0*cr+u0*sr;m[1]=r1*cr+u1*sr;m[2]=r2*cr+u2*sr;
    m[8]=-r0*sr+u0*cr;m[9]=-r1*sr+u1*cr;m[10]=-r2*sr+u2*cr;
}

/* Each field is written only if its bit is set, so the
 * engine keeps ownership of everything else.
 */
static void ApplyPose(float *m, float fov) {
    if (g_apply & CAM_ADS_HANDOFF) {
        ShVec3 h;
        /* During native ADS ownership, pass the real sight camera through.
         * If the engine exposes its shoulder camera for transition frames,
         * cover those frames immediately with the last first-person profile.
         * This runs in the render-camera hook and therefore does not inherit
         * the hotmod's 60 ms polling gap. */
        if (ShHeadCached(&h)) {
            float dx=m[12]-h.x,dy=m[13]-h.y,dz=m[14]-h.z;
            float d=sqrtf(dx*dx+dy*dy+dz*dz);
            float fl=sqrtf(m[4]*m[4]+m[5]*m[5]+m[6]*m[6]);
            float perp=d;
            if(fl>0.01f){
                float t=(h.x-m[12])*(m[4]/fl)
                       +(h.y-m[13])*(m[5]/fl)
                       +(h.z-m[14])*(m[6]/fl);
                float p2=d*d-t*t;
                perp=p2>0.0f?sqrtf(p2):0.0f;
            }
            /* Distance alone is insufficient: the transient shoulder
             * camera can already be inside CAM_ADS_FAR. Native ADS is
             * both close to the head and aimed through the sight line. */
            if (d < CAM_ADS_FAR && perp < CAM_ADS_PMAX) {
                if (g_adsNativeFrames < 6) g_adsNativeFrames++;
            } else {
                g_adsNativeFrames = 0;
            }
            if ((d >= CAM_ADS_FAR || perp >= CAM_ADS_PMAX
                 || g_adsNativeFrames < 6)
                && fov >= CAM_ZOOM_FOV)
                ApplyHead(m,fov);
        }
        return;
    }
    if (g_apply & SH_CAM_ROT) WriteRot(m);
    else if (g_apply & CAM_LEAN_BIT) ApplyLean(m);
    if (g_apply & CAM_HEAD_BIT) ApplyHead(m, fov);
    else if (g_apply & CAM_ORBIT_BIT) ApplyOrbit(m);
    else if (g_apply & SH_CAM_POS)
        WritePos(m, g_absPos.x, g_absPos.y, g_absPos.z);
}

/* Skew and mode belong to the render camera, so they stay
 * on the camera build. Position, rotation and fov are all
 * taken further up, at their own source. */
static void ApplyFields(uint64_t cam) {
    if (g_apply & SH_CAM_SKEW) {
        *(float *)(uintptr_t)(cam + CAM_SKEWX) = g_skewX;
        *(float *)(uintptr_t)(cam + CAM_SKEWY) = g_skewY;
    }
    if (g_apply & SH_CAM_MODE)
        *(int *)(uintptr_t)(cam + CAM_MODE) = g_modeSet;
}

/* Runs on the engine's own thread, immediately before the
 * transform is consumed, so there is no race to lose.
 */
static void __attribute__((ms_abi)) CamCallback(uint64_t rcx) {
    const float *f;
    int mode, ui;

    if (!rcx || !ShReadableAddr(rcx, CAM_FOV + 4)) return;
    mode = *(const int *)(uintptr_t)(rcx + CAM_MODE);
    if (mode != 0) {
        /* Vehicle views use a nonzero render-camera mode. Refresh the seated
         * player head and the cached vehicle hint before deciding whether to
         * accept that camera; doing this after the early return meant the
         * hint could remain stuck in its previous on-foot state forever. */
        if (g_apply & (CAM_HEAD_BIT|CAM_ADS_HANDOFF)) ShHeadWant();
        ShHeadPump();
        g_otherMode = mode;
        g_otherAt = g_calls;
        /* Special gameplay cameras remain native by default. Swimming is
         * the one opt-in exception: the validated locomotion state lets a
         * First Person mod follow both normal and fast swim cameras without
         * touching drones, menus, cinematics, or vehicle views. */
        if (!InterlockedCompareExchange(&g_swimmingFirstPerson,0,0)
            && !g_vehHint) return;
    }

    g_cam = rcx;
    g_calls++;

    f = (const float *)(uintptr_t)(rcx + CAM_POSE);
    ui = f[0] == 1.0f && f[1] == 0.0f && f[2] == 0.0f &&
         f[4] == 0.0f && f[5] == 1.0f && f[6] == 0.0f &&
         f[8] == 0.0f && f[9] == 0.0f && f[10] == 1.0f;
    if (ui) g_uiAt = g_calls;

    /* The engine stamps visibility back here, so a held
     * override is reapplied in the same window.
     */
    ShVisibilityPump();
    ShTransformPump();
    ShDominoPump();

    /* The menu camera never takes the head, so its frames
     * do no head work at all. The pump keeps its state and
     * resumes on the first world frame. */
    if (!ui) {
        if (mode == 0) {
            if (g_apply & (CAM_HEAD_BIT|CAM_ADS_HANDOFF)) ShHeadWant();
            ShHeadPump();
        }
        /* Active swimming is built from a dedicated nonzero-mode render
         * camera. The ordinary manager hook still receives the on-foot
         * manager and therefore cannot move the camera that is ultimately
         * displayed. While the validated swimming opt-in is active, apply
         * the owned First Person pose to this final render camera itself. */
        if (mode != 0
            && (InterlockedCompareExchange(&g_swimmingFirstPerson,0,0)
                || g_vehHint)
            && g_apply)
            ApplyPose((float *)(uintptr_t)(rcx + CAM_POSE),
                      *(const float *)(uintptr_t)(rcx + CAM_FOV));
        /* Vehicle cameras carry a world-matrix override at +0x2A0. The
         * projection builder copies that matrix over Camera+0 after this
         * callback, which otherwise erases the First Person vehicle offset.
         * Apply the same absolute head-anchored pose to the authoritative
         * override only while the occupied-vehicle hint is active. */
        if (g_vehHint) {
            uint64_t override = *(const uint64_t *)(uintptr_t)
                                (rcx + CAM_WORLD_OVERRIDE);
            if (override && ShReadableAddr(override, 0x40))
                ApplyPose((float *)(uintptr_t)override,
                          *(const float *)(uintptr_t)(rcx + CAM_FOV));
        }
        if (g_apply) ApplyFields(rcx);
    }
}

/* The manager's own transform, before any consumer reads
 * it. Rows match CAM_POSE: 0 right, 1 forward, 2 up, 3 the
 * translation. RAX holds the manager at the patch site. */
static void __attribute__((ms_abi)) MgrCallback(uint64_t cm) {
    float *m, *p;

    if (!cm || !g_apply) return;
    if (!ShReadableAddr(cm + MGR_XFORM, 0x40)) return;
    /* Same identity basis test ShInPauseMenu reports on. */
    if (g_uiAt != 0 && g_calls - g_uiAt <= 4) return;

    m = (float *)(uintptr_t)(cm + MGR_XFORM);
    p = (float *)(uintptr_t)(cm + MGR_POS);

    ApplyPose(m, *(const float *)(uintptr_t)(cm + MGR_FOV));

    /* The engine fills the position vector from a second
     * call, so the row above is restated here rather than
     * left to disagree with it. */
    p[0] = m[12];
    p[1] = m[13];
    p[2] = m[14];
    p[3] = 0.0f;
    g_writes++;
}

/* The site keeps its call opcode, so the stub is entered
 * with the return address already pushed and tail jumps to
 * the original target, which returns past the site. */
/* The site's frame sits 8 off a 16 byte boundary, which
 * faults the callback's movaps spills. Align through rbp
 * rather than assume, then restore the exact rsp. */
static int BuildMgrStub(void) {
    uint8_t *s = (uint8_t *)ShAllocNear(MGR_SITE);
    int64_t rel;
    int o = 0;

    if (!s) return 0;
    memset(s, 0xCC, 0x1000);

    s[o++] = 0x55;
    s[o++] = 0x48; s[o++] = 0x89; s[o++] = 0xE5;
    s[o++] = 0x48; s[o++] = 0x83; s[o++] = 0xE4; s[o++] = 0xF0;
    s[o++] = 0x48; s[o++] = 0x83; s[o++] = 0xEC; s[o++] = 0x20;
    s[o++] = 0x48; s[o++] = 0x89; s[o++] = 0xC1;
    s[o++] = 0x48; s[o++] = 0xB8;
    *(uint64_t *)(s + o) = (uint64_t)(uintptr_t)MgrCallback;
    o += 8;
    s[o++] = 0xFF; s[o++] = 0xD0;
    s[o++] = 0x48; s[o++] = 0x89; s[o++] = 0xEC;
    s[o++] = 0x5D;

    rel = (int64_t)MGR_NEXT - ((int64_t)(uintptr_t)(s + o) + 5);
    if (rel > 0x7FFFFFFFLL || rel < -0x7FFFFFFFLL) return 0;
    s[o++] = 0xE9;
    *(int32_t *)(s + o) = (int32_t)rel;

    g_mgrStub = s;
    return 1;
}

/* Refuse anything but the call we decoded, so a build we do
 * not know keeps its own camera. */
static int MgrInstall(void) {
    uint8_t *at = (uint8_t *)(uintptr_t)MGR_SITE;
    int64_t rel;
    DWORD old;

    if (g_mgrHooked) return 1;
    if (!ShReadableAddr(MGR_SITE, MGR_LEN)) return 0;
    if (at[0] != 0xE8) return 0;
    if ((uint64_t)((int64_t)MGR_SITE + MGR_LEN
                   + *(int32_t *)(at + 1)) != MGR_NEXT)
        return 0;
    if (!BuildMgrStub()) return 0;

    rel = (int64_t)(uintptr_t)g_mgrStub - ((int64_t)MGR_SITE + 5);
    if (rel > 0x7FFFFFFFLL || rel < -0x7FFFFFFFLL) return 0;
    if (!VirtualProtect(at, MGR_LEN, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    *(int32_t *)(at + 1) = (int32_t)rel;
    VirtualProtect(at, MGR_LEN, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, MGR_LEN);
    g_mgrHooked = 1;
    return 1;
}

static int BuildStub(void) {
    static const uint8_t SAVE[] = {
        0x48,0x81,0xEC,0xC8,0x00,0x00,0x00,
        0x48,0x89,0x4C,0x24,0x20,
        0x48,0x89,0x54,0x24,0x28,
        0x4C,0x89,0x44,0x24,0x30,
        0x4C,0x89,0x4C,0x24,0x38,
        0x4C,0x89,0x54,0x24,0x40,
        0x4C,0x89,0x5C,0x24,0x48,
        0x48,0x89,0x44,0x24,0x50,
        0x0F,0x11,0x44,0x24,0x60,
        0x0F,0x11,0x4C,0x24,0x70,
        0x0F,0x11,0x94,0x24,0x80,0x00,0x00,0x00,
        0x0F,0x11,0x9C,0x24,0x90,0x00,0x00,0x00,
        0x0F,0x11,0xA4,0x24,0xA0,0x00,0x00,0x00,
        0x0F,0x11,0xAC,0x24,0xB0,0x00,0x00,0x00,
        0x48,0x8B,0x4C,0x24,0x20
    };
    static const uint8_t REST[] = {
        0x0F,0x10,0xAC,0x24,0xB0,0x00,0x00,0x00,
        0x0F,0x10,0xA4,0x24,0xA0,0x00,0x00,0x00,
        0x0F,0x10,0x9C,0x24,0x90,0x00,0x00,0x00,
        0x0F,0x10,0x94,0x24,0x80,0x00,0x00,0x00,
        0x0F,0x10,0x4C,0x24,0x70,
        0x0F,0x10,0x44,0x24,0x60,
        0x48,0x8B,0x44,0x24,0x50,
        0x4C,0x8B,0x5C,0x24,0x48,
        0x4C,0x8B,0x54,0x24,0x40,
        0x4C,0x8B,0x4C,0x24,0x38,
        0x4C,0x8B,0x44,0x24,0x30,
        0x48,0x8B,0x54,0x24,0x28,
        0x48,0x8B,0x4C,0x24,0x20,
        0x48,0x81,0xC4,0xC8,0x00,0x00,0x00
    };
    uint8_t *s = (uint8_t *)ShAllocNear(CAM_THUNK);
    int o = 0;

    if (!s) return 0;
    memset(s, 0xCC, 0x1000);

    memcpy(s + o, SAVE, sizeof(SAVE)); o += (int)sizeof(SAVE);
    s[o++] = 0x48; s[o++] = 0xB8;
    *(uint64_t *)(s + o) = (uint64_t)(uintptr_t)CamCallback; o += 8;
    s[o++] = 0xFF; s[o++] = 0xD0;
    memcpy(s + o, REST, sizeof(REST)); o += (int)sizeof(REST);
    s[o++] = 0x48; s[o++] = 0xB8;
    *(uint64_t *)(s + o) = CAM_IMPL; o += 8;
    s[o++] = 0xFF; s[o++] = 0xE0;

    g_camStub = s;
    return 1;
}

/* A 5 byte jmp in a 16 byte slot, so the hook is a rel32
 * rewrite with nothing displaced or relocated.
 */
static int PatchThunk(void) {
    uint8_t *t = (uint8_t *)(uintptr_t)CAM_THUNK;
    int64_t rel;
    DWORD old;

    if (!g_camStub) return 0;
    rel = (int64_t)(uintptr_t)g_camStub - ((int64_t)CAM_THUNK + 5);
    if (rel > 0x7FFFFFFFLL || rel < -0x7FFFFFFFLL) return 0;
    if (!VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(g_thunkOrig, t, 5);
    *(int32_t *)(t + 1) = (int32_t)rel;
    VirtualProtect(t, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, 5);
    return 1;
}

SH_API int ShCameraHookInstall(void) {
    uint8_t *t = (uint8_t *)(uintptr_t)CAM_THUNK;
    int64_t cur, rel;
    DWORD old;

    if (g_camStub) return 1;
    if (!ShReadableAddr(CAM_THUNK, 5)) {
        ShSetError(SH_ERR_HOOK_FAILED);
        return 0;
    }
    if (t[0] != 0xE9) {
        ShSetError(SH_ERR_HOOK_FAILED);
        return 0;
    }
    cur = (int64_t)CAM_THUNK + 5 + *(int32_t *)(t + 1);
    if ((uint64_t)cur != CAM_IMPL) {
        ShSetError(SH_ERR_HOOK_FAILED);
        return 0;
    }
    if (!BuildStub()) {
        ShSetError(SH_ERR_HOOK_FAILED);
        return 0;
    }

    if (!PatchThunk()) {
        ShSetError(SH_ERR_HOOK_FAILED);
        return 0;
    }
    if (!MgrInstall()) {
        ShSetError(SH_ERR_HOOK_FAILED);
        return 0;
    }
    (void)rel;
    (void)old;
    ShSetError(SH_OK);
    return 1;
}

/* The patch is code, so it survives a level change. Verify
 * rather than assume, and re-arm if anything restored it.
 */
static int ThunkPointsAtStub(void) {
    const uint8_t *t = (const uint8_t *)(uintptr_t)CAM_THUNK;
    int64_t tgt;

    if (!g_camStub) return 0;
    if (!ShReadableAddr(CAM_THUNK, 5) || t[0] != 0xE9) return 0;
    tgt = (int64_t)CAM_THUNK + 5 + *(const int32_t *)(t + 1);
    return (uint64_t)tgt == (uint64_t)(uintptr_t)g_camStub;
}

/* Called on the transition into Playing, so the camera is
 * available to plugins without anyone asking for it.
 */
void ShCameraOnEnterPlaying(void) {
    g_cam = 0;
    if (!g_camStub) {
        ShCameraHookInstall();
        return;
    }
    if (!ThunkPointsAtStub()) PatchThunk();
}

SH_API int ShCameraReady(void) {
    return g_camStub != NULL && g_cam != 0;
}

SH_API uint64_t ShCameraCalls(void) { return g_calls; }
SH_API uint64_t ShCameraWrites(void) { return g_writes; }

/* The last nonzero camera mode seen, and how many mode 0
 * frames ago, so a REPL probe can name the special views.
 */
SH_API int ShCameraOtherMode(uint64_t *framesAgo) {
    if (framesAgo) *framesAgo = g_calls - g_otherAt;
    return g_otherMode;
}

/** True while the pause menu's camera is rendering. */
SH_API int ShInPauseMenu(void) {
    return g_uiAt != 0 && g_calls - g_uiAt <= 4;
}

SH_API int ShGetCamera(ShCamera *out) {
    uint64_t cam = g_cam;
    const float *m;

    if (!out) { ShSetError(SH_ERR_BAD_ARG); return 0; }
    if (!cam || !ShReadableAddr(cam, CAM_FOV + 4)) {
        ShSetError(SH_ERR_NO_CANDIDATE);
        return 0;
    }
    m = (const float *)(uintptr_t)(cam + CAM_POSE);
    memset(out, 0, sizeof(*out));
    out->camera = cam;
    out->right.x = m[0];   out->right.y = m[1];   out->right.z = m[2];
    out->forward.x = m[4]; out->forward.y = m[5]; out->forward.z = m[6];
    out->up.x = m[8];      out->up.y = m[9];      out->up.z = m[10];
    out->pos.x = m[12];    out->pos.y = m[13];    out->pos.z = m[14];
    out->fov = *(const float *)(uintptr_t)(cam + CAM_FOV);
    out->mode = *(const int *)(uintptr_t)(cam + CAM_MODE);
    ShSetError(SH_OK);
    return 1;
}

SH_API int ShSetCamera(const ShVec3 *pos) {
    if (!pos) { ShSetError(SH_ERR_BAD_ARG); return 0; }
    if (!ShCameraHookInstall()) return 0;
    g_absPos = *pos;
    g_apply = (g_apply & ~CAM_DERIVED) | SH_CAM_POS;
    ShSetError(SH_OK);
    return 1;
}

/* Metres behind the player along the camera's forward axis,
 * and height above. It follows the player because the
 * engine still owns the basis and the position. */
SH_API int ShCameraOrbit(float back, float up) {
    if (!ShCameraHookInstall()) return 0;
    g_back = back;
    g_right = 0.0f;
    g_up = up;
    g_apply = (g_apply & ~CAM_HEAD_BIT) | SH_CAM_POS | CAM_ORBIT_BIT;
    ShSetError(SH_OK);
    return 1;
}

extern void ShHeadOnEnterPlaying(void);
extern void ShVisibilityOnWorldReset(void);
SH_API int ShCameraResetRuntime(void) {
    g_cam=0;
    g_adsNativeFrames=0;
    g_filteredValid=0;
    g_weaponProfile=0;
    g_weaponReferenceAge=0;
    g_lastHipWeaponValid=0;
    memset(g_weaponReferences,0,sizeof(g_weaponReferences));
    ShVisibilityOnWorldReset();
    ShHeadOnEnterPlaying();
    ShHeadWant();
    if(!g_camStub)return ShCameraHookInstall();
    if(!ThunkPointsAtStub())PatchThunk();
    ShSetError(SH_OK);
    return 1;
}

SH_API int ShCameraOrbitAdvanced(float back, float right, float up) {
    if (!ShCameraHookInstall()) return 0;
    g_back = back;
    g_right = right;
    g_up = up;
    g_apply = (g_apply & ~CAM_HEAD_BIT) | SH_CAM_POS | CAM_ORBIT_BIT;
    ShSetError(SH_OK);
    return 1;
}

/* First person: the eye tracks the head bone every frame
 * and eases onto the aim ray during ADS, so sights stay
 * centered. forward clears the face. */
SH_API int ShCameraFirstPerson(float forward, float up) {
    if (!ShCameraHookInstall()) return 0;
    ShHeadWant();
    g_back = forward;
    g_up = up;
    g_right = 0.0f;
    g_leanRoll = 0.0f;
    g_headSmoothXY = g_headSmoothZ = 0.0f;
    g_adsNativeFrames = 0;
    g_apply = (g_apply & ~(CAM_ORBIT_BIT|CAM_LEAN_BIT|CAM_ADS_HANDOFF)) | SH_CAM_POS | CAM_HEAD_BIT;
    ShSetError(SH_OK);
    return 1;
}

SH_API int ShCameraFirstPersonLean(float forward,float right,float up,float roll){
    if(!ShCameraHookInstall())return 0;
    ShHeadWant();
    if(right<-1.0f)right=-1.0f;if(right>1.0f)right=1.0f;
    if(roll<-0.8f)roll=-0.8f;if(roll>0.8f)roll=0.8f;
    g_back=forward;g_right=right;g_up=up;g_leanRoll=roll;
    g_adsNativeFrames=0;
    g_apply=(g_apply&~(CAM_ORBIT_BIT|CAM_ADS_HANDOFF))|SH_CAM_POS|CAM_HEAD_BIT|CAM_LEAN_BIT;
    ShSetError(SH_OK);return 1;
}

SH_API int ShCameraFirstPersonAdvanced(float forward,float right,float up,float roll,float smoothing){
    if(smoothing<0.0f)smoothing=0.0f;
    if(smoothing>0.95f)smoothing=0.95f;
    if(!ShCameraFirstPersonLean(forward,right,up,roll))return 0;
    g_headSmoothXY=g_headSmoothZ=smoothing;
    return 1;
}

SH_API int ShCameraFirstPersonProfile(float forward,float right,float up,float roll,float smoothXY,float smoothZ){
    if(smoothXY<0)smoothXY=0;if(smoothXY>0.95f)smoothXY=0.95f;
    if(smoothZ<0)smoothZ=0;if(smoothZ>0.95f)smoothZ=0.95f;
    if(!ShCameraFirstPersonLean(forward,right,up,roll))return 0;
    g_headSmoothXY=smoothXY;g_headSmoothZ=smoothZ;return 1;
}

SH_API int ShCameraFirstPersonWeaponProfile(float forward,float right,float up,float roll,float smoothXY,float smoothZ,float stabRight,float stabForward,float stabUp,int profile){
    /* ABI-safe control channel for optional manual alignment.  Keeping this
     * on the existing API avoids changing the framework export table used by
     * legacy ASI plugins. Values 2..7 are commands, never camera profiles. */
    if(profile==2){
        if(!g_lastHipWeaponValid){ShSetError(SH_ERR_NO_POSITION);return 0;}
        g_weaponSnapshotRight=g_lastHipWeaponRight;g_weaponSnapshotForward=g_lastHipWeaponForward;g_weaponSnapshotUp=g_lastHipWeaponUp;
        g_weaponSnapshotOffsetRight=g_weaponSnapshotOffsetForward=g_weaponSnapshotOffsetUp=0.0f;g_weaponSnapshotValid=1;g_weaponSnapshotEnabled=1;WeaponSnapshotSave();ShSetError(SH_OK);return 1;
    }
    if(profile==3){char path[MAX_PATH];g_weaponSnapshotEnabled=0;g_weaponSnapshotValid=0;WeaponSnapshotPath(path,sizeof(path));WritePrivateProfileStringA("ManualWeaponAlignment","Enabled","0",path);ShSetError(SH_OK);return 1;}
    if(profile==4){if(!g_weaponSnapshotValid){ShSetError(SH_ERR_NO_POSITION);return 0;}g_weaponSnapshotEnabled=1;ShSetError(SH_OK);return 1;}
    if(profile==5){g_weaponSnapshotEnabled=0;ShSetError(SH_OK);return 1;}
    if(profile==6){WeaponSnapshotLoad();if(!g_weaponSnapshotValid){ShSetError(SH_ERR_NO_POSITION);return 0;}g_weaponSnapshotOffsetForward=forward;g_weaponSnapshotOffsetRight=right;g_weaponSnapshotOffsetUp=up;g_weaponSnapshotEnabled=1;WeaponSnapshotSave();ShSetError(SH_OK);return 1;}
    if(profile==7){WeaponSnapshotUseDefault();WeaponSnapshotSave();ShSetError(SH_OK);return 1;}
    if(stabRight<0)stabRight=0;if(stabRight>1)stabRight=1;
    if(stabForward<0)stabForward=0;if(stabForward>1)stabForward=1;
    if(stabUp<0)stabUp=0;if(stabUp>1)stabUp=1;
    if(!ShCameraFirstPersonProfile(forward,right,up,roll,smoothXY,smoothZ))return 0;
    g_weaponStabRight=stabRight;g_weaponStabForward=stabForward;g_weaponStabUp=stabUp;g_weaponProfile=profile?1:0;return 1;
}

SH_API int ShCameraVehicleClass(void){return g_vehicleClass;}
SH_API int ShCameraSwimmingFirstPerson(int enabled){InterlockedExchange(&g_swimmingFirstPerson,enabled?1:0);return 1;}

/* Position and aim both ours. Angles are radians: yaw 0
 * faces +y, pitch is positive looking up.
 */
SH_API int ShCameraFree(const ShVec3 *pos, float yaw, float pitch) {
    if (!pos) { ShSetError(SH_ERR_BAD_ARG); return 0; }
    if (!ShCameraHookInstall()) return 0;
    if (pitch > 1.55f) pitch = 1.55f;
    if (pitch < -1.55f) pitch = -1.55f;
    g_absPos = *pos;
    g_yaw = yaw;
    g_pitch = pitch;
    g_apply = (g_apply & ~CAM_DERIVED) | SH_CAM_POS | SH_CAM_ROT;
    ShSetError(SH_OK);
    return 1;
}

/* Where the engine's camera is aiming right now, so a free
 * camera can start from the current view.
 */
SH_API int ShCameraAngles(float *yaw, float *pitch) {
    ShCamera c;

    if (!ShGetCamera(&c)) return 0;
    if (yaw) *yaw = atan2f(c.forward.x, c.forward.y);
    if (pitch) {
        float s = c.forward.z;
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        *pitch = asinf(s);
    }
    return 1;
}

SH_API int ShCameraApply(const ShCameraOverride *o) {
    if (!o || !o->apply) { ShSetError(SH_ERR_BAD_ARG); return 0; }
    if (!ShCameraHookInstall()) return 0;

    if (o->apply & SH_CAM_POS) g_absPos = o->pos;
    if (o->apply & SH_CAM_ROT) {
        float p = o->pitch;
        if (p > 1.55f) p = 1.55f;
        if (p < -1.55f) p = -1.55f;
        g_yaw = o->yaw;
        g_pitch = p;
        g_roll = o->roll;
    }
    if (o->apply & SH_CAM_FOV) {
        g_fov = o->fov;
        if (!ShFovSet(o->fov)) {
            ShSetError(SH_ERR_NO_CANDIDATE);
            return 0;
        }
    }
    if (o->apply & SH_CAM_SKEW) {
        g_skewX = o->skewX;
        g_skewY = o->skewY;
    }
    if (o->apply & SH_CAM_MODE) g_modeSet = o->mode;

    /* Merged, so applying fov leaves another plugin's
     * position and rotation alone.
     */
    if (o->apply & SH_CAM_POS) g_apply &= ~CAM_DERIVED;
    g_apply |= o->apply;
    ShSetError(SH_OK);
    return 1;
}

SH_API int ShCameraMatrix(int index, float *out16) {
    uint64_t cam = g_cam;
    uint64_t at;

    if (!out16 || index < 0 || index >= CAM_MAT_N) {
        ShSetError(SH_ERR_BAD_ARG);
        return 0;
    }
    if (!cam) { ShSetError(SH_ERR_NO_CANDIDATE); return 0; }
    at = cam + CAM_MATS + (uint64_t)index * 0x40;
    if (!ShReadableAddr(at, 0x40)) {
        ShSetError(SH_ERR_NO_CANDIDATE);
        return 0;
    }
    memcpy(out16, (const void *)(uintptr_t)at, 0x40);
    ShSetError(SH_OK);
    return 1;
}

SH_API void ShCameraRelease(void) {
    ((void)0);
    g_apply = 0;
    ShFovClear();
}

/* Give back only what you took, so releasing a free camera
 * leaves another plugin's fov override running.
 */
SH_API void ShCameraReleaseFields(uint32_t fields) {
    /* A normal position release is also a hard exit from an earlier ADS
     * handoff. Otherwise toggling stock Third Person while ADS is held can
     * leave the handoff watcher alive and reacquire First Person when aim is
     * released. */
    if (fields & SH_CAM_POS) fields |= CAM_DERIVED | CAM_ADS_HANDOFF;
    if(fields&SH_CAM_POS)((void)0);
    if (fields & SH_CAM_FOV) ShFovClear();
    g_apply &= ~fields;
    if(fields&SH_CAM_POS)g_filteredValid=0;
}

/* Purpose-built seamless handoff to the game's native sight camera.  Normal
 * releases (mode off, menus, drone) must not leave this watcher active. */
SH_API void ShCameraAdsHandoff(void) {
    if(!(g_apply&CAM_HEAD_BIT)){ShCameraReleaseFields(SH_CAM_POS);return;}
    ((void)0);
    g_adsNativeFrames=0;
    g_apply&=~(SH_CAM_POS|CAM_DERIVED);
    g_apply|=CAM_ADS_HANDOFF;
}

/** Which fields are currently overridden. */
SH_API uint32_t ShCameraOwned(void) {
    return g_apply & 0xFFu;
}

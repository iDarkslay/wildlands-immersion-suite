/* Wildlands Mod Framework / Immersion Suite fork.
 * Modified by iDarkslay through 2026-09-09; public release preparation 2026-09-09.
 * GPL-3.0; see LICENSE and NOTICE.md for upstream attribution and changes.
 */
/* A shared menu, one root owned by the API. Every plugin
 * registers a submenu, so sixteen addons cost sixteen rows.
 * Drawn by the engine itself through the native UI. */
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define SH_BUILD 1
#include "scripthook.h"
#include "scripthook_config.h"

#define MENUS       128
#define ITEMS       96
#define LABEL       64
#define DESCRIPTION 768
#define VISIBLE     10
#define TICK_MS     40
#define OPTS        12
#define TOOLTIP_COLS 76

/* Geometry defaults to the established 150% layout. */
static float g_scale = 1.0f;
static float g_textScale = 1.0f;
#define S(v)        ((v) * g_scale)
#define MENU_X      S(g_menuX)
#define MENU_Y      S(g_menuY)
#define MENU_W      S(720.0f)
#define PAD         S(20.0f)
#define BORDER      S(1.0f)
#define HEADER_H    S(62.0f)
#define ROW_H       S(46.0f)
#define VALUE_W     S(245.0f)
#define ACCENT_W    S(6.0f)
#define TOOL_TITLE_H S(34.0f)
#define TOOL_LINE_H S(29.0f)
#define TOOL_PAD_Y  S(14.0f)
#define FOOTER_H    S(72.0f)

/* Tactical HUD palette from the Immersion Suite visual specification. */
#define C_WINDOW    0x0B1118u
#define C_SECONDARY 0x0E1720u
#define C_BORDER    0x294354u
#define C_SEPARATOR 0x26333Du
#define C_TITLE     0xF2B51Du
#define C_ROW       0xD5DCE5u
#define C_SEL       0xFFC928u
#define C_FOOT      0xBAC5D5u
#define C_MUTED     0x929CAAu
#define C_STATUS    0xA0E6A0u
#define C_BAR       0x2B2715u

enum { IT_ACTION = 0, IT_SUB, IT_TOGGLE, IT_NUMBER, IT_LIST };

typedef struct {
    int      used;
    int      kind;
    char     label[LABEL];
    char     description[DESCRIPTION];
    uint32_t sub;
    int      value;
    float    num, lo, hi, step;
    const char *opts[OPTS];
    int      nopts;
    ShMenuFn fn;
    void    *user;
} Item;

typedef struct {
    int      used;
    char     title[LABEL];
    char     status[96];
    uint32_t parent;
    int      sel;
    int      top;
    int      count;
    Item     items[ITEMS];
} Menu;

/* What is on screen, so only differences are pushed. */
typedef struct {
    char name[LABEL];
    char value[32];
    int  shown;
    int  selected;
} RowView;

typedef struct {
    char    title[LABEL];
    char    tooltipTitle[LABEL];
    char    status[96];
    char    footer[224];
    char    tooltip[DESCRIPTION];
    int     rows;
    int     sel;
    RowView row[VISIBLE];
} View;

static Menu g_menus[MENUS];
static uint32_t g_root = 0;
static volatile uint32_t g_current = 0;
static volatile int g_open = 0;
static volatile int g_key = VK_F4;
static int g_up = 'W', g_down = 'S', g_left = 'A', g_right = 'D';
static int g_confirm = VK_RETURN, g_back = VK_ESCAPE, g_back2 = VK_BACK;
static int g_reloadDisplayKey=VK_F9;
static int g_repeatDelay = 350, g_repeatInterval = 60;
static volatile int g_greeted = 0;
static volatile int g_started = 0;
static uint32_t g_optionsMenu = 0;
static float g_menuX = 24.0f, g_menuY = 24.0f;
static int g_pendingRepeatDelay=350,g_pendingRepeatInterval=60;
static int g_pendingScale=150,g_pendingTextScale=200,g_pendingMenuX=24,g_pendingMenuY=24;
static CRITICAL_SECTION g_lock;
static volatile int g_lockReady = 0;

/* Native widget ids, valid for one UI generation. */
static struct {
    int      built, gen, shown;
    uint32_t panel, inner, header, title, version, headerLine;
    uint32_t bar, accent, footerBg, footerLine, footer;
    uint32_t tooltipBg, tooltipLine, tooltipTitle, tooltip, status;
    uint32_t separator[VISIBLE];
    uint32_t name[VISIBLE], value[VISIBLE];
    View     drawn;
} g_ui;

extern void ShSetError(int err);

static void Lock(void) { if (g_lockReady) EnterCriticalSection(&g_lock); }
static void Unlock(void) { if (g_lockReady) LeaveCriticalSection(&g_lock); }
static Menu *MenuOf(uint32_t h);
static uint32_t NewMenu(const char *title, uint32_t parent);
static Item *NewItem(Menu *m, int kind, const char *label,
                     ShMenuFn fn, void *user);
static void DropWidgets(void);

static void SetOptionNumber(Menu *m,const char *label,float value){int i;if(!m)return;for(i=0;i<m->count;i++)if(m->items[i].kind==IT_NUMBER&&!strcmp(m->items[i].label,label)){m->items[i].num=value;break;}}

static void MoveFrameworkOptionsLast(Menu *root){int i;if(!root||!g_optionsMenu)return;for(i=0;i<root->count-1;i++){if(root->items[i].kind==IT_SUB&&root->items[i].sub==g_optionsMenu){Item keep=root->items[i];memmove(&root->items[i],&root->items[i+1],(size_t)(root->count-i-1)*sizeof(Item));root->items[root->count-1]=keep;break;}}}

static void FrameworkOptionChanged(uint32_t menu, uint32_t item,
                                   int value, void *user) {
    const char *key = (const char *)user;
    (void)menu;
    (void)item;
    if (!key) return;
    if (!strcmp(key,"ValueRepeatDelayMs"))g_pendingRepeatDelay=value;
    else if (!strcmp(key,"ValueRepeatIntervalMs"))g_pendingRepeatInterval=value;
    else if (!strcmp(key,"ScalePercent"))g_pendingScale=value;
    else if (!strcmp(key,"TextScalePercent"))g_pendingTextScale=value;
    else if (!strcmp(key,"WindowX"))g_pendingMenuX=value;
    else if (!strcmp(key,"WindowY"))g_pendingMenuY=value;
}

static void ApplyFrameworkOptions(uint32_t menu,uint32_t item,int value,void*user){Menu*m;(void)item;(void)value;(void)user;g_repeatDelay=g_pendingRepeatDelay;g_repeatInterval=g_pendingRepeatInterval;g_scale=(float)g_pendingScale/150.0f;g_textScale=(float)g_pendingTextScale/100.0f;g_menuX=(float)g_pendingMenuX;g_menuY=(float)g_pendingMenuY;DropWidgets();Lock();m=MenuOf(menu);if(m)strncpy(m->status,"Selected framework settings applied live. Use Save to persist them.",sizeof(m->status)-1);Unlock();}
static void SaveFrameworkOptions(uint32_t menu,uint32_t item,int value,void*user){Menu*m;(void)item;(void)value;(void)user;ShCfgWriteInt("Menu","ValueRepeatDelayMs",g_repeatDelay);ShCfgWriteInt("Menu","ValueRepeatIntervalMs",g_repeatInterval);ShCfgWriteInt("Menu","ScalePercent",(int)(g_scale*150.0f+0.5f));ShCfgWriteInt("Menu","TextScalePercent",(int)(g_textScale*100.0f+0.5f));ShCfgWriteInt("Menu","WindowX",(int)g_menuX);ShCfgWriteInt("Menu","WindowY",(int)g_menuY);Lock();m=MenuOf(menu);if(m)strncpy(m->status,"Applied framework configuration saved to ModFramework.cfg",sizeof(m->status)-1);Unlock();}
static void ResetFrameworkPosition(uint32_t menu,uint32_t item,int value,void*user){Menu*m;(void)item;(void)value;(void)user;g_pendingMenuX=24;g_pendingMenuY=24;Lock();m=MenuOf(menu);SetOptionNumber(m,"Window X",24);SetOptionNumber(m,"Window Y",24);if(m)strncpy(m->status,"Default position selected. Press Apply, then Save to persist it.",sizeof(m->status)-1);Unlock();}

static void AddFrameworkOptions(int scale) {
    Menu *root, *opts;
    Item *it;
    g_optionsMenu=NewMenu("Mod Framework Options",g_root);
    root=MenuOf(g_root);opts=MenuOf(g_optionsMenu);
    if(!root||!opts)return;
    it=NewItem(root,IT_SUB,"Mod Framework Options",NULL,NULL);if(it)it->sub=g_optionsMenu;
    it=NewItem(opts,IT_NUMBER,"Value repeat delay ms",FrameworkOptionChanged,"ValueRepeatDelayMs");
    if(it){it->num=(float)g_pendingRepeatDelay;it->lo=100;it->hi=1500;it->step=25;strncpy(it->description,"Select a value, then use Apply. Save writes the applied value to ModFramework.cfg.",DESCRIPTION-1);}
    it=NewItem(opts,IT_NUMBER,"Value repeat interval ms",FrameworkOptionChanged,"ValueRepeatIntervalMs");
    if(it){it->num=(float)g_pendingRepeatInterval;it->lo=20;it->hi=500;it->step=10;strncpy(it->description,"Select a value, then use Apply. Save writes the applied value to ModFramework.cfg.",DESCRIPTION-1);}
    it=NewItem(opts,IT_NUMBER,"Menu scale %",FrameworkOptionChanged,"ScalePercent");
    if(it){it->num=(float)scale;it->lo=75;it->hi=250;it->step=5;strncpy(it->description,"Applied live only after pressing Apply. Save makes the applied scale persistent.",DESCRIPTION-1);}
    it=NewItem(opts,IT_NUMBER,"Text scale %",FrameworkOptionChanged,"TextScalePercent");
    if(it){it->num=(float)g_pendingTextScale;it->lo=60;it->hi=200;it->step=5;strncpy(it->description,"Changes only the menu font size. Press Apply for a live preview, then Save to persist it.",DESCRIPTION-1);}
    it=NewItem(opts,IT_NUMBER,"Window X",FrameworkOptionChanged,"WindowX");if(it){it->num=(float)g_pendingMenuX;it->lo=0;it->hi=2000;it->step=10;strncpy(it->description,"Selects horizontal window position. Press Apply for a live preview, then Save to persist it.",DESCRIPTION-1);}
    it=NewItem(opts,IT_NUMBER,"Window Y",FrameworkOptionChanged,"WindowY");if(it){it->num=(float)g_pendingMenuY;it->lo=0;it->hi=1200;it->step=10;strncpy(it->description,"Selects vertical window position. Press Apply for a live preview, then Save to persist it.",DESCRIPTION-1);}
    it=NewItem(opts,IT_ACTION,"Apply selected settings",ApplyFrameworkOptions,NULL);if(it)strncpy(it->description,"Applies repeat timing, window scale, text scale, and window position live without saving them.",DESCRIPTION-1);
    it=NewItem(opts,IT_ACTION,"Save framework configuration",SaveFrameworkOptions,NULL);if(it)strncpy(it->description,"Saves the currently applied framework settings to ModFramework.cfg.",DESCRIPTION-1);
    it=NewItem(opts,IT_ACTION,"Reset default window position",ResetFrameworkPosition,NULL);if(it)strncpy(it->description,"Selects the default top-left position (24, 24). Press Apply to preview it, then Save to make it persistent.",DESCRIPTION-1);
    it=NewItem(opts,IT_ACTION,"Keybindings: edit ModFramework.cfg",NULL,NULL);
    if(it)strncpy(it->description,"All framework menu key mappings must currently be edited in ModFramework.cfg: [Menu] OpenKey, UpKey, DownKey, LeftKey, RightKey, ConfirmKey, BackKey and AlternateBackKey; [HotMods] ReloadKey.",DESCRIPTION-1);
}

static Menu *MenuOf(uint32_t h) {
    if (h == 0 || h > MENUS) return NULL;
    if (!g_menus[h - 1].used) return NULL;
    return &g_menus[h - 1];
}

static uint32_t NewMenu(const char *title, uint32_t parent) {
    int i;

    for (i = 0; i < MENUS; i++) {
        if (g_menus[i].used) continue;
        memset(&g_menus[i], 0, sizeof(g_menus[i]));
        g_menus[i].used = 1;
        g_menus[i].parent = parent;
        if (title) {
            strncpy(g_menus[i].title, title, LABEL - 1);
            g_menus[i].title[LABEL - 1] = 0;
        }
        return (uint32_t)(i + 1);
    }
    return 0;
}

/* Free a menu and everything under it. Caller holds the
 * lock. Depth is bounded by MENUS, so recursion is safe. */
static void DropMenu(uint32_t h) {
    Menu *m = MenuOf(h);
    int i;

    if (!m) return;
    for (i = 0; i < m->count; i++)
        if (m->items[i].kind == IT_SUB && m->items[i].sub)
            DropMenu(m->items[i].sub);
    memset(m, 0, sizeof(*m));
}

static Item *NewItem(Menu *m, int kind, const char *label,
                     ShMenuFn fn, void *user) {
    Item *it;

    if (!m || m->count >= ITEMS) return NULL;
    it = &m->items[m->count++];
    memset(it, 0, sizeof(*it));
    it->used = 1;
    it->kind = kind;
    it->fn = fn;
    it->user = user;
    if (label) {
        strncpy(it->label, label, LABEL - 1);
        it->label[LABEL - 1] = 0;
    }
    return it;
}

/* Rendered text for the value side of a row. */
static void ValueText(const Item *it, char *out, int n) {
    out[0] = 0;
    if (it->kind == IT_SUB) snprintf(out, n, ">");
    else if (it->kind == IT_TOGGLE)
        snprintf(out, n, it->value ? "[on]" : "[off]");
    else if (it->kind == IT_NUMBER)
        snprintf(out, n, "< %.2f >", it->num);
    else if (it->kind == IT_LIST && it->nopts)
        snprintf(out, n, "< %s >", it->opts[it->value % it->nopts]);
}

/* Receivers run on the menu thread, so the lock is dropped
 * around the call and any API call is legal inside one.
 */
static void Fire(uint32_t menu, int idx, Item *it) {
    ShMenuFn fn = it->fn;
    void *user = it->user;
    int v = (it->kind == IT_NUMBER) ? (int)it->num : it->value;

    if (!fn) return;
    Unlock();
    fn(menu, (uint32_t)idx, v, user);
    Lock();
}

static int Pressed(int vk) {
    static unsigned char was[256];
    int d = (GetAsyncKeyState(vk) & 0x8000) != 0;
    int hit = d && !was[vk & 0xFF];
    was[vk & 0xFF] = (unsigned char)d;
    return hit;
}

/* Value controls repeat after a short hold, like a native settings menu. */
static int Repeated(int vk) {
    static unsigned char down[256];
    static DWORD next[256];
    int i = vk & 0xFF;
    int held = (GetAsyncKeyState(vk) & 0x8000) != 0;
    DWORD now = GetTickCount();
    if (!held) { down[i] = 0; next[i] = 0; return 0; }
    if (!down[i]) {
        down[i] = 1;
        next[i] = now + (DWORD)g_repeatDelay;
        return 1;
    }
    if ((LONG)(now - next[i]) >= 0) {
        next[i] = now + (DWORD)g_repeatInterval;
        return 1;
    }
    return 0;
}

/* Selection scrolls with the cursor, so a long menu shows a
 * window of rows rather than running off the screen.
 */
static void Scroll(Menu *m) {
    if (m->sel < m->top) m->top = m->sel;
    if (m->sel >= m->top + VISIBLE) m->top = m->sel - VISIBLE + 1;
    if (m->top < 0) m->top = 0;
}

static void Navigate(void) {
    Menu *m = MenuOf(g_current);
    Item *it;
    int left, right;

    if (!m || m->count == 0) return;
    /* Navigation uses only the configured keys. Arrow keys remain free
     * for gameplay mods such as Object Mover while this menu is open. */
    if (Pressed(g_up))
        m->sel = (m->sel + m->count - 1) % m->count;
    if (Pressed(g_down))
        m->sel = (m->sel + 1) % m->count;
    Scroll(m);

    it = &m->items[m->sel];
    left = Repeated(g_left);
    right = Repeated(g_right);
    if (left || right) {
        int dir = right ? 1 : -1;
        if (it->kind == IT_NUMBER) {
            it->num += it->step * dir;
            if (it->num < it->lo) it->num = it->lo;
            if (it->num > it->hi) it->num = it->hi;
            Fire(g_current, m->sel, it);
        } else if (it->kind == IT_LIST && it->nopts) {
            it->value = (it->value + it->nopts + dir) % it->nopts;
            Fire(g_current, m->sel, it);
        }
    }
    if (Pressed(g_confirm)) {
        if (it->kind == IT_SUB && it->sub) {
            g_current = it->sub;
        } else if (it->kind == IT_TOGGLE) {
            it->value = !it->value;
            Fire(g_current, m->sel, it);
        } else {
            Fire(g_current, m->sel, it);
        }
    }
    if (Pressed(g_back) || (g_back2 != g_back && Pressed(g_back2))) {
        if (m->parent) g_current = m->parent;
        else g_open = 0;
    }
}

/* Compact names for the configurable virtual keys shown in the footer. The
 * values themselves still come from ModFramework.cfg through ShCfgKey. */
static void KeyText(int vk,char *out,int cap){
    const char *name=NULL;
    if(cap<1)return;
    if(vk>='A'&&vk<='Z'){out[0]=(char)vk;out[1]=0;return;}
    if(vk>='0'&&vk<='9'){out[0]=(char)vk;out[1]=0;return;}
    if(vk>=VK_F1&&vk<=VK_F24){snprintf(out,cap,"F%d",vk-VK_F1+1);return;}
    switch(vk){
    case VK_RETURN:name="ENTER";break;case VK_ESCAPE:name="ESC";break;
    case VK_BACK:name="BACKSPACE";break;case VK_SPACE:name="SPACE";break;
    case VK_TAB:name="TAB";break;case VK_UP:name="UP";break;
    case VK_DOWN:name="DOWN";break;case VK_LEFT:name="LEFT";break;
    case VK_RIGHT:name="RIGHT";break;case VK_PRIOR:name="PAGEUP";break;
    case VK_NEXT:name="PAGEDOWN";break;case VK_HOME:name="HOME";break;
    case VK_END:name="END";break;case VK_INSERT:name="INSERT";break;
    case VK_DELETE:name="DELETE";break;case VK_MENU:name="ALT";break;
    case VK_LMENU:name="LEFT ALT";break;case VK_RMENU:name="RIGHT ALT";break;
    case VK_CONTROL:name="CTRL";break;case VK_LCONTROL:name="LEFT CTRL";break;
    case VK_RCONTROL:name="RIGHT CTRL";break;case VK_SHIFT:name="SHIFT";break;
    case VK_LSHIFT:name="LEFT SHIFT";break;case VK_RSHIFT:name="RIGHT SHIFT";break;
    case VK_CAPITAL:name="CAPSLOCK";break;
    default:break;
    }
    if(name){strncpy(out,name,(size_t)cap-1);out[cap-1]=0;}
    else snprintf(out,cap,"VK %d",vk);
}

/* Snapshot of the current menu. Caller holds the lock. */
static void Capture(View *v) {
    Menu *m = MenuOf(g_current);
    int i;
    char up[20],down[20],confirm[20],back[20],open[20],reload[20];

    memset(v, 0, sizeof(*v));
    if (!m) return;
    strncpy(v->title, m->title, LABEL - 1);
    /* Legacy root titles may carry an informational second line. The compact
     * redesign keeps the header single-line without changing the menu data. */
    { char *line=strchr(v->title,'\n');if(line)*line=0; }
    strncpy(v->status, m->status, sizeof(v->status) - 1);
    for (i = m->top; i < m->count && i < m->top + VISIBLE; i++) {
        RowView *r = &v->row[v->rows];
        strncpy(r->name, m->items[i].label, LABEL - 1);
        ValueText(&m->items[i], r->value, sizeof(r->value));
        r->shown = 1;
        r->selected = (i == m->sel);
        if (r->selected) {
            v->sel = v->rows;
            strncpy(v->tooltipTitle,m->items[i].label,LABEL-1);
        }
        v->rows++;
    }
    KeyText(g_up,up,sizeof(up));KeyText(g_down,down,sizeof(down));
    KeyText(g_confirm,confirm,sizeof(confirm));KeyText(g_back,back,sizeof(back));
    KeyText(g_key,open,sizeof(open));KeyText(g_reloadDisplayKey,reload,sizeof(reload));
    snprintf(v->footer,sizeof(v->footer),
             "%s / %s   Navigate      %s   Select      %s   Back\n%s   Open / Close Menu      %s   Reload Mods",
             up,down,confirm,back,open,reload);
    if(m->count>0){
        const char *src=m->items[m->sel].description;char *dst=v->tooltip;int col=0,left=DESCRIPTION-1;
        while(*src&&left>0){
            const char *word=src;int len=0;
            while(word[len]&&word[len]!=' '&&word[len]!='\n')len++;
            if(col>0&&col+1+len>TOOLTIP_COLS){*dst++='\n';left--;col=0;}
            if(col>0&&left>0){*dst++=' ';left--;col++;}
            while(len--&&left>0){*dst++=*src++;left--;col++;}
            if(*src=='\n'){if(left>0){*dst++='\n';left--;col=0;}src++;}
            else while(*src==' ')src++;
        }
        *dst=0;
    }
}

static int TooltipLines(const char *s){int n;if(!s||!*s)return 0;n=1;while(*s)if(*s++=='\n')n++;return n;}

static float RowY(int i) {
    return HEADER_H + ROW_H * (float)i;
}

static float PanelHeight(const View *v) {
    int lines=TooltipLines(v->tooltip);
    float tooltipH=lines?(TOOL_PAD_Y+TOOL_TITLE_H+TOOL_LINE_H*(float)lines+TOOL_PAD_Y):0.0f;
    float gap=lines?S(14.0f):0.0f;
    return HEADER_H+ROW_H*(float)v->rows+gap+tooltipH+FOOTER_H;
}

static void DropWidgets(void) {
    if (g_ui.built && g_ui.gen == ShUiGen() && g_ui.panel)
        ShUiDestroy(g_ui.panel);
    memset(&g_ui, 0, sizeof(g_ui));
}

/* Every widget of the menu, or none of it. A single create
 * can fail when the game state flickers, and a menu missing
 * half its rows never repaired itself. */
static int Complete(void) {
    int i;

    if (!g_ui.panel||!g_ui.inner||!g_ui.header||!g_ui.title||!g_ui.version||
        !g_ui.headerLine||!g_ui.bar||!g_ui.accent||!g_ui.footerBg||
        !g_ui.footerLine||!g_ui.footer||!g_ui.tooltipBg||!g_ui.tooltipLine||
        !g_ui.tooltipTitle||!g_ui.tooltip||!g_ui.status)
        return 0;
    for (i = 0; i < VISIBLE; i++)
        if (!g_ui.name[i] || !g_ui.value[i] || !g_ui.separator[i]) return 0;
    return 1;
}

static void ScaleLabelFactor(uint32_t id,float factor) {
    float size;
    if (ShUiGetF(id, SH_P_FONTSIZE, &size) && size > 0.0f)
        ShUiSetF(id, SH_P_FONTSIZE, size*g_textScale*factor);
}

static void ScaleLabelText(uint32_t id){ScaleLabelFactor(id,1.0f);}

/* One creation per widget, hidden rows included, so later
 * updates are text and position only. */
static int BuildWidgets(void) {
    int i;

    memset(&g_ui, 0, sizeof(g_ui));
    if (!ShUiReady()) return 0;
    g_ui.gen = ShUiGen();
    /* The panel itself is the one-pixel outer border. Everything else is a
     * native solid-colour image, so this design needs no packaged textures. */
    g_ui.panel=ShUiPanel(MENU_X,MENU_Y,MENU_W,200.0f,C_BORDER,0.96f);
    if (!g_ui.panel) return 0;
    g_ui.inner=ShUiImage(g_ui.panel,BORDER,BORDER,MENU_W-2*BORDER,198.0f,C_WINDOW,0.94f);
    g_ui.header=ShUiImage(g_ui.panel,BORDER,BORDER,MENU_W-2*BORDER,HEADER_H-BORDER,C_WINDOW,0.98f);
    g_ui.headerLine=ShUiImage(g_ui.panel,BORDER,HEADER_H-BORDER,MENU_W-2*BORDER,BORDER,C_SEPARATOR,1.0f);
    g_ui.title=ShUiLabel(g_ui.panel,PAD,S(14.0f),MENU_W-S(150.0f),HEADER_H-S(16.0f)," ",C_TITLE);
    g_ui.version=ShUiLabel(g_ui.panel,MENU_W-S(112.0f),S(16.0f),S(88.0f),HEADER_H-S(18.0f),"v1.0",C_ROW);
    g_ui.bar=ShUiImage(g_ui.panel,BORDER,RowY(0),MENU_W-2*BORDER,ROW_H,C_BAR,0.96f);
    g_ui.accent=ShUiImage(g_ui.panel,BORDER,RowY(0)+S(3.0f),ACCENT_W,ROW_H-S(6.0f),C_SEL,1.0f);
    for (i = 0; i < VISIBLE; i++) {
        g_ui.separator[i]=ShUiImage(g_ui.panel,PAD,RowY(i)+ROW_H-BORDER,
                                   MENU_W-2*PAD,BORDER,C_SEPARATOR,0.9f);
        g_ui.name[i]=ShUiLabel(g_ui.panel,PAD+S(4.0f),RowY(i)+S(8.0f),
                               MENU_W-VALUE_W-PAD,ROW_H-S(8.0f)," ",C_ROW);
        g_ui.value[i]=ShUiLabel(g_ui.panel,MENU_W-PAD-VALUE_W,
                                RowY(i)+S(8.0f),VALUE_W,ROW_H-S(8.0f)," ",C_ROW);
    }
    g_ui.tooltipBg=ShUiImage(g_ui.panel,BORDER,RowY(0),MENU_W-2*BORDER,ROW_H,C_SECONDARY,0.98f);
    g_ui.tooltipLine=ShUiImage(g_ui.panel,PAD,RowY(0),MENU_W-2*PAD,BORDER,C_SEPARATOR,1.0f);
    g_ui.tooltipTitle=ShUiLabel(g_ui.panel,PAD+S(18.0f),RowY(0),MENU_W-2*PAD-S(18.0f),TOOL_TITLE_H," ",C_SEL);
    g_ui.tooltip=ShUiLabel(g_ui.panel,PAD+S(18.0f),RowY(0),MENU_W-2*PAD-S(18.0f),ROW_H," ",C_MUTED);
    g_ui.status=ShUiLabel(g_ui.panel,PAD,RowY(0),MENU_W-2*PAD,ROW_H," ",C_STATUS);
    g_ui.footerBg=ShUiImage(g_ui.panel,BORDER,RowY(0),MENU_W-2*BORDER,FOOTER_H-BORDER,C_WINDOW,0.98f);
    g_ui.footerLine=ShUiImage(g_ui.panel,BORDER,RowY(0),MENU_W-2*BORDER,BORDER,C_SEPARATOR,1.0f);
    g_ui.footer=ShUiLabel(g_ui.panel,PAD,RowY(0),MENU_W-2*PAD,FOOTER_H," ",C_FOOT);

    /* Destroying the panel takes the subtree with it, so the
     * next tick starts clean instead of leaking slots. */
    if (!Complete()) {
        ShUiDestroy(g_ui.panel);
        memset(&g_ui, 0, sizeof(g_ui));
        return 0;
    }

    ScaleLabelFactor(g_ui.title,1.18f);
    ScaleLabelFactor(g_ui.version,0.82f);
    ScaleLabelFactor(g_ui.tooltipTitle,1.02f);
    ScaleLabelFactor(g_ui.tooltip,0.88f);
    ScaleLabelFactor(g_ui.footer,0.84f);
    ScaleLabelText(g_ui.status);
    for (i = 0; i < VISIBLE; i++) {
        ScaleLabelText(g_ui.name[i]);
        ScaleLabelText(g_ui.value[i]);
    }

    for (i = 0; i < VISIBLE; i++) {
        ShUiShow(g_ui.name[i], 0);
        ShUiShow(g_ui.value[i], 0);
        ShUiShow(g_ui.separator[i],0);
    }
    ShUiShow(g_ui.bar,0);ShUiShow(g_ui.accent,0);
    ShUiShow(g_ui.tooltipBg,0);ShUiShow(g_ui.tooltipLine,0);
    ShUiShow(g_ui.tooltipTitle,0);
    ShUiShow(g_ui.tooltip,0);ShUiShow(g_ui.footerBg,0);
    ShUiShow(g_ui.footerLine,0);ShUiShow(g_ui.footer,0);
    ShUiShow(g_ui.status, 0);
    ShUiShow(g_ui.panel, 0);
    g_ui.built = 1;
    g_ui.shown = 0;
    return 1;
}

static void SetTextIf(uint32_t id, char *have, int cap,
                      const char *want) {
    if (strcmp(have, want) == 0) return;
    strncpy(have, want, cap - 1);
    have[cap - 1] = 0;
    ShUiSetText(id, want[0] ? want : " ");
}

/* Push the differences between the drawn view and v. */
static void Sync(const View *v) {
    View *d = &g_ui.drawn;
    float y,tooltipY,tooltipH,footerY,panelH;
    int lines,hasTooltip;
    int i;

    SetTextIf(g_ui.title, d->title, LABEL, v->title);
    SetTextIf(g_ui.tooltipTitle,d->tooltipTitle,LABEL,v->tooltipTitle);
    for (i = 0; i < VISIBLE; i++) {
        const RowView *r = &v->row[i];
        RowView *dr = &d->row[i];

        if (r->shown != dr->shown) {
            ShUiShow(g_ui.name[i], r->shown);
            ShUiShow(g_ui.value[i], r->shown);
            ShUiShow(g_ui.separator[i],r->shown);
            dr->shown = r->shown;
        }
        if (!r->shown) continue;
        SetTextIf(g_ui.name[i], dr->name, LABEL, r->name);
        SetTextIf(g_ui.value[i], dr->value, sizeof(dr->value), r->value);
        if (r->selected != dr->selected) {
            ShUiSetColour(g_ui.name[i], r->selected ? C_SEL : C_ROW);
            ShUiSetColour(g_ui.value[i], r->selected ? C_SEL : C_ROW);
            dr->selected = r->selected;
        }
    }
    if (v->sel != d->sel || v->rows != d->rows) {
        ShUiSetPos(g_ui.bar,BORDER,RowY(v->sel));
        ShUiSetPos(g_ui.accent,BORDER,RowY(v->sel)+S(3.0f));
        d->sel = v->sel;
    }
    if((v->rows>0)!=(d->rows>0)){
        ShUiShow(g_ui.bar,v->rows>0);
        ShUiShow(g_ui.accent,v->rows>0);
    }
    y=RowY(v->rows);
    lines=TooltipLines(v->tooltip);
    hasTooltip=lines>0;
    tooltipY=y+(hasTooltip?S(14.0f):0.0f);
    tooltipH=hasTooltip?(TOOL_PAD_Y+TOOL_TITLE_H+TOOL_LINE_H*(float)lines+TOOL_PAD_Y):0.0f;
    footerY=tooltipY+tooltipH;
    panelH=footerY+FOOTER_H;
    if (v->rows != d->rows || strcmp(v->footer, d->footer) != 0 ||
        strcmp(v->tooltipTitle,d->tooltipTitle)!=0 ||
        strcmp(v->tooltip,d->tooltip)!=0) {
        if(hasTooltip){
            ShUiSetPos(g_ui.tooltipBg,BORDER,tooltipY);
            ShUiSetSize(g_ui.tooltipBg,MENU_W-2*BORDER,tooltipH);
            ShUiSetPos(g_ui.tooltipLine,PAD,tooltipY);
            ShUiSetPos(g_ui.tooltipTitle,PAD+S(18.0f),tooltipY+TOOL_PAD_Y);
            ShUiSetPos(g_ui.tooltip,PAD+S(18.0f),tooltipY+TOOL_PAD_Y+TOOL_TITLE_H);
            ShUiSetSize(g_ui.tooltip,MENU_W-2*PAD-S(18.0f),TOOL_LINE_H*(float)lines);
        }
        ShUiSetPos(g_ui.footerBg,BORDER,footerY);
        ShUiSetPos(g_ui.footerLine,BORDER,footerY);
        ShUiSetPos(g_ui.footer,PAD,footerY+S(13.0f));
        ShUiSetSize(g_ui.inner,MENU_W-2*BORDER,panelH-2*BORDER);
        ShUiSetSize(g_ui.panel,MENU_W,panelH);
        /* ShMenuStatus remains API-compatible for existing plugins, but the
         * redesigned menu deliberately has no separate green status row. */
        ShUiShow(g_ui.status,0);
        ShUiShow(g_ui.tooltipBg,hasTooltip);ShUiShow(g_ui.tooltipLine,hasTooltip);
        ShUiShow(g_ui.tooltipTitle,hasTooltip);
        ShUiShow(g_ui.tooltip,hasTooltip);ShUiShow(g_ui.footerBg,1);
        ShUiShow(g_ui.footerLine,1);ShUiShow(g_ui.footer,1);
        SetTextIf(g_ui.footer,d->footer,sizeof(d->footer),v->footer);
        SetTextIf(g_ui.tooltip,d->tooltip,sizeof(d->tooltip),v->tooltip);
        d->status[0]=0;
        d->rows = v->rows;
    }
}

/* Keys are polled here, the engine draws the result. */
static DWORD WINAPI MenuThread(LPVOID p) {
    View v;
    int captured = -1;
    (void)p;

    for (;;) {
        Sleep(TICK_MS);

        if (Pressed(g_key)) {
            g_open = !g_open;
            if (g_open) g_current = g_root;
        }
        /* an open menu owns the keyboard, however it opened */
        if (g_open != captured) {
            captured = g_open;
            ShCaptureKeys(captured);
        }
        if (g_ui.built && g_ui.gen != ShUiGen()) DropWidgets();
        if (!g_open) {
            if (g_ui.built && g_ui.shown) {
                ShUiShow(g_ui.panel, 0);
                g_ui.shown = 0;
            }
            continue;
        }
        if (!g_ui.built && !BuildWidgets()) continue;

        Lock();
        Navigate();
        Capture(&v);
        Unlock();

        Sync(&v);
        if (!g_ui.shown) {
            ShUiShow(g_ui.panel, 1);
            g_ui.shown = 1;
        }
    }
    return 0;
}

static void EnsureMenu(void) {
    int scale, textScale;
    if (g_started) return;
    g_started = 1;
    g_key = ShCfgKey("Menu", "OpenKey", VK_F1);
    g_up = ShCfgKey("Menu", "UpKey", 'W');
    g_down = ShCfgKey("Menu", "DownKey", 'S');
    g_left = ShCfgKey("Menu", "LeftKey", 'A');
    g_right = ShCfgKey("Menu", "RightKey", 'D');
    g_confirm = ShCfgKey("Menu", "ConfirmKey", VK_RETURN);
    g_back = ShCfgKey("Menu", "BackKey", VK_ESCAPE);
    g_back2 = ShCfgKey("Menu", "AlternateBackKey", VK_BACK);
    g_reloadDisplayKey=ShCfgKey("HotMods","ReloadKey",VK_F9);
    g_repeatDelay = ShCfgInt("Menu", "ValueRepeatDelayMs", 350, 100, 1500);
    g_repeatInterval = ShCfgInt("Menu", "ValueRepeatIntervalMs", 60, 20, 500);
    scale = ShCfgInt("Menu", "ScalePercent", 150, 75, 250);
    textScale = ShCfgInt("Menu", "TextScalePercent", 200, 60, 200);
    g_menuX=(float)ShCfgInt("Menu","WindowX",24,0,2000);
    g_menuY=(float)ShCfgInt("Menu","WindowY",24,0,1200);
    g_pendingRepeatDelay=g_repeatDelay;
    g_pendingRepeatInterval=g_repeatInterval;
    g_pendingScale=scale;
    g_pendingTextScale=textScale;
    g_pendingMenuX=(int)g_menuX;
    g_pendingMenuY=(int)g_menuY;
    g_scale = (float)scale / 150.0f;
    g_textScale = (float)textScale / 100.0f;
    InitializeCriticalSection(&g_lock);
    g_lockReady = 1;
    g_root = NewMenu("Wildlands Mod Framework\na modified GRW ScriptHook fork", 0);
    AddFrameworkOptions(scale);
    CreateThread(NULL, 0, MenuThread, NULL, 0, NULL);
}

SH_API uint32_t ShMenuCreate(const char *title) {
    Menu *root;
    uint32_t h;
    Item *it;

    EnsureMenu();
    Lock();
    h = NewMenu(title, g_root);
    root = MenuOf(g_root);
    if (h && root) {
        it = NewItem(root, IT_SUB, title, NULL, NULL);
        if (it) it->sub = h;
        MoveFrameworkOptionsLast(root);
    }
    Unlock();
    ShSetError(h ? SH_OK : SH_ERR_NO_CANDIDATE);
    return h;
}

/* Drop the items but keep the row, so a plugin can rebuild
 * its own menu (a reload) without stacking duplicates. */
SH_API int ShMenuClear(uint32_t menu) {
    Menu *m;

    Lock();
    m = MenuOf(menu);
    if (m) {
        int i;
        for (i = 0; i < m->count; i++)
            if (m->items[i].kind == IT_SUB && m->items[i].sub)
                DropMenu(m->items[i].sub);
        m->count = 0;
        m->sel = 0;
    }
    Unlock();
    return m != NULL;
}

/* Remove the row itself, and its subtree with it. */
SH_API int ShMenuDestroy(uint32_t menu) {
    Menu *parent;
    int found = 0;

    if (menu == g_root) return 0;
    Lock();
    parent = MenuOf(MenuOf(menu) ? MenuOf(menu)->parent : 0);
    if (parent) {
        int i, w = 0;
        for (i = 0; i < parent->count; i++) {
            if (parent->items[i].kind == IT_SUB &&
                parent->items[i].sub == menu) {
                found = 1;
                continue;
            }
            if (w != i) parent->items[w] = parent->items[i];
            w++;
        }
        parent->count = w;
        if (parent->sel >= w) parent->sel = w ? w - 1 : 0;
    }
    DropMenu(menu);
    Unlock();
    return found;
}

SH_API uint32_t ShMenuSub(uint32_t parent, const char *label) {
    uint32_t h;
    Item *it;
    Menu *m;

    EnsureMenu();
    Lock();
    h = NewMenu(label, parent);
    m = MenuOf(parent);
    if (h && m) {
        it = NewItem(m, IT_SUB, label, NULL, NULL);
        if (it) it->sub = h;
    }
    Unlock();
    return h;
}

SH_API int ShMenuAction(uint32_t menu, const char *label,
                        ShMenuFn fn, void *user) {
    Item *it;

    Lock();
    it = NewItem(MenuOf(menu), IT_ACTION, label, fn, user);
    Unlock();
    return it != NULL;
}

SH_API int ShMenuToggle(uint32_t menu, const char *label,
                        int initial, ShMenuFn fn, void *user) {
    Item *it;

    Lock();
    it = NewItem(MenuOf(menu), IT_TOGGLE, label, fn, user);
    if (it) it->value = initial ? 1 : 0;
    Unlock();
    return it != NULL;
}

SH_API int ShMenuNumber(uint32_t menu, const char *label,
                        float initial, float lo, float hi,
                        float step, ShMenuFn fn, void *user) {
    Item *it;

    Lock();
    it = NewItem(MenuOf(menu), IT_NUMBER, label, fn, user);
    if (it) {
        it->num = initial;
        it->lo = lo;
        it->hi = hi;
        it->step = step;
    }
    Unlock();
    return it != NULL;
}

/* The option strings are borrowed, so they must outlive the
 * menu. String literals are the intended case.
 */
SH_API int ShMenuList(uint32_t menu, const char *label,
                      const char **opts, int n, int initial,
                      ShMenuFn fn, void *user) {
    Item *it;
    int i;

    if (n > OPTS) n = OPTS;
    Lock();
    it = NewItem(MenuOf(menu), IT_LIST, label, fn, user);
    if (it) {
        for (i = 0; i < n; i++) it->opts[i] = opts[i];
        it->nopts = n;
        it->value = (n > 0) ? (initial % n) : 0;
    }
    Unlock();
    return it != NULL;
}

/* Update an existing numeric row without firing its callback. This is used
 * by live inputs such as mouse-wheel movement control, where the value may
 * change while the menu is open or closed. */
SH_API int ShMenuSetNumber(uint32_t menu,const char *label,float value){
    Menu*m;int i,found=0;
    if(!label)return 0;
    Lock();m=MenuOf(menu);
    if(m){
        for(i=m->count-1;i>=0;i--){
            Item*it=&m->items[i];
            if(it->kind==IT_NUMBER&&strcmp(it->label,label)==0){
                if(value<it->lo)value=it->lo;
                if(value>it->hi)value=it->hi;
                it->num=value;found=1;break;
            }
        }
    }
    Unlock();return found;
}

/* Update a toggle row without firing its callback. */
SH_API int ShMenuSetToggle(uint32_t menu,const char *label,int value){
    Menu*m;int i,found=0;
    if(!label)return 0;
    Lock();m=MenuOf(menu);
    if(m){
        for(i=m->count-1;i>=0;i--){
            Item*it=&m->items[i];
            if(it->kind==IT_TOGGLE&&strcmp(it->label,label)==0){
                it->value=value?1:0;found=1;break;
            }
        }
    }
    Unlock();return found;
}

SH_API int ShMenuDescribe(uint32_t menu,const char *label,const char *description){Menu*m;int i,found=0;if(!label)return 0;Lock();m=MenuOf(menu);if(m){for(i=m->count-1;i>=0;i--){if(strcmp(m->items[i].label,label)==0){if(description){strncpy(m->items[i].description,description,DESCRIPTION-1);m->items[i].description[DESCRIPTION-1]=0;}else m->items[i].description[0]=0;found=1;break;}}}Unlock();return found;}

/* The line under the items, for whatever the last action
 * has to report. Empty text removes it.
 */
SH_API int ShMenuStatus(uint32_t menu, const char *text) {
    Menu *m;

    Lock();
    m = MenuOf(menu);
    if (!m) { Unlock(); ShSetError(SH_ERR_BAD_ARG); return 0; }
    if (text) {
        strncpy(m->status, text, sizeof(m->status) - 1);
        m->status[sizeof(m->status) - 1] = 0;
    } else {
        m->status[0] = 0;
    }
    Unlock();
    return 1;
}

SH_API void ShMenuSetKey(int vk) { g_key = vk; }
SH_API int  ShMenuIsOpen(void) { return g_open; }

SH_API void ShMenuOpen(int open) {
    EnsureMenu();
    g_open = open ? 1 : 0;
    if (g_open) g_current = g_root;
}

/* Never create/show the menu during a loading-scene transition. Doing so can
 * leave an orphaned native panel behind after the UI generation changes.
 * F1 (or the configured OpenKey) is the only automatic entry point. */
void ShMenuOnEnterPlaying(void) {
    Menu *root;
    int have = 0;

    if (!g_started || g_greeted) return;
    Lock();
    root = MenuOf(g_root);
    if (root) have = root->count > 0;
    Unlock();
    if (!have) return;
    g_greeted = 1;
}

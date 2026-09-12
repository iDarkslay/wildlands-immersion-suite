/* Wildlands Mod Framework / Immersion Suite fork.
 * Original GRW ScriptHook portions Copyright (C) 2026 PhialsBasement.
 * Modifications and additions Copyright (C) 2026 iDarkslay.
 * Modified through 2026-09-12. SPDX-License-Identifier: GPL-3.0-only.
 * See LICENSE, NOTICE.md and UPSTREAM_CHANGES.md.
 */
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scripthook_config.h"

void ShCfgPath(char *out, size_t cap) {
    char *slash, legacy[MAX_PATH];
    GetModuleFileNameA(NULL, out, (DWORD)cap);
    out[cap - 1] = 0;
    slash = strrchr(out, '\\');
    if (slash) slash[1] = 0;
    else out[0] = 0;
    strncpy(legacy,out,sizeof(legacy)-1);legacy[sizeof(legacy)-1]=0;
    strncat(out, "ModFramework.cfg", cap - strlen(out) - 1);
    strncat(legacy,"scripthook.cfg",sizeof(legacy)-strlen(legacy)-1);
    if(GetFileAttributesA(out)==INVALID_FILE_ATTRIBUTES&&GetFileAttributesA(legacy)!=INVALID_FILE_ATTRIBUTES)
        CopyFileA(legacy,out,TRUE);
}

int ShCfgInt(const char *section, const char *key, int fallback,
             int minimum, int maximum) {
    char path[MAX_PATH], text[64], def[32], *end;
    long value;
    ShCfgPath(path, sizeof(path));
    snprintf(def, sizeof(def), "%d", fallback);
    GetPrivateProfileStringA(section, key, def, text, sizeof(text), path);
    value = strtol(text, &end, 0);
    while (*end && isspace((unsigned char)*end)) end++;
    if (end == text || *end || value < minimum || value > maximum)
        return fallback;
    return (int)value;
}

static int NamedKey(const char *name) {
    struct Pair { const char *name; int vk; };
    static const struct Pair keys[] = {
        {"UP",VK_UP},{"DOWN",VK_DOWN},{"LEFT",VK_LEFT},{"RIGHT",VK_RIGHT},
        {"ENTER",VK_RETURN},{"RETURN",VK_RETURN},{"ESC",VK_ESCAPE},
        {"ESCAPE",VK_ESCAPE},{"BACKSPACE",VK_BACK},{"SPACE",VK_SPACE},
        {"TAB",VK_TAB},{"PAGEUP",VK_PRIOR},{"PAGEDOWN",VK_NEXT},
        {"HOME",VK_HOME},{"END",VK_END},{"INSERT",VK_INSERT},{"DELETE",VK_DELETE},
        {"ALT",VK_MENU},{"LEFTALT",VK_LMENU},{"RIGHTALT",VK_RMENU},
        {"CTRL",VK_CONTROL},{"LEFTCTRL",VK_LCONTROL},{"RIGHTCTRL",VK_RCONTROL},
        {"SHIFT",VK_SHIFT},{"LEFTSHIFT",VK_LSHIFT},{"RIGHTSHIFT",VK_RSHIFT},
        {"CAPSLOCK",VK_CAPITAL},{"NUMPAD0",VK_NUMPAD0},{"NUMPAD1",VK_NUMPAD1},
        {"NUMPAD2",VK_NUMPAD2},{"NUMPAD3",VK_NUMPAD3},{"NUMPAD4",VK_NUMPAD4},
        {"NUMPAD5",VK_NUMPAD5},{"NUMPAD6",VK_NUMPAD6},{"NUMPAD7",VK_NUMPAD7},
        {"NUMPAD8",VK_NUMPAD8},{"NUMPAD9",VK_NUMPAD9}
    };
    char upper[32];
    size_t i, n = strlen(name);
    if (n >= sizeof(upper)) return 0;
    for (i = 0; i <= n; i++) upper[i] = (char)toupper((unsigned char)name[i]);
    if (n == 1 && ((upper[0] >= 'A' && upper[0] <= 'Z') ||
                   (upper[0] >= '0' && upper[0] <= '9'))) return upper[0];
    if (upper[0] == 'F') {
        int f = atoi(upper + 1);
        if (f >= 1 && f <= 24) return VK_F1 + f - 1;
    }
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (!_stricmp(upper, keys[i].name)) return keys[i].vk;
    return 0;
}

int ShCfgKey(const char *section, const char *key, int fallback) {
    char path[MAX_PATH], text[64],*end;
    int vk;long numeric;
    ShCfgPath(path, sizeof(path));
    GetPrivateProfileStringA(section, key, "", text, sizeof(text), path);
    vk = NamedKey(text);
    if(!vk){numeric=strtol(text,&end,0);while(*end&&isspace((unsigned char)*end))end++;if(end!=text&&!*end&&numeric>=1&&numeric<=255)vk=(int)numeric;}
    return vk ? vk : fallback;
}

int ShCfgWriteInt(const char *section, const char *key, int value) {
    char path[MAX_PATH], text[32];
    if (!section || !key) return 0;
    ShCfgPath(path, sizeof(path));
    snprintf(text, sizeof(text), "%d", value);
    return WritePrivateProfileStringA(section, key, text, path) != 0;
}

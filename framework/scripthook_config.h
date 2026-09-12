/* Wildlands Mod Framework / Immersion Suite fork.
 * Original GRW ScriptHook portions Copyright (C) 2026 PhialsBasement.
 * Modifications and additions Copyright (C) 2026 iDarkslay.
 * Modified through 2026-09-12. SPDX-License-Identifier: GPL-3.0-only.
 * See LICENSE, NOTICE.md and UPSTREAM_CHANGES.md.
 */
#ifndef SCRIPHOOK_CONFIG_H
#define SCRIPHOOK_CONFIG_H
#include <stddef.h>

int ShCfgInt(const char *section, const char *key, int fallback,
             int minimum, int maximum);
int ShCfgKey(const char *section, const char *key, int fallback);
int ShCfgWriteInt(const char *section, const char *key, int value);
void ShCfgPath(char *out, size_t cap);

#endif

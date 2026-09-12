/* Wildlands Mod Framework / Immersion Suite fork.
 * Modified by iDarkslay through 2026-09-09; public release preparation 2026-09-09.
 * GPL-3.0; see LICENSE and NOTICE.md for upstream attribution and changes.
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

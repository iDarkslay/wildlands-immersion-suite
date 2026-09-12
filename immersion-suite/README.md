# Wildlands Immersion Suite source

This directory contains the complete preferred-form source for the shipped
`Fmods/immersion_suite.asi`:

- `immersion_suite.c` — main mod, camera behavior, configuration and F1 menu
- `movement.c` — movement-speed feature
- `ballistics.c` — projectile-velocity feature

The ASI uses the public framework API and headers in `../framework/`.
`../build.ps1` shows the exact compiler invocation. The expected release hash
and matching tag are recorded in `../BINARY_SOURCE_MAP.md`.

Copyright (C) 2026 iDarkslay. Licensed under GNU GPL version 3; see
`../LICENSE`.

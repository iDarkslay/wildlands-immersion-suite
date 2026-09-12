# Wildlands Immersion Suite

Wildlands Immersion Suite is a first-person-focused gameplay mod for Tom
Clancy's Ghost Recon Wildlands. It combines camera, movement, weapon, HUD and
environment controls in one native in-game menu.

The included Wildlands Mod Framework is the technical runtime developed to make
the suite possible. It is a modified fork of PhialsBasement's GRW ScriptHook,
not a separate mod users need to find or configure before installing the suite.

This repository is the complete corresponding source for the binaries in the
`0.1.0` release. Both the framework and suite are licensed under
GPL-3.0. Development is human-directed and AI-assisted.

## Features

- First-person camera with hidden head and configurable positioning
- first/third-person Alt toggle, FOV and weapon-alignment controls
- sprint camera lift and stationary body follow
- movement speed and projectile velocity controls
- separate accuracy controls for default/hip aim and native ADS
- HUD, weather, time, profiles and recovery controls
- native F1 menu and F9 hot reload
- ASI loading from both the game directory and `Fmods`; `Fmods` wins on a
  duplicate filename
- cooperative hot modules named `*.grwmod.dll` from `Fmods`, with
  `*.grwmod.next.dll` promotion during reload

## Included runtime and repository layout

- `vendor/grw-scripthook/`: complete framework source used for `dinput8.dll`
- `vendor/grw-scripthook/firstperson.c`: main suite module
- `mods/suite_movement/`: embedded movement feature
- `mods/suite_ballistics/`: embedded ballistics feature
- `config/`: factory configuration files
- `build.ps1`: deterministic release build entry point

The movement and ballistics directories are original components of Wildlands
Immersion Suite; their names describe the features they implement.

The repository deliberately excludes private development bridges, MCP tools,
diagnostic features, logs, test assets, reverse-engineering archives and
unrelated mods.

## Install and configure

Use the user package and follow `docs/INSTALL.md`. Open the framework menu with
F1. Adjust values in the menu or edit `ModFramework.cfg` and
`Fmods/immersion_suite.ini`. Preserve those files when updating.

## Build

See `BUILDING.md`. The two output files are
`build/release/dinput8.dll` and `build/release/immersion_suite.asi`.

## Compatibility and support

The current native offsets target GRW build `133.1.0.9840374` (Steam build
`24669148`). Other game builds are unverified. Read `KNOWN_ISSUES.md` before
installing and `docs/TROUBLESHOOTING.md` when diagnosing a problem.

## License and attribution

GPL-3.0 applies to this repository; see `LICENSE`. See `NOTICE.md` for upstream
origin, modifications and credits. This project is unofficial and is not
affiliated with or endorsed by Ubisoft.

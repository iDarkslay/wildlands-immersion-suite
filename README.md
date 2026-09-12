# Wildlands Mod Framework

## Including the Wildlands Immersion Suite

> **AI-DEVELOPED PROJECT** — iDarkslay directed the project, testing and release
> decisions; implementation and documentation were produced through extensive
> AI-assisted development.

**Wildlands Mod Framework** is a substantially modified Windows fork of
[PhialsBasement's GRW ScriptHook](https://github.com/PhialsBasement/grw-scripthook).
It loads as a `dinput8.dll` proxy, exposes a native C API, loads ASI mods and
provides a configurable in-game framework menu.

The flagship mod included with the framework is **Wildlands Immersion Suite**,
a first-person-focused overhaul for Tom Clancy's Ghost Recon Wildlands. The
suite combines camera, weapon stabilization, movement, ballistics, accuracy,
HUD, weather and time controls in one native F1 menu. Users receive the
framework and suite together; no separate ScriptHook download is required.

This repository is the complete corresponding source for both binaries in the
`0.1.0` package. The framework and suite are released under GPL-3.0.

## Framework highlights

- Windows `dinput8.dll` proxy with forwarding to the system DirectInput library
- ASI loading from both the game directory and `Fmods`; `Fmods` wins when a
  duplicate filename exists
- compatibility-oriented extension of the original GRW ScriptHook C ABI
- configurable native F1 menu and F9 cooperative hot-module reload
- camera, input, HUD, weather, time, accuracy, ballistics and weapon APIs
- deterministic PowerShell release build with dependency and hash checks

ASI mods built for the original GRW ScriptHook are expected to remain
compatible because the fork retains and extends its C API. Third-party mods
still need individual testing against the installed game and framework build.

## Wildlands Immersion Suite highlights

- first-person camera with hidden head and configurable positioning
- first/third-person Alt toggle, FOV and per-weapon alignment controls
- weapon stabilization, sprint camera lift and stationary body follow
- movement speed and projectile velocity controls
- separate accuracy controls for default/hip aim and native ADS
- HUD, weather, time, profiles and recovery controls

## Where the `dinput8.dll` source is

The complete framework source used to build `dinput8.dll` is under
`framework/`. Its proxy entry point is `loader.c`; DirectInput key
handling is in `scripthook_dinput.c`; the remaining framework translation units
are listed explicitly in `framework-sources.txt`. `build.ps1` compiles that list
directly, so no prebuilt private framework library is used.

The suite ASI is built from:

- `immersion-suite/immersion_suite.c` — main mod and F1 menu
- `immersion-suite/movement.c` — movement controls
- `immersion-suite/ballistics.c` — projectile controls
- the public headers included by those files

See `BINARY_SOURCE_MAP.md` for the exact binary hashes and source mapping.

## Source and documentation

- `BUILDING.md` — compiler requirements and exact build command
- `UPSTREAM_CHANGES.md` — changes from the original GRW ScriptHook fork point
- `CHANGELOG.md` — version history for framework and suite
- `GPL_COMPLIANCE.md` — corresponding-source and distribution checklist
- `NOTICE.md` — upstream attribution, modification notice and credits
- `RELEASE_POLICY.md` — version, tag and binary/source rules
- `docs/` — installation, compatibility and troubleshooting

## Build

See `BUILDING.md`. The outputs are `build/release/dinput8.dll` and
`build/release/immersion_suite.asi`.

## Compatibility

The current native offsets target GRW build `133.1.0.9840374` (Steam build
`24669148`). Other builds are unverified. Read `KNOWN_ISSUES.md` before using
the suite.

## License and attribution

This modified work is released under GNU GPL version 3; see `LICENSE`.
PhialsBasement's original GRW ScriptHook, the exact upstream commit and the
modification dates are recorded in `NOTICE.md` and `UPSTREAM_CHANGES.md`.
Ghost Recon and Tom Clancy's Ghost Recon Wildlands are trademarks of their
respective owners. This project is unofficial and is not affiliated with or
endorsed by Ubisoft.

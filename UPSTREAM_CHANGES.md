# Changes from GRW ScriptHook

Wildlands Mod Framework is based on PhialsBasement's GPL-3.0 GRW ScriptHook at
commit `3284ccd0ae84738aaba40964e960c6c9aae9b29b`, dated 2026-08-23. This file
records the material changes made by iDarkslay through 2026-09-12 for version
0.1.0. It describes the released result rather than private experiments.

Upstream repository:
https://github.com/PhialsBasement/grw-scripthook

## Runtime and loading

- Added a Windows release path for the `dinput8.dll` proxy.
- Extended ASI discovery to the game root and `Fmods`, with deterministic
  duplicate handling in favor of `Fmods`.
- Added framework configuration through `ModFramework.cfg`.
- Added cooperative `*.grwmod.dll` hot modules and F9 reload support.
- Added safe startup/state handling needed by the packaged suite.

## Framework API and native integration

- Extended the public C ABI used by camera, state, input, HUD, weather, time,
  accuracy, ballistics and weapon features.
- Added native menu configuration and the shared F1 interface used by the
  Immersion Suite.
- Added native weather application/release and world-time controls.
- Added accuracy and projectile-velocity controls.
- Added read-only player-skeleton, held-weapon identity and live Fake_gunroot
  data used for first-person weapon alignment.
- Updated camera/head handling and pose reads for stable first-person use.

## Bundled flagship mod

- Expanded the original first-person example into Wildlands Immersion Suite.
- Added camera modes, FOV and position tuning, weapon stabilization, body
  follow, movement and ballistics controls, HUD/environment controls, profiles,
  configuration persistence and recovery actions.
- Added the original suite movement and suite ballistics components.

## Release engineering

- Replaced the upstream Makefile release path with `build.ps1` for the shipped
  Windows binaries.
- Added deterministic timestamps/image base, dependency inspection and SHA-256
  reporting.
- Added complete binary/source mapping, a hashed source manifest, versioning
  policy, installation documentation and explicit upstream attribution.
- The release source and binaries have no runtime dependency on development
  utilities, network services or proprietary SDKs.

The detailed version history is in `CHANGELOG.md`. The exact framework files
compiled into `dinput8.dll` are listed in `framework-sources.txt`.

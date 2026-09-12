# Changelog

## 0.1.0 - 2026-09-12

First public release.

This release is derived from PhialsBasement's GRW ScriptHook at commit
`3284ccd0ae84738aaba40964e960c6c9aae9b29b`. See `UPSTREAM_CHANGES.md` for the
consolidated comparison with that upstream base.

### Wildlands Mod Framework

- Forked GRW ScriptHook at upstream commit
  `3284ccd0ae84738aaba40964e960c6c9aae9b29b`.
- Added Windows-focused proxy loading and ASI discovery in `Fmods` and the game
  root, with `Fmods` taking precedence on duplicate names.
- Added the configurable F1 native menu, F9 hot reload and APIs used by the
  suite for camera, input, HUD, weather, time, accuracy and ballistics.
- Added the framework configuration file `ModFramework.cfg`.
- Routed weather changes through the game's native ChangeTimeAndWeather action
  after the older request-record writes proved ineffective.
- Weather controls now use each preset's native transition. The ineffective
  user-facing transition-time setting was removed; the legacy framework API
  remains available for compatibility.
- Added a read-only held-weapon identity cache for stable per-weapon camera
  alignment.
- Corrected model-space Fake_gunroot conversion to use the live pose root
  translation used by the previously validated read-only path.
- Read the currently published pose buffer on every camera frame instead of
  retaining a stale bone buffer.
- Select the newest player skeleton actually published by the engine instead
  of assuming the first skeleton-like entity component drives rendered arms.
- Recompute the current Fake_gunroot world position inside the camera
  correction itself, avoiding a pose value cached during an earlier engine
  phase in the same rendered frame.
- Removed the separate tooltip accent widget that could remain visible beside
  the first row; the gold left bar now belongs only to the selected row.
- Added deterministic build, dependency checks, exact binary/source mapping,
  public installation documentation and GPL release records.

### Wildlands Immersion Suite

- Added first-person and configurable third-person cameras, FOV controls,
  hidden-head handling, perspective switching and recovery actions.
- Added sprint camera lift, stationary body follow, movement speed control,
  ballistics control, Super Accuracy and HUD controls.
- Added weather/time controls, persistent profiles and editable factory values.
- Marked vehicle-camera and holstered-camera features experimental.
- Enabled Super Accuracy for Default/Hip Aim in factory defaults.
- Applied the dark-gold selected-row menu style.
- Kept the weapon stabilizer's learned hip-fire alignment and restored its
  per-weapon identity lookup inside the public framework.

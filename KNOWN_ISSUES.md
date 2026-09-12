# Known issues

- Vehicle type detection exists, but custom Ground/Water/Air position offsets
  are experimental and are not confirmed in every vehicle.
- The holstered third-person camera is experimental and may not remain stable.
- First-person swimming may briefly or continuously return to third person
  during normal or fast swimming. It is disabled by default.
- Reload-state camera compensation is inactive pending reliable detection.
- Native ADS transitions and perspective switching still have edge cases.
- Stationary body follow can look stepped or stuttery.
- Weapon stabilization may settle only after the first aim or fire action.
- Version 0.1.0 selects the newest player skeleton published by the engine and
  recomputes Fake_gunroot from the current pose at the exact camera-correction
  point. Weapon stabilization can still vary across character or weapon state
  changes.
- Changing the character body or clothing can invalidate first-person
  positioning until the game is restarted.
- The visible tracer can arrive after the actual hit at high projectile
  velocity multipliers.
- Native offsets target GRW build `133.1.0.9840374` (Steam build `24669148`).
  Other builds may fail to initialize or behave incorrectly.

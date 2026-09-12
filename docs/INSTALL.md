# Install, update, uninstall and configure

## Install

1. Close Ghost Recon Wildlands and confirm `GRW.exe` is not running.
2. Back up an existing `dinput8.dll`, `ModFramework.cfg` and
   `Fmods/immersion_suite.ini` if present.
3. Copy the contents of the user package to the folder containing `GRW.exe`.
4. If Windows asks to overwrite a CFG or INI during a first install, inspect
   the existing file first; it may belong to an older setup.
5. Start the game. Open the menu with F1.

The package contains `dinput8.dll` in the game root and
`Fmods/immersion_suite.asi`. It never includes `GRW.exe` or Ubisoft files.

## Update

1. Close the game and confirm `GRW.exe` is not running.
2. Back up the current installation.
3. Replace only `dinput8.dll` and `Fmods/immersion_suite.asi`.
4. Keep your existing `ModFramework.cfg` and
   `Fmods/immersion_suite.ini`. Compare new factory configs manually when the
   changelog announces new options.

## Configuration

- F1 opens the framework menu.
- W/S navigate, A/D change a value, Enter confirms, Escape/Backspace returns.
- F9 hot reloads compatible hot modules.
- `ModFramework.cfg` controls framework keys, timing, scale and placement.
- `Fmods/immersion_suite.ini` controls suite features and saved profiles.

## Uninstall

Close the game, then remove only the files supplied by this package:

- `dinput8.dll`
- `Fmods/immersion_suite.asi`

Optionally remove `ModFramework.cfg` and `Fmods/immersion_suite.ini` after
backing them up. Do not delete the whole `Fmods` folder; other mods and user
files may be stored there.

If another mod previously supplied `dinput8.dll`, restore its backup after
removing this framework.


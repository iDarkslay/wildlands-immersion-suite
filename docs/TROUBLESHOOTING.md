# Troubleshooting

## The game does not start

Close the game, remove the package's `dinput8.dll` and restore your backup. Then
check the game build and conflicts with another proxy DLL. Verify game files if
the original installation was changed.

## The menu or suite does not appear

Confirm that `dinput8.dll` is beside `GRW.exe` and that
`Fmods/immersion_suite.asi` exists. Check that antivirus software did not
quarantine either file. F1 opens the default menu.

## Settings do not persist

Use the relevant Save action or `Save ALL persistent settings to INI`. Confirm
that the game can write to `Fmods/immersion_suite.ini`. Weather and time are
session controls and are deliberately not restored automatically.

## Camera/body/weapon position is wrong

Use `Profiles and recovery > Recapture current camera`, followed by the
emergency recovery action if needed. Restart the game after character, body or
clothing changes. Restore the factory INI only after backing up your profile.

## Reporting a reproducible problem

Include the mod version, game executable version, storefront/build, installed
ASI filenames, reproduction steps and whether a clean factory configuration
changes the result. Do not upload save games, personal paths or unrelated logs
unless specifically requested.


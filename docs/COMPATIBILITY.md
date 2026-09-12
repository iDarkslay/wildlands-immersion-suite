# Compatibility

Wildlands Mod Framework is a `dinput8.dll` proxy. Only one file with that name
can occupy the game directory. Other proxy loaders, injectors or mods that
replace `dinput8.dll` may conflict.

The framework loads `.asi` files from `Fmods` and from the game root. When the
same filename exists in both places, the `Fmods` copy is loaded and the root
copy is skipped. Legacy root ASI support is intentional, but `Fmods` is the
recommended location.

Reloadable framework modules use the specific filename
`name.grwmod.dll` inside `Fmods` and must export `GrwModInit` and
`GrwModShutdown`. A builder can place `name.grwmod.next.dll` there for F9 to
promote on reload. Arbitrary `.dll` files are not discovered or loaded as mods.

ASI mods compiled for the original PhialsBasement GRW ScriptHook are expected
to remain compatible because this fork retains and extends the original plain C
API. This is best-effort compatibility rather than a guarantee: a third-party
mod may depend on a specific framework build, undocumented behavior or game
hooks that conflict with the suite. Report compatibility per mod and version.
Use only the fork's `dinput8.dll`; two loader DLLs cannot coexist under the same
filename.

Avoid running multiple camera or FOV mods together because they can compete for
the same game state. ReShade and unrelated graphics injectors are outside the
supported release scope and are not bundled.

The native offsets target GRW build `133.1.0.9840374` (Steam build `24669148`).
Offline single-player use is the supported scenario.

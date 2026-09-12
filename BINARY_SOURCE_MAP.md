# Binary/source mapping

Release: `0.1.0`  
Required Git tag at publication: `v0.1.0`  
Upstream base: `3284ccd0ae84738aaba40964e960c6c9aae9b29b`

| Nexus package path | SHA-256 | Exact corresponding source |
| --- | --- | --- |
| `dinput8.dll` | `366F6C5C8ABE0CA829EF2FA4FACE0D8630C2EA7A1E5E90BE2FD26AFD480BE754` | `framework-sources.txt` and all files it lists at tag `v0.1.0`; proxy entry point: `framework/loader.c` |
| `Fmods/immersion_suite.asi` | `495DCC829C6663E3A124ED3D3EBC3DFC56AE6045D83EF4C5019710BE451AC688` | `immersion-suite/immersion_suite.c`, `immersion-suite/movement.c`, `immersion-suite/ballistics.c` and their included framework headers at tag `v0.1.0` |

Build environment and commands are recorded in `BUILDING.md`. The matching
`SOURCE_MANIFEST.sha256` hashes every public source/config/document file except
itself. The annotated `v0.1.0` tag is the authoritative source identity for
these binaries. If any build input changes, publish a new version and rebuild
and recheck both binaries.

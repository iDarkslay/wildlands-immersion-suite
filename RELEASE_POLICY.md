# Release and version policy

The project uses Semantic Versioning. Release candidates use tags such as
`v0.1.0-rc.7`; stable releases use tags such as `v0.1.0`.

Every distributed binary set must meet all of these rules:

1. Build it from the exact source committed at the matching Git tag.
2. Put the same version in `VERSION`, package documentation and Nexus filename.
3. Record SHA-256 hashes for `dinput8.dll` and `immersion_suite.asi` in the
   release manifest committed at that tag.
4. Publish the corresponding repository/tag no later than the binary release.
5. Never rebuild or silently replace files under an existing version. Publish
   a new version and tag for any binary change.
6. Keep GPL-3.0, attribution, build instructions and complete framework and
   suite source available for as long as the binaries are distributed.
7. Build the user package only from its explicit allowlist. Do not copy the
   development workspace or an old playtest tree.

Release candidates require an in-game smoke test. Stable releases additionally
require testing of first person, perspective switching, ADS, HUD, movement,
ballistics, menu/hot reload, configuration preservation, root ASI loading and
`Fmods` ASI loading.

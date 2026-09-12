# Development workflow

This public repository is the authoritative source for release runtime code.
Normal updates must be developed here rather than copied from a mixed private
workspace at release time.

Private bridge, MCP, scanners and reverse-engineering tools belong in a
separate private development tree. They may consume documented framework APIs,
but public runtime code must compile, start and provide its advertised features
without them. Do not add includes, imports, dynamic lookups or fallback behavior
that depends on private modules.

For each update:

1. Change the public source and documentation.
2. Increment `VERSION` according to `RELEASE_POLICY.md`.
3. Build twice with `build.ps1` and compare SHA-256 hashes.
4. Test the exact output pair in game after confirming `GRW.exe` is closed.
5. Update `BINARY_SOURCE_MAP.md`, changelog and package manifest.
6. Generate the user package from the explicit allowlist and run the audit.
7. Commit the reviewed source, create the matching version tag, and verify that
   the tagged source rebuilds the package binaries.
8. Publish only after explicit owner approval.

This separation means later releases do not require manually deleting private
code. If the public runtime ever needs behavior prototyped in a private tool,
port and review that behavior as ordinary GPL source before building a release.


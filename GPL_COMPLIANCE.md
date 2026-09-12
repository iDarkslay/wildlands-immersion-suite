# GPL-3.0 release compliance

Wildlands Mod Framework is a modified version of PhialsBasement's GPL-3.0 GRW
ScriptHook. Wildlands Immersion Suite is distributed with and built against
that framework. Version 0.1.0 is distributed as GPL-3.0 software.

## Material supplied for version 0.1.0

- the unmodified GNU GPL version 3 text in `LICENSE`
- prominent upstream attribution and dated modification notice in `NOTICE.md`
- dated license and modification headers in public C and header files
- complete preferred-form source for `dinput8.dll` and
  `Fmods/immersion_suite.asi`
- all public headers and configuration files needed to modify the work
- `build.ps1`, `framework-sources.txt` and exact build instructions
- installation and configuration information for the user-installed binaries
- exact tag and SHA-256 mappings in `BINARY_SOURCE_MAP.md`
- a full source-tree hash list in `SOURCE_MANIFEST.sha256`
- changes from the upstream fork point in `UPSTREAM_CHANGES.md`
- the GPL warranty disclaimer and redistribution terms in the package license

No Ubisoft game files, private libraries, generated source, proprietary SDKs or
private runtime services are required to build the two distributed binaries.
Windows and compiler runtime libraries used through standard interfaces are
system/toolchain components described in `BUILDING.md`.

## Publication rule

When the binary package is offered for download, the repository and matching
`v0.1.0` source tag must be publicly accessible at no additional charge. The
binary download page must link directly to that source. The source must remain
available for as long as version 0.1.0 binaries are distributed.

Any changed binary requires a new version, rebuilt corresponding source, new
hashes and a new tag. An existing release must never be silently replaced.

This checklist documents the release materials and does not replace the terms
of the GPL-3.0 license itself.

# Building from source

## Requirements

- 64-bit Windows or PowerShell 7 on Windows
- x86_64 MinGW-w64 GCC compatible with GCC 16.1.0
- `gcc.exe` and the matching `objdump.exe` in the same directory

The release candidate was built with:

`gcc.exe (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0`

No proprietary SDK, generated source, private bridge, prebuilt import library,
network service or game file is required.

## Command

From PowerShell in the repository root:

```powershell
.\build.ps1 -Compiler 'C:\path\to\mingw64\bin\gcc.exe'
```

Outputs:

- `build/release/dinput8.dll`
- `build/release/immersion_suite.asi`

The script compiles every framework translation unit listed in
`framework-sources.txt`, embeds the movement and ballistics modules in the ASI,
checks the target triplet, strips linker symbols, rejects unexpected DLL
dependencies and prints SHA-256 hashes. `SOURCE_DATE_EPOCH`, the PE timestamp
and image base are fixed so repeated builds with the same compiler and source
can be compared byte for byte.

Run the command twice with different build names if you want to verify
reproducibility:

```powershell
.\build.ps1 -Compiler 'C:\path\to\mingw64\bin\gcc.exe' -BuildName release
.\build.ps1 -Compiler 'C:\path\to\mingw64\bin\gcc.exe' -BuildName verify
Get-FileHash .\build\release\* , .\build\verify\* -Algorithm SHA256
```

Do not copy either binary into the Wildlands directory while `GRW.exe` is
running.

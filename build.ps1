param(
    [string]$Compiler = 'gcc.exe',
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$BuildName = 'release'
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$output = Join-Path $repo "build/$BuildName"
New-Item -ItemType Directory -Force -Path $output | Out-Null
$cc = (Get-Command $Compiler -ErrorAction Stop).Source
$objdump = Join-Path (Split-Path $cc) 'objdump.exe'
if (-not (Test-Path -LiteralPath $objdump)) { throw 'Matching objdump.exe is required beside gcc.exe.' }
$machine = & $cc -dumpmachine
if ($LASTEXITCODE -ne 0 -or $machine -ne 'x86_64-w64-mingw32') { throw 'Use x86_64-w64-mingw32 GCC.' }
$previousEpoch = $env:SOURCE_DATE_EPOCH
# Freeze __DATE__/__TIME__; --no-insert-timestamp freezes PE timestamps.
$env:SOURCE_DATE_EPOCH = '1788912000'
Push-Location $repo
try {
    $flags = @(
        '-O2','-Wall','-Wextra','-shared','-static-libgcc','-s',
        '-Wl,--no-insert-timestamp','-Wl,--image-base,0x180000000'
    )
    $framework = @(Get-Content -LiteralPath (Join-Path $repo 'framework-sources.txt'))
    foreach ($source in $framework) {
        if ($source -notmatch '^framework/[a-z_]+\.c$' -or -not (Test-Path -LiteralPath $source)) { throw "Invalid source entry: $source" }
    }
    & $cc @flags -o "$output/dinput8.dll" @framework -ldinput8 -ldxguid -lgdi32 -luser32
    if ($LASTEXITCODE -ne 0) { throw 'Framework compilation failed.' }
    & $cc @flags -I framework -DIMMERSIVE_MOVEMENT_EMBEDDED -DIMMERSIVE_BALLISTICS_EMBEDDED -o "$output/immersion_suite.asi" immersion-suite/immersion_suite.c immersion-suite/movement.c immersion-suite/ballistics.c -lgdi32 -luser32
    if ($LASTEXITCODE -ne 0) { throw 'Suite compilation failed.' }
    foreach ($name in @('dinput8.dll','immersion_suite.asi')) {
        $headers = & $objdump -p (Join-Path $output $name)
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $name" }
        $imports = @($headers | Select-String 'DLL Name:' | ForEach-Object { ($_.Line -split 'DLL Name:')[1].Trim().ToLowerInvariant() })
        $unexpected = @($imports | Where-Object { $_ -notin @('kernel32.dll','user32.dll','gdi32.dll','msvcrt.dll','ucrtbase.dll','dinput8.dll') -and $_ -notmatch '^api-ms-win-crt-[a-z0-9-]+\.dll$' })
        if ($unexpected.Count) { throw "Unexpected DLL dependencies for ${name}: $unexpected" }
        Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $output $name)
    }
    & $cc --version | Select-Object -First 1
} finally {
    Pop-Location
    $env:SOURCE_DATE_EPOCH = $previousEpoch
}

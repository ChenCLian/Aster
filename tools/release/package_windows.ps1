param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$QtBin,
    [Parameter(Mandatory=$true)][string]$VcpkgPrefix,
    [string]$OutputDir = "",
    [string]$VcRedist = ""
)
# Run from the x64 Native Tools Command Prompt / its PowerShell child.
# This script packages a previously built MSVC x64 Release executable.
$ErrorActionPreference = "Stop"
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
$version = (Get-Content -LiteralPath (Join-Path $projectRoot "VERSION") -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') { throw "Invalid VERSION" }
$buildRoot = (Resolve-Path -LiteralPath $BuildDir).Path
$qtRoot = (Resolve-Path -LiteralPath $QtBin).Path
$dependencyRoot = (Resolve-Path -LiteralPath $VcpkgPrefix).Path
$app = Join-Path $buildRoot "bin/Aster.exe"
if (!(Test-Path -LiteralPath $app)) { throw "Build the root Aster.pro in Release first: missing bin/Aster.exe" }
$deploy = Join-Path $qtRoot "windeployqt.exe"
if (!(Test-Path -LiteralPath $deploy)) { throw "QtBin must contain windeployqt.exe from the matching Qt kit" }
$dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
# PE/COFF machine check prevents mislabeled ARM64/x86 release archives.
$bytes = [IO.File]::ReadAllBytes($app)
if ($bytes.Length -lt 64) { throw "Invalid executable" }
$peOffset = [BitConverter]::ToInt32($bytes, 60)
if ($peOffset -lt 0 -or $peOffset -gt ($bytes.Length - 6) -or [BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes, $peOffset + 4) -ne 0x8664) {
    throw "This packager requires an MSVC x64 executable"
}
$fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($app).ProductVersion
$baseVersion = $version.Split('-')[0]
if ($fileVersion -notmatch ('^' + [regex]::Escape($baseVersion) + '(?:\.0)?$')) {
    throw "Executable version '$fileVersion' does not match VERSION '$version'. Rerun qmake and rebuild."
}
if (!$OutputDir) { $OutputDir = Join-Path $projectRoot "dist" }
$outputRoot = [IO.Path]::GetFullPath($OutputDir)
$name = "Aster-$version-windows-x64"
$stage = Join-Path $outputRoot $name
$archive = Join-Path $outputRoot "$name.zip"
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $archive)) { throw "Output already exists. Use a new OutputDir; existing packages are never overwritten." }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $app -Destination (Join-Path $stage "Aster.exe")
& $deploy --release --no-compiler-runtime --dir $stage (Join-Path $stage "Aster.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
# Aster opens only QSQLITE connections. windeployqt can also copy optional
# SQL plugins whose database clients are not part of this application.
# Prune only the new deployment directory, before collecting DLL dependencies.
$sqlDriverDir = Join-Path $stage "sqldrivers"
$sqliteDriver = Join-Path $sqlDriverDir "qsqlite.dll"
if (!(Test-Path -LiteralPath $sqliteDriver -PathType Leaf)) {
    throw "Missing required SQLite plugin: $sqliteDriver. Check the matching Qt Release kit."
}
Get-ChildItem -LiteralPath $sqlDriverDir -File | Where-Object {
    $_.Extension -ieq '.dll' -and $_.Name -ine 'qsqlite.dll'
} | ForEach-Object {
    Write-Host "Excluding unused SQL driver: $($_.Name)"
    Remove-Item -LiteralPath $_.FullName -Force
}
# Resolve transitive native DLL imports. windeployqt does not deploy GSL/OpenBLAS.
$searchDirs = @((Join-Path $dependencyRoot "bin"), $qtRoot)
$queue = New-Object 'System.Collections.Generic.Queue[string]'
Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object { $_.Extension -in @('.exe','.dll') } | ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    if (!$seen.Add($binary)) { continue }
    $imports = & $dumpbin /nologo /dependents $binary
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect DLL dependencies: $binary" }
    foreach ($line in $imports) {
        if ($line -notmatch '^\s+([A-Za-z0-9_.+-]+\.dll)\s*$') { continue }
        $dll = $Matches[1]
        if ($dll -match '^(Qt6.*d|gsld|gslcblasd|MSVCP\d+D|VCRUNTIME\d+D|ucrtbased)\.dll$') { throw "Debug runtime detected: $dll. Use a clean Release build." }
        if ($dll -match '^(api-ms-win-|ext-ms-win-|MSVCP\d+|VCRUNTIME\d+|CONCRT\d+)') { continue }
        $destination = Join-Path $stage $dll
        if (Test-Path -LiteralPath $destination) { continue }
        if (Test-Path -LiteralPath (Join-Path ([Environment]::SystemDirectory) $dll)) { continue }
        $found = $null
        foreach ($dir in $searchDirs) { $candidate = Join-Path $dir $dll; if (Test-Path -LiteralPath $candidate) { $found = $candidate; break } }
        if (!$found) { throw "Missing runtime dependency: $dll (required by $binary). Check VcpkgPrefix and QtBin." }
        Copy-Item -LiteralPath $found -Destination $destination
        $queue.Enqueue($destination)
    }
}
foreach ($required in @("Qt6Core.dll","Qt6Network.dll","platforms/qwindows.dll","sqldrivers/qsqlite.dll","tls/qschannelbackend.dll")) {
    if (!(Test-Path -LiteralPath (Join-Path $stage $required))) { throw "Missing deployment component: $required. Use the standard Qt MSVC build with Schannel TLS support." }
}
# Keep upstream notices alongside the executable. No developer database/settings are copied.
$notices = Join-Path $stage "licenses"
New-Item -ItemType Directory -Path $notices | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot "docs/third_party.md") -Destination $notices
Get-ChildItem -LiteralPath (Join-Path $projectRoot "src/third_party") -Recurse -File | Where-Object { $_.Name -match '^(COPYING|COPYRIGHT|LICENSE)' } | ForEach-Object {
    $relative = $_.FullName.Substring($projectRoot.Length+1)
    $target = Join-Path $notices $relative
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $target
}
foreach ($folder in @("licenses", "resources")) {
    $source = Join-Path $projectRoot $folder
    if (Test-Path -LiteralPath $source) {
        Get-ChildItem -LiteralPath $source -Recurse -File | Where-Object { $_.Extension -in @('.md','.txt') } | ForEach-Object {
            $target = Join-Path $notices ($_.FullName.Substring($projectRoot.Length+1))
            New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
            Copy-Item -LiteralPath $_.FullName -Destination $target
        }
    }
}
foreach ($port in @("gsl", "openblas", "lapack", "lapack-reference")) {
    $copyright = Join-Path $dependencyRoot "share/$port/copyright"
    if (Test-Path -LiteralPath $copyright) { Copy-Item -LiteralPath $copyright -Destination (Join-Path $notices "$port-copyright.txt") }
}
$qtLicenses = Join-Path (Split-Path $qtRoot) "LICENSES"
if (Test-Path -LiteralPath $qtLicenses) { Copy-Item -LiteralPath $qtLicenses -Destination (Join-Path $notices "Qt") -Recurse }
foreach ($document in @("README.md", "AUTHORS.md", "RELEASE_NOTES.md", "QUICK_START.md", "FAQ.md")) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $document) -Destination $stage
}
New-Item -ItemType Directory -Path (Join-Path $stage "resources/app") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot "resources/app/aster.png") -Destination (Join-Path $stage "resources/app/aster.png")
if ($VcRedist) {
    if (!(Test-Path -LiteralPath $VcRedist -PathType Leaf)) { throw "VcRedist must be the official vc_redist.x64.exe installer" }
    Copy-Item -LiteralPath $VcRedist -Destination (Join-Path $stage "vc_redist.x64.exe")
}
@"
Aster $version (Windows x64)

Extract the entire folder before running Aster.exe.
Getting started: QUICK_START.md. Troubleshooting: FAQ.md.
If the Microsoft Visual C++ x64 runtime is missing, install the official package:
https://aka.ms/vc14/vc_redist.x64.exe

To update: close Aster and extract the new full package to a NEW application folder.
Your database and settings remain at their existing locations. If you manually placed
a database inside the old application folder, keep that folder or move the database
and select its new location in Settings. Do not delete the old folder before checking.

Check for updates: Settings -> Check for updates.
This release checks GitHub releases and opens the browser; it does not auto-install.
Releases: https://github.com/ChenCLian/Aster/releases
"@ | Set-Content -LiteralPath (Join-Path $stage "START_HERE.txt") -Encoding UTF8
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name.zip" | Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII
Write-Host "Created $archive"
Write-Host "SHA256: $hash"
Write-Host "Test this folder on a Windows host without Qt/vcpkg before publishing."

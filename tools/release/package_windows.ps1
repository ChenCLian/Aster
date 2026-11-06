param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$QtBin,
    [Parameter(Mandatory=$true)][string]$VcpkgPrefix,
    [string]$OutputDir = "",
    [string]$VcRedist = "",
    [string]$VcpkgRoot = "",
    [string]$DependencyCache = "",
    [string]$DependencySupplement = "",
    [string]$PythonExe = ""
)
# Run from the x64 Native Tools Command Prompt / its PowerShell child.
# This script packages a previously built MSVC x64 Release executable.
$ErrorActionPreference = "Stop"

function Get-AsterPython {
    param([string]$Explicit = "")
    $candidates = @()
    $names = if ($Explicit) { @($Explicit) } else { @("py", "python", "python3") }
    foreach ($name in $names) {
        $command = Get-Command -Name $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if (!$command) { continue }
        $prefix = @()
        if ([IO.Path]::GetFileNameWithoutExtension($command.Source) -eq "py") { $prefix = @("-3") }
        $candidates += [pscustomobject]@{ Path = $command.Source; Prefix = $prefix }
    }
    $probe = "import sys,json; print(json.dumps({'executable':sys.executable,'version':list(sys.version_info[:3])})); sys.exit(0 if sys.version_info >= (3,10) else 10)"
    $attempts = @()
    foreach ($candidate in $candidates) {
        $savedPreference = $ErrorActionPreference
        $PSNativeCommandUseErrorActionPreference = $false
        try {
            # A command on PATH may be a Windows Store alias, an old interpreter,
            # or a launcher. Require an actual successful Python execution.
            $ErrorActionPreference = "Continue"
            $global:LASTEXITCODE = $null
            $prefix = $candidate.Prefix
            $output = @(& $candidate.Path @prefix -I -X utf8 -c $probe 2>&1)
            $code = $global:LASTEXITCODE
            $details = ($output | ForEach-Object { $_.ToString() }) -join "`n"
            if ($null -eq $code -or $code -ne 0) { throw "exit $code; $details" }
            $info = $details | ConvertFrom-Json -ErrorAction Stop
            if (!$info.executable -or !(Test-Path -LiteralPath $info.executable -PathType Leaf) -or
                $info.version.Count -lt 2 -or [int]$info.version[0] -lt 3 -or
                ([int]$info.version[0] -eq 3 -and [int]$info.version[1] -lt 10)) {
                throw "The probe did not identify Python 3.10 or newer."
            }
            return [pscustomobject]@{ Path = [string]$info.executable; Version = $info.version -join "." }
        } catch {
            $attempts += "$($candidate.Path): $($_.Exception.Message)"
        } finally {
            $ErrorActionPreference = $savedPreference
        }
    }
    $detail = if ($attempts.Count) { "`n" + ($attempts -join "`n") } else { " No Python executable was found." }
    throw "A working Python 3.10+ interpreter is required. Install Python or pass -PythonExe with its full executable path. No staging folder has been created.$detail"
}

function Invoke-AsterCollector {
    param([string]$Python, [string[]]$CollectorArguments, [string]$MaterialRoot, [string]$Version)
    New-Item -ItemType Directory -Path $MaterialRoot -Force | Out-Null
    $marker = Join-Path $MaterialRoot ".aster-dependency-materials"
    if ((Get-ChildItem -LiteralPath $MaterialRoot -Force | Select-Object -First 1) -and !(Test-Path -LiteralPath $marker)) {
        throw "Dependency output must be empty or a previous collector output: $MaterialRoot"
    }
    # The collector accepts only its own output directory. Mark it before the log
    # makes the directory nonempty, and never treat an old report as a new result.
    Set-Content -LiteralPath $marker -Value "Aster dependency collector output" -Encoding ASCII
    $reportPath = Join-Path $MaterialRoot "report.json"
    $logPath = Join-Path $MaterialRoot "collector.log"
    if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath -Force }
    Set-Content -LiteralPath $logPath -Value "Aster $Version dependency collector`nPython: $Python" -Encoding UTF8
    $savedPreference = $ErrorActionPreference
    $savedEncoding = [Console]::OutputEncoding
    $PSNativeCommandUseErrorActionPreference = $false
    $exitCode = $null
    $launchError = $null
    try {
        $ErrorActionPreference = "Continue"
        [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
        $global:LASTEXITCODE = $null
        & $Python -u -X utf8 @CollectorArguments 2>&1 | ForEach-Object {
            $line = $_.ToString()
            Add-Content -LiteralPath $logPath -Value $line -Encoding UTF8 -ErrorAction Stop
            Write-Host $line
        }
        $exitCode = $global:LASTEXITCODE
    } catch {
        $launchError = $_.Exception.Message
        Add-Content -LiteralPath $logPath -Value $launchError -Encoding UTF8
    } finally {
        [Console]::OutputEncoding = $savedEncoding
        $ErrorActionPreference = $savedPreference
    }
    Add-Content -LiteralPath $logPath -Value "Process exit code: $exitCode" -Encoding UTF8
    $report = $null
    if (Test-Path -LiteralPath $reportPath -PathType Leaf) {
        try { $report = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json -ErrorAction Stop }
        catch { Add-Content -LiteralPath $logPath -Value "Collector report is not valid JSON: $($_.Exception.Message)" -Encoding UTF8 }
    }
    if ($null -ne $exitCode -and $exitCode -eq 0 -and !$launchError -and $report -and $report.status -eq "collected") { return }
    if (!$report -or $report.status -ne "incomplete") {
        # Startup, argument, import and unexpected process failures may happen
        # before Python can write its normal report. Preserve that distinction.
        $message = "Collector terminated without a successful result. Exit code: $exitCode. See collector.log for process output."
        $report = [ordered]@{ schema = 1; project = "Aster"; version = $Version; platform = "windows-x64";
            status = "incomplete"; phase = "collector-process"; exit_code = $exitCode;
            components = @(); binaries = @(); errors = @(@{ component = "collector-process"; message = $message }) }
        try { [IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 12), (New-Object System.Text.UTF8Encoding($false))) }
        catch { Write-Warning "Could not write the fallback report: $($_.Exception.Message)" }
    }
    $reportHint = if (Test-Path -LiteralPath $reportPath -PathType Leaf) { "Report: $reportPath." } else { "No report could be written." }
    throw "Dependency collector failed (exit $exitCode). $reportHint Log: $logPath. The staging folder is preserved; no release archive was created."
}

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
$python = Get-AsterPython -Explicit $PythonExe
Write-Host "Dependency collector: Python $($python.Version) ($($python.Path))"
$deployArgs = @("--release", "--no-compiler-runtime", "--no-opengl-sw", "--no-system-d3d-compiler")
$deployHelp = (& $deploy --help 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0) { throw "Cannot query windeployqt options" }
if ($deployHelp -match '--no-system-dxc-compiler') { $deployArgs += "--no-system-dxc-compiler" }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $app -Destination (Join-Path $stage "Aster.exe")
# Aster's QWidget/QCustomPlot views use CPU painting; no software OpenGL or
# system D3D compiler redistributable is needed for these views.
& $deploy @deployArgs --dir $stage (Join-Path $stage "Aster.exe")
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
            $relative = if ($folder -eq "licenses") {
                $_.FullName.Substring($source.Length+1)
            } else {
                $_.FullName.Substring($projectRoot.Length+1)
            }
            $target = Join-Path $notices $relative
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
foreach ($document in @("README.md", "AUTHORS.md", "RELEASE_NOTES.md", "QUICK_START.md", "FAQ.md", "LICENSE", "COPYRIGHT.md", "SOURCE_CODE.md")) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $document) -Destination $stage
}
New-Item -ItemType Directory -Path (Join-Path $stage "resources/app") -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot "resources/app/aster.png") -Destination (Join-Path $stage "resources/app/aster.png")
if ($VcRedist) {
    if (!(Test-Path -LiteralPath $VcRedist -PathType Leaf)) { throw "VcRedist must be the official vc_redist.x64.exe installer" }
    Copy-Item -LiteralPath $VcRedist -Destination (Join-Path $stage "vc_redist.x64.exe")
}
# Collect sources separately; copy verified notices into the runnable package.
$materialRoot = Join-Path $outputRoot "dependency-materials/windows-x64"
$materialArgs = @("$PSScriptRoot/collect_dependency_materials.py", "--platform", "windows-x64",
    "--package-dir", $stage, "--qmake", (Join-Path $qtRoot "qmake.exe"),
    "--vcpkg-prefix", $dependencyRoot, "--output", $materialRoot)
if ($VcpkgRoot) { $materialArgs += @("--vcpkg-root", $VcpkgRoot) }
if ($DependencyCache) { $materialArgs += @("--cache", $DependencyCache) }
if ($DependencySupplement) { $materialArgs += @("--supplement", $DependencySupplement) }
Invoke-AsterCollector -Python $python.Path -CollectorArguments $materialArgs -MaterialRoot $materialRoot -Version $version
@"
Aster $version (Windows x64)

Extract the entire folder before running Aster.exe.
Copyright (C) 2026 ZiJie Chen. License: GPL-3.0-only; see LICENSE.
Source and redistribution information: SOURCE_CODE.md.
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

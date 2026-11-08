param([string]$PythonExe = "")
# Copyright (C) 2026 ZiJie Chen
# SPDX-License-Identifier: GPL-3.0-only
# Runs without Qt/MSVC or network access; exercises the native process boundary.
$ErrorActionPreference = "Stop"
$tokens = $null
$parseErrors = $null
$scriptPath = Join-Path $PSScriptRoot "package_windows.ps1"
$ast = [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
foreach ($name in @("Assert-AsterExecutableVersion", "Get-AsterPython", "Invoke-AsterCollector", "Invoke-AsterReuse")) {
    $definition = $ast.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
    if (!$definition) { throw "Missing function: $name" }
    . ([ScriptBlock]::Create($definition.Extent.Text))
}
$python = Get-AsterPython -Explicit $PythonExe
$script:passed = 0
function Assert-True($Condition, [string]$Message) {
    if (!$Condition) { throw $Message }
}
function Expect-Failure([scriptblock]$Action, [string]$Pattern) {
    $message = $null
    try { & $Action } catch { $message = $_.Exception.Message }
    Assert-True ($message -and $message -match $Pattern) "Expected failure matching '$Pattern'; got '$message'"
}
function Pass([string]$Name) {
    $script:passed++
    Write-Host "PASS $Name"
}
$temporary = Join-Path ([IO.Path]::GetTempPath()) ("aster-packaging-tests-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporary | Out-Null
try {
    $versionFixture = Join-Path $temporary "version fixture.py"
    @'
import os, sys, time
assert os.environ.get('QT_COMMAND_LINE_PARSER_NO_GUI_MESSAGE_BOXES') == '1'
mode = sys.argv[1]
if mode == 'timeout':
    time.sleep(10)
if mode != 'empty':
    print('Aster_Workspace ' + ('1.2.0-beta.2' if mode == 'old' else '1.2.0-beta.5'))
if mode == 'error':
    print('runtime load failed', file=sys.stderr)
    sys.exit(3)
'@ | Set-Content -LiteralPath $versionFixture -Encoding UTF8
    foreach ($mode in @('ok', 'old', 'empty', 'error', 'timeout')) {
        $arguments = '"' + $versionFixture + '" ' + $mode
        $timeout = if ($mode -eq 'timeout') { 500 } else { 15000 }
        $invoke = { Assert-AsterExecutableVersion -App $python.Path -Version '1.2.0-beta.5' -Arguments $arguments -TimeoutMilliseconds $timeout }
        if ($mode -eq 'ok') { & $invoke }
        elseif ($mode -eq 'timeout') { Expect-Failure $invoke 'timed out' }
        elseif ($mode -eq 'error') { Expect-Failure $invoke 'exited with 3' }
        else { Expect-Failure $invoke 'expected.*1.2.0-beta.5' }
        Pass "Executable full version: $mode"
    }
    Expect-Failure { Assert-AsterExecutableVersion -App (Join-Path $temporary 'missing.exe') -Version '1.2.0-beta.5' } '.+'
    Pass 'Executable version launch failure'
    $versionGate = @($ast.EndBlock.Statements | Where-Object {
        $_.Extent.Text.StartsWith('Assert-AsterExecutableVersion -App ')
    })
    Assert-True ($versionGate.Count -eq 1) 'Missing unconditional full-version gate'
    $materialOffset = $ast.Extent.Text.IndexOf('$materialRoot = if')
    Assert-True ($versionGate[0].Extent.StartOffset -lt $materialOffset) 'Version check must precede every material mode'
    Pass 'Stage, Reuse and Collect share the executable version gate'
    Assert-True (Test-Path -LiteralPath $python.Path -PathType Leaf) "Python probe did not resolve an executable"
    Pass "Python interpreter resolution"
    Expect-Failure { Get-AsterPython -Explicit (Join-Path $temporary "missing-python.exe") } "Python 3.10"
    Pass "Missing interpreter rejected before staging"
    $fixture = Join-Path $temporary "collector fixture.py"
    @'
import json
from pathlib import Path
import sys
mode, directory = sys.argv[1:]
root = Path(directory)
print('collector stdout: ' + chr(0x4e2d), flush=True)
print('collector stderr: ' + chr(0x6587), file=sys.stderr, flush=True)
if mode == 'crash':
    raise RuntimeError('simulated failure before report')
if mode == 'no-report':
    sys.exit(0)
if mode == 'invalid-json':
    (root / 'report.json').write_text('{broken', encoding='utf-8')
    sys.exit(2)
if mode == 'invalid-status':
    (root / 'report.json').write_text('{"status":"plan-only"}', encoding='utf-8')
    sys.exit(0)
report = {'status': 'collected', 'errors': []}
if mode == 'incomplete':
    report = {'status': 'incomplete', 'errors': [{'component': 'qt', 'message': 'exact source unavailable'}]}
(root / 'report.json').write_text(json.dumps(report), encoding='utf-8')
sys.exit(2 if mode in ('incomplete', 'late-failure') else 0)
'@ | Set-Content -LiteralPath $fixture -Encoding UTF8
    foreach ($mode in @("success", "crash", "no-report", "invalid-json", "invalid-status", "incomplete", "late-failure")) {
        $output = Join-Path $temporary ("output " + $mode)
        $invoke = { Invoke-AsterCollector -Python $python.Path -CollectorArguments @($fixture, $mode, $output) -MaterialRoot $output -Version "1.1.1" }
        if ($mode -eq "success") { & $invoke } else { Expect-Failure $invoke "Dependency collector failed" }
        $report = Get-Content -LiteralPath (Join-Path $output "report.json") -Raw -Encoding UTF8 | ConvertFrom-Json
        $log = Get-Content -LiteralPath (Join-Path $output "collector.log") -Raw -Encoding UTF8
        Assert-True ($log.Contains("collector stdout: " + [char]0x4e2d)) "Missing/incorrect UTF-8 stdout"
        Assert-True ($log.Contains("collector stderr: " + [char]0x6587)) "Missing/incorrect UTF-8 stderr"
        Assert-True ($log.Contains("Process exit code:")) "Missing process exit code"
        if ($mode -eq "success") {
            Assert-True ($report.status -eq "collected") "Successful report changed"
        } elseif ($mode -eq "incomplete") {
            Assert-True ($report.errors[0].component -eq "qt") "Specific dependency error was discarded"
        } else {
            Assert-True ($report.status -eq "incomplete" -and $report.phase -eq "collector-process") "Missing fallback failure report"
            if ($mode -eq "crash") { Assert-True ($log.Contains("simulated failure before report")) "Traceback not preserved" }
        }
        Pass $mode
    }
    $output = Join-Path $temporary "stale-report"
    New-Item -ItemType Directory -Path $output | Out-Null
    Set-Content -LiteralPath (Join-Path $output ".aster-dependency-materials") -Value "collector output"
    Set-Content -LiteralPath (Join-Path $output "report.json") -Value '{"status":"collected"}'
    Expect-Failure { Invoke-AsterCollector -Python $python.Path -CollectorArguments @($fixture, "no-report", $output) -MaterialRoot $output -Version "1.1.1" } "Dependency collector failed"
    $report = Get-Content -LiteralPath (Join-Path $output "report.json") -Raw | ConvertFrom-Json
    Assert-True ($report.status -eq "incomplete") "Stale successful report accepted"
    Pass "Stale report cannot mask failure"
    $output = Join-Path $temporary "launch-failure"
    Expect-Failure { Invoke-AsterCollector -Python (Join-Path $temporary "missing-python.exe") -CollectorArguments @() -MaterialRoot $output -Version "1.1.1" } "Dependency collector failed"
    $report = Get-Content -LiteralPath (Join-Path $output "report.json") -Raw | ConvertFrom-Json
    Assert-True ($report.phase -eq "collector-process") "Launch failure has no fallback report"
    Pass "Process launch failure"
    $output = Join-Path $temporary "unowned-directory"
    New-Item -ItemType Directory -Path $output | Out-Null
    Set-Content -LiteralPath (Join-Path $output "keep.txt") -Value "keep"
    Expect-Failure { Invoke-AsterCollector -Python $python.Path -CollectorArguments @() -MaterialRoot $output -Version "1.1.1" } "must be empty"
    Assert-True ((Get-Content -LiteralPath (Join-Path $output "keep.txt") -Raw).Trim() -eq "keep") "Unowned directory changed"
    Pass "Unrelated directory preserved"
    $modeParameter = $ast.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq "DependencyMode" }
    Assert-True ($modeParameter.DefaultValue.Value -eq "Reuse") "Windows packaging must default to offline reuse"
    Pass "Offline reuse is the default"
    # Exercise the production mode branches in isolation. Deployment itself
    # requires Windows/Qt/MSVC; these checks need no native deployment tools.
    $preflight = @($ast.EndBlock.Statements | Where-Object {
        $_ -is [System.Management.Automation.Language.IfStatementAst] -and
        $_.Clauses[0].Item1.Extent.Text -eq '$DependencyMode -ne "Stage"'
    })
    Assert-True ($preflight.Count -eq 1) "Missing dependency-mode interpreter preflight"
    $preflightBlock = [ScriptBlock]::Create($preflight[0].Extent.Text)
    $materialStart = @($ast.EndBlock.Statements | Where-Object {
        $_ -is [System.Management.Automation.Language.AssignmentStatementAst] -and
        $_.Left.Extent.Text -eq '$materialRoot'
    })
    Assert-True ($materialStart.Count -eq 1) "Missing material dispatch"
    $materialBlock = [ScriptBlock]::Create($ast.Extent.Text.Substring($materialStart[0].Extent.StartOffset))
    $allowedModes = @($modeParameter.Attributes | Where-Object { $_.TypeName.Name -eq 'ValidateSet' })
    Assert-True (($allowedModes.PositionalArguments.Value -join ',') -eq 'Stage,Reuse,Collect') "Unexpected packaging modes"
    foreach ($mode in @("Stage", "Reuse", "Collect")) {
        & {
            $DependencyMode = $mode
            $PythonExe = "not-an-installed-python"
            $python = $null
            $calls = @{ python = 0; reuse = 0; collect = 0 }
            function Get-AsterPython {
                param([string]$Explicit)
                $calls.python++
                return [pscustomobject]@{ Path = 'test-python'; Version = '3.12.0' }
            }
            . $preflightBlock
            $expected = if ($mode -eq "Stage") { 0 } else { 1 }
            Assert-True ($calls.python -eq $expected) "${mode}: wrong interpreter preflight"
            $stage = Join-Path $temporary ("stage-mode-" + $mode)
            New-Item -ItemType Directory -Path $stage | Out-Null
            $pending = Join-Path $stage "DEPENDENCY_MATERIALS_PENDING.txt"
            Set-Content -LiteralPath $pending -Value "pending"
            Set-Content -LiteralPath (Join-Path $stage "Aster.exe") -Value "deployed fixture"
            $outputRoot = $temporary
            $DependencyMaterials = Join-Path $temporary ("materials-mode-" + $mode)
            $qtRoot = Join-Path $temporary "Qt"
            $dependencyRoot = Join-Path $temporary "vcpkg"
            $VcpkgRoot = $DependencyCache = $DependencySupplement = ""
            $version = "1.2.0-beta.5"
            $name = "Aster-$version-windows-x64"
            $failStep = $keepPending = $false
            function Invoke-AsterCollector {
                param($Python, $CollectorArguments, $MaterialRoot, $Version)
                $calls.collect++
                if ($failStep) { throw "fixture collector failed" }
                if (!$keepPending) { Remove-Item -LiteralPath $pending }
            }
            function Invoke-AsterReuse {
                param($Python, $ReuseArguments)
                $calls.reuse++
                if ($failStep) { throw "fixture reuse failed" }
                if (!$keepPending) { Remove-Item -LiteralPath $pending }
            }
            $messages = (& $materialBlock 6>&1 | Out-String)
            Assert-True (Test-Path -LiteralPath (Join-Path $stage "Aster.exe")) "Deployment was removed"
            if ($mode -eq "Stage") {
                Assert-True ($calls.reuse -eq 0 -and $calls.collect -eq 0) "Stage invoked material processing"
                Assert-True (Test-Path -LiteralPath $pending) "Stage removed the pending marker"
                Assert-True (!(Test-Path -LiteralPath $DependencyMaterials)) "Stage created a material output"
                Assert-True ($messages.Contains("Created staging folder:") -and !$messages.Contains("Created release folder:")) "Stage was described as a completed release"
            } else {
                $expectedReuse = if ($mode -eq "Reuse") { 1 } else { 0 }
                Assert-True ($calls.reuse -eq $expectedReuse -and $calls.collect -eq (1 - $expectedReuse)) "Wrong material process invoked"
                Assert-True (!(Test-Path -LiteralPath $pending)) "Material completion left a marker"
                Assert-True ($messages.Contains("Created release folder:")) "Missing completed-release message"
                Set-Content -LiteralPath $pending -Value "pending"
                $failStep = $true
                Expect-Failure { & $materialBlock } "fixture .* failed"
                Assert-True (Test-Path -LiteralPath $pending) "Failure lost the pending marker"
                $failStep = $false
                $keepPending = $true
                Expect-Failure { & $materialBlock } "verification did not complete"
            }
            Pass "$mode dispatch, interpreter requirement and release gate"
        }
    }
    $output = Join-Path $temporary "reuse-output"
    New-Item -ItemType Directory -Path $output | Out-Null
    foreach ($mode in @("success", "incomplete", "crash")) {
        $invoke = { Invoke-AsterReuse -Python $python.Path -ReuseArguments @($fixture, $mode, $output) }
        if ($mode -eq "success") { & $invoke } else { Expect-Failure $invoke "Dependency reuse failed" }
        Pass "Offline reuse process: $mode"
    }
    Expect-Failure { Invoke-AsterReuse -Python (Join-Path $temporary "missing-python.exe") -ReuseArguments @() } "Dependency reuse failed"
    Pass "Offline reuse launch failure"
    Write-Host "Windows packaging process tests: $script:passed passed, 0 failed"
    Write-Host "Environment: PowerShell $($PSVersionTable.PSVersion); Python $($python.Version); $([Environment]::OSVersion.Platform)"
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force
}

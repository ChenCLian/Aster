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
foreach ($name in @("Get-AsterPython", "Invoke-AsterCollector")) {
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
    Write-Host "Windows packaging process tests: $script:passed passed, 0 failed"
    Write-Host "Environment: PowerShell $($PSVersionTable.PSVersion); Python $($python.Version); $([Environment]::OSVersion.Platform)"
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force
}

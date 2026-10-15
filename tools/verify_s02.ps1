# Rebuild and verify S02; preserve S01 evidence. Run from any directory.
$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\msys64\clang64\bin;C:\msys64\usr\bin;' + $env:PATH
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $projectRoot
$evidenceDir = Join-Path $projectRoot 'results/S02'
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null
$script:records = @()
function Run-Check {
    param([string]$Name, [string]$Program, [string[]]$Arguments, [int]$Expected = 0)
    $log = Join-Path $evidenceDir "$Name.log"
    $displayArgs = $Arguments | ForEach-Object { if ($_ -match '\s') { "'" + $_.Replace("'", "''") + "'" } else { $_ } }
    $command = $Program + ' ' + ($displayArgs -join ' ')
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = & $Program @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    $lines | ForEach-Object { $_.ToString() } | Out-File -LiteralPath $log -Encoding utf8
    $script:records += [pscustomobject]@{Test=$Name; Command=$command; ExitCode=$code; Expected=$Expected}
    $script:records | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'commands.csv')
    Write-Output "$command -> exit=$code expected=$Expected"
    if ($code -ne $Expected) { throw "Unexpected exit: $Name (see $log)" }
}
Run-Check '10-compiler' 'clang' @('--version')
Run-Check '11-clean' 'make' @('clean')
if (Test-Path build) { throw 'clean left build behind' }
Run-Check '12-all' 'make' @('all')
Run-Check '13-debug' 'make' @('debug')
Run-Check '14-contracts' './build/char.exe' @('contracts-check')
Run-Check '15-params' './build/char.exe' @('params-check')
Run-Check '16-boundary' './build/char.exe' @('boundary-check')
Run-Check '17-check' 'make' @('check')
Run-Check '18-debug-check' './build/char_debug.exe' @('check')
Run-Check '19-default' 'make' @('run')
Run-Check '20-constant' './build/main.exe' @('--config','config/constant.cfg','--sample','8')
Run-Check '21-ramp' './build/main.exe' @('--config','config/delayed_ramp.cfg','--sample','2')
Run-Check '22-precedence' './build/main.exe' @('--set','input.Wg=5.7','--config','config/in_range.cfg')
Run-Check '23-invalid-pressure' './build/main.exe' @('--set','input.p_chamber=0') 1
Run-Check '24-legacy-key' './build/main.exe' @('--set','p23_total=3600000') 1
Run-Check '25-cli-missing' './build/main.exe' @('--config') 1
Run-Check '26-noop' 'make' @('-q','all','debug')

$objects = @(Get-ChildItem -LiteralPath build -Filter '*.d' -Recurse | Where-Object {
    (Get-Content -Raw -LiteralPath $_.FullName) -match 'include/solver/Solver.h'
} | ForEach-Object { [IO.Path]::ChangeExtension($_.FullName, '.o') })
if ($objects.Count -ne 14) { throw "Expected 14 state-contract dependents, got $($objects.Count)" }
$before = @{}
foreach ($obj in $objects) { $before[$obj] = (Get-Item -LiteralPath $obj).LastWriteTimeUtc.Ticks }
$header = Get-Item include/solver/Solver.h
$originalTime = $header.LastWriteTimeUtc
try {
    $header.LastWriteTimeUtc = [DateTime]::UtcNow
    Run-Check '27-header-rebuild' 'make' @('all','debug')
    $dependencyResults = foreach ($obj in $objects) {
        $after = (Get-Item -LiteralPath $obj).LastWriteTimeUtc.Ticks
        if ($after -le $before[$obj]) { throw "Object not rebuilt: $obj" }
        [pscustomobject]@{Object=$obj; BeforeTicks=$before[$obj]; AfterTicks=$after; Result='PASS'}
    }
    $dependencyResults | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'dependencies.csv')
} finally { (Get-Item include/solver/Solver.h).LastWriteTimeUtc = $originalTime }
Run-Check '28-noop' 'make' @('-q','all','debug')
Run-Check '29-clean-strict' 'make' @('clean')
Run-Check '30-strict-build' 'make' @('all','debug','EXTRA_CFLAGS=-pedantic-errors -Werror')
Run-Check '31-strict-check' 'make' @('check')
Run-Check '32-strict-debug-check' './build/char_debug.exe' @('check')
Run-Check '33-clean-final' 'make' @('clean')
Run-Check '34-final-build' 'make' @('all','debug')
Run-Check '35-clangd-main' 'clangd' @('--check=src/main.c')
Run-Check '36-noop-final' 'make' @('-q','all','debug')
$warnings = @(Get-ChildItem -LiteralPath $evidenceDir -Filter '*.log' | Where-Object {
    $_.Name -match '^(12|13|27|30|34)-'
} | Select-String -Pattern 'warning:|error:')
if ($warnings.Count) { throw 'Compiler warning/error in build evidence' }
$blueprintFiles = @(Get-ChildItem -LiteralPath '..' -Filter 'ATRcore_*.md' -File)
if ($blueprintFiles.Count -ne 1) { throw 'Expected exactly one frozen ATRcore blueprint' }
$blueprint = (Get-FileHash -LiteralPath $blueprintFiles[0].FullName -Algorithm SHA256).Hash
if ($blueprint -ne 'D92E8B4A1881A50F094507EDC1A861F3D76D8311194247B84009BFB02DE4E86A') {
    throw 'Frozen blueprint hash changed'
}
"SHA256=$blueprint" | Out-File -Encoding utf8 -LiteralPath (Join-Path $evidenceDir 'blueprint.sha256.txt')
Write-Output "PASS S02: $($script:records.Count) commands, 14 header dependents, release/debug/strict C checks."

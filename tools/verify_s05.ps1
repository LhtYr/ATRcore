# Rebuild and verify S05, preserving historical S01-S04 evidence.
$ErrorActionPreference = 'Stop'
$env:GIT_OPTIONAL_LOCKS = '0'
$env:PATH = 'C:\msys64\clang64\bin;C:\msys64\usr\bin;' + $env:PATH
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $projectRoot
$evidenceDir = Join-Path $projectRoot 'results/S05'
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
$script:dependencyRecords = @()
function Check-Dependency {
    param([string]$Header, [int]$ExpectedCount, [string]$LogName)
    $objects = @(Get-ChildItem -LiteralPath build -Filter '*.d' -Recurse | Where-Object {
        (Get-Content -Raw -LiteralPath $_.FullName).Contains($Header)
    } | ForEach-Object { [IO.Path]::ChangeExtension($_.FullName, '.o') })
    if ($objects.Count -ne $ExpectedCount) { throw "Expected $ExpectedCount dependents of $Header, got $($objects.Count)" }
    $before = @{}
    foreach ($obj in $objects) { $before[$obj] = (Get-Item -LiteralPath $obj).LastWriteTimeUtc.Ticks }
    $originalTime = (Get-Item -LiteralPath $Header).LastWriteTimeUtc
    try {
        (Get-Item -LiteralPath $Header).LastWriteTimeUtc = [DateTime]::UtcNow
        Run-Check $LogName 'make' @('all','debug')
        foreach ($obj in $objects) {
            $after = (Get-Item -LiteralPath $obj).LastWriteTimeUtc.Ticks
            if ($after -le $before[$obj]) { throw "Object not rebuilt: $obj" }
            $script:dependencyRecords += [pscustomobject]@{Header=$Header; Object=$obj; BeforeTicks=$before[$obj]; AfterTicks=$after; Result='PASS'}
        }
        $script:dependencyRecords | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'dependencies.csv')
    } finally { (Get-Item -LiteralPath $Header).LastWriteTimeUtc = $originalTime }
}

# Verify that the test reference retains the original arithmetic exactly.
$sourcePath = '../EngineSim/MyFile/EngineSystem/Engine/C_Base/C_Base.c'
$source = Get-Content -Raw -LiteralPath $sourcePath
$terminal = [regex]::Match($source, '(?ms)^sreal\s+C_Terminal_Data\([^\r\n]*\r?\n\{.*?^\}').Value
$exp = [regex]::Match($source, '(?ms)^sreal C_exp\([^\r\n]*\r?\n\{.*?^\}').Value
if (!$terminal -or !$exp) { throw 'Reference extraction failed' }
$expected = ($terminal + "`n" + $exp) -replace '//[^\r\n]*','' -replace '\bsreal\b','double' -replace '\bC_Terminal_Data\b','reference_terminal_data' -replace '\bC_exp\b','reference_exp' -replace '\bTmin\b','1e-6'
$expected = $expected -replace '^double\s+reference_terminal_data','static double reference_terminal_data'
$actual = Get-Content -Raw -LiteralPath 'tools/reference_exp.c'
$actual = $actual -replace '(?s)/\*.*?\*/','' -replace '#include[^\r\n]*',''
if (($expected -replace '\s','') -cne ($actual -replace '\s','')) { throw 'Reference arithmetic differs from source' }
$header = Get-Content -Raw -LiteralPath '../EngineSim/MyFile/EngineSystem/Engine/C_Base/C_Base.h'
if ($header -notmatch '#define\s+sreal\s+double\b' -or $header -notmatch '#define\s+Tmin\s+0\.000001\b') {
    throw 'Reference source type or Tmin changed'
}
"PASS reference arithmetic matches original after documented name/type/comment substitutions" | Out-File -Encoding utf8 -LiteralPath (Join-Path $evidenceDir 'reference-integrity.txt')
Get-FileHash -LiteralPath $sourcePath,'../EngineSim/MyFile/EngineSystem/Engine/C_Base/C_Base.h','tools/reference_exp.c' |
    Select-Object Path,Hash | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'reference.sha256.csv')

# Independent lexical audit of BOTH production and test data against the frozen
# blueprint. This verifies, never regenerates, the checked-in coefficient files.
$blueprints = @(Get-ChildItem -LiteralPath '..' -Filter 'ATRcore_*.md' -File)
if ($blueprints.Count -ne 1) { throw 'Expected one blueprint' }
$blueprintHash = (Get-FileHash -LiteralPath $blueprints[0].FullName).Hash
if ($blueprintHash -ne 'D92E8B4A1881A50F094507EDC1A861F3D76D8311194247B84009BFB02DE4E86A') { throw 'Frozen blueprint changed' }
$rows = @(Get-Content -LiteralPath $blueprints[0].FullName | Where-Object { $_ -match '^\|\s*\d+\s*\|\s*-?\d+\.\d{8}\s*\|' })
if ($rows.Count -ne 50) { throw 'Expected 50 blueprint rows' }
$expectedInc = @()
$expectedCsv = @('i,beta,bias,A0_ox,A1_fuel,A2_pressure')
$mapping = @()
for ($i = 0; $i -lt 50; $i++) {
    $cells = @($rows[$i].Split('|') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
    if ($cells.Count -ne 6 -or [int]$cells[0] -ne $i) { throw 'Blueprint column/index mismatch' }
    $expectedInc += 'GG_ROW(' + ($cells -join ', ') + ')'
    $expectedCsv += $cells -join ','
    $mapping += [pscustomobject]@{Index=$i;Beta=$cells[1];Bias=$cells[2];A0=$cells[3];A1=$cells[4];A2=$cells[5];BetaImageLine=72+$i;BiasImageLine=126+$i;InputImageLine=180+$i}
}
$actualInc = @(Get-Content -LiteralPath 'model/component/gas_generator_coefficients.inc' | Where-Object { $_ -match '^GG_ROW' })
$actualCsv = @(Get-Content -LiteralPath 'tools/fixtures/gg_appendix_a.csv')
if (($actualInc -join "`n") -cne ($expectedInc -join "`n") -or ($actualCsv -join "`n") -cne ($expectedCsv -join "`n")) { throw 'Coefficient data differs from Appendix A' }
$mapping | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'coefficient-audit.csv')
"PASS 250/250 coefficients: literals, signs, order, columns and image line mapping; SHA256=$blueprintHash" | Out-File -Encoding utf8 -LiteralPath (Join-Path $evidenceDir 'blueprint.sha256.txt')
$gitHeadBefore = & git rev-parse HEAD
$indexPath = & git rev-parse --git-path index
$indexHashBefore = (Get-FileHash -LiteralPath $indexPath).Hash
$stagedBefore = (& git ls-files --stage) -join "`n"

Run-Check '10-compiler' 'clang' @('--version')
Run-Check '11-clean' 'make' @('clean')
if (Test-Path build) { throw 'clean left build behind' }
Run-Check '12-all' 'make' @('all')
Run-Check '13-debug' 'make' @('debug')
Run-Check '14-gg-check' './build/char.exe' @('gg-check','tools/fixtures/gg_appendix_a.csv','results/S05/gg_check.csv')
Run-Check '15-debug-gg-check' './build/char_debug.exe' @('gg-check','tools/fixtures/gg_appendix_a.csv','results/S05/gg_check_debug.csv')
Run-Check '16-compare-cases' './build/char.exe' @('gg-compare','results/S05/gg_check.csv','results/S05/gg_check_debug.csv')
Run-Check '17-compare-nodes' './build/char.exe' @('gg-compare','results/S05/gg_check.csv.nodes.csv','results/S05/gg_check_debug.csv.nodes.csv')
Run-Check '18-regression' 'make' @('check')
Run-Check '19-debug-regression' './build/char_debug.exe' @('check')
Run-Check '20-point' './build/char.exe' @('gg-point','3600000','2.05','5.1','results/S05/gg_point.csv')
Run-Check '21-zero-point' './build/char.exe' @('gg-point','3600000','0','0','results/S05/gg_zero.csv')
Run-Check '22-source-init-rejected' './build/char.exe' @('gg-point','101325','0.0001','0.0004','results/S05/invalid_init.csv') 1
Run-Check '23-invalid-cli-number' './build/char.exe' @('gg-point','nan','2.05','5.1','results/S05/invalid_nan.csv') 1
Run-Check '24-invalid-cli-arity' './build/char.exe' @('gg-scan','results/S05/unused.csv','extra') 1
Run-Check '25-missing-parent' './build/char.exe' @('gg-point','3600000','2.05','5.1','build/no-such-s05-dir/out.csv') 1
Run-Check '26-missing-reference' './build/char.exe' @('gg-check','tools/fixtures/no-such-file.csv','results/S05/unused.csv') 1
# Deliberately tamper only a disposable reference fixture to prove detection.
$tampered = (Get-Content -Raw 'tools/fixtures/gg_appendix_a.csv').Replace('7.78444163','7.78444164')
[IO.File]::WriteAllText((Join-Path $evidenceDir 'tampered_coefficients.csv'),$tampered,[Text.UTF8Encoding]::new($false))
Run-Check '27-tampered-reference' './build/char.exe' @('gg-check','results/S05/tampered_coefficients.csv','results/S05/expected_failure.csv') 1
Run-Check '28-scan' './build/char.exe' @('gg-scan','results/S05/gg_scan.csv')
Run-Check '29-debug-scan' './build/char_debug.exe' @('gg-scan','results/S05/gg_scan_debug.csv')
Run-Check '30-compare-scan' './build/char.exe' @('gg-compare','results/S05/gg_scan.csv','results/S05/gg_scan_debug.csv')
Run-Check '31-help' './build/char.exe' @('--help')
Run-Check '32-noop' 'make' @('-q','all','debug')
Check-Dependency 'include/component/GasGenerator.h' 24 '33-header-dependency'
Check-Dependency 'model/component/gas_generator_coefficients.inc' 2 '34-coefficient-dependency'
Run-Check '35-noop' 'make' @('-q','all','debug')
Run-Check '36-clean-strict' 'make' @('clean')
Run-Check '37-strict-build' 'make' @('all','debug','EXTRA_CFLAGS=-pedantic-errors -Werror')
Run-Check '38-strict-check' 'make' @('check')
Run-Check '39-strict-debug-check' './build/char_debug.exe' @('check')
Run-Check '40-clean-final' 'make' @('clean')
Run-Check '41-final-build' 'make' @('all','debug')
Run-Check '42-final-noop' 'make' @('-q','all','debug')
Run-Check '43-run-boundary' 'make' @('run')

$pythonCandidates = @('../.venv/Scripts/python.exe','../.venv/bin/python','.venv/Scripts/python.exe','.venv/bin/python')
$python = $pythonCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (!$python) { $python = if (Get-Command python -ErrorAction SilentlyContinue) { 'python' } else { 'python3' } }
Run-Check '44-python-dependencies' $python @('-m','pip','freeze')
Run-Check '45-plot' 'make' @('plot','PLOT_SCRIPT=plotting/plot_char.py','PLOT_ARGS=--csv results/S05/gg_scan.csv --outdir results/S05/figures')
Run-Check '46-plot-missing-input' $python @('plotting/plot_char.py','--csv','results/S05/no-such.csv','--outdir','results/S05/figures') 1
$warnings = @(Get-ChildItem -LiteralPath $evidenceDir -Filter '*.log' | Where-Object {
    $_.Name -match '^(12|13|33|34|37|41)-'
} | Select-String -Pattern 'warning:|error:')
if ($warnings.Count) { throw 'Compiler warning/error in formal build evidence' }
foreach ($name in @('gg_scan_temperature.png','gg_scan_fuel_slices.png')) {
    if (!(Test-Path -LiteralPath (Join-Path $evidenceDir "figures/$name"))) { throw "Missing figure $name" }
}
$gitHeadAfter = & git rev-parse HEAD
$indexHashAfter = (Get-FileHash -LiteralPath $indexPath).Hash
$stagedAfter = (& git ls-files --stage) -join "`n"
if ($gitHeadBefore -ne $gitHeadAfter -or $stagedBefore -cne $stagedAfter) { throw 'Git HEAD or staged content changed during verification' }
"HEAD=$gitHeadAfter`nIndexSHA256Before=$indexHashBefore`nIndexSHA256After=$indexHashAfter`nPASS HEAD and staged content unchanged; no add/commit/push. Index stat-cache bytes are informational." | Out-File -Encoding utf8 -LiteralPath (Join-Path $evidenceDir 'git-readonly.txt')
Get-FileHash -LiteralPath 'results/S05/gg_check.csv','results/S05/gg_check_debug.csv','results/S05/gg_scan.csv','results/S05/gg_scan_debug.csv' |
    Select-Object Path,Hash | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'csv.sha256.csv')
Write-Output "PASS S05: $($script:records.Count) commands; 250 coefficients; dependencies 24/24 + 2/2; C checks and plots complete; Git unchanged."


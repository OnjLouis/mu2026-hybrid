[CmdletBinding()]
param(
    [ValidateSet('AN', 'DX', 'Both', 'Setup')][string] $Mode,
    [switch] $NonInteractive,
    [switch] $Force
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$state = Join-Path $root 'user'
$python = Join-Path $state 'venv\Scripts\python.exe'
$transcriptStarted = $false
$result = 1
$pauseWhenDone = $false
try {
    if (-not $Mode) {
        if ($NonInteractive -or [Console]::IsInputRedirected -or [Console]::IsOutputRedirected) {
            throw 'Specify -Mode AN, DX, Both or Setup for unattended use.'
        }
        $pauseWhenDone = $true
        Write-Host 'Mu2026 Voice Bank Tools 1.0.0'
        Write-Host '1. Convert AN'
        Write-Host '2. Convert DX'
        Write-Host '3. Convert both'
        Write-Host '4. Set up local Python dependencies (downloads packages)'
        $answer = Read-Host 'Choose 1 to 4'
        $choices = @{ '1' = 'AN'; '2' = 'DX'; '3' = 'Both'; '4' = 'Setup' }
        if (-not $choices.ContainsKey($answer)) { throw 'No valid option selected.' }
        $Mode = $choices[$answer]
    }
    New-Item -ItemType Directory -Path $state -Force | Out-Null
    Start-Transcript -LiteralPath (Join-Path $state 'session.log') -Force | Out-Null
    $transcriptStarted = $true
    if ($Mode -eq 'Setup') {
        Write-Host 'Installing pinned packages from PyPI into user\venv. System Python is not modified.'
        if (-not (Test-Path -LiteralPath $python)) {
            $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
            if ($launcher) {
                & $launcher.Source -3 -m venv (Join-Path $state 'venv')
            } else {
                $launcher = Get-Command python.exe -ErrorAction SilentlyContinue
                if (-not $launcher) { throw 'Python is required. See README.html.' }
                & $launcher.Source -m venv (Join-Path $state 'venv')
            }
            if ($LASTEXITCODE -ne 0) { throw 'Could not create the local Python environment.' }
        }
        & $python -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'
        if ($LASTEXITCODE -ne 0) { throw 'Python 3.10 or newer is required.' }
        & $python -m pip install --disable-pip-version-check --no-cache-dir -r (Join-Path $root 'requirements.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
        Write-Host 'Local setup complete. Run this launcher again to convert.'
    } else {
        if (-not (Test-Path -LiteralPath $python)) { throw 'Run option 4 (Setup) first. Nothing has been converted.' }
        $kinds = if ($Mode -eq 'Both') { @('an', 'dx') } else { @($Mode.ToLowerInvariant()) }
        foreach ($kind in $kinds) {
            $romName = if ($kind -eq 'an') { 'IC3_XZ970A0_MBM29F800BA.bin' } else { 'PLG150-DX.BIN' }
            $manualName = if ($kind -eq 'an') { 'PLG150AN.pdf' } else { 'PLG150DXE.pdf' }
            foreach ($name in @($romName, $manualName)) {
                if (-not (Test-Path -LiteralPath (Join-Path $root "input\$name") -PathType Leaf)) {
                    throw "Required input missing: input\$name. See README.html."
                }
            }
        }
        foreach ($kind in $kinds) {
            $romName = if ($kind -eq 'an') { 'IC3_XZ970A0_MBM29F800BA.bin' } else { 'PLG150-DX.BIN' }
            $manualName = if ($kind -eq 'an') { 'PLG150AN.pdf' } else { 'PLG150DXE.pdf' }
            $arguments = @((Join-Path $root "recover_$kind.py"), '--rom', (Join-Path $root "input\$romName"),
                '--manual', (Join-Path $root "input\$manualName"), '--output', (Join-Path $root "plg-$kind-voices.bin"),
                '--log', (Join-Path $state "$kind.log"))
            if ($Force) { $arguments += '--force' }
            & $python @arguments
            if ($LASTEXITCODE -ne 0) { throw "$kind conversion failed; see user\$kind.log." }
        }
        Write-Host 'Conversion complete. Close the audio host, copy the new bank files beside mu2026-hybrid.dll, then reopen it.'
    }
    $result = 0
} catch {
    Write-Host ('Error: ' + $_.Exception.Message)
} finally {
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
}
if ($pauseWhenDone) { Read-Host 'Press Enter to close' | Out-Null }
exit $result

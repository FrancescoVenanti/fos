<#
.SYNOPSIS
    Builds one PlatformIO environment and prints a short summary.

.EXAMPLE
    .\tools\build.ps1 simulator
    .\tools\build.ps1 sticky
    .\tools\build.ps1 sticky -Target upload
#>
param(
    [Parameter(Mandatory = $true)][string]$Env,
    [string]$Target = "",
    [string]$Log = ""
)

$repo = (Resolve-Path "$PSScriptRoot\..").Path
$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
if (-not (Test-Path $pio)) { $pio = "pio" }
if (-not $Log) { $Log = Join-Path $repo ".pio\build-$Env.log" }
New-Item -ItemType Directory -Force (Split-Path $Log) | Out-Null

# PlatformIO echoes compiler output through Python; force UTF-8 so non-ASCII
# lines (language names, paths) cannot crash it when output is redirected.
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"

$args = @("run", "-d", $repo, "-e", $Env)
if ($Target) { $args += @("-t", $Target) }
& $pio @args *> $Log
$code = $LASTEXITCODE

Select-String -Path $Log -Pattern "error:|undefined reference|multiple definition|\*\*\*" |
    Select-Object -First 30 | ForEach-Object { $_.Line }
Select-String -Path $Log -Pattern "^(RAM|Flash):" | ForEach-Object { $_.Line }
Get-Content $Log -Tail 2
"EXIT=$code (log: $Log)"
exit $code

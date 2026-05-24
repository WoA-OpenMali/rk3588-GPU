<#
.SYNOPSIS
  Push the test cert .cer to the target and install in the trust stores.

.DESCRIPTION
  scp's build\test-cert\WinMaliTest.cer to C:\drv on the target, then
  imports it via SSH into LocalMachine\Root and LocalMachine\TrustedPublisher.
  pnputil checks both stores when validating a driver .cat signature.

  Run this ONCE per target machine (or whenever the cert rotates).
#>
param([switch]$Force)

$ErrorActionPreference = "Stop"
$scriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
$cerPath       = Join-Path $workspaceRoot "build\test-cert\WinMaliTest.cer"

if (-not (Test-Path $cerPath)) {
  throw "Missing $cerPath - run new-test-cert.ps1 first."
}

$cfg = Import-PowerShellDataFile -Path (Join-Path $scriptDir "target.psd1")
$endpoint = "$($cfg.User)@$($cfg.HostIp)"

Write-Host "scp $cerPath -> ${endpoint}:C:/drv/" -ForegroundColor Cyan
& scp -o BatchMode=yes $cerPath "${endpoint}:C:/drv/WinMaliTest.cer"
if ($LASTEXITCODE -ne 0) { throw "scp failed" }

Write-Host "Importing into LocalMachine\Root and LocalMachine\TrustedPublisher..." -ForegroundColor Cyan
$remote = @'
$cer = 'C:\drv\WinMaliTest.cer'
foreach ($store in 'Root','TrustedPublisher') {
  Write-Host ("  Cert:\LocalMachine\{0}" -f $store)
  Import-Certificate -FilePath $cer -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
}
Write-Host "  Verifying:"
Get-ChildItem Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher |
  Where-Object { $_.Subject -match 'WinMali Test Cert' } |
  ForEach-Object { "    {0}  {1}" -f $_.PSParentPath.Split('\')[-1], $_.Thumbprint }
'@

$bytes   = [Text.Encoding]::Unicode.GetBytes($remote)
$encoded = [Convert]::ToBase64String($bytes)
& ssh -o BatchMode=yes $endpoint "powershell -NoProfile -EncodedCommand $encoded"
if ($LASTEXITCODE -ne 0) { throw "Cert import on target failed" }

Write-Host "Done. Test cert is trusted on $($cfg.HostIp)." -ForegroundColor Green

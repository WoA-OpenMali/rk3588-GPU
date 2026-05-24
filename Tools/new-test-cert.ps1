<#
.SYNOPSIS
  Create a self-signed code-signing cert for driver test-signing.

.DESCRIPTION
  Generates a 3-year self-signed code-signing cert in Cert:\CurrentUser\My,
  records its thumbprint in Tools\.testcert.txt, and exports the public
  half to build\test-cert\WinMaliTest.cer for deployment to the target.

  Run this ONCE on the dev box. install-target-cert.ps1 deploys the
  exported .cer to the target's TrustedRoot + TrustedPublisher stores.

.EXAMPLE
  .\new-test-cert.ps1
#>
param([string]$Subject = "CN=WinMali Test Cert")

$ErrorActionPreference = "Stop"
$scriptDir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$workspaceRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
$outDir        = Join-Path $workspaceRoot "build\test-cert"
$thumbFile     = Join-Path $scriptDir ".testcert.txt"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

if (Test-Path $thumbFile) {
  $thumb = (Get-Content $thumbFile -Raw).Trim()
  if ($thumb) {
    $existing = Get-Item "Cert:\CurrentUser\My\$thumb" -ErrorAction SilentlyContinue
    if ($existing) {
      Write-Host "Cert already present (thumbprint $thumb). Skipping." -ForegroundColor Yellow
      Write-Host "  Subject : $($existing.Subject)"
      Write-Host "  NotAfter: $($existing.NotAfter)"
      return
    }
  }
}

$cert = New-SelfSignedCertificate `
  -Type CodeSigningCert `
  -Subject $Subject `
  -KeyAlgorithm RSA -KeyLength 2048 `
  -HashAlgorithm SHA256 `
  -KeyUsage DigitalSignature `
  -CertStoreLocation Cert:\CurrentUser\My `
  -NotAfter (Get-Date).AddYears(3) `
  -KeyExportPolicy Exportable `
  -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3")

Set-Content -Path $thumbFile -Value $cert.Thumbprint -NoNewline

$cerPath = Join-Path $outDir "WinMaliTest.cer"
Export-Certificate -Cert $cert -FilePath $cerPath -Type CERT | Out-Null

Write-Host "Test cert created." -ForegroundColor Green
Write-Host "  Thumbprint : $($cert.Thumbprint)"
Write-Host "  Subject    : $($cert.Subject)"
Write-Host "  NotAfter   : $($cert.NotAfter)"
Write-Host "  Exported to: $cerPath"
Write-Host ""
Write-Host "Next: run install-target-cert.ps1 to deploy the public half to the target."

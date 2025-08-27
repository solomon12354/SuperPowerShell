<#
.SYNOPSIS
Install self-signed test driver and create auto-start service
#>

# Set default service name
$ServiceName = "MyDriver"

# Get current folder
$CurrentDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Set file paths automatically
$DriverSysPath  = Join-Path $CurrentDir "MyDriver.sys"
$DriverInfPath  = Join-Path $CurrentDir "MyDriver.inf"
$PfxPath        = Join-Path $CurrentDir "MyDriver.pfx"
$PfxPassword    = "12345"

Write-Host "Using folder: $CurrentDir"

# Enable test mode
Write-Host "Enabling Windows test mode..."
bcdedit /set testsigning on

# Import certificate to Trusted Root
Write-Host "Importing test certificate..."
$securePwd = ConvertTo-SecureString -String $PfxPassword -Force -AsPlainText
Import-PfxCertificate -FilePath $PfxPath -CertStoreLocation Cert:\LocalMachine\Root -Password $securePwd -Exportable

# Sign driver
Write-Host "Signing driver..."
.\signtool.exe sign /v /f $PfxPath /p $PfxPassword /tr http://timestamp.digicert.com /td sha256 /fd sha256 $DriverSysPath

# Install INF driver
Write-Host "Installing driver..."
pnputil /add-driver $DriverInfPath /install

# Create auto-start service
Write-Host "Creating auto-start service..."
if (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue) {
    Write-Host "Service exists, removing..."
    sc.exe stop $ServiceName
    sc.exe delete $ServiceName
}

sc.exe create $ServiceName type= kernel start= auto binPath= "$DriverSysPath"
sc.exe start $ServiceName

Write-Host "`nDriver installed and service created. Reboot to activate test mode."

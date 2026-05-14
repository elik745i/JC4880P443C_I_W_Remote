$sp = New-Object System.IO.Ports.SerialPort 'COM3',115200,'None',8,'One'
$sp.NewLine = "`r`n"
$sp.ReadTimeout = 200
$sp.WriteTimeout = 1000
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.Open()
$sp.RtsEnable = $true
Start-Sleep -Milliseconds 150
$sp.RtsEnable = $false
Write-Output "Reset issued, waiting for boot..."
Start-Sleep -Seconds 6
$txt = $sp.ReadExisting()
Write-Output ("Got " + $txt.Length + " bytes")
Write-Output $txt
$sp.Close()

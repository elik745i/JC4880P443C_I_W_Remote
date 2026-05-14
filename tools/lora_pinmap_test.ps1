# Comprehensive LoRa pinmap discrimination test.
# Resets the device, exercises both pin orderings, captures full output.
param(
    [string]$Port = 'COM3'
)
function Connect-Device {
    param([string]$PortName)
    $sp = New-Object System.IO.Ports.SerialPort $PortName,115200,'None',8,'One'
    $sp.NewLine = "`r`n"
    $sp.ReadTimeout = 200
    $sp.WriteTimeout = 1000
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.Open()
    $sp.RtsEnable = $true
    Start-Sleep -Milliseconds 150
    $sp.RtsEnable = $false
    Start-Sleep -Seconds 6
    $sp.DiscardInBuffer()
    return $sp
}
function Send-Cmd {
    param($sp,[string]$cmd,[int]$wait_ms=2200)
    Write-Output ""
    Write-Output ("===> " + $cmd)
    $sp.WriteLine($cmd)
    $deadline = (Get-Date).AddMilliseconds($wait_ms)
    while ((Get-Date) -lt $deadline) {
        if ($sp.BytesToRead -gt 0) {
            $chunk = $sp.ReadExisting()
            Write-Output $chunk
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
}

$sp = Connect-Device -PortName $Port
try {
    Write-Output "=========================================="
    Write-Output " TEST A: default map tx=31 rx=30"
    Write-Output "=========================================="
    Send-Cmd $sp 'lora.close' 800
    Send-Cmd $sp 'lora.pinmap.show' 800
    Send-Cmd $sp 'lora.pinmap.set tx=31 rx=30 m0=51 m1=29 aux=33' 800
    Send-Cmd $sp 'lora.apply' 3500
    Send-Cmd $sp 'lora.open' 4500
    Send-Cmd $sp 'lora.selftest.start' 5000
    Send-Cmd $sp 'lora.status' 1200

    Write-Output ""
    Write-Output "=========================================="
    Write-Output " TEST B: swapped map tx=30 rx=31"
    Write-Output "=========================================="
    Send-Cmd $sp 'lora.close' 800
    Send-Cmd $sp 'lora.pinmap.set tx=30 rx=31 m0=51 m1=29 aux=33' 800
    Send-Cmd $sp 'lora.apply' 3500
    Send-Cmd $sp 'lora.open' 4500
    Send-Cmd $sp 'lora.selftest.start' 5000
    Send-Cmd $sp 'lora.status' 1200
} finally {
    $sp.Close()
}

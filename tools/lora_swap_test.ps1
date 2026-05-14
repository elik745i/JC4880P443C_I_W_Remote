$port = 'COM3'
$baud = 115200
$cmds = @(
  'lora.close',
  'lora.pinmap.show',
  'lora.pinmap.set tx=30 rx=31 m0=51 m1=29 aux=33',
  'lora.apply',
  'lora.open',
  'lora.selftest.start',
  'lora.status'
)
$sp = New-Object System.IO.Ports.SerialPort $port,$baud,'None',8,'One'
$sp.NewLine = "`r`n"
$sp.ReadTimeout = 500
$sp.WriteTimeout = 2000
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.Open()
Start-Sleep -Milliseconds 800
$sp.DiscardInBuffer()
foreach ($cmd in $cmds) {
    Write-Output "===> $cmd"
    $sp.WriteLine($cmd)
    $deadline = (Get-Date).AddMilliseconds(2500)
    while ((Get-Date) -lt $deadline) {
        if ($sp.BytesToRead -gt 0) {
            $txt = $sp.ReadExisting()
            Write-Output $txt
        } else {
            Start-Sleep -Milliseconds 50
        }
    }
}
$sp.Close()

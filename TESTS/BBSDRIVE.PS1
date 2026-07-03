# BBSDRIVE.PS1 - scripted telnet session driver for BBS testing.
#
# Connects to a telnet BBS, handles telnet option negotiation and ANSI
# cursor-position-report probes automatically, drives a scripted sequence
# of wait-for-text / send-keys steps, and saves both a raw (with ANSI) and
# a cleaned transcript. Handy for exercising a door module end to end.
#
# usage:
#   BBSDRIVE.PS1 -BbsHost <host> -Steps @(
#       @{wait='User-ID'; send="MYUSER`r"; pause=800},
#       @{wait='Choose';  send='B'} ) -Transcript out.txt -HoldSeconds 6
#
# Each step: wait = regex to await in the cleaned screen ('' = no wait),
#            send = text/keys to transmit ($null = send nothing),
#            pause = optional ms to sleep after sending.
param(
    [Parameter(Mandatory=$true)][array]$Steps,
    [string]$Transcript = "$PSScriptRoot\transcript.txt",
    [Parameter(Mandatory=$true)][string]$BbsHost,
    [int]$Port = 23,
    [int]$StepTimeoutSec = 20,
    [int]$HoldSeconds = 3
)

$client = New-Object System.Net.Sockets.TcpClient
$client.Connect($BbsHost, $Port)
$stream = $client.GetStream()
$stream.ReadTimeout = 500

$raw = New-Object System.IO.MemoryStream    # everything received (post-telnet-stripping)
$enc = [System.Text.Encoding]::GetEncoding(437)

function Send-Bytes([byte[]]$b) { $stream.Write($b, 0, $b.Length); $stream.Flush() }
function Send-Text([string]$s) { Send-Bytes ($enc.GetBytes($s)) }

$IAC=255; $DONT=254; $DO=253; $WONT=252; $WILL=251; $SB=250; $SE=240

# Pump incoming bytes: answer telnet negotiation + ANSI device probes, store the rest
$script:pending = New-Object System.Collections.Generic.List[byte]
function Pump {
    $buf = New-Object byte[] 4096
    try {
        while ($stream.DataAvailable) {
            $n = $stream.Read($buf, 0, $buf.Length)
            if ($n -le 0) { break }
            for ($i=0; $i -lt $n; $i++) { $script:pending.Add($buf[$i]) }
        }
    } catch {}
    # process pending: telnet stripping
    $out = New-Object System.Collections.Generic.List[byte]
    $i = 0
    while ($i -lt $script:pending.Count) {
        $b = $script:pending[$i]
        if ($b -eq $IAC) {
            if ($i+1 -ge $script:pending.Count) { break }  # wait for more
            $cmd = $script:pending[$i+1]
            if ($cmd -eq $IAC) { $out.Add($IAC); $i+=2; continue }
            if ($cmd -in $DO,$DONT,$WILL,$WONT) {
                if ($i+2 -ge $script:pending.Count) { break }
                $opt = $script:pending[$i+2]
                if ($cmd -eq $DO)   { Send-Bytes ([byte[]]($IAC,$WONT,$opt)) }
                if ($cmd -eq $WILL) { if ($opt -in 1,3) { Send-Bytes ([byte[]]($IAC,$DO,$opt)) } else { Send-Bytes ([byte[]]($IAC,$DONT,$opt)) } }
                $i+=3; continue
            }
            if ($cmd -eq $SB) {   # skip subnegotiation to IAC SE
                $j = $i+2
                while ($j+1 -lt $script:pending.Count -and -not ($script:pending[$j] -eq $IAC -and $script:pending[$j+1] -eq $SE)) { $j++ }
                if ($j+1 -ge $script:pending.Count) { break }
                $i = $j+2; continue
            }
            $i+=2; continue
        }
        $out.Add($b); $i++
    }
    $script:pending.RemoveRange(0, $i)
    if ($out.Count -gt 0) {
        $bytes = $out.ToArray()
        $raw.Write($bytes, 0, $bytes.Length)
        # answer ANSI probes: cursor position report ESC[6n -> ESC[24;80R
        $s = $enc.GetString($bytes)
        if ($s.Contains("$([char]27)[6n")) { Send-Text "$([char]27)[24;80R" }
    }
}

function CleanText {
    $s = $enc.GetString($raw.ToArray())
    return ($s -replace "`e\[[0-9;?]*[A-Za-z]", '' -replace "`e", '')
}

foreach ($st in $Steps) {
    $deadline = (Get-Date).AddSeconds($StepTimeoutSec)
    $found = $false
    while ((Get-Date) -lt $deadline) {
        Pump
        $txt = CleanText
        if ($st.wait -eq '' -or $txt -match $st.wait) { $found = $true; break }
        Start-Sleep -Milliseconds 200
    }
    if (-not $found) {
        "STEP-TIMEOUT waiting for: $($st.wait)" | Out-File -Append -Encoding utf8 $Transcript
        break
    }
    if ($null -ne $st.send) { Send-Text $st.send }
    if ($st.pause) { Start-Sleep -Milliseconds $st.pause }
}

# linger to capture trailing output
$deadline = (Get-Date).AddSeconds($HoldSeconds)
while ((Get-Date) -lt $deadline) { Pump; Start-Sleep -Milliseconds 150 }

$client.Close()

# write both the raw (with ANSI) and cleaned transcripts
[System.IO.File]::WriteAllBytes(($Transcript -replace '\.txt$', '.raw'), $raw.ToArray())
CleanText | Out-File -Encoding utf8 $Transcript
"done: $($raw.Length) bytes received"

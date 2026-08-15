$ErrorActionPreference = "Continue"
$log = "e:\Project\TranslateNX\deploy_log.txt"
function Log($m) { $m | Out-File -Append -Encoding ascii $log }
try { Remove-Item $log -ErrorAction SilentlyContinue } catch {}
"=== deploy start $(Get-Date) ===" | Out-File -Encoding ascii $log

$localPath  = "e:\Project\TranslateNX\overlay\translatenx.ovl"
$ip = "192.168.1.6"; $port = 21

# Build the Chinese dir name "1. SD卡:" from explicit UTF-8 bytes to avoid script file encoding issues
$bytes = [byte[]]@(0x31,0x2E,0x20,0x53,0x44,0xE5,0x8D,0xA1,0x3A)  # "1. SD卡:"
$dirName = [System.Text.Encoding]::UTF8.GetString($bytes)

# Correct FTP path percent-encoding (UTF-8 bytes)
function FtpEnc($s) {
    $enc = [System.Text.Encoding]::UTF8
    $b = $enc.GetBytes($s)
    $out = ""
    foreach ($x in $b) {
        if (($x -ge 0x30 -and $x -le 0x39) -or ($x -ge 0x41 -and $x -le 0x5A) -or ($x -ge 0x61 -and $x -le 0x7A)) {
            $out += [char]$x
        } else {
            $out += "%" + $x.ToString("X2")
        }
    }
    return $out
}

$dirPath    = "/" + (FtpEnc $dirName) + "/switch/.overlays"
$remotePath = "$dirPath/translate.ovl"
Log ("dirPath   : $dirPath")
Log ("remotePath: $remotePath")

function FtpReq($path, $method) {
    $uri = "ftp://${ip}:${port}${path}"
    $req = [System.Net.FtpWebRequest]::Create($uri)
    $req.Method = $method
    $req.UseBinary = $true
    $req.UsePassive = $true
    $req.Credentials = New-Object System.Net.NetworkCredential("anonymous","")
    $req.KeepAlive = $false
    return $req
}

try {
    try {
        $d = FtpReq $dirPath ([System.Net.WebRequestMethods+Ftp]::MakeDirectory)
        $dr = $d.GetResponse(); $dr.Close()
        Log "Mkdir OK"
    } catch { Log ("Mkdir skipped: " + $_.Exception.Message) }

    $data = [System.IO.File]::ReadAllBytes($localPath)
    Log ("Local bytes = " + $data.Length)

    $req = FtpReq $remotePath ([System.Net.WebRequestMethods+Ftp]::UploadFile)
    $req.ContentLength = $data.Length
    $s = $req.GetRequestStream()
    $s.Write($data, 0, $data.Length)
    $s.Close()
    $r = $req.GetResponse(); $r.Close()
    Log "Upload done"

    $rs = FtpReq $remotePath ([System.Net.WebRequestMethods+Ftp]::GetFileSize)
    $rr = $rs.GetResponse(); $rsize = $rr.ContentLength; $rr.Close()
    Log ("Remote bytes = " + $rsize)
    if ($data.Length -eq $rsize) { Log "RESULT=OK match" } else { Log "RESULT=MISMATCH" }
} catch {
    Log ("EXCEPTION=" + $_.Exception.Message)
    if ($_.Exception.InnerException) { Log ("INNER=" + $_.Exception.InnerException.Message) }
}

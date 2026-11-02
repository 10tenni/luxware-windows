param(
    [ValidateSet('Check','Install','Library')][string]$Mode = 'Check',
    [string]$InstallDirectory,
    [string]$StateDirectory,
    [string]$CurrentVersion = '1.1.0',
    [int]$ParentProcessId = 0
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:Feed = 'https://github.com/10tenni/luxware-windows/releases/latest/download/latest.json'
$script:AppId = 'luxware-windows-x64'

function Read-Release([string]$Json, [string]$Current) {
    $r = $Json | ConvertFrom-Json
    if ($r.appId -ne $script:AppId -or $r.version -notmatch '^\d{1,4}\.\d{1,4}\.\d{1,4}$' -or
        $r.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $r.bytes -lt 1024 -or $r.bytes -gt 268435456) {
        throw 'Invalid Luxware release manifest.'
    }
    $u = [Uri]$r.url
    if ($u.Scheme -ne 'https' -or $u.Host -ne 'github.com' -or $u.UserInfo -or
        $u.AbsolutePath -notmatch '^/10tenni/luxware-windows/releases/download/v\d+\.\d+\.\d+/Luxware-[0-9.]+-Windows-x64\.zip$') {
        throw 'Release URL is outside the Luxware release repository.'
    }
    if ($r.url -ne ('https://github.com/10tenni/luxware-windows/releases/download/v' + $r.version + '/Luxware-' + $r.version + '-Windows-x64.zip')) {
        throw 'Release version and URL disagree.'
    }
    return @{ Release=$r; Newer=([Version]$r.version -gt [Version]$Current) }
}
function Get-ReleaseFile([string]$Url, [string]$Destination, [long]$Limit) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $began = [DateTime]::UtcNow
    for ($redirect = 0; $redirect -lt 8; $redirect++) {
        $u = [Uri]$Url
        if ($u.Scheme -ne 'https' -or $u.Host -notin @('github.com','release-assets.githubusercontent.com','objects.githubusercontent.com') -or $u.UserInfo) {
            throw 'An update download redirected outside trusted HTTPS hosts.'
        }
        $request = [Net.HttpWebRequest]::Create($u)
        $request.AllowAutoRedirect = $false
        $request.Timeout = 15000
        $request.ReadWriteTimeout = 15000
        $request.UserAgent = 'Luxware-Updater/1.1'
        $response = $request.GetResponse()
        try {
            $code = [int]$response.StatusCode
            if ($code -ge 300 -and $code -lt 400) {
                $Url = ([Uri]::new($u, $response.Headers['Location'])).AbsoluteUri
                continue
            }
            if ($code -ne 200 -or $response.ContentLength -gt $Limit) { throw 'Unexpected download response or size.' }
            $inputStream = $response.GetResponseStream()
            $output = [IO.File]::Create($Destination)
            try {
                $buffer = New-Object byte[] 65536
                [long]$total = 0
                while (($count = $inputStream.Read($buffer,0,$buffer.Length)) -gt 0) {
                    $total += $count
                    if ($total -gt $Limit -or ([DateTime]::UtcNow - $began).TotalSeconds -gt 180) {
                        throw 'Update download exceeded its size or time limit.'
                    }
                    $output.Write($buffer,0,$count)
                }
            } finally { $output.Dispose(); $inputStream.Dispose() }
            return
        } finally { $response.Dispose() }
    }
    throw 'Too many update redirects.'
}
function Assert-Package([string]$Zip, $Release) {
    if ((Get-Item -LiteralPath $Zip).Length -ne $Release.bytes -or
        (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash -ne $Release.sha256) {
        throw 'Update checksum or length mismatch. Nothing was installed.'
    }
}
function Expand-Package([string]$Zip, [string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        [long]$expanded = 0
        $names = New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $archive.Entries) {
            $n = $entry.FullName.Replace('\','/')
            $expanded += $entry.Length
            if ($n -notmatch '^Luxware/' -or $n -match '(^|/)\.\.?(/|$)|:|[\x00-\x1f]' -or
                $n.StartsWith('/') -or $n.Contains('//') -or $n.Split('/') -match '[. ]$' -or
                (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000 -or
                !$names.Add($n.TrimEnd('/')) -or $expanded -gt 1073741824 -or $archive.Entries.Count -gt 5000 -or
                $n -match '(?i)(^|/)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])([./]|$)') {
                throw 'Unsafe or oversized update archive.'
            }
        }
    } finally { $archive.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($Zip,$Destination)
    $payload = Join-Path $Destination 'Luxware'
    foreach ($file in @('Luxware.exe','Models/LuxwarePlayers.onnx','onnxruntime.dll','DirectML.dll','Updates/Update.ps1','release.json')) {
        if (!(Test-Path -LiteralPath (Join-Path $payload $file) -PathType Leaf)) { throw "Missing update file: $file" }
    }
    $identity = Get-Content -LiteralPath (Join-Path $payload 'release.json') -Raw | ConvertFrom-Json
    if ($identity.appId -ne $script:AppId) { throw 'Wrong application package.' }
    return $payload
}
function Install-Payload([string]$Payload, [string]$Target, [string]$Version, [scriptblock]$AfterBackup = {}) {
    $targetPath = [IO.Path]::GetFullPath($Target).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ($targetPath -eq [IO.Path]::GetPathRoot($targetPath) -or
        !(Test-Path -LiteralPath (Join-Path $targetPath 'Luxware.exe') -PathType Leaf)) { throw 'Invalid installation folder.' }
    $identity = Get-Content -LiteralPath (Join-Path $targetPath 'release.json') -Raw | ConvertFrom-Json
    $next = Get-Content -LiteralPath (Join-Path $Payload 'release.json') -Raw | ConvertFrom-Json
    if ($identity.appId -ne $script:AppId -or $next.appId -ne $script:AppId -or $next.version -ne $Version -or
        [Version]$Version -le [Version]$identity.version) { throw 'Update identity or version mismatch.' }
    # Only replace files owned by this package. Never rename/delete the user's app folder.
    $sibling = $targetPath + '.incoming-' + [Guid]::NewGuid().ToString('N')
    $backup = $targetPath + '.previous-' + [Guid]::NewGuid().ToString('N')
    $applied = New-Object 'Collections.Generic.List[object]'
    try {
        Copy-Item -LiteralPath $Payload -Destination $sibling -Recurse
        $files = @(Get-ChildItem -LiteralPath $sibling -Recurse -File)
        [IO.Directory]::CreateDirectory($backup) | Out-Null
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($sibling.Length + 1)
            $destination = Join-Path $targetPath $relative
            $old = Join-Path $backup $relative
            if (Test-Path -LiteralPath $destination -PathType Container) { throw 'An update file collides with a directory.' }
            if (Test-Path -LiteralPath $destination) {
                [IO.Directory]::CreateDirectory((Split-Path $old)) | Out-Null
                Copy-Item -LiteralPath $destination -Destination $old
            }
        }
        try {
            foreach ($file in $files) {
                $relative = $file.FullName.Substring($sibling.Length + 1)
                $destination = Join-Path $targetPath $relative
                $old = Join-Path $backup $relative
                [IO.Directory]::CreateDirectory((Split-Path $destination)) | Out-Null
                # Register before copying so a failed/partial write also restores its original.
                $applied.Add(@{Destination=$destination;Backup=$old})
                Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
                if ($applied.Count -eq 1) { & $AfterBackup }
            }
        } catch {
            $failure = $_
            for ($i=$applied.Count-1; $i -ge 0; $i--) {
                $item = $applied[$i]
                if (Test-Path -LiteralPath $item.Backup) {
                    Copy-Item -LiteralPath $item.Backup -Destination $item.Destination -Force
                } elseif (Test-Path -LiteralPath $item.Destination) {
                    Remove-Item -LiteralPath $item.Destination -Force
                }
            }
            throw $failure
        }
    } finally {
        if (Test-Path -LiteralPath $sibling) { Remove-Item -LiteralPath $sibling -Recurse -Force }
    }
    # Keep original replaced files for manual rollback. Unrelated files are untouched.
    return $backup
}
function Write-Status([string]$State, [string]$Message) {
    $Message = $Message.Replace("`r",' ').Replace("`n",' ')
    $text = "[Update]`r`nState=$State`r`nMessage=$Message`r`n"
    $temp = Join-Path $StateDirectory 'status.tmp'
    [IO.File]::WriteAllText($temp,$text,[Text.Encoding]::Unicode)
    Move-Item -LiteralPath $temp -Destination (Join-Path $StateDirectory 'status.ini') -Force
}
if ($Mode -eq 'Library') { return }
try {
    [IO.Directory]::CreateDirectory($StateDirectory) | Out-Null
    if ($Mode -eq 'Check') {
        Write-Status 'Checking' 'Checking for updates...'
        Get-ReleaseFile $script:Feed (Join-Path $StateDirectory 'manifest.json') 16384
        $info = Read-Release (Get-Content -LiteralPath (Join-Path $StateDirectory 'manifest.json') -Raw) $CurrentVersion
        if (!$info.Newer) { Write-Status 'Current' 'Luxware is up to date.'; exit 0 }
        Write-Status 'Downloading' ('Downloading Luxware ' + $info.Release.version + '...')
        $zip = Join-Path $StateDirectory 'package.zip'
        $cached = $false
        if (Test-Path -LiteralPath $zip) {
            try { Assert-Package $zip $info.Release; $cached = $true } catch {}
        }
        if (!$cached) { Get-ReleaseFile $info.Release.url $zip $info.Release.bytes }
        Assert-Package $zip $info.Release
        Write-Status 'Ready' ('Update ' + $info.Release.version + ' ready. Install when you are finished playing.')
    } else {
        $info = Read-Release (Get-Content -LiteralPath (Join-Path $StateDirectory 'manifest.json') -Raw) $CurrentVersion
        if (!$info.Newer) { throw 'This update is not newer.' }
        $zip = Join-Path $StateDirectory 'package.zip'
        Assert-Package $zip $info.Release
        $unpack = Join-Path $StateDirectory ('unpack-' + [Guid]::NewGuid().ToString('N'))
        $payload = Expand-Package $zip $unpack
        if ($ParentProcessId -le 0) { throw 'No application process to update.' }
        $parent = Get-Process -Id $ParentProcessId -ErrorAction SilentlyContinue
        if ($parent) {
            $expected = [IO.Path]::GetFullPath((Join-Path $InstallDirectory 'Luxware.exe'))
            if (![String]::Equals($parent.Path,$expected,[StringComparison]::OrdinalIgnoreCase)) { throw 'Application process does not match its folder.' }
            if (!$parent.WaitForExit(60000)) { throw 'Luxware did not close. Update cancelled.' }
        }
        $backup = Install-Payload $payload $InstallDirectory $info.Release.version
        Write-Status 'Installed' ('Updated to ' + $info.Release.version + '. Previous version: ' + $backup)
        # The user explicitly requested installation; restart with all input off.
        Start-Process -FilePath (Join-Path $InstallDirectory 'Luxware.exe') -WorkingDirectory $InstallDirectory
    }
    exit 0
} catch {
    try { Write-Status 'Error' ('Update failed: ' + $_.Exception.Message) } catch {}
    exit 1
}

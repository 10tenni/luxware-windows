param([string]$Report)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../Updates/Update.ps1') -Mode Library
$results = [ordered]@{}
function Check([string]$Name, [scriptblock]$Action) {
    & $Action
    $results[$Name] = $true
}
function Must-Fail([scriptblock]$Action) {
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    if (!$failed) { throw 'Expected rejection did not occur.' }
}
$root = Join-Path ([IO.Path]::GetTempPath()) ('luxware-update-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($root) | Out-Null
try {
    $release = @{appId='luxware-windows-x64';version='1.2.0';sha256=('a'*64);bytes=4096;url='https://github.com/10tenni/luxware-windows/releases/download/v1.2.0/Luxware-1.2.0-Windows-x64.zip'}
    Check 'semanticVersionOrdering' { if (!(Read-Release ($release|ConvertTo-Json) '1.1.0').Newer) { throw 'Version' } }
    Check 'sameVersionNoUpdate' { if ((Read-Release ($release|ConvertTo-Json) '1.2.0').Newer) { throw 'Version' } }
    Check 'downgradeRejected' { if ((Read-Release ($release|ConvertTo-Json) '1.10.0').Newer) { throw 'Version' } }
    Check 'wrongAppRejected' { $copy=$release.Clone();$copy.appId='other'; Must-Fail { Read-Release ($copy|ConvertTo-Json) '1.0.0' } }
    Check 'externalURLRejected' { $copy=$release.Clone();$copy.url='https://example.com/app.zip'; Must-Fail { Read-Release ($copy|ConvertTo-Json) '1.0.0' } }
    Check 'insecureURLRejected' { $copy=$release.Clone();$copy.url=$copy.url.Replace('https:','http:'); Must-Fail { Read-Release ($copy|ConvertTo-Json) '1.0.0' } }
    Check 'invalidHashRejected' { $copy=$release.Clone();$copy.sha256='missing'; Must-Fail { Read-Release ($copy|ConvertTo-Json) '1.0.0' } }
    Check 'versionURLMismatchRejected' { $copy=$release.Clone();$copy.version='1.3.0'; Must-Fail { Read-Release ($copy|ConvertTo-Json) '1.0.0' } }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $package = Join-Path $root 'package'
    $payload = Join-Path $package 'Luxware'
    foreach ($name in @('Luxware.exe','Models/LuxwarePlayers.onnx','onnxruntime.dll','DirectML.dll','Updates/Update.ps1')) {
        $p=Join-Path $payload $name;[IO.Directory]::CreateDirectory((Split-Path $p))|Out-Null;[IO.File]::WriteAllText($p,'test fixture, not an executable')
    }
    [IO.File]::WriteAllText((Join-Path $payload 'release.json'),'{"appId":"luxware-windows-x64","version":"1.2.0"}')
    $zip=Join-Path $root 'release.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($package,$zip)
    $release.bytes=(Get-Item $zip).Length;$release.sha256=(Get-FileHash $zip -Algorithm SHA256).Hash
    Check 'correctArchiveHashAccepted' { Assert-Package $zip $release }
    Check 'tamperedArchiveRejected' { $bad=$release.Clone();$bad.sha256=('0'*64);Must-Fail { Assert-Package $zip $bad } }
    Check 'wrongArchiveSizeRejected' { $bad=$release.Clone();$bad.bytes++;Must-Fail { Assert-Package $zip $bad } }
    Check 'packageExtraction' { $script:extracted=Expand-Package $zip (Join-Path $root 'extract') }
    Check 'zipTraversalRejected' {
        $badZip=Join-Path $root 'bad.zip';$a=[IO.Compression.ZipFile]::Open($badZip,'Create')
        $a.CreateEntry('Luxware/../../outside.txt')|Out-Null;$a.Dispose()
        Must-Fail { Expand-Package $badZip (Join-Path $root 'badextract') }
        if (Test-Path (Join-Path $root 'outside.txt')) { throw 'Escaped ZIP' }
    }
    Check 'caseCollisionRejected' {
        $badZip=Join-Path $root 'collision.zip';$a=[IO.Compression.ZipFile]::Open($badZip,'Create')
        $a.CreateEntry('Luxware/FILE')|Out-Null;$a.CreateEntry('Luxware/file')|Out-Null;$a.Dispose()
        Must-Fail { Expand-Package $badZip (Join-Path $root 'collision') }
    }
    $target=Join-Path $root 'installed';[IO.Directory]::CreateDirectory($target)|Out-Null
    [IO.File]::WriteAllText((Join-Path $target 'Luxware.exe'),'old version')
    [IO.File]::WriteAllText((Join-Path $target 'release.json'),'{"appId":"luxware-windows-x64","version":"1.1.0"}')
    [IO.File]::WriteAllText((Join-Path $target 'unrelated.txt'),'keep unrelated file')
    $userData=Join-Path $root 'user-settings.ini';[IO.File]::WriteAllText($userData,'user settings remain')
    Check 'failedReplacementRollsBack' {
        Must-Fail { Install-Payload $extracted $target '1.2.0' { throw 'Injected failure after backup' } }
        if ((Get-Content (Join-Path $target 'Luxware.exe') -Raw) -ne 'old version') { throw 'Rollback failed' }
    }
    Check 'successfulReplacementKeepsBackup' {
        $backup=Install-Payload $extracted $target '1.2.0'
        if ((Get-Content (Join-Path $target 'release.json') -Raw|ConvertFrom-Json).version -ne '1.2.0' -or
            (Get-Content (Join-Path $backup 'Luxware.exe') -Raw) -ne 'old version') { throw 'Install failed' }
    }
    Check 'unrelatedInstallFilesPreserved' { if ((Get-Content (Join-Path $target 'unrelated.txt') -Raw) -ne 'keep unrelated file') { throw 'Unrelated file changed' } }
    Check 'settingsPreserved' { if ((Get-Content $userData -Raw) -ne 'user settings remain') { throw 'User data changed' } }
    Check 'repeatInstallRejected' { Must-Fail { Install-Payload $extracted $target '1.2.0' } }
    $reportObject=@{passed=$true;checks=$results;host=[Environment]::OSVersion.VersionString;scope='Updater manifest, checksum, ZIP validation, filesystem replacement and forced rollback on temporary fixtures; no live app replaced.'}
    if ($Report) { $reportObject|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $Report -Encoding UTF8 }
    Write-Output ('Passed '+$results.Count+' updater checks')
} finally { Remove-Item -LiteralPath $root -Recurse -Force }

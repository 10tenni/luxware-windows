$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
  throw 'Install Visual Studio 2022 Build Tools with Desktop development with C++ and CMake, then open Developer PowerShell.'
}
cmake -S . -B build-windows -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
cmake --build build-windows --config Release
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Write-Host 'Built build-windows\Release\Luxware.exe with its runtime/model folders.'

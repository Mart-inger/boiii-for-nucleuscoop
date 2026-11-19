$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output = Join-Path $repo 'build\hotkeys-validation'
New-Item -ItemType Directory -Path $output -Force | Out-Null

# Use the actual component implementation with simulated engine/input state.
# Named mutexes and the peer publishers use real Windows processes and APIs.
$body = [IO.File]::ReadAllText((Join-Path $repo 'src\client\component\host_hotkeys.cpp'))
$body = [regex]::Replace($body, '(?m)^#include[^\r\n]*\r?\n', '')
$prefix = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'host_hotkeys_fixture.cpp'))
$cases = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'host_hotkeys_cases.cpp'))
[IO.File]::WriteAllText((Join-Path $output 'hotkeys-tests.cpp'), $prefix + "`n" + $body + "`n" + $cases)
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'host_hotkeys.vcxproj') -Destination (Join-Path $output 'hotkeys-tests.vcxproj') -Force

$msbuild = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
if ($msbuild) {
    $msbuildPath = $msbuild.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'MSBuild was not found. Run from a Visual Studio developer shell.'
    }
    $msbuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if (-not $msbuildPath) { throw 'MSBuild was not found.' }
}
& $msbuildPath (Join-Path $output 'hotkeys-tests.vcxproj') /p:Configuration=Release /p:Platform=x64 /m:1 /nr:false /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Hotkey test build failed.' }
& (Join-Path $output 'hotkeys-tests\hotkeys-tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Hotkey regression tests failed.' }

param(
    [switch]$Test,
    [string]$Distribution = 'Ubuntu-24.04'
)

$ErrorActionPreference = 'Stop'
$LinuxRoot = & wsl.exe -d $Distribution -- wslpath -u $PSScriptRoot
if ($LASTEXITCODE -ne 0) {
    throw 'Cannot resolve the project path in WSL.'
}

if ($Test) {
    & wsl.exe -d $Distribution --cd $LinuxRoot -- bash test.sh
    if ($LASTEXITCODE -ne 0) {
        throw 'Firmware tests failed.'
    }
}

& wsl.exe -d $Distribution --cd $LinuxRoot -- bash build.sh
if ($LASTEXITCODE -ne 0) {
    throw 'Firmware build failed.'
}

param(
    [ValidateSet('rev3', 'early')][string]$ChipRevision = 'early',
    [string]$IdfRoot = $env:IDF_PATH,
    [string]$ToolRoot = 'C:\Espressif'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$env:IDF_PATH = $IdfRoot
$env:IDF_TOOLS_PATH = Join-Path $env:USERPROFILE '.espressif'
$env:IDF_PYTHON_ENV_PATH = Join-Path $ToolRoot 'tools\python\v5.5.4\venv'
$env:ESP_ROM_ELF_DIR = Join-Path $ToolRoot 'tools\esp-rom-elfs\20241011'
$pythonExe = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
$env:PATH = (Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts') + ';' +
    (Join-Path $ToolRoot 'tools\cmake\3.30.2\bin') + ';' +
    (Join-Path $ToolRoot 'tools\ninja\1.12.1') + ';' +
    (Join-Path $ToolRoot 'tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin') + ';' + $env:PATH
$buildDir = if ($ChipRevision -eq 'early') { 'build-p4-early' } else { 'build-p4' }
$configFile = if ($ChipRevision -eq 'early') { 'sdkconfig.p4-early' } else { 'sdkconfig.p4' }
$defaults = Join-Path $projectRoot 'sdkconfig.defaults'
if ($ChipRevision -eq 'early') {
    $defaults += ';' + (Join-Path $projectRoot 'sdkconfig.defaults.early')
}
Push-Location $projectRoot
try {
    if (!(Test-Path $configFile)) { Copy-Item config/st7102-p4-rev1.defaults $configFile }
    $env:SF64_CC = Join-Path $ToolRoot 'tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin/riscv32-esp-elf-gcc.exe'
    if (!(Select-String -Path third_party/tgx/src/Renderer3D.h -Pattern 'setFragmentAlpha' -Quiet)) {
        & git -C third_party/tgx apply --check "$projectRoot/patches/tgx-alpha.patch"
        if ($LASTEXITCODE) { throw 'TGX patch preflight failed' }
        & git -C third_party/tgx apply "$projectRoot/patches/tgx-alpha.patch"
        if ($LASTEXITCODE) { throw 'TGX patch failed' }
    }
    if (!(Test-Path build-starship/starship-core.o)) {
        & $pythonExe tools/starship_compile.py
        if ($LASTEXITCODE -or !(Test-Path build-starship/starship-core.o)) { throw 'Native core compilation failed' }
    }
    & $pythonExe tools/build_ship_runtime.py
    if ($LASTEXITCODE) { throw 'Runtime archive generation failed' }
    & $pythonExe "$IdfRoot\tools\idf.py" -B $buildDir -D "SDKCONFIG=$projectRoot\$configFile" -D "SDKCONFIG_DEFAULTS=$defaults" build
    if ($LASTEXITCODE -ne 0) { throw "ESP-IDF build failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}

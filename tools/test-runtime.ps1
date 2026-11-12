param([string]$ToolRoot='C:\Espressif')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$compiler=Join-Path $ToolRoot 'tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin\riscv32-esp-elf-g++.exe'
$pythonExe=Join-Path $ToolRoot 'tools\python\v5.5.4\venv\Scripts\python.exe'
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force build-tests | Out-Null
    if (!(Test-Path build-tests/python/unicorn)) {
        & $pythonExe -m pip install --target build-tests/python 'unicorn==2.1.4' 'pyelftools==0.33' --disable-pip-version-check
        if ($LASTEXITCODE) { throw 'Test dependency installation failed' }
    }
    & $compiler -march=rv32im -mabi=ilp32 -O2 -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -msmall-data-limit=0 -nostdlib -Imain -Itools/test_stubs main/f3dex.cpp main/input.c main/mio0.cpp main/audio_output.c tools/f3dex_test_adapter.cpp '-Wl,-e,test_run' '-Wl,-Ttext=0x10000' '-Wl,--no-relax' -o build-tests/f3dex-test.elf
    if ($LASTEXITCODE) { throw 'RV32IM test compilation failed' }
    & $pythonExe tools/export_arwing.py
    if ($LASTEXITCODE) { throw 'Model export failed' }
    & $pythonExe tools/test_rom_tools.py
    if ($LASTEXITCODE) { throw 'ROM tests failed' }
    & $pythonExe tools/test_f3dex_riscv.py
    if ($LASTEXITCODE) { throw 'RV32IM tests failed' }
} finally { Pop-Location }

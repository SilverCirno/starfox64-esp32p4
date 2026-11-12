# Star Fox 64 for ESP32-P4

An experimental native ESP32-P4 port based on [Starship](https://github.com/HarbourMasters/Starship), using [TGX](https://github.com/vindar/tgx) for software rendering and ESP-IDF for display and input.

## Hardware

- ESP32-P4 revision 1.x, 16 MB flash and 32 MB PSRAM.
- 480 x 480 ST7102 MIPI DSI display.
- ST7123 touch controller on I2C: SDA GPIO 7, SCL GPIO 8.
- 320 x 240 rendering scaled to a 480 x 360 game viewport.

Other boards may require changes to the display, memory and pin configuration.

## Controls

The screen provides a virtual analog stick and A, B, Z, Start, L, R and C buttons.
Start opens the normal menu flow. Z on the title or menu launches Corneria directly.
The input provider interface supports replacing touch controls with another controller implementation.

## ROM separation

The program and game data occupy separate flash partitions:

| Content | Offset | Size |
| --- | --- | --- |
| Application | `0x10000` | up to `0x3f0000` |
| User-provided ROM | `0x400000` | 12 MiB |

This repository and its firmware do not include a ROM. Supply your own legally obtained **USA Rev A / US 1.1** dump. SHA-1: `09f0d105f476b00efa5303a3ebc42e60a7753b7a`.

The checked-in resource metadata contains names, offsets and layout information. Models, textures, animations and dialogue are loaded from the separate ROM partition at runtime.

## Build

Requirements: Windows PowerShell, Git, ESP-IDF 5.5.4, the ESP32-P4 RISC-V toolchain, and Python. The default tool root is `C:\Espressif`.

```powershell
git clone --recurse-submodules https://github.com/SilverCirno/starfox64-esp32p4.git
cd starfox64-esp32p4
python -m pip install -r requirements.txt
.\tools\build.ps1 -IdfRoot "C:\path\to\esp-idf" -ToolRoot "C:\Espressif"
```

The build applies the checked-in TGX patch and compiles the native C core. A ROM is not required to compile the application.

## Flash the application

Use the files in `firmware/`, or your own files under `build-p4-early/`. Replace `COMx` with your board's port.

```powershell
python -m esptool --chip esp32p4 --port COMx write_flash 0x2000 firmware/bootloader.bin 0x8000 firmware/partition-table.bin 0x10000 firmware/starfox64.bin
```

This command does not provision the ROM partition. Avoid `erase_flash` when preserving an already provisioned ROM.

## Prepare and provision your ROM locally

```powershell
python tools/prepare_rom.py "C:\path\to\your-own-dump.zip"
python -m esptool --chip esp32p4 --port COMx write_flash 0x400000 assets/local/baserom.us.z64
```

ROMs, extracted data and full-device backups must remain local. Do not add them to commits, release attachments or firmware bundles.

## Credits and licenses

See [NOTICE.md](NOTICE.md), [LICENSE](LICENSE) and `LICENSES/`. Upstream source revisions are pinned in `.gitmodules` and `third_party/sources.lock.json`.

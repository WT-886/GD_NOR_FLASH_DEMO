# GD32F450 SPI NOR Demo

SPI5 NOR Flash demo for GD32F450ZKT6, with separate single/dual/quad read APIs,
single/quad program APIs, and an optional full-device random test.

**Warning: the current code enables the destructive full-device test. It erases
all 4 MiB of external NOR data on every reset. Back up data before running.**

## Features

- Single read (`03h`), dual-output read (`3Bh`), quad-output read (`6Bh`).
- Single Page Program (`02h`), quad Page Program (`32h`), 4 KiB sector erase.
- 256-byte page splitting, byte-by-byte verification and serial diagnostics.
- JEDEC ID is printed, not used as a device whitelist.
- Full-device test: erase/blank-check, single random program/readback, erase,
  quad random program/readback, then final erase/blank-check.

Single and quad transfers use hardware SPI5. **Dual data reception currently
uses GPIO diagnostic sampling**, not native hardware dual SPI. Passing a blank
`FF` comparison does not prove that random-data transfers are reliable.

The NOR command sequence follows the official GD32F4xx Demo Suites V2.6.4
`GD32450Z_EVAL_Demo_Suites/Projects/12_SPI_Quad_Flash` example. The official
example does not provide dual reads. See [detailed notes](src/readme.txt).

## Wiring

| MCU Pin | NOR Signal |
| --- | --- |
| PG9 | CS# |
| PG13 | CLK |
| PG14 | IO0 / MOSI |
| PG12 | IO1 / MISO |
| PG10 | IO2 / WP# |
| PG11 | IO3 / HOLD# |
| GND | GND |

Match Flash supply and I/O voltage to the exact device datasheet; do not connect
a 1.8 V device directly to 3.3 V I/O. Use short connections and local decoupling.
Console: USART0, PA9 TX / PA10 RX, **115200 baud, 8N1**.

## Configure

Edit [inc/spi_nor.h](inc/spi_nor.h). Test switches are code settings, not CMake
options; all build directories use the same header configuration.

| Setting | Current Value / Meaning |
| --- | --- |
| `SPI_NOR_FLASH_SIZE` | 4 MiB = 32 Mbit; addresses `0x000000` to `0x3FFFFF` |
| `SPI_NOR_PRESCALE` | `SPI_PSC_8`; SPI clock is APB2 divided by 8 |
| `SPI_NOR_PAGE_SIZE` | 256 bytes |
| `SPI_NOR_SECTOR_SIZE` | 4096 bytes |
| `SPI_NOR_DEMO_ADDRESS` | `0x00100000`; reserved sector for the simple test |

Select the test using these two definitions:

| `SPI_NOR_DEMO_ALLOW_ERASE` | `SPI_NOR_FULL_TEST` | Mode |
| --- | --- | --- |
| `0U` | `0U` | Simple comparison of existing array data |
| `1U` | `0U` | Destructive single-sector write/read demo |
| `1U` | `1U` | Destructive full-device random test (currently selected) |

`0U / 1U` is rejected at compile time. Quad reads may set nonvolatile SR2.QE
even when array erase/program is disabled. Protection bits are preserved.
Verify the actual Flash capacity, QE location and command compatibility before
use; the current profile uses 24-bit addressing and GD25Q/W25Q-style registers.

## Build

Requirements: CMake 3.20+, Ninja and an ARM GNU `arm-none-eabi` toolchain.
Configure the local compiler directory explicitly:

```powershell
cmake --preset Debug -DTOOLCHAIN_BIN_DIR="C:/Tools/ArmGNU/bin"
cmake --build --preset Debug
```

Replace the example path with your installation. The existing default in
[arm-none-eabi-gcc.cmake](arm-none-eabi-gcc.cmake) is a workstation-specific path.
CMake and Ninja must be available to the terminal or configured in the IDE.
Outputs are generated under `build/Debug/` as ELF, HEX, BIN, MAP and LST files.
Changing a test mode requires rebuilding and downloading the new image.

## Flash And Run

1. Stop all active GDB/OpenOCD sessions to release the debug probe.
2. Download `build/Debug/GD32450Z_QSPI_Nor.hex` using GD-Link/DevSuite or OpenOCD.
3. Open the serial console before resetting the board.
4. For debugging, choose **GD32 Debug (OpenOCD)** in VS Code. Update the local
   GDB and OpenOCD paths in [.vscode/launch.json](.vscode/launch.json) first.

The simple test prints `demo PASS/FAIL`. The full-device test prints progress,
random seeds, the first failing address/value, and `FULL CHIP TEST PASS/FAIL`.
On success it leaves the whole device erased; on failure it preserves the data
and skips later stages. Earlier random-data tests found read mismatches; this
repository is a demonstration and diagnostic project, not a qualified driver.

After testing, set both code switches to `0U`, rebuild and download again.
Editing the header alone does not disable a destructive image already installed.

## Layout

| Path | Purpose |
| --- | --- |
| [src/main.c](src/main.c) | Initialization, simple demo and optional test call |
| [src/spi_nor.c](src/spi_nor.c) | Independent NOR read/program/erase transactions |
| [src/spi_nor_test.c](src/spi_nor_test.c) | Full-device random test and private buffers |
| [inc/spi_nor_test.h](inc/spi_nor_test.h) | `spi_nor_full_test()` entry point |
| [CMakePresets.json](CMakePresets.json) | Debug/Release build presets |
| [openocd_gdlink/openocd_gdlink.cfg](openocd_gdlink/openocd_gdlink.cfg) | CMSIS-DAP/SWD configuration |
| `Firmware/`, `gcc_startup/`, `ldscripts/` | Vendor library, startup and linker script |

The OpenOCD script and linker currently use a conservative 512 KiB internal MCU
Flash range; this is separate from the external 4 MiB NOR capacity. Keep the
original vendor license notices when redistributing vendor files.

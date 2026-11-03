# GD32F450 SPI NOR Demo

SPI5 NOR Flash demo for GD32F450ZKT6, with separate single/dual/quad read APIs,
single/quad program APIs, and an optional full-device random test.

**Warning: enabling the full-device test erases all 4 MiB of external NOR data
on every reset. Back up data before enabling destructive tests.**

## Author And Development

- Author / maintainer: **Teng Wu**, Flash Field Application Engineer (FAE),
   **GigaDevice**.
- Contact: [teng.wu@gigadevice.com](mailto:teng.wu@gigadevice.com).
- AI assistance: **GitHub Copilot** assisted with demo implementation, test
   code, build/debug analysis and documentation. The author directed the
   requirements and performed hardware validation.

This attribution covers the demo-specific additions. Vendor libraries,
startup files and reference components retain their original attribution
and license notices. AI assistance and board testing do not constitute
production qualification or guarantee compatibility with every Flash device.

## Features

- Single read (`03h`), dual-output read (`3Bh`), quad-output read (`6Bh`).
- Single Page Program (`02h`), quad Page Program (`32h`), 4 KiB sector erase.
- Chip Erase (`C7h`) with a separate configurable BUSY timeout.
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
| `SPI_NOR_FULL_TEST_CHIP_ERASE` | `0U`: sector loop; `1U`: use C7h in the full test |
| `SPI_NOR_CHIP_ERASE_TIMEOUT_MS` | `180000U`; adjust to the actual datasheet maximum |

Select the test using these two definitions:

| `SPI_NOR_DEMO_ALLOW_ERASE` | `SPI_NOR_FULL_TEST` | Mode |
| --- | --- | --- |
| `0U` | `0U` | Simple comparison of existing array data |
| `1U` | `0U` | Destructive single-sector write/read demo |
| `1U` | `1U` | Destructive full-device random test |

`0U / 1U` is rejected at compile time. Quad reads may set nonvolatile SR2.QE
even when array erase/program is disabled. Protection bits are preserved.
Verify the actual Flash capacity, QE location and command compatibility before
use; the current profile uses 24-bit addressing and GD25Q/W25Q-style registers.

`spi_nor_chip_erase()` erases the entire physical chip, regardless of the
configured capacity. The header selects sector erases or C7h for the full test.
Selecting C7h changes all three erase stages and still blank-checks every byte.
Chip Erase can take much longer than a sector erase. A timeout does not cancel
the physical operation; do not reset or program the device while it is busy.

## GD25Q32E Protection

This branch provides three fixed array-protection presets, not an arbitrary
address-range configuration API:

| API | Protected Addresses | BP / CMP |
| --- | --- | --- |
| `spi_nor_protect_all()` | `0x000000` to `0x3FFFFF` | `07h / 0` |
| `spi_nor_protect_bottom64k()` | `0x000000` to `0x00FFFF` | `09h / 0` |
| `spi_nor_unprotect()` | None | `00h / 0` |

Call one preset explicitly after `spi_nor_init()` and check its result:

```c
spi_nor_protection_t state;
spi_nor_protect_result_t result = spi_nor_protect_bottom64k();
if (SPI_NOR_PROTECT_OK == result) {
   result = spi_nor_get_protection(&state);
}
```

Use the other two presets instead of the partial preset when needed. Status
is read fresh for each operation. One Write Enable is followed by `01h`,
the new SR1 byte and the new SR2 byte under the same CS, matching the official
GD25Qxx reference. BP and CMP change together; SRP, LB and QE are preserved.
Each write has a 100 ms BUSY wait and strict final readback checks. This replaces
the separate `01h`/`31h` path, which failed to preserve QE in reported testing.
The paired sequence preserved QE in the subsequent board log supplied by the author.

For simplicity, any set SRP0 or SRP1 returns `SPI_NOR_PROTECT_ERR_LOCKED`,
even if a more complete WP#/QE policy could allow writing. This demo never
changes SRP or permanent locks. A failure may leave a partly updated setting;
read status again before retrying. Protection is nonvolatile on this profile.
Writes/sector erases outside the protected range remain available; overlap is
rejected, and Chip Erase is rejected while any array range is protected.

At the end of `main()`, after the selected array test, a simple protection demo
runs: prepare data -> full protection -> unprotect -> bottom 64 KiB protection
-> unprotect.
Each step prints its result and fresh SR1/SR2, BP/CMP and protected range.
With SPI_NOR_DEMO_ALLOW_ERASE=1U, the protection demo also uses disposable sectors
at `0x000000` and `SPI_NOR_DEMO_ADDRESS`. It first unlocks and prepares a known
nonblank pattern in the bottom sector: 32 patterned bytes, then FF to 4 KiB.
While protected, diagnostic APIs send actual `06h` + `02h` Page Program with
zero data, and `06h` + `20h` Sector Erase, bypassing only the BP/CMP software
range guard. After each attempt, every byte in the 4 KiB sector must remain
unchanged. An API error or BUSY timeout is a test failure, not a protection PASS.
The same raw commands must also erase, program and read back data successfully
after unlocking and outside the partial protection range. The positive test
checks erased sample bytes are FF before programming its non-FF pattern.
Raw helpers retain bounds, page, WEL and BUSY checks; `04h` clears residual WEL
after a completed attempt. Their declarations are in `spi_nor_test.h`, not the
normal driver API. Normal read/write APIs still enforce software protection.
No extra Chip Erase is added. This new hardware test still needs on-board validation.
A successful run leaves the array unprotected; it does not restore the original setting.
If the first unprotect step fails, the partial-protection test is skipped.
If protected data changes, the test attempts to unlock and stops without
further array writes, leaving the failed data available for inspection.
For this small write/read test, set erase permission to `1U` and full test to `0U`.
Set both to `0U` to skip array changes; protection-register writes still run.

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
The simple demo shows the first 32 reference and received bytes, in 16-byte rows.
The full test shows the first block's program/reference/readback data only,
so data dumps do not flood the console or interrupt the SPI transaction itself.
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

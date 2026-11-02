/*!
    \file    readme.txt
    \brief   description of the master receiver
    
    \version 2026-02-05, V3.3.3, firmware for GD32F4xx
*/

/*
    Copyright (c) 2026, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this 
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, 
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors 
       may be used to endorse or promote products derived from this software without 
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.
*/

GD32F450 SPI NOR demo: 1x / 2x / 4x
=================================

References
----------
Uses the bundled GD32F4xx standard peripheral library (spi_quad_* APIs).
Primary NOR reference: GD32F4xx Demo Suites V2.6.4,
GD32450Z_EVAL_Demo_Suites/Projects/12_SPI_Quad_Flash/Soft_Drive/gd25qxx.c.
SPI5 pin setup, 6Bh quad-read dummy phase and 32h quad-program sequence
follow this official GD32450Z-EVAL example. APIs remain separate and page
splitting retains 32-bit lengths for the full-device test.
Unlike the official example, this demo does not reject a JEDEC ID or clear
protection bits when enabling QE. QE is only written when currently disabled.
The official example has no dual-output read implementation; the GPIO dual
diagnostic in this project is not part of the official demo.

Default wiring (same SPI5 pins as the NAND reference)
--------------------------------------------------
MCU       NOR signal
PG9       CS#
PG13      CLK
PG14      IO0 / DI
PG12      IO1 / DO
PG10      IO2 / WP#
PG11      IO3 / HOLD#
GND       GND
Supply    Match the exact Flash datasheet and MCU I/O voltage.

Use a 3.3 V-compatible part for direct connection to 3.3 V MCU I/O.
Do not directly connect a 1.8 V Flash. Check board jumpers, pin multiplexing,
decoupling and CS#/WP#/HOLD# pull-ups. Remove the NAND from the same CS line.
SPI mode 0, MSB first, APB2/64 (1.5625 MHz if APB2 is 100 MHz).
The user's /64 divider is retained; the official example uses /32.
CLK/IO pins use the official 25 MHz GPIO speed setting with no internal pulls;
this GPIO speed setting is not the SPI clock frequency. CS uses 50 MHz GPIO.
IO2/IO3 drive enable is QCTL bit 2, matching spi_quad_io23_output_enable() in
the official library. The bundled library lacks that API, so it is set locally.
Pins and prescaler are defined in inc/spi_nor.h.
Console: USART0, PA9 TX / PA10 RX, 115200 baud, 8N1, common ground.

Device configuration
--------------------
This demo uses a GD25Q/W25Q-style command profile: 24-bit byte addresses,
256-byte pages, 4 KiB sectors, and QE in SR2 bit 1. Check the exact Flash
datasheet for the command set, dummy clocks and status-register write format.
The demo is not a universal driver for every serial NOR part.

SPI_NOR_FLASH_SIZE in inc/spi_nor.h defaults to 4 MiB (32 Mbit). Set it to the actual
Flash capacity before testing; it is a configured value, not detected size.
JEDEC ID is read and printed without a whitelist or an ID-based failure check.
Tests proceed for any ID value, including 000000h or FFFFFFh; normal BUSY,
write-enable and data comparisons may still report a failed operation.
ID display does not prove that the configured command profile is compatible.
No support for NAND, QPI, DTR, 4-byte addressing or memory-mapped XIP.
The device must start in ordinary SPI mode with three-byte addressing.
An external reset/power cycle may be needed after other firmware uses QPI.

Operation           Command    Instruction/address/data bus width
JEDEC ID            9Fh        1-line, no address or NAND dummy byte
Single read         03h        1-1-1, no dummy clocks
Dual output read    3Bh        1-1-2, 8 single-line dummy clocks
Quad output read    6Bh        1-1-4, 8 quad-mode dummy clocks
Single program      02h        1-1-1
Quad program        32h        1-1-4
4 KiB erase         20h        1-1-0
SR1/SR2 read        05h/35h
Write enable        06h
Status write        01h        SR1 followed by SR2, preserves other bits

After the 6Bh command and three address bytes, switch SPI5 to quad read
before the dummy phase. Four quad byte transfers produce eight dummy clocks,
as in the official example; data is then read in quad mode. Dummy bytes are A5h.
Quad Page Program switches after the address and has no dummy phase.
The GPIO/sequence changes require new hardware testing; compilation alone
does not establish that the earlier nonblank-data failures have been fixed.

Current diagnostic build: 3Bh command/address/dummy clocks use SPI5, then
the dual data phase uses GPIO clocking and direct IO1:IO0 sampling. GPIO holds
WP#/HOLD# high while SPI is disabled, then restores all five alternate pins.
The console
labels this as GPIO diagnostic sampling. It is not a hardware dual benchmark.
The original SPI5 quad-receiver sampling produced intermittent IO0-bit errors
on this setup. GPIO sampling passed an all-FF comparison, but uses slower,
different timing; a nonblank pattern is still required for full validation.
Single and quad reads still use hardware SPI5. Throughput is not guaranteed
to scale 2x/4x because software waits between transfers.
Only data uses multiple lines: these are not BBh/EBh multi-line-address reads.
There is no dual program API because there is no common 2x program command.

Unlike SPI NAND, reads address the NOR array directly. There is no page-to-cache
load, program-execute command, ECC management, spare area or bad-block handling.
Each NOR Page Program needs Write Enable, a page-bounded payload and BUSY polling.

Safety and self-test
--------------------
Test switches are configured only in inc/spi_nor.h, not in CMake options.
Default SPI_NOR_DEMO_ALLOW_ERASE=0: reads existing data in all three modes
and compares against 1x. Array contents are not changed, but the first quad
read MAY SET NONVOLATILE SR2.QE via 06h + 01h. Protection is not cleared.
QE remains enabled after the demo. This is not a strictly read-only test
of configuration registers. Back up the array and register settings first.
Existing all-FF data gives weak coverage; use a known pattern for verification.

For erase/program verification, reserve a disposable sector and edit the header:
    #define SPI_NOR_DEMO_ALLOW_ERASE    1U
    #define SPI_NOR_FULL_TEST           0U
    #define SPI_NOR_DEMO_ADDRESS        0x00100000U
Then rebuild and download:
    cmake --build --preset Debug

THIS DESTROYS ALL 4096 BYTES AT 0x00100000..0x00100FFF ON EVERY RESET.
The sector is erased twice: once for 1x program, once for 4x program.
Each pass erases the sector, writes a 768-byte varying pattern at sector+123
(spans four pages), and uses memcmp to verify 1x/2x/4x read results.
There is no full-sector blank scan or automatic bus recovery in this demo.
The final 4x pattern is left in Flash; original data is not restored.
For a 1 MiB part, set SPI_NOR_FLASH_SIZE accordingly and choose a reserved
aligned sector below 0x00100000. Never choose a sector holding live data.

Disable destructive testing again in the header after use:
    #define SPI_NOR_DEMO_ALLOW_ERASE    0U
    #define SPI_NOR_FULL_TEST           0U
Then rebuild and download the ordinary firmware:
    cmake --build --preset Debug

Full-device random test
-----------------------
WARNING: this test destroys every byte in the configured NOR capacity.
Check SPI_NOR_FLASH_SIZE against the actual device before enabling it.
Enable both code switches in inc/spi_nor.h:
    #define SPI_NOR_DEMO_ALLOW_ERASE    1U
    #define SPI_NOR_FULL_TEST           1U
The implementation is isolated in src/spi_nor_test.c with the public entry
spi_nor_full_test() declared in inc/spi_nor_test.h. main.c calls this entry
only when SPI_NOR_FULL_TEST is 1; otherwise it runs the simple sector demo.
Rebuild with the normal build command and download the resulting image:
    cmake --build --preset Debug
No -D test options are needed or used. All build directories now use the same
header settings; the name build/FullTest no longer selects a different mode.

1. Erase every 4 KiB sector and verify every byte is FF with single-line read.
2. Program the entire device using single-line Page Program and xorshift32
   seed 12345678h; regenerate and compare every byte with all three read APIs.
3. Erase/blank-check again, then repeat with quad Page Program and seed 9E3779B9h.
4. Erase/blank-check once more. A successful test leaves the entire device FF.

The two 768-byte buffers are reused; the last partial chunk is also checked.
Seeds are fixed for reproducibility, not cryptographic randomness. Data is
programmed across the whole device before readback, helping detect aliasing.
The console reports progress near each MiB and the first bad address/value.
An error stops the test without cleanup so the failed data can be inspected.
Dual read is still the GPIO diagnostic path, not native SPI5 dual reception.
Final result is printed as FULL CHIP TEST PASS or FAIL. Expect several minutes.
EVERY RESET REPEATS THE TEST while this image is installed. Set both header
switches back to 0, rebuild and download after use. Editing the header alone
does not replace firmware already installed on the board.

The toolchain file follows the NAND project's explicit ARM compiler path;
TOOLCHAIN_BIN_DIR can override it. CMake and Ninja must also be configured.
Outputs: build/Debug/GD32450Z_QSPI_Nor.elf, .hex, .bin and .map.
Open the console before reset. The demo prints JEDEC ID and configured capacity,
then write and read/verify results as PASS or FAIL. LED1 blinks independently.

Code layout
-----------
src/main.c: init -> print ID -> nor_demo -> separate write/read demonstrations.
With erase disabled, nor_demo only reads and compares existing array data.
src/spi_nor_test.c: optional full-device erase/random-write/readback test,
including its own buffers, random generator, progress and mismatch reporting.
It is compiled out when SPI_NOR_FULL_TEST is 0.
The driver exposes separate transactions, with no transfer-mode parameter:
    spi_nor_read             03h single-output read
    spi_nor_dual_read        3Bh dual-output read
    spi_nor_quad_read        6Bh quad-output read
    spi_nor_program          02h single-line Page Program
    spi_nor_quad_program     32h quad-line Page Program
    spi_nor_erase_sector     20h 4 KiB sector erase
Only low-level GPIO/SPI, address and status helpers are shared.

Driver contract and limitations
-------------------------------
Initialize SysTick before spi_nor_init; do not call the driver in an ISR
or with interrupts disabled. Calls are blocking and not thread-safe.
Like the NAND reference, SPI hardware flags are polled directly without a
timeout or recovery state machine. Incorrect SPI setup may block a call.
Flash BUSY polling still has a 5000 ms limit.
Program automatically splits at page boundaries; erase must be 4 KiB aligned.
All ranges are checked without address+length overflow. Erase before program;
NOR programming only changes bits from 1 to 0. No automatic erase is performed.
Erase/program refuse protected configurations rather than unlocking the chip.
The driver checks WEL and BUSY; callers must verify array contents
because a cleared BUSY bit alone is not proof of successful programming.
Quad support, dual reconstruction, pin mapping and electrical timing require
on-board validation against the actual Flash model and, ideally, a logic analyzer.


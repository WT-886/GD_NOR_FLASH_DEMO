/*!
    \file    main.c
    \brief   SPI NOR single / dual / quad demonstration

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

#include "gd32f4xx.h"
#include "systick.h"
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "gd32f450i_eval.h"
#include "spi_nor.h"
#include "spi_nor_test.h"

static void print_result(const char *name, ErrStatus result)
{
    printf("%-24s %s\r\n", name, (SUCCESS == result) ? "PASS" : "FAIL");
}

#if !SPI_NOR_FULL_TEST
/* Start inside a page to demonstrate automatic page-boundary splitting. */
#define TEST_LENGTH  768U
#define TEST_OFFSET  123U

static uint8_t expected[TEST_LENGTH];
static uint8_t received[TEST_LENGTH];

static ErrStatus verify_data(const char *name, uint32_t address, ErrStatus result)
{
    if(ERROR == result) {
        print_result(name, ERROR);
        printf("Read operation failed before data comparison.\r\n");
        return ERROR;
    }
    spi_nor_dump_data(name, address, received, TEST_LENGTH);
    if(0 != memcmp(expected, received, TEST_LENGTH)) {
        uint32_t offset = 0U;
        while(expected[offset] == received[offset]) {
            offset++;
        }
        uint32_t count = TEST_LENGTH - offset;
        if(count > 16U) {
            count = 16U;
        }
        print_result(name, ERROR);
        printf("Mismatch at 0x%08lX, offset %lu\r\n",
               (unsigned long)(address + offset), (unsigned long)offset);
        printf("Expected:");
        for(uint32_t index = 0U; index < count; index++) {
            printf(" %02X", (unsigned)expected[offset + index]);
        }
        printf("\r\nReceived:");
        for(uint32_t index = 0U; index < count; index++) {
            printf(" %02X", (unsigned)received[offset + index]);
        }
        printf("\r\n");
        return ERROR;
    }
    print_result(name, SUCCESS);
    return SUCCESS;
}

static ErrStatus verify_reads(uint32_t address)
{
    ErrStatus result;
    ErrStatus overall = SUCCESS;
    uint8_t sr1;
    uint8_t sr2;

    spi_nor_dump_data("Expected / reference", address, expected, TEST_LENGTH);
    /* Test every interface even if another interface has failed. */
    result = spi_nor_read(address, received, TEST_LENGTH);
    if(ERROR == verify_data("single read / verify", address, result)) {
        overall = ERROR;
    }
    printf("Dual receive: GPIO diagnostic sampling\r\n");
    result = spi_nor_dual_read(address, received, TEST_LENGTH);
    if(ERROR == verify_data("dual read / verify", address, result)) {
        overall = ERROR;
    }
    result = spi_nor_quad_read(address, received, TEST_LENGTH);
    if(ERROR == verify_data("quad read / verify", address, result)) {
        overall = ERROR;
    }
    spi_nor_read_status(&sr1, &sr2);
    printf("Status after reads: SR1=%02X SR2=%02X\r\n", (unsigned)sr1, (unsigned)sr2);
    return overall;
}

#if SPI_NOR_DEMO_ALLOW_ERASE
static void fill_test_data(uint8_t seed)
{
    for(uint32_t index = 0U; index < TEST_LENGTH; index++) {
        expected[index] = (uint8_t)(index + index / SPI_NOR_PAGE_SIZE + seed);
    }
}
#endif

static ErrStatus nor_demo(void)
{
    const uint32_t address = SPI_NOR_DEMO_ADDRESS + TEST_OFFSET;

    if((0U != (SPI_NOR_DEMO_ADDRESS % SPI_NOR_SECTOR_SIZE)) ||
    (SPI_NOR_DEMO_ADDRESS >= SPI_NOR_FLASH_SIZE) ||
    (SPI_NOR_SECTOR_SIZE > SPI_NOR_FLASH_SIZE - SPI_NOR_DEMO_ADDRESS) ||
       (TEST_OFFSET + TEST_LENGTH > SPI_NOR_SECTOR_SIZE)) {
        printf("Invalid test sector\r\n");
        return ERROR;
    }
    printf("Test: address 0x%08lX, %u bytes\r\n", (unsigned long)address, (unsigned)TEST_LENGTH);
    printf("Quad read may set nonvolatile QE.\r\n");

#if SPI_NOR_DEMO_ALLOW_ERASE
    ErrStatus result;

    printf("WARNING: erase test sector twice, original data will be lost.\r\n");
    /* First pass: single-line program, then verify every read interface. */
    fill_test_data(0x55U);
    result = spi_nor_erase_sector(SPI_NOR_DEMO_ADDRESS);
    print_result("sector erase", result);
    if(ERROR == result) {
        return ERROR;
    }
    result = spi_nor_program(address, expected, TEST_LENGTH);
    print_result("single write", result);
    if((ERROR == result) || (ERROR == verify_reads(address))) {
        return ERROR;
    }

    /* Second pass: erase again before quad-line program. */
    fill_test_data(0xAAU);
    result = spi_nor_erase_sector(SPI_NOR_DEMO_ADDRESS);
    print_result("sector erase", result);
    if(ERROR == result) {
        return ERROR;
    }
    result = spi_nor_quad_program(address, expected, TEST_LENGTH);
    print_result("quad write", result);
    if(ERROR == result) {
        return ERROR;
    }
    return verify_reads(address);
#else
    /* Compare existing array contents without erasing or programming. */
    printf("Erase/program disabled; blank data gives weak test coverage.\r\n");
    if(ERROR == spi_nor_read(address, expected, TEST_LENGTH)) {
        return ERROR;
    }
    return verify_reads(address);
#endif
}

#endif

static ErrStatus show_protection_result(const char *name, spi_nor_protect_result_t result)
{
    spi_nor_protection_t protection;
    const spi_nor_protect_result_t status = spi_nor_get_protection(&protection);
    const ErrStatus passed = ((SPI_NOR_PROTECT_OK == result) &&
                              (SPI_NOR_PROTECT_OK == status)) ? SUCCESS : ERROR;

    print_result(name, passed);
    if(SPI_NOR_PROTECT_OK != result) {
        printf("Protection error: %u\r\n", (unsigned)result);
    }
    if(SPI_NOR_PROTECT_OK == status) {
        printf("SR1=%02X SR2=%02X BP=%02X CMP=%u, start=0x%06lX size=%lu\r\n",
               (unsigned)protection.sr1, (unsigned)protection.sr2,
               (unsigned)protection.bp, (unsigned)protection.cmp,
               (unsigned long)protection.start, (unsigned long)protection.length);
    } else {
        printf("Read protection status failed: %u\r\n", (unsigned)status);
    }
    return passed;
}

#if SPI_NOR_DEMO_ALLOW_ERASE
/* Use one disposable sector and one short pattern after unlocking. */
static ErrStatus protection_rw_test(uint32_t address, uint8_t seed)
{
    uint8_t written[32];
    uint8_t readback[32];
    ErrStatus result;

    printf("Erase/write/read @0x%06lX\r\n", (unsigned long)address);
    result = spi_nor_test_erase_raw(address);
    print_result("sector erase", result);
    if(ERROR == result) {
        return ERROR;
    }
    result = spi_nor_read(address, readback, sizeof(readback));
    if(SUCCESS == result) {
        for(uint32_t index = 0U; index < sizeof(readback); index++) {
            if(0xFFU != readback[index]) {
                result = ERROR;
                break;
            }
        }
    }
    print_result("erase blank / verify", result);
    if(ERROR == result) {
        return ERROR;
    }
    for(uint32_t index = 0U; index < sizeof(written); index++) {
        written[index] = (uint8_t)(seed + index);
    }
    result = spi_nor_test_program_raw(address, written, sizeof(written));
    print_result("single write", result);
    if(ERROR == result) {
        return ERROR;
    }
    result = spi_nor_read(address, readback, sizeof(readback));
    if(SUCCESS == result) {
        spi_nor_dump_data("Readback", address, readback, sizeof(readback));
        result = (0 == memcmp(written, readback, sizeof(written))) ? SUCCESS : ERROR;
    }
    print_result("read / verify", result);
    return result;
}

/* Preparation writes 32 patterned bytes and leaves the rest of the sector FF. */
static ErrStatus protection_verify_sector(uint32_t address, uint8_t seed)
{
    uint8_t readback[32];

    for(uint32_t offset = 0U; offset < SPI_NOR_SECTOR_SIZE; offset += sizeof(readback)) {
        if(ERROR == spi_nor_read(address + offset, readback, sizeof(readback))) {
            printf("Protected sector read failed @0x%06lX\r\n", (unsigned long)(address + offset));
            return ERROR;
        }
        for(uint32_t index = 0U; index < sizeof(readback); index++) {
            const uint8_t expected_byte = (0U == offset) ? (uint8_t)(seed + index) : 0xFFU;
            if(readback[index] != expected_byte) {
                printf("Protected data changed @0x%06lX: expected=%02X got=%02X\r\n",
                       (unsigned long)(address + offset + index),
                       (unsigned)expected_byte, (unsigned)readback[index]);
                return ERROR;
            }
        }
    }
    return SUCCESS;
}

/* Send real commands: only unchanged readback proves hardware rejection. */
static ErrStatus protection_block_test(uint32_t address, uint8_t seed)
{
    uint8_t pattern[32] = {0U};
    spi_nor_protection_t protection;
    ErrStatus result;

    if((SPI_NOR_PROTECT_OK != spi_nor_get_protection(&protection)) ||
       (protection.length < SPI_NOR_SECTOR_SIZE) || (address < protection.start) ||
       (address - protection.start > protection.length - SPI_NOR_SECTOR_SIZE)) {
        print_result("protected range check", ERROR);
        return ERROR;
    }
    result = protection_verify_sector(address, seed);
    print_result("nonblank baseline", result);
    if(ERROR == result) {
        return ERROR;
    }

    result = spi_nor_test_program_raw(address, pattern, sizeof(pattern));
    print_result("02h program command sent", result);
    if(ERROR == result) {
        return ERROR;
    }
    result = protection_verify_sector(address, seed);
    print_result("program blocked (flash)", result);
    if(ERROR == result) {
        return ERROR;
    }

    result = spi_nor_test_erase_raw(address);
    print_result("20h erase command sent", result);
    if(ERROR == result) {
        return ERROR;
    }
    result = protection_verify_sector(address, seed);
    print_result("erase blocked (flash)", result);
    return result;
}
#endif

static ErrStatus protection_demo(void)
{
    ErrStatus overall = SUCCESS;

    printf("\r\nProtection test: full -> none -> bottom 64 KiB -> none\r\n");
    if(ERROR == show_protection_result("initial protection", SPI_NOR_PROTECT_OK)) {
        return ERROR;
    }
#if SPI_NOR_DEMO_ALLOW_ERASE
    if((0U != SPI_NOR_DEMO_ADDRESS % SPI_NOR_SECTOR_SIZE) ||
       (SPI_NOR_DEMO_ADDRESS < 0x010000U) ||
       (SPI_NOR_DEMO_ADDRESS >= SPI_NOR_FLASH_SIZE) ||
       (SPI_NOR_SECTOR_SIZE > SPI_NOR_FLASH_SIZE - SPI_NOR_DEMO_ADDRESS)) {
        printf("Choose an unprotected aligned test sector above the bottom 64 KiB.\r\n");
        return ERROR;
    }
    printf("WARNING: R/W tests destroy sectors 0x000000 and 0x%06lX.\r\n",
           (unsigned long)SPI_NOR_DEMO_ADDRESS);
    printf("Hardware protection test: raw 02h/20h commands bypass the BP/CMP guard.\r\n");
    if(ERROR == show_protection_result("prepare unprotect", spi_nor_unprotect())) {
        return ERROR;
    }
    if(ERROR == protection_rw_test(0U, 0x55U)) {
        return ERROR;
    }
#else
    printf("Array checks skipped: erase/program is disabled.\r\n");
#endif
    if(ERROR == show_protection_result("full protection", spi_nor_protect_all())) {
        overall = ERROR;
    }
#if SPI_NOR_DEMO_ALLOW_ERASE
    else if(ERROR == protection_block_test(0U, 0x55U)) {
        show_protection_result("cleanup unprotect", spi_nor_unprotect());
        return ERROR;
    }
#endif
    /* Always try to clear the full protection before testing a partial range. */
    if(ERROR == show_protection_result("unprotect full", spi_nor_unprotect())) {
        printf("Cannot clear protection; test stopped.\r\n");
        return ERROR;
    }
#if SPI_NOR_DEMO_ALLOW_ERASE
    if(ERROR == protection_rw_test(0U, 0x55U)) {
        overall = ERROR;
    }
#endif
    if(ERROR == show_protection_result("bottom 64 KiB protect", spi_nor_protect_bottom64k())) {
        overall = ERROR;
    }
#if SPI_NOR_DEMO_ALLOW_ERASE
    else {
        if(ERROR == protection_block_test(0U, 0x55U)) {
            show_protection_result("cleanup unprotect", spi_nor_unprotect());
            return ERROR;
        }
        if(ERROR == protection_rw_test(SPI_NOR_DEMO_ADDRESS, 0x77U)) {
            overall = ERROR;
        }
    }
#endif
    if(ERROR == show_protection_result("unprotect partial", spi_nor_unprotect())) {
        overall = ERROR;
    }
#if SPI_NOR_DEMO_ALLOW_ERASE
    else if(ERROR == protection_rw_test(0U, 0xAAU)) {
        overall = ERROR;
    }
#endif
    return overall;
}

/*!
    \brief    toggle the led every 500ms
    \param[in]  none
    \param[out] none
    \retval     none
*/
void led_spark(void)
{
    static uint32_t ticks;

    if(++ticks >= 500U) {
        ticks = 0U;
        gd_eval_led_toggle(LED1);
    }
}

/*!
    \brief    main function
    \param[in]  none
    \param[out] none
    \retval     none
*/
int main(void)
{
    uint8_t sr1;
    uint8_t sr2;

    gd_eval_led_init(LED1);
    gd_eval_com_init(EVAL_COM0);
    systick_config();
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("\r\nGD32F450 SPI5 NOR demo: 1x / 2x / 4x\r\n");
    spi_nor_init();
    /* Print any ID value, then run the test without a device whitelist. */
    printf("JEDEC ID: %06lX\r\n", (unsigned long)spi_nor_read_id());
    printf("Configured capacity: %lu bytes\r\n", (unsigned long)SPI_NOR_FLASH_SIZE);
    spi_nor_read_status(&sr1, &sr2);
    printf("Initial status: SR1=%02X SR2=%02X\r\n", (unsigned)sr1, (unsigned)sr2);
#if SPI_NOR_FULL_TEST
    print_result("FULL CHIP TEST", spi_nor_full_test());
#else
    print_result("demo", nor_demo());
#endif
    /* Run after array tests; a successful sequence leaves the array unlocked. */
    print_result("PROTECTION TEST", protection_demo());
    while(1) {
    }
}


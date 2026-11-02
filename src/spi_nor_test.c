#include "spi_nor_test.h"
#include "spi_nor.h"
#include <stdio.h>
#include <string.h>

#if SPI_NOR_FULL_TEST

#define FULL_TEST_BUFFER_SIZE  768U
#define FULL_TEST_SEED_SINGLE  0x12345678U
#define FULL_TEST_SEED_QUAD    0x9E3779B9U
#define FULL_TEST_REPORT_SIZE  (1024U * 1024U)

static uint8_t expected[FULL_TEST_BUFFER_SIZE];
static uint8_t received[FULL_TEST_BUFFER_SIZE];

_Static_assert(0U == SPI_NOR_FLASH_SIZE % SPI_NOR_SECTOR_SIZE,
               "Full-device test requires a whole number of sectors");

static void print_result(const char *name, ErrStatus result)
{
    printf("%-24s %s\r\n", name, (SUCCESS == result) ? "PASS" : "FAIL");
}

/* A fixed seed reproduces the same byte stream without storing the device image. */
static void fill_random(uint8_t *data, uint32_t length, uint32_t *state)
{
    for(uint32_t index = 0U; index < length; index++) {
        *state ^= *state << 13;
        *state ^= *state >> 17;
        *state ^= *state << 5;
        data[index] = (uint8_t)*state;
    }
}

static uint32_t full_chunk_size(uint32_t address)
{
    const uint32_t remaining = SPI_NOR_FLASH_SIZE - address;
    return (remaining < FULL_TEST_BUFFER_SIZE) ? remaining : FULL_TEST_BUFFER_SIZE;
}

static void full_progress(const char *stage, uint32_t address, uint32_t length)
{
    const uint32_t completed = address + length;
    if((completed == SPI_NOR_FLASH_SIZE) ||
       (address / FULL_TEST_REPORT_SIZE != completed / FULL_TEST_REPORT_SIZE)) {
        printf("%-16s %lu / %lu bytes\r\n", stage, (unsigned long)completed,
               (unsigned long)SPI_NOR_FLASH_SIZE);
    }
}

static ErrStatus full_compare(const char *stage, uint32_t address, uint32_t length, ErrStatus result)
{
    if(ERROR == result) {
        printf("%s operation failed at 0x%08lX\r\n", stage, (unsigned long)address);
        return ERROR;
    }
    for(uint32_t index = 0U; index < length; index++) {
        if(expected[index] != received[index]) {
            printf("%s mismatch at 0x%08lX: expected=%02X received=%02X\r\n",
                   stage, (unsigned long)(address + index),
                   (unsigned)expected[index], (unsigned)received[index]);
            return ERROR;
        }
    }
    return SUCCESS;
}

/* Sector erases cover the whole device and keep each BUSY wait bounded. */
static ErrStatus full_erase(void)
{
    for(uint32_t address = 0U; address < SPI_NOR_FLASH_SIZE; address += SPI_NOR_SECTOR_SIZE) {
        if(ERROR == spi_nor_erase_sector(address)) {
            printf("Erase failed at 0x%08lX\r\n", (unsigned long)address);
            return ERROR;
        }
        full_progress("erase", address, SPI_NOR_SECTOR_SIZE);
    }
    memset(expected, 0xFF, sizeof(expected));
    for(uint32_t address = 0U; address < SPI_NOR_FLASH_SIZE;) {
        const uint32_t length = full_chunk_size(address);
        const ErrStatus result = spi_nor_read(address, received, length);
        if(ERROR == full_compare("blank check", address, length, result)) {
            return ERROR;
        }
        full_progress("blank check", address, length);
        address += length;
    }
    print_result("full erase / blank", SUCCESS);
    return SUCCESS;
}

static ErrStatus full_program(const char *stage, uint32_t seed,
                             ErrStatus (*program)(uint32_t, const uint8_t *, uint32_t))
{
    uint32_t state = seed;
    printf("%s seed: %08lX\r\n", stage, (unsigned long)seed);
    for(uint32_t address = 0U; address < SPI_NOR_FLASH_SIZE;) {
        const uint32_t length = full_chunk_size(address);
        fill_random(expected, length, &state);
        if(ERROR == program(address, expected, length)) {
            printf("%s failed at 0x%08lX\r\n", stage, (unsigned long)address);
            return ERROR;
        }
        full_progress(stage, address, length);
        address += length;
    }
    print_result(stage, SUCCESS);
    return SUCCESS;
}

static ErrStatus full_verify(uint32_t seed)
{
    uint32_t state = seed;
    printf("Readback: single SPI5, dual GPIO diagnostic, quad SPI5\r\n");
    for(uint32_t address = 0U; address < SPI_NOR_FLASH_SIZE;) {
        const uint32_t length = full_chunk_size(address);
        ErrStatus overall = SUCCESS;
        ErrStatus result;

        fill_random(expected, length, &state);
        result = spi_nor_read(address, received, length);
        if(ERROR == full_compare("single read", address, length, result)) {
            overall = ERROR;
        }
        result = spi_nor_dual_read(address, received, length);
        if(ERROR == full_compare("dual read (GPIO)", address, length, result)) {
            overall = ERROR;
        }
        result = spi_nor_quad_read(address, received, length);
        if(ERROR == full_compare("quad read", address, length, result)) {
            overall = ERROR;
        }
        if(ERROR == overall) {
            return ERROR;
        }
        full_progress("verify 1x/2x/4x", address, length);
        address += length;
    }
    print_result("all read interfaces", SUCCESS);
    return SUCCESS;
}

ErrStatus spi_nor_full_test(void)
{
    printf("WARNING: ALL %lu NOR bytes will be destroyed on every reset!\r\n",
           (unsigned long)SPI_NOR_FLASH_SIZE);
    printf("1/3: erase, single random program, verify all read interfaces\r\n");
    if((ERROR == full_erase()) ||
       (ERROR == full_program("single program", FULL_TEST_SEED_SINGLE, spi_nor_program)) ||
       (ERROR == full_verify(FULL_TEST_SEED_SINGLE))) {
        return ERROR;
    }
    printf("2/3: erase, quad random program, verify all read interfaces\r\n");
    if((ERROR == full_erase()) ||
       (ERROR == full_program("quad program", FULL_TEST_SEED_QUAD, spi_nor_quad_program)) ||
       (ERROR == full_verify(FULL_TEST_SEED_QUAD))) {
        return ERROR;
    }
    /* A successful test leaves every byte erased; errors retain evidence. */
    printf("3/3: final erase and full blank check\r\n");
    return full_erase();
}

#endif
#ifndef SPI_NOR_TEST_H
#define SPI_NOR_TEST_H

#include "gd32f4xx.h"

/* Print at most the first 32 bytes, in 16-byte rows. */
void spi_nor_dump_data(const char *name, uint32_t address, const uint8_t *data, uint32_t length);

/* Destructive full-device test; initialize SPI NOR and SysTick before calling. */
ErrStatus spi_nor_full_test(void);

#endif
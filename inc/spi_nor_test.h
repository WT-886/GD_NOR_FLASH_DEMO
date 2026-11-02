#ifndef SPI_NOR_TEST_H
#define SPI_NOR_TEST_H

#include "gd32f4xx.h"

/* Destructive full-device test; initialize SPI NOR and SysTick before calling. */
ErrStatus spi_nor_full_test(void);

#endif
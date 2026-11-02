#ifndef SPI_NOR_H
#define SPI_NOR_H

#include "gd32f4xx.h"

#define SPI_NOR_SPI                 SPI5
#define SPI_NOR_SPI_CLK             RCU_SPI5
#define SPI_NOR_GPIO_PORT           GPIOG
#define SPI_NOR_GPIO_CLK            RCU_GPIOG
#define SPI_NOR_GPIO_AF             GPIO_AF_5
#define SPI_NOR_CS_PIN              GPIO_PIN_9
#define SPI_NOR_IO2_PIN             GPIO_PIN_10  /* WP# in single-line mode. */
#define SPI_NOR_IO3_PIN             GPIO_PIN_11  /* HOLD# in single-line mode. */
#define SPI_NOR_IO1_PIN             GPIO_PIN_12  /* MISO. */
#define SPI_NOR_SCK_PIN             GPIO_PIN_13
#define SPI_NOR_IO0_PIN             GPIO_PIN_14  /* MOSI. */
#define SPI_NOR_PRESCALE            SPI_PSC_8
#define SPI_NOR_PAGE_SIZE           256U
#define SPI_NOR_SECTOR_SIZE         4096U

/* Set this to the actual NOR capacity; JEDEC ID is for display only. */
#ifndef SPI_NOR_FLASH_SIZE
#define SPI_NOR_FLASH_SIZE          (4U * 1024U * 1024U)
#endif

/* Erase/program is disabled until a disposable test sector is selected. */
#define SPI_NOR_DEMO_ALLOW_ERASE    1U
/* Destructive full-device test; both switches must be enabled. */
#define SPI_NOR_FULL_TEST           1U
#if SPI_NOR_FULL_TEST && !SPI_NOR_DEMO_ALLOW_ERASE
#error "Full-device test requires SPI_NOR_DEMO_ALLOW_ERASE=1"
#endif
#define SPI_NOR_DEMO_ADDRESS        0x00100000U

void spi_nor_init(void);
uint32_t spi_nor_read_id(void);
ErrStatus spi_nor_read_status(uint8_t *sr1, uint8_t *sr2);
/* Separate APIs show the 1-1-1, 1-1-2 and 1-1-4 bus transactions. */
ErrStatus spi_nor_read(uint32_t address, uint8_t *data, uint32_t length);
ErrStatus spi_nor_dual_read(uint32_t address, uint8_t *data, uint32_t length);
ErrStatus spi_nor_quad_read(uint32_t address, uint8_t *data, uint32_t length);
/* Programs split at page boundaries; the caller must erase first. */
ErrStatus spi_nor_program(uint32_t address, const uint8_t *data, uint32_t length);
ErrStatus spi_nor_quad_program(uint32_t address, const uint8_t *data, uint32_t length);
/* Erases one aligned 4 KiB sector, not the whole chip. */
ErrStatus spi_nor_erase_sector(uint32_t address);

#endif
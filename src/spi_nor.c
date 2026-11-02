#include "spi_nor.h"
#include "systick.h"
#include <stddef.h>

#define CMD_READ_ID        0x9FU
#define CMD_READ_SR1       0x05U
#define CMD_READ_SR2       0x35U
#define CMD_WRITE_SR       0x01U
#define CMD_WRITE_ENABLE   0x06U
#define CMD_READ           0x03U
#define CMD_DUAL_READ      0x3BU
#define CMD_QUAD_READ      0x6BU
#define CMD_PROGRAM        0x02U
#define CMD_QUAD_PROGRAM   0x32U
#define CMD_SECTOR_ERASE   0x20U
#define CMD_CHIP_ERASE     0xC7U
#define DUMMY_BYTE         0xA5U
#define SR_BUSY            0x01U
#define SR_WEL             0x02U
#define SR_BP_MASK         0x7CU
#define SR2_QE             0x02U
#define SR2_CMP            0x40U
#define IO23_DRIVE         BIT(2)  /* Official SPI_QCTL_IO23_DRV definition. */
#define READY_TIMEOUT_MS   5000U

_Static_assert((SPI_NOR_FLASH_SIZE > 0U) && (SPI_NOR_FLASH_SIZE <= 0x01000000U),
               "Configured capacity must fit 24-bit addressing");

/* Blocking byte transfer, SPI mode 0, MSB first. */
static uint8_t transfer(uint8_t value)
{
    while(RESET == spi_i2s_flag_get(SPI_NOR_SPI, SPI_FLAG_TBE)) {
    }
    spi_i2s_data_transmit(SPI_NOR_SPI, value);
    while(RESET == spi_i2s_flag_get(SPI_NOR_SPI, SPI_FLAG_RBNE)) {
    }
    return (uint8_t)spi_i2s_data_receive(SPI_NOR_SPI);
}

static void wait_idle(void)
{
    while(SET == spi_i2s_flag_get(SPI_NOR_SPI, SPI_FLAG_TRANS)) {
    }
}

static void select_flash(void)
{
    gpio_bit_reset(SPI_NOR_GPIO_PORT, SPI_NOR_CS_PIN);
}

static void deselect_flash(void)
{
    /* Finish the last clock before releasing CS or changing line width. */
    wait_idle();
    gpio_bit_set(SPI_NOR_GPIO_PORT, SPI_NOR_CS_PIN);
    spi_quad_disable(SPI_NOR_SPI);
}

static void send_address(uint32_t address)
{
    /* NOR commands in this demo use a 24-bit byte address. */
    transfer((uint8_t)(address >> 16));
    transfer((uint8_t)(address >> 8));
    transfer((uint8_t)address);
}

static uint8_t read_register(uint8_t command)
{
    uint8_t value;

    select_flash();
    transfer(command);
    value = transfer(DUMMY_BYTE);
    deselect_flash();
    return value;
}

ErrStatus spi_nor_read_status(uint8_t *sr1, uint8_t *sr2)
{
    if((NULL == sr1) || (NULL == sr2)) {
        return ERROR;
    }
    *sr1 = read_register(CMD_READ_SR1);
    *sr2 = read_register(CMD_READ_SR2);
    return SUCCESS;
}

static ErrStatus wait_ready_timeout(uint32_t timeout_ms)
{
    for(uint32_t elapsed = 0U; elapsed < timeout_ms; elapsed++) {
        if(0U == (read_register(CMD_READ_SR1) & SR_BUSY)) {
            return SUCCESS;
        }
        delay_1ms(1U);
    }
    return ERROR;
}

static ErrStatus wait_ready(void)
{
    return wait_ready_timeout(READY_TIMEOUT_MS);
}

static ErrStatus write_enable(void)
{
    select_flash();
    transfer(CMD_WRITE_ENABLE);
    deselect_flash();
    return (read_register(CMD_READ_SR1) & SR_WEL) ? SUCCESS : ERROR;
}

static ErrStatus enable_quad(void)
{
    const uint8_t sr1 = read_register(CMD_READ_SR1);
    const uint8_t sr2 = read_register(CMD_READ_SR2);

    if(sr2 & SR2_QE) {
        return SUCCESS;
    }
    if(ERROR == write_enable()) {
        return ERROR;
    }
    /* GD25Q/W25Q profile: set SR2.QE and preserve protection settings. */
    select_flash();
    transfer(CMD_WRITE_SR);
    transfer(sr1 & (uint8_t)~(SR_BUSY | SR_WEL));
    transfer(sr2 | SR2_QE);
    deselect_flash();
    if(ERROR == wait_ready()) {
        return ERROR;
    }
    return (read_register(CMD_READ_SR2) & SR2_QE) ? SUCCESS : ERROR;
}

static void quad_read_enable(void)
{
    wait_idle();
    spi_quad_enable(SPI_NOR_SPI);
    spi_quad_read_enable(SPI_NOR_SPI);
}

static void quad_write_enable(void)
{
    wait_idle();
    spi_quad_enable(SPI_NOR_SPI);
    spi_quad_write_enable(SPI_NOR_SPI);
}

static ErrStatus valid_range(uint32_t address, uint32_t length)
{
    return ((address <= SPI_NOR_FLASH_SIZE) &&
            (length <= SPI_NOR_FLASH_SIZE - address)) ? SUCCESS : ERROR;
}

static ErrStatus writable(void)
{
    if(ERROR == wait_ready()) {
        return ERROR;
    }
    return ((read_register(CMD_READ_SR1) & SR_BP_MASK) ||
            (read_register(CMD_READ_SR2) & SR2_CMP)) ? ERROR : SUCCESS;
}

void spi_nor_init(void)
{
    spi_parameter_struct config;
    const uint32_t pins = SPI_NOR_SCK_PIN | SPI_NOR_IO0_PIN | SPI_NOR_IO1_PIN |
                          SPI_NOR_IO2_PIN | SPI_NOR_IO3_PIN;

    rcu_periph_clock_enable(SPI_NOR_GPIO_CLK);
    rcu_periph_clock_enable(SPI_NOR_SPI_CLK);
    gpio_bit_set(SPI_NOR_GPIO_PORT, SPI_NOR_CS_PIN);
    gpio_mode_set(SPI_NOR_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_NOR_CS_PIN);
    gpio_output_options_set(SPI_NOR_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SPI_NOR_CS_PIN);
    gpio_af_set(SPI_NOR_GPIO_PORT, SPI_NOR_GPIO_AF, pins);
    /* Match the official SPI5 quad-flash pin configuration. */
    gpio_mode_set(SPI_NOR_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, pins);
    gpio_output_options_set(SPI_NOR_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_25MHZ, pins);
    spi_i2s_deinit(SPI_NOR_SPI);
    spi_struct_para_init(&config);
    config.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    config.device_mode = SPI_MASTER;
    config.frame_size = SPI_FRAMESIZE_8BIT;
    config.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    config.nss = SPI_NSS_SOFT;
    config.prescale = SPI_NOR_PRESCALE;
    config.endian = SPI_ENDIAN_MSB;
    spi_init(SPI_NOR_SPI, &config);
    /* Keep WP# and HOLD# high outside quad transfers. */
    SPI_QCTL(SPI_NOR_SPI) |= IO23_DRIVE;
    spi_enable(SPI_NOR_SPI);
    delay_1ms(10U);
}

uint32_t spi_nor_read_id(void)
{
    uint32_t jedec_id;

    /* NOR 9Fh returns three ID bytes without an address or dummy byte. */
    select_flash();
    transfer(CMD_READ_ID);
    jedec_id = (uint32_t)transfer(DUMMY_BYTE) << 16;
    jedec_id |= (uint32_t)transfer(DUMMY_BYTE) << 8;
    jedec_id |= transfer(DUMMY_BYTE);
    deselect_flash();
    return jedec_id;
}

/* 03h: single-line command, address and data; no dummy clocks. */
ErrStatus spi_nor_read(uint32_t address, uint8_t *data, uint32_t length)
{
    if((NULL == data) || (ERROR == valid_range(address, length))) {
        return ERROR;
    }
    if(0U == length) {
        return SUCCESS;
    }
    if(ERROR == wait_ready()) {
        return ERROR;
    }

    select_flash();
    transfer(CMD_READ);
    send_address(address);
    while(length-- > 0U) {
        *data++ = transfer(DUMMY_BYTE);
    }
    deselect_flash();
    return SUCCESS;
}

/* 3Bh: single-line command/address, eight dummy clocks, dual data. */
ErrStatus spi_nor_dual_read(uint32_t address, uint8_t *data, uint32_t length)
{
    if((NULL == data) || (ERROR == valid_range(address, length))) {
        return ERROR;
    }
    if(0U == length) {
        return SUCCESS;
    }
    if(ERROR == wait_ready()) {
        return ERROR;
    }

    select_flash();
    transfer(CMD_DUAL_READ);
    send_address(address);
    transfer(DUMMY_BYTE);
    wait_idle();
    /* Keep WP#/HOLD# high while SPI is disabled for GPIO diagnostics. */
    gpio_bit_set(SPI_NOR_GPIO_PORT, SPI_NOR_IO2_PIN | SPI_NOR_IO3_PIN);
    gpio_mode_set(SPI_NOR_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                  SPI_NOR_IO2_PIN | SPI_NOR_IO3_PIN);
    spi_disable(SPI_NOR_SPI);
    /* Diagnostic: sample IO1:IO0 directly, bypassing the quad receiver. */
    gpio_bit_reset(SPI_NOR_GPIO_PORT, SPI_NOR_SCK_PIN);
    gpio_mode_set(SPI_NOR_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SPI_NOR_SCK_PIN);
    gpio_mode_set(SPI_NOR_GPIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE,
                  SPI_NOR_IO0_PIN | SPI_NOR_IO1_PIN);
    while(length-- > 0U) {
        uint8_t value = 0U;
        for(uint32_t sample = 0U; sample < 4U; sample++) {
            gpio_bit_set(SPI_NOR_GPIO_PORT, SPI_NOR_SCK_PIN);
            for(volatile uint32_t delay = 0U; delay < 16U; delay++) {
                __NOP();
            }
            const uint32_t pins = GPIO_ISTAT(SPI_NOR_GPIO_PORT);
            value = (uint8_t)((value << 2) | ((pins & SPI_NOR_IO1_PIN) ? 2U : 0U) |
                              ((pins & SPI_NOR_IO0_PIN) ? 1U : 0U));
            gpio_bit_reset(SPI_NOR_GPIO_PORT, SPI_NOR_SCK_PIN);
            for(volatile uint32_t delay = 0U; delay < 16U; delay++) {
                __NOP();
            }
        }
        *data++ = value;
    }
    deselect_flash();
    gpio_mode_set(SPI_NOR_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  SPI_NOR_SCK_PIN | SPI_NOR_IO0_PIN | SPI_NOR_IO1_PIN |
                  SPI_NOR_IO2_PIN | SPI_NOR_IO3_PIN);
    spi_enable(SPI_NOR_SPI);
    return SUCCESS;
}

/* 6Bh: single-line command/address, eight dummy clocks, quad data. */
ErrStatus spi_nor_quad_read(uint32_t address, uint8_t *data, uint32_t length)
{
    if((NULL == data) || (ERROR == valid_range(address, length))) {
        return ERROR;
    }
    if(0U == length) {
        return SUCCESS;
    }
    if((ERROR == wait_ready()) || (ERROR == enable_quad())) {
        return ERROR;
    }

    select_flash();
    transfer(CMD_QUAD_READ);
    send_address(address);
    quad_read_enable();
    /* Official sequence: four quad bytes generate eight dummy clocks. */
    for(uint32_t dummy = 0U; dummy < 4U; dummy++) {
        transfer(DUMMY_BYTE);
    }
    while(length-- > 0U) {
        *data++ = transfer(DUMMY_BYTE);
    }
    deselect_flash();
    return SUCCESS;
}

/* 02h: single-line Page Program. NOR writes only change bits from 1 to 0. */
ErrStatus spi_nor_program(uint32_t address, const uint8_t *data, uint32_t length)
{
    if((NULL == data) || (ERROR == valid_range(address, length))) {
        return ERROR;
    }
    if(0U == length) {
        return SUCCESS;
    }
    if(ERROR == writable()) {
        return ERROR;
    }
    while(length > 0U) {
        /* A Page Program must not wrap inside the 256-byte page. */
        uint32_t chunk = SPI_NOR_PAGE_SIZE - (address % SPI_NOR_PAGE_SIZE);
        if(chunk > length) {
            chunk = length;
        }
        if(ERROR == write_enable()) {
            return ERROR;
        }
        select_flash();
        transfer(CMD_PROGRAM);
        send_address(address);
        for(uint32_t index = 0U; index < chunk; index++) {
            transfer(data[index]);
        }
        deselect_flash();
        if(ERROR == wait_ready()) {
            return ERROR;
        }
        address += chunk;
        data += chunk;
        length -= chunk;
    }
    return SUCCESS;
}

/* 32h: single-line command/address, quad data, no dummy clocks. */
ErrStatus spi_nor_quad_program(uint32_t address, const uint8_t *data, uint32_t length)
{
    if((NULL == data) || (ERROR == valid_range(address, length))) {
        return ERROR;
    }
    if(0U == length) {
        return SUCCESS;
    }
    if((ERROR == writable()) || (ERROR == enable_quad())) {
        return ERROR;
    }
    while(length > 0U) {
        uint32_t chunk = SPI_NOR_PAGE_SIZE - (address % SPI_NOR_PAGE_SIZE);
        if(chunk > length) {
            chunk = length;
        }
        /* Each page needs a new Write Enable command. */
        if(ERROR == write_enable()) {
            return ERROR;
        }
        select_flash();
        transfer(CMD_QUAD_PROGRAM);
        send_address(address);
        quad_write_enable();
        for(uint32_t index = 0U; index < chunk; index++) {
            transfer(data[index]);
        }
        deselect_flash();
        if(ERROR == wait_ready()) {
            return ERROR;
        }
        address += chunk;
        data += chunk;
        length -= chunk;
    }
    return SUCCESS;
}

/* 20h: erase one 4 KiB sector before programming new data. */
ErrStatus spi_nor_erase_sector(uint32_t address)
{
    if((0U != (address % SPI_NOR_SECTOR_SIZE)) ||
       (ERROR == valid_range(address, SPI_NOR_SECTOR_SIZE)) ||
       (ERROR == writable()) || (ERROR == write_enable())) {
        return ERROR;
    }
    select_flash();
    transfer(CMD_SECTOR_ERASE);
    send_address(address);
    deselect_flash();
    return wait_ready();
}

/* C7h: erase all physical sectors with a dedicated, longer BUSY timeout. */
ErrStatus spi_nor_chip_erase(void)
{
    if((ERROR == writable()) || (ERROR == write_enable())) {
        return ERROR;
    }
    select_flash();
    transfer(CMD_CHIP_ERASE);
    deselect_flash();
    return wait_ready_timeout(SPI_NOR_CHIP_ERASE_TIMEOUT_MS);
}
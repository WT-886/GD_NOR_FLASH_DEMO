#include "spi_nor.h"
#include "spi_nor_test.h"
#include "systick.h"
#include <stddef.h>

#define CMD_READ_ID        0x9FU
#define CMD_READ_SR1       0x05U
#define CMD_READ_SR2       0x35U
#define CMD_WRITE_SR       0x01U
#define CMD_WRITE_ENABLE   0x06U
#define CMD_WRITE_DISABLE  0x04U
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
#define SR_SRP0            0x80U
#define SR2_SRP1           0x01U
#define SR2_QE             0x02U
#define SR2_CMP            0x40U
#define IO23_DRIVE         BIT(2)  /* Official SPI_QCTL_IO23_DRV definition. */
#define READY_TIMEOUT_MS   5000U
#define STATUS_TIMEOUT_MS  100U
#define GD25Q32E_SIZE       (4U * 1024U * 1024U)

typedef struct {
    uint32_t start;
    uint32_t length;
} protection_range_t;

/* GD25Q32E CMP=0 ranges, indexed by BP4:BP0. CMP=1 selects the complement. */
static const protection_range_t protection_ranges[32] = {
    {0x000000U, 0x000000U}, {0x3F0000U, 0x010000U},
    {0x3E0000U, 0x020000U}, {0x3C0000U, 0x040000U},
    {0x380000U, 0x080000U}, {0x300000U, 0x100000U},
    {0x200000U, 0x200000U}, {0x000000U, 0x400000U},
    {0x000000U, 0x000000U}, {0x000000U, 0x010000U},
    {0x000000U, 0x020000U}, {0x000000U, 0x040000U},
    {0x000000U, 0x080000U}, {0x000000U, 0x100000U},
    {0x000000U, 0x200000U}, {0x000000U, 0x400000U},
    {0x000000U, 0x000000U}, {0x3FF000U, 0x001000U},
    {0x3FE000U, 0x002000U}, {0x3FC000U, 0x004000U},
    {0x3F8000U, 0x008000U}, {0x3F8000U, 0x008000U},
    {0x3F8000U, 0x008000U}, {0x000000U, 0x400000U},
    {0x000000U, 0x000000U}, {0x000000U, 0x001000U},
    {0x000000U, 0x002000U}, {0x000000U, 0x004000U},
    {0x000000U, 0x008000U}, {0x000000U, 0x008000U},
    {0x000000U, 0x008000U}, {0x000000U, 0x400000U}
};

spi_nor_protect_result_t spi_nor_decode_protection(uint8_t sr1, uint8_t sr2,
                                                 spi_nor_protection_t *protection)
{
    if(NULL == protection) {
        return SPI_NOR_PROTECT_ERR_ARGUMENT;
    }
    protection->bp = (sr1 & SR_BP_MASK) >> 2;
    protection->cmp = (sr2 & SR2_CMP) ? 1U : 0U;
    protection->sr1 = sr1;
    protection->sr2 = sr2;
    protection->start = protection_ranges[protection->bp].start;
    protection->length = protection_ranges[protection->bp].length;
    if(protection->cmp) {
        const uint32_t unprotected_length = protection->length;
        protection->start = (0U == protection->start) ? unprotected_length : 0U;
        protection->length = GD25Q32E_SIZE - unprotected_length;
    }
    if(0U == protection->length) {
        protection->start = 0U;
    }
    return SPI_NOR_PROTECT_OK;
}

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

spi_nor_protect_result_t spi_nor_get_protection(spi_nor_protection_t *protection)
{
    uint8_t sr1;
    uint8_t sr2;

    if(NULL == protection) {
        return SPI_NOR_PROTECT_ERR_ARGUMENT;
    }
    if(ERROR == wait_ready()) {
        return SPI_NOR_PROTECT_ERR_TIMEOUT;
    }
    spi_nor_read_status(&sr1, &sr2);
    return spi_nor_decode_protection(sr1, sr2, protection);
}

/* Fixed presets only; conservatively refuse any active SRP lock setting. */
static spi_nor_protect_result_t set_protection(uint8_t bp)
{
    uint8_t sr1;
    uint8_t sr2;
    uint8_t saved_sr1;
    uint8_t saved_sr2;

    if(SPI_NOR_FLASH_SIZE != GD25Q32E_SIZE) {
        return SPI_NOR_PROTECT_ERR_RANGE;
    }
    if(ERROR == wait_ready()) {
        return SPI_NOR_PROTECT_ERR_TIMEOUT;
    }
    spi_nor_read_status(&sr1, &sr2);
    saved_sr1 = sr1;
    saved_sr2 = sr2;
    if((sr1 & SR_SRP0) || (sr2 & SR2_SRP1)) {
        return SPI_NOR_PROTECT_ERR_LOCKED;
    }

    if(((sr1 & SR_BP_MASK) != (uint8_t)(bp << 2)) || (sr2 & SR2_CMP)) {
        if(ERROR == write_enable()) {
            return SPI_NOR_PROTECT_ERR_WRITE_ENABLE;
        }
        select_flash();
        transfer(CMD_WRITE_SR);
        /* Official GD25Qxx sequence: 01h followed by SR1 and SR2 under one CS. */
        transfer((sr1 & (uint8_t)~(SR_BP_MASK | SR_BUSY | SR_WEL)) | (uint8_t)(bp << 2));
        transfer(sr2 & (uint8_t)~SR2_CMP);
        deselect_flash();
        if(ERROR == wait_ready_timeout(STATUS_TIMEOUT_MS)) {
            return SPI_NOR_PROTECT_ERR_TIMEOUT;
        }
    }

    spi_nor_read_status(&sr1, &sr2);
    if(((sr1 & SR_BP_MASK) != (uint8_t)(bp << 2)) || (sr2 & SR2_CMP) ||
       ((sr1 & SR_SRP0) != (saved_sr1 & SR_SRP0)) ||
       ((sr2 & (uint8_t)~SR2_CMP) != (saved_sr2 & (uint8_t)~SR2_CMP))) {
        return SPI_NOR_PROTECT_ERR_VERIFY;
    }
    return SPI_NOR_PROTECT_OK;
}

spi_nor_protect_result_t spi_nor_protect_all(void)
{
    return set_protection(0x07U);
}

spi_nor_protect_result_t spi_nor_protect_bottom64k(void)
{
    return set_protection(0x09U);
}

spi_nor_protect_result_t spi_nor_unprotect(void)
{
    return set_protection(0x00U);
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

static ErrStatus writable(uint32_t address, uint32_t length)
{
    spi_nor_protection_t protection;

    if(SPI_NOR_PROTECT_OK != spi_nor_get_protection(&protection)) {
        return ERROR;
    }
    if(0U == protection.length) {
        return SUCCESS;
    }
    /* Reject overlap, not every operation on a partially protected device. */
    return ((address < protection.start + protection.length) &&
            (protection.start < address + length)) ? ERROR : SUCCESS;
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
    if(ERROR == writable(address, length)) {
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
    if((ERROR == writable(address, length)) || (ERROR == enable_quad())) {
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
    (ERROR == writable(address, SPI_NOR_SECTOR_SIZE)) || (ERROR == write_enable())) {
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
    if((ERROR == writable(0U, GD25Q32E_SIZE)) || (ERROR == write_enable())) {
        return ERROR;
    }
    select_flash();
    transfer(CMD_CHIP_ERASE);
    deselect_flash();
    return wait_ready_timeout(SPI_NOR_CHIP_ERASE_TIMEOUT_MS);
}

#if SPI_NOR_DEMO_ALLOW_ERASE
/* A rejected command may leave WEL set; disarm it before leaving a raw test. */
static ErrStatus finish_raw_test(void)
{
    if(ERROR == wait_ready()) {
        return ERROR;
    }
    select_flash();
    transfer(CMD_WRITE_DISABLE);
    deselect_flash();
    return (read_register(CMD_READ_SR1) & SR_WEL) ? ERROR : SUCCESS;
}

ErrStatus spi_nor_test_program_raw(uint32_t address, const uint8_t *data, uint32_t length)
{
    if((NULL == data) || (0U == length) || (ERROR == valid_range(address, length)) ||
       (length > SPI_NOR_PAGE_SIZE - address % SPI_NOR_PAGE_SIZE) ||
       (ERROR == wait_ready()) || (ERROR == write_enable())) {
        return ERROR;
    }
    select_flash();
    transfer(CMD_PROGRAM);
    send_address(address);
    for(uint32_t index = 0U; index < length; index++) {
        transfer(data[index]);
    }
    deselect_flash();
    return finish_raw_test();
}

ErrStatus spi_nor_test_erase_raw(uint32_t address)
{
    if((0U != address % SPI_NOR_SECTOR_SIZE) ||
       (ERROR == valid_range(address, SPI_NOR_SECTOR_SIZE)) ||
       (ERROR == wait_ready()) || (ERROR == write_enable())) {
        return ERROR;
    }
    select_flash();
    transfer(CMD_SECTOR_ERASE);
    send_address(address);
    deselect_flash();
    return finish_raw_test();
}
#endif
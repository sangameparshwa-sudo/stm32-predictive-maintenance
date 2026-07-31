#include <stdint.h>
#include <math.h>

// ============================================================================
// REGISTERS & MEMORY MAP (STM32F446RE)
// ============================================================================

#define PERIPH_BASE         ((uint32_t)0x40000000)
#define AHB1PERIPH_BASE     (PERIPH_BASE + 0x00020000)
#define APB1PERIPH_BASE     (PERIPH_BASE + 0x00000000)
#define APB2PERIPH_BASE     (PERIPH_BASE + 0x00010000)

// RCC
#define RCC_BASE            (AHB1PERIPH_BASE + 0x00003800)
#define RCC_AHB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_APB2ENR         (*(volatile uint32_t *)(RCC_BASE + 0x44))

// GPIOA (PA0=SCT ch0, PA1=MIC ch1, PA4=ADXL CS, PA5=SCK, PA6=MISO, PA7=MOSI)
#define GPIOA_BASE          (AHB1PERIPH_BASE + 0x00000000)
#define GPIOA_MODER         (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OTYPER        (*(volatile uint32_t *)(GPIOA_BASE + 0x04))
#define GPIOA_OSPEEDR       (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_PUPDR         (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_ODR           (*(volatile uint32_t *)(GPIOA_BASE + 0x14))
#define GPIOA_AFRL          (*(volatile uint32_t *)(GPIOA_BASE + 0x20))  // AF for pins 0-7

// GPIOB (PB0 = heartbeat LED, PB8 = I2C1_SCL, PB9 = I2C1_SDA)
#define GPIOB_BASE          (AHB1PERIPH_BASE + 0x00000400)
#define GPIOB_MODER         (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OTYPER        (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_OSPEEDR       (*(volatile uint32_t *)(GPIOB_BASE + 0x08))
#define GPIOB_PUPDR         (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_ODR           (*(volatile uint32_t *)(GPIOB_BASE + 0x14))
#define GPIOB_AFRH          (*(volatile uint32_t *)(GPIOB_BASE + 0x24))

// ADC1
#define ADC1_BASE           (APB2PERIPH_BASE + 0x00002000)
#define ADC_SR              (*(volatile uint32_t *)(ADC1_BASE + 0x00))
#define ADC_CR2             (*(volatile uint32_t *)(ADC1_BASE + 0x08))
#define ADC_SMPR2           (*(volatile uint32_t *)(ADC1_BASE + 0x10))
#define ADC_SQR3            (*(volatile uint32_t *)(ADC1_BASE + 0x34))
#define ADC_DR              (*(volatile uint32_t *)(ADC1_BASE + 0x4C))

// I2C1
#define I2C1_BASE           (APB1PERIPH_BASE + 0x00005400)
#define I2C_CR1             (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C_CR2             (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C_DR              (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C_SR1             (*(volatile uint32_t *)(I2C1_BASE + 0x14))
#define I2C_SR2             (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C_CCR             (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C_TRISE           (*(volatile uint32_t *)(I2C1_BASE + 0x20))

// SPI1 (on APB2)
#define SPI1_BASE           (APB2PERIPH_BASE + 0x00003000)
#define SPI_CR1             (*(volatile uint32_t *)(SPI1_BASE + 0x00))
#define SPI_SR              (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI_DR              (*(volatile uint32_t *)(SPI1_BASE + 0x0C))

#define SAMPLES     800
#define MIC_SAMPLES 2000
#define VIB_SAMPLES 256        // vibration burst for RMS
#define MLX_ADDR    0x5A
#define MLX_TOBJ1   0x07

// ADXL345 registers
#define ADXL_DEVID       0x00   // reads 0xE5
#define ADXL_POWER_CTL   0x2D
#define ADXL_DATA_FORMAT 0x31
#define ADXL_DATAX0      0x32

// ADXL345 SPI address-byte bits
#define ADXL_READ  0x80
#define ADXL_MB    0x40   // multi-byte auto-increment

// ============================================================================
// GLOBALS (watch these in Live Expressions)
// ============================================================================
volatile uint32_t raw_adc = 0;
volatile float    dc_offset = 0.0f;
volatile float    rms_counts = 0.0f;
volatile float    current_rms = 0.0f;

volatile uint16_t mlx_raw = 0;
volatile float    object_temp_c = 0.0f;
volatile int      mlx_error = 0;

volatile float    mic_dc_offset = 0.0f;
volatile float    acoustic_rms = 0.0f;

volatile uint8_t  adxl_devid = 0;      // MUST read 0xE5 (229). This is your test.
volatile int16_t  accel_x = 0, accel_y = 0, accel_z = 0;
volatile float    vibration_rms = 0.0f; // RMS of acceleration magnitude (fault feature)

// ============================================================================
// VECTOR TABLE
// ============================================================================
int main(void);
void Reset_Handler(void);
void Default_Handler(void) { while(1); }

__attribute__((section(".isr_vector"), used))
const uint32_t g_pfnVectors[] = {
    0x2001C000,
    (uint32_t)&Reset_Handler,
    (uint32_t)&Default_Handler, (uint32_t)&Default_Handler,
    (uint32_t)&Default_Handler, (uint32_t)&Default_Handler,
    (uint32_t)&Default_Handler,
    0, 0, 0, 0,
    (uint32_t)&Default_Handler, (uint32_t)&Default_Handler,
    0,
    (uint32_t)&Default_Handler, (uint32_t)&Default_Handler
};

void Reset_Handler(void) { main(); }

int _close(int f){return -1;} int _lseek(int f,int p,int d){return 0;}
int _read(int f,char*p,int l){return 0;} int _write(int f,char*p,int l){return l;}

void delay(volatile uint32_t count) { while(count--) { __asm("nop"); } }

// ============================================================================
// ADC1 DRIVER  (SCT ch0/PA0, MIC ch1/PA1)
// ============================================================================
void ADC1_Init(void) {
    RCC_AHB1ENR |= (1 << 0);
    RCC_APB2ENR |= (1 << 8);
    GPIOA_MODER |= (3 << (0*2)) | (3 << (1*2));   // PA0, PA1 analog
    ADC_SMPR2 &= ~((7 << 0) | (7 << 3));
    ADC_SMPR2 |=  ((4 << 0) | (4 << 3));
    ADC_CR2 |= (1 << 0);
}

uint32_t ADC1_ReadChannel(uint8_t ch) {
    ADC_SQR3 = ch;
    ADC_CR2 |= (1 << 30);
    while (!(ADC_SR & (1 << 1)));
    return ADC_DR;
}

// ============================================================================
// I2C1 DRIVER  (MLX90614 — parked, retained)
// ============================================================================
void I2C1_Init(void) {
    RCC_AHB1ENR |= (1 << 1);
    RCC_APB1ENR |= (1 << 21);
    GPIOB_MODER &= ~((3 << (8*2)) | (3 << (9*2)));
    GPIOB_MODER |=  ((2 << (8*2)) | (2 << (9*2)));
    GPIOB_OTYPER |= (1 << 8) | (1 << 9);
    GPIOB_OSPEEDR |= (3 << (8*2)) | (3 << (9*2));
    GPIOB_PUPDR &= ~((3 << (8*2)) | (3 << (9*2)));
    GPIOB_AFRH &= ~((0xF << 0) | (0xF << 4));
    GPIOB_AFRH |=  ((4 << 0) | (4 << 4));
    I2C_CR1 |= (1 << 15); I2C_CR1 &= ~(1 << 15);
    I2C_CR2 = 16; I2C_CCR = 80; I2C_TRISE = 17;
    I2C_CR1 |= (1 << 0);
}

static int i2c_wait(volatile uint32_t flag_mask) {
    uint32_t timeout = 100000;
    while (!(I2C_SR1 & flag_mask)) { if (--timeout == 0) { mlx_error = 1; return -1; } }
    return 0;
}

int MLX_ReadTemp(uint8_t reg, uint16_t *out) {
    mlx_error = 0;
    I2C_CR1 |= (1 << 8);
    if (i2c_wait(1 << 0) < 0) return -1;
    I2C_DR = (MLX_ADDR << 1) | 0;
    if (i2c_wait(1 << 1) < 0) return -1;
    (void)I2C_SR1; (void)I2C_SR2;
    if (i2c_wait(1 << 7) < 0) return -1;
    I2C_DR = reg;
    if (i2c_wait(1 << 2) < 0) return -1;
    I2C_CR1 |= (1 << 8);
    if (i2c_wait(1 << 0) < 0) return -1;
    I2C_DR = (MLX_ADDR << 1) | 1;
    if (i2c_wait(1 << 1) < 0) return -1;
    I2C_CR1 |= (1 << 10);
    (void)I2C_SR1; (void)I2C_SR2;
    if (i2c_wait(1 << 6) < 0) return -1;
    uint8_t lsb = I2C_DR;
    if (i2c_wait(1 << 6) < 0) return -1;
    uint8_t msb = I2C_DR;
    I2C_CR1 &= ~(1 << 10);
    I2C_CR1 |= (1 << 9);
    if (i2c_wait(1 << 6) < 0) return -1;
    (void)I2C_DR;
    *out = ((uint16_t)msb << 8) | lsb;
    return 0;
}

// ============================================================================
// SPI1 DRIVER  (ADXL345, Mode 3, software CS on PA4)
// ============================================================================
#define CS_LOW()   (GPIOA_ODR &= ~(1 << 4))
#define CS_HIGH()  (GPIOA_ODR |=  (1 << 4))

void SPI1_Init(void) {
    RCC_AHB1ENR |= (1 << 0);           // GPIOA
    RCC_APB2ENR |= (1 << 12);          // SPI1 clock

    // PA5/PA6/PA7 -> Alternate Function mode (10)
    GPIOA_MODER &= ~((3 << (5*2)) | (3 << (6*2)) | (3 << (7*2)));
    GPIOA_MODER |=  ((2 << (5*2)) | (2 << (6*2)) | (2 << (7*2)));

    // High speed on the SPI pins
    GPIOA_OSPEEDR |= (3 << (5*2)) | (3 << (6*2)) | (3 << (7*2));

    // AF5 = SPI1, for pins 5,6,7 (AFRL nibbles 5,6,7)
    GPIOA_AFRL &= ~((0xF << (5*4)) | (0xF << (6*4)) | (0xF << (7*4)));
    GPIOA_AFRL |=  ((5 << (5*4)) | (5 << (6*4)) | (5 << (7*4)));

    // PA4 = CS as plain push-pull output, idle HIGH (deselected)
    GPIOA_MODER &= ~(3 << (4*2));
    GPIOA_MODER |=  (1 << (4*2));
    CS_HIGH();

    // SPI1 config:
    // CPOL=1 CPHA=1 (Mode 3), master, software NSS, MSB first,
    // baud = fPCLK/32 (bits 5:3 = 100). APB2 default 16MHz -> 500kHz. Safe.
    SPI_CR1 = 0;
    SPI_CR1 |= (1 << 0);               // CPHA = 1
    SPI_CR1 |= (1 << 1);               // CPOL = 1
    SPI_CR1 |= (1 << 2);               // MSTR = master
    SPI_CR1 |= (4 << 3);               // BR = /32
    SPI_CR1 |= (1 << 8) | (1 << 9);    // SSI=1, SSM=1 (software slave mgmt)
    SPI_CR1 |= (1 << 6);               // SPE = enable
}

uint8_t SPI1_Transfer(uint8_t data) {
    while (!(SPI_SR & (1 << 1)));      // TXE
    SPI_DR = data;
    while (!(SPI_SR & (1 << 0)));      // RXNE
    return SPI_DR;
}

void ADXL_WriteReg(uint8_t reg, uint8_t val) {
    CS_LOW();
    SPI1_Transfer(reg & 0x3F);         // write: R/W=0, MB=0
    SPI1_Transfer(val);
    CS_HIGH();
}

uint8_t ADXL_ReadReg(uint8_t reg) {
    CS_LOW();
    SPI1_Transfer(reg | ADXL_READ);    // read bit set
    uint8_t v = SPI1_Transfer(0xFF);   // dummy to clock data out
    CS_HIGH();
    return v;
}

void ADXL_Init(void) {
    // DATA_FORMAT = 0x0B -> full resolution, +/-16g range
    ADXL_WriteReg(ADXL_DATA_FORMAT, 0x0B);
    // POWER_CTL = 0x08 -> Measure bit set (exit standby)
    ADXL_WriteReg(ADXL_POWER_CTL, 0x08);
}

void ADXL_ReadXYZ(int16_t *x, int16_t *y, int16_t *z) {
    CS_LOW();
    // read + multi-byte, starting at DATAX0: clocks out 6 bytes
    SPI1_Transfer(ADXL_DATAX0 | ADXL_READ | ADXL_MB);
    uint8_t x0 = SPI1_Transfer(0xFF);
    uint8_t x1 = SPI1_Transfer(0xFF);
    uint8_t y0 = SPI1_Transfer(0xFF);
    uint8_t y1 = SPI1_Transfer(0xFF);
    uint8_t z0 = SPI1_Transfer(0xFF);
    uint8_t z1 = SPI1_Transfer(0xFF);
    CS_HIGH();
    // little-endian, signed 16-bit
    *x = (int16_t)(((uint16_t)x1 << 8) | x0);
    *y = (int16_t)(((uint16_t)y1 << 8) | y0);
    *z = (int16_t)(((uint16_t)z1 << 8) | z0);
}

// ============================================================================
// MAIN
// ============================================================================
int main(void) {
    // Heartbeat LED on PB0
    RCC_AHB1ENR |= (1 << 1);
    GPIOB_MODER &= ~(3 << (0 * 2));
    GPIOB_MODER |=  (1 << (0 * 2));

    ADC1_Init();
    I2C1_Init();
    SPI1_Init();

    // --- ADXL bring-up: verify identity BEFORE trusting any data ---
    adxl_devid = ADXL_ReadReg(ADXL_DEVID);   // must be 0xE5 (229)
    ADXL_Init();

    while(1) {

        // ---------- SCT current (ch0) ----------
        uint32_t offset_sum = 0;
        for (int i = 0; i < SAMPLES; i++) { offset_sum += ADC1_ReadChannel(0); delay(80); }
        dc_offset = (float)offset_sum / SAMPLES;

        double sum_sq = 0.0;
        for (int i = 0; i < SAMPLES; i++) {
            raw_adc = ADC1_ReadChannel(0);
            float centered = (float)raw_adc - dc_offset;
            sum_sq += (double)(centered * centered);
            delay(80);
        }
        rms_counts = sqrtf((float)(sum_sq / SAMPLES));
        current_rms = ((rms_counts / 4095.0f) * 3.3f) * 30.0f;
        if (rms_counts < 35.0f) current_rms = 0.0f;

        // ---------- MAX4466 acoustic (ch1) ----------
        uint32_t mic_offset_sum = 0;
        for (int i = 0; i < MIC_SAMPLES; i++) { mic_offset_sum += ADC1_ReadChannel(1); }
        mic_dc_offset = (float)mic_offset_sum / MIC_SAMPLES;

        double mic_sum_sq = 0.0;
        for (int i = 0; i < MIC_SAMPLES; i++) {
            float c = (float)ADC1_ReadChannel(1) - mic_dc_offset;
            mic_sum_sq += (double)(c * c);
        }
        acoustic_rms = sqrtf((float)(mic_sum_sq / MIC_SAMPLES));

        // ---------- ADXL345 vibration ----------
        // RMS of acceleration magnitude around gravity. At rest the magnitude
        // is ~constant (1g); vibration makes it fluctuate. We remove the mean
        // so the RMS reflects the AC fluctuation = vibration energy.
        double mag_sum = 0.0, mag_sq_sum = 0.0;
        float mags[VIB_SAMPLES];
        for (int i = 0; i < VIB_SAMPLES; i++) {
            ADXL_ReadXYZ((int16_t*)&accel_x, (int16_t*)&accel_y, (int16_t*)&accel_z);
            float m = sqrtf((float)((int32_t)accel_x*accel_x
                                  + (int32_t)accel_y*accel_y
                                  + (int32_t)accel_z*accel_z));
            mags[i] = m;
            mag_sum += m;
        }
        float mag_mean = (float)(mag_sum / VIB_SAMPLES);
        for (int i = 0; i < VIB_SAMPLES; i++) {
            float d = mags[i] - mag_mean;
            mag_sq_sum += (double)(d * d);
        }
        vibration_rms = sqrtf((float)(mag_sq_sum / VIB_SAMPLES));

        // ---------- MLX90614 (parked) ----------
        if (MLX_ReadTemp(MLX_TOBJ1, (uint16_t*)&mlx_raw) == 0) {
            object_temp_c = ((float)mlx_raw * 0.02f) - 273.15f;
        }

        GPIOB_ODR ^= (1 << 0);   // heartbeat
    }
}

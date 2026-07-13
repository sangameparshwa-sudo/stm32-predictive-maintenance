#include <stdint.h>

// ============================================================================
// REGISTERS & MEMORY MAP
// ============================================================================

// Base Addresses
#define PERIPH_BASE         ((uint32_t)0x40000000)
#define AHB1PERIPH_BASE     (PERIPH_BASE + 0x00020000)
#define APB1PERIPH_BASE     (PERIPH_BASE + 0x00000000)

// Reset and Clock Control (RCC)
#define RCC_BASE            (AHB1PERIPH_BASE + 0x00003800)
#define RCC_AHB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR         (*(volatile uint32_t *)(RCC_BASE + 0x40))

// GPIO Port A (For User LED LD2 on PA5)
#define GPIOA_BASE          (AHB1PERIPH_BASE + 0x00000000)
#define GPIOA_MODER         (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_ODR           (*(volatile uint32_t *)(GPIOA_BASE + 0x14))

// GPIO Port B (For I2C1 Pins: PB8 = SCL, PB9 = SDA)
#define GPIOB_BASE          (AHB1PERIPH_BASE + 0x00000400)
#define GPIOB_MODER         (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_OTYPER        (*(volatile uint32_t *)(GPIOB_BASE + 0x04))
#define GPIOB_PUPDR         (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_AFRH          (*(volatile uint32_t *)(GPIOB_BASE + 0x24))

// I2C1 Peripherals
#define I2C1_BASE           (APB1PERIPH_BASE + 0x00005400)
#define I2C1_CR1            (*(volatile uint32_t *)(I2C1_BASE + 0x00))
#define I2C1_CR2            (*(volatile uint32_t *)(I2C1_BASE + 0x04))
#define I2C1_DR             (*(volatile uint32_t *)(I2C1_BASE + 0x10))
#define I2C1_SR1            (*(volatile uint32_t *)(I2C1_BASE + 0x14))
#define I2C1_SR2            (*(volatile uint32_t *)(I2C1_BASE + 0x18))
#define I2C1_CCR            (*(volatile uint32_t *)(I2C1_BASE + 0x1C))
#define I2C1_TRISE          (*(volatile uint32_t *)(I2C1_BASE + 0x20))

// ADXL345 I2C Target Parameters (SDO grounded = 0x53)
#define ADXL_DEV_ADDR       0x53
#define ADXL_REG_DEVID      0x00
#define ADXL_REG_POWER_CTL  0x2D
#define ADXL_REG_DATAX0     0x32

// Global variables to store live axis readings for the debugger expressions view
volatile int16_t x_axis = 0;
volatile int16_t y_axis = 0;
volatile int16_t z_axis = 0;
volatile uint8_t device_id = 0;

// ============================================================================
// BOOT VECTOR SYSTEM TABLE
// ============================================================================

int main(void);
void Reset_Handler(void);

__attribute__((section(".isr_vector"), used))
const uint32_t g_pfnVectors[] = {
    0x20020000,                    // 1. Initial Stack Pointer (Top of 128KB SRAM)
    (uint32_t)&Reset_Handler,      // 2. Reset Vector Address
};

void Reset_Handler(void) {
    main();
}

// Software blocking delay loop
void delay(volatile uint32_t count) {
    while(count--) {
        __asm("nop");
    }
}

// System low-level stubs to keep standard compiler satisfied
int _close(int file) { return -1; }
int _lseek(int file, int ptr, int dir) { return 0; }
int _read(int file, char *ptr, int len) { return 0; }
int _write(int file, char *ptr, int len) { return len; }

// ============================================================================
// BARE-METAL I2C DRIVER ENGINE
// ============================================================================

void I2C1_Init(void) {
    // 1. Enable AHB1/APB1 clocks for GPIOB and I2C1 blocks
    RCC_AHB1ENR |= (1 << 1);  // Enable GPIOB Clock
    RCC_APB1ENR |= (1 << 21); // Enable I2C1 Clock

    // 2. Configure PB8 (SCL) and PB9 (SDA) to Alternate Function Mode (10)
    GPIOB_MODER &= ~((3 << (8 * 2)) | (3 << (9 * 2)));
    GPIOB_MODER |=  ((2 << (8 * 2)) | (2 << (9 * 2)));

    // 3. Set Pins to Open-Drain (Mandatory requirement for I2C bus physical layers)
    GPIOB_OTYPER |= (1 << 8) | (1 << 9);

    // 4. Enable Pull-Up Resistors for safety (01)
    GPIOB_PUPDR &= ~((3 << (8 * 2)) | (3 << (9 * 2)));
    GPIOB_PUPDR |=  ((1 << (8 * 2)) | (1 << (9 * 2)));

    // 5. Map PB8 and PB9 to Alternate Function 4 (AF4 = I2C1) inside high register
    GPIOB_AFRH &= ~((15 << ((8 - 8) * 4)) | (15 << ((9 - 8) * 4)));
    GPIOB_AFRH |=  ((4 << ((8 - 8) * 4))  | (4 << ((9 - 8) * 4)));

    // 6. Reset the I2C Peripheral to guarantee a clean hardware state
    I2C1_CR1 |=  (1 << 15);
    I2C1_CR1 &= ~(1 << 15);

    // 7. Define Peripheral Input Frequency (Defaults to internal HSI = 16 MHz)
    I2C1_CR2 |= (16 & 0x3F); // Set FREQ bits to 16

    // 8. Configure Clock Control Register (CCR) for standard 100 kHz speed limits
    // Formula: CCR = T_i2c / (2 * T_pclk1) -> 10us / (2 * 62.5ns) = 80
    I2C1_CCR = 80;

    // 9. Configure Maximum Rise Time Register (TRISE)
    // Formula: (1000ns / T_pclk1) + 1 -> (1000ns / 62.5ns) + 1 = 17
    I2C1_TRISE = 17;

    // 10. Turn on the I2C peripheral block
    I2C1_CR1 |= (1 << 0); // PE = 1
}

void I2C1_BurstRead(uint8_t slave_addr, uint8_t reg_addr, uint8_t *buffer, uint8_t size) {
    // 1. Generate Start condition
    I2C1_CR1 |= (1 << 8);
    while (!(I2C1_SR1 & (1 << 0))); // Wait until SB (Start Bit) flag triggers

    // 2. Transmit Slave Target Address with Write Bit (0)
    I2C1_DR = (slave_addr << 1);
    while (!(I2C1_SR1 & (1 << 1))); // Wait until ADDR flag flags an address match
    (void)I2C1_SR2;                 // Clear ADDR flag bit by reading SR2 status map

    // 3. Write target internal memory address location register pointer
    I2C1_DR = reg_addr;
    while (!(I2C1_SR1 & (1 << 7))); // Wait until TXE (Transmit Data Register Empty) clears

    // 4. Generate a Repeated Start condition to flip direction bounds
    I2C1_CR1 |= (1 << 8);
    while (!(I2C1_SR1 & (1 << 0))); // Wait for SB flag again

    // 5. Transmit Slave Target Address with Read Bit (1)
    I2C1_DR = (slave_addr << 1) | 0x01;
    while (!(I2C1_SR1 & (1 << 1))); // Wait for ADDR flag match
    (void)I2C1_SR2;                 // Clear ADDR sequence layout

    // 6. Loop to fetch byte streaming parameters safely
    for (uint8_t i = 0; i < size; i++) {
        if (i == (size - 1)) {
            // Last byte processing: Disable Master ACKing and generate a STOP command
            I2C1_CR1 &= ~(1 << 10); // Clear ACK bit
            I2C1_CR1 |=  (1 << 9);  // Generate Stop
        } else {
            I2C1_CR1 |= (1 << 10);  // Enable ACK for remaining pipeline blocks
        }

        while (!(I2C1_SR1 & (1 << 6))); // Wait until RXNE (Receive Buffer Not Empty) updates
        buffer[i] = I2C1_DR;            // Extract register block directly from data lines
    }
}

void I2C1_WriteRegister(uint8_t slave_addr, uint8_t reg_addr, uint8_t data) {
    // 1. Generate Start condition
    I2C1_CR1 |= (1 << 8);
    while (!(I2C1_SR1 & (1 << 0)));

    // 2. Transmit Address (Write mode)
    I2C1_DR = (slave_addr << 1);
    while (!(I2C1_SR1 & (1 << 1)));
    (void)I2C1_SR2;

    // 3. Send target data destination location
    I2C1_DR = reg_addr;
    while (!(I2C1_SR1 & (1 << 7)));

    // 4. Dump configuration byte payload parameters
    I2C1_DR = data;
    while (!(I2C1_SR1 & (1 << 7)));
    while (!(I2C1_SR1 & (1 << 2))); // Wait for BTF (Byte Transfer Finished) to clear pipeline

    // 5. Fire Stop execution cycle
    I2C1_CR1 |= (1 << 9);
}

// ============================================================================
// MAIN PIPELINE ENTRY
// ============================================================================

int main(void) {
    uint8_t data_buffer[6] = {0};

    // 1. Enable GPIOA Peripherals (for our local sanity check blink LED)
    RCC_AHB1ENR |= (1 << 0);
    GPIOA_MODER &= ~(3 << (5 * 2));
    GPIOA_MODER |=  (1 << (5 * 2));

    // 2. Boot up the I2C Hardware Block
    I2C1_Init();
    delay(10000); // Small wait loop for power line stabilization

    // 3. Read Device ID to confirm wiring is working properly (Should equal 0xE5)
    I2C1_BurstRead(ADXL_DEV_ADDR, ADXL_REG_DEVID, (uint8_t*)&device_id, 1);

    if (device_id == 0xE5) {
        // Validation check matches! Wake sensor from standby and activate measurements
        I2C1_WriteRegister(ADXL_DEV_ADDR, ADXL_REG_POWER_CTL, 0x08);
    } else {
        // Hardware communication link failure loop indicator
        while(1) {
            GPIOA_ODR ^= (1 << 5); // Rapidly flash LED to alert something is broken
            delay(100000);
        }
    }

    /* Primary Application Operational Pipeline Loop */
    while(1) {
        // Fetch 6 continuous data data bytes starting at Axis X low byte register 0x32
        I2C1_BurstRead(ADXL_DEV_ADDR, ADXL_REG_DATAX0, data_buffer, 6);

        // Assemble two uint8_t blocks into signed 16-bit integer containers using bit-shifts
        x_axis = (int16_t)((data_buffer[1] << 8) | data_buffer[0]);
        y_axis = (int16_t)((data_buffer[3] << 8) | data_buffer[2]);
        z_axis = (int16_t)((data_buffer[5] << 8) | data_buffer[4]);

        // Toggle user LED on each loop execution pass to verify activity
        GPIOA_ODR ^= (1 << 5);

        // Keep loop update interval running smoothly
        delay(200000);
    }
}

#include <stdint.h>
#include <math.h>

// ============================================================================
// PdM EDGE NODE — FOUR SENSORS + UART STREAM
// STM32F446RE, bare-metal, for a CubeIDE "Empty Project"
// (startup + syscalls provided by project; NO vector table / stubs here)
//
//   SCT-013   current   -> ADC1 ch0  (PA0)
//   MAX4466   acoustic   -> ADC1 ch1  (PA1)
//   ADXL345   vibration  -> SPI1 (PA4 CS, PA5 SCK, PA6 MISO, PA7 MOSI)
//   DS18B20   temperature-> 1-Wire on PC0 (+4.7k pull-up to 3.3V)
//   UART out  -> USART2 TX (PA2) -> ST-Link USB COM port @ 115200
//
// FPU is enabled (float math would HardFault otherwise on an Empty Project).
// DS18B20 is NON-BLOCKING: we start its conversion, read the 3 fast sensors,
// and collect the temperature on the next loop -> fast sensors stay fast.
//
// Stream format:  "current,vibration,acoustic,temperature\n"
//   e.g.  "0.00,1.80,120.5,26.75\n"
// ============================================================================

#define PERIPH_BASE     ((uint32_t)0x40000000)
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000)
#define APB1PERIPH_BASE (PERIPH_BASE + 0x00000000)
#define APB2PERIPH_BASE (PERIPH_BASE + 0x00010000)

// RCC
#define RCC_BASE     (AHB1PERIPH_BASE + 0x00003800)
#define RCC_AHB1ENR  (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR  (*(volatile uint32_t *)(RCC_BASE + 0x40))
#define RCC_APB2ENR  (*(volatile uint32_t *)(RCC_BASE + 0x44))

// GPIOA (PA0 SCT, PA1 MIC, PA2 UART_TX, PA4 CS, PA5 SCK, PA6 MISO, PA7 MOSI)
#define GPIOA_BASE    (AHB1PERIPH_BASE + 0x00000000)
#define GPIOA_MODER   (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_OSPEEDR (*(volatile uint32_t *)(GPIOA_BASE + 0x08))
#define GPIOA_ODR     (*(volatile uint32_t *)(GPIOA_BASE + 0x14))
#define GPIOA_AFRL    (*(volatile uint32_t *)(GPIOA_BASE + 0x20))

// GPIOC (PC0 = 1-Wire)
#define GPIOC_BASE    (AHB1PERIPH_BASE + 0x00000800)
#define GPIOC_MODER   (*(volatile uint32_t *)(GPIOC_BASE + 0x00))
#define GPIOC_OTYPER  (*(volatile uint32_t *)(GPIOC_BASE + 0x04))
#define GPIOC_PUPDR   (*(volatile uint32_t *)(GPIOC_BASE + 0x0C))
#define GPIOC_IDR     (*(volatile uint32_t *)(GPIOC_BASE + 0x10))
#define GPIOC_ODR     (*(volatile uint32_t *)(GPIOC_BASE + 0x14))

// ADC1
#define ADC1_BASE   (APB2PERIPH_BASE + 0x00002000)
#define ADC_SR      (*(volatile uint32_t *)(ADC1_BASE + 0x00))
#define ADC_CR2     (*(volatile uint32_t *)(ADC1_BASE + 0x08))
#define ADC_SMPR2   (*(volatile uint32_t *)(ADC1_BASE + 0x10))
#define ADC_SQR3    (*(volatile uint32_t *)(ADC1_BASE + 0x34))
#define ADC_DR      (*(volatile uint32_t *)(ADC1_BASE + 0x4C))

// SPI1
#define SPI1_BASE   (APB2PERIPH_BASE + 0x00003000)
#define SPI_CR1     (*(volatile uint32_t *)(SPI1_BASE + 0x00))
#define SPI_SR      (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI_DR      (*(volatile uint32_t *)(SPI1_BASE + 0x0C))

// TIM2 (microsecond delay)
#define TIM2_BASE   (APB1PERIPH_BASE + 0x00000000)
#define TIM2_CR1    (*(volatile uint32_t *)(TIM2_BASE + 0x00))
#define TIM2_CNT    (*(volatile uint32_t *)(TIM2_BASE + 0x24))
#define TIM2_PSC    (*(volatile uint32_t *)(TIM2_BASE + 0x28))
#define TIM2_ARR    (*(volatile uint32_t *)(TIM2_BASE + 0x2C))
#define TIM2_EGR    (*(volatile uint32_t *)(TIM2_BASE + 0x14))

// USART2
#define USART2_BASE  (APB1PERIPH_BASE + 0x00004400)
#define USART_SR     (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART_DR     (*(volatile uint32_t *)(USART2_BASE + 0x04))
#define USART_BRR    (*(volatile uint32_t *)(USART2_BASE + 0x08))
#define USART_CR1    (*(volatile uint32_t *)(USART2_BASE + 0x0C))

#define OW_PIN 0            // PC0
#define SAMPLES     500     // SCT window (reduced for speed)
#define MIC_SAMPLES 1000    // mic burst (reduced for speed)
#define VIB_SAMPLES 128     // vibration burst (reduced for speed)

// ADXL345
#define ADXL_DEVID       0x00
#define ADXL_POWER_CTL   0x2D
#define ADXL_DATA_FORMAT 0x31
#define ADXL_DATAX0      0x32
#define ADXL_READ  0x80
#define ADXL_MB    0x40

// ---- globals (also visible in debugger) ----
volatile float    current_rms   = 0.0f;
volatile float    acoustic_rms  = 0.0f;
volatile uint8_t  adxl_devid    = 0;
volatile int16_t  accel_x=0, accel_y=0, accel_z=0;
volatile float    vibration_rms = 0.0f;
volatile int16_t  temp_raw      = 0;
volatile float    temperature_c = 0.0f;
volatile int      ds_present     = 0;

// ============================================================================
// microsecond timer
// ============================================================================
void us_timer_init(void){
    RCC_APB1ENR |= (1 << 0);
    TIM2_PSC = 16 - 1;
    TIM2_ARR = 0xFFFFFFFF;
    TIM2_EGR = 1;
    TIM2_CR1 = 1;
}
void delay_us(uint32_t us){ uint32_t s=TIM2_CNT; while((TIM2_CNT-s)<us){} }

// ============================================================================
// UART
// ============================================================================
void UART2_Init(void){
    RCC_AHB1ENR |= (1 << 0);
    RCC_APB1ENR |= (1 << 17);
    GPIOA_MODER &= ~(3u << (2*2));
    GPIOA_MODER |=  (2u << (2*2));
    GPIOA_AFRL  &= ~(0xFu << (2*4));
    GPIOA_AFRL  |=  (7u << (2*4));
    USART_BRR = 139;
    USART_CR1 = (1<<3) | (1<<13);
}
void uart_putc(char c){ while(!(USART_SR & (1<<7))); USART_DR = c; }
void uart_puts(const char*s){ while(*s) uart_putc(*s++); }
void uart_putfloat(float v,int dec){
    if(v<0){ uart_putc('-'); v=-v; }
    long scale=1; for(int i=0;i<dec;i++) scale*=10;
    long scaled=(long)(v*scale+0.5f);
    long ip=scaled/scale, fp=scaled%scale;
    char buf[12]; int n=0;
    if(ip==0) buf[n++]='0';
    while(ip>0){ buf[n++]='0'+(ip%10); ip/=10; }
    while(n--) uart_putc(buf[n]);
    if(dec>0){ uart_putc('.');
        for(int i=dec-1;i>=0;i--){ long p=1; for(int k=0;k<i;k++) p*=10; uart_putc('0'+((fp/p)%10)); } }
}

// ============================================================================
// ADC (SCT ch0, MIC ch1)
// ============================================================================
void ADC1_Init(void){
    RCC_AHB1ENR |= (1 << 0);
    RCC_APB2ENR |= (1 << 8);
    GPIOA_MODER |= (3u<<(0*2)) | (3u<<(1*2));
    ADC_SMPR2 &= ~((7u<<0)|(7u<<3));
    ADC_SMPR2 |=  ((4u<<0)|(4u<<3));
    ADC_CR2 |= (1<<0);
}
uint32_t ADC1_Read(uint8_t ch){
    ADC_SQR3 = ch;
    ADC_CR2 |= (1<<30);
    while(!(ADC_SR & (1<<1)));
    return ADC_DR;
}

// ============================================================================
// SPI1 (ADXL345, Mode 3, CS on PA4, /128)
// ============================================================================
#define CS_LOW()  (GPIOA_ODR &= ~(1<<4))
#define CS_HIGH() (GPIOA_ODR |=  (1<<4))
void SPI1_Init(void){
    RCC_AHB1ENR |= (1 << 0);
    RCC_APB2ENR |= (1 << 12);
    GPIOA_MODER &= ~((3u<<(5*2))|(3u<<(6*2))|(3u<<(7*2)));
    GPIOA_MODER |=  ((2u<<(5*2))|(2u<<(6*2))|(2u<<(7*2)));
    GPIOA_OSPEEDR |= (3u<<(5*2))|(3u<<(6*2))|(3u<<(7*2));
    GPIOA_AFRL &= ~((0xFu<<(5*4))|(0xFu<<(6*4))|(0xFu<<(7*4)));
    GPIOA_AFRL |=  ((5u<<(5*4))|(5u<<(6*4))|(5u<<(7*4)));
    GPIOA_MODER &= ~(3u<<(4*2));
    GPIOA_MODER |=  (1u<<(4*2));
    CS_HIGH();
    SPI_CR1 = 0;
    SPI_CR1 |= (1<<0)|(1<<1);   // Mode 3
    SPI_CR1 |= (1<<2);          // master
    SPI_CR1 |= (6<<3);          // /128
    SPI_CR1 |= (1<<8)|(1<<9);
    SPI_CR1 |= (1<<6);
}
uint8_t SPI1_TX(uint8_t d){ while(!(SPI_SR&(1<<1))); SPI_DR=d; while(!(SPI_SR&(1<<0))); return SPI_DR; }
void ADXL_W(uint8_t r,uint8_t v){ CS_LOW(); SPI1_TX(r&0x3F); SPI1_TX(v); CS_HIGH(); }
uint8_t ADXL_R(uint8_t r){ CS_LOW(); SPI1_TX(r|ADXL_READ); uint8_t v=SPI1_TX(0xFF); CS_HIGH(); return v; }
void ADXL_Init(void){ ADXL_W(ADXL_DATA_FORMAT,0x0B); ADXL_W(ADXL_POWER_CTL,0x08); }
void ADXL_XYZ(int16_t*x,int16_t*y,int16_t*z){
    CS_LOW();
    SPI1_TX(ADXL_DATAX0|ADXL_READ|ADXL_MB);
    uint8_t x0=SPI1_TX(0xFF),x1=SPI1_TX(0xFF);
    uint8_t y0=SPI1_TX(0xFF),y1=SPI1_TX(0xFF);
    uint8_t z0=SPI1_TX(0xFF),z1=SPI1_TX(0xFF);
    CS_HIGH();
    *x=(int16_t)((x1<<8)|x0); *y=(int16_t)((y1<<8)|y0); *z=(int16_t)((z1<<8)|z0);
}

// ============================================================================
// 1-WIRE (DS18B20)
// ============================================================================
static inline void ow_low(void){ GPIOC_MODER&=~(3u<<(OW_PIN*2)); GPIOC_MODER|=(1u<<(OW_PIN*2)); GPIOC_ODR&=~(1u<<OW_PIN); }
static inline void ow_rel(void){ GPIOC_MODER&=~(3u<<(OW_PIN*2)); }
static inline int  ow_pin(void){ return (GPIOC_IDR>>OW_PIN)&1; }
int ow_reset(void){ int p; ow_low(); delay_us(480); ow_rel(); delay_us(70); p=(ow_pin()==0); delay_us(410); return p; }
void ow_wbit(int b){ if(b){ow_low();delay_us(6);ow_rel();delay_us(64);}else{ow_low();delay_us(60);ow_rel();delay_us(10);} }
int  ow_rbit(void){ int b; ow_low();delay_us(6);ow_rel();delay_us(9); b=ow_pin(); delay_us(55); return b; }
void ow_wbyte(uint8_t v){ for(int i=0;i<8;i++){ow_wbit(v&1);v>>=1;} }
uint8_t ow_rbyte(void){ uint8_t v=0; for(int i=0;i<8;i++){if(ow_rbit())v|=(1<<i);} return v; }
uint8_t crc8(uint8_t*d,int n){ uint8_t c=0; for(int i=0;i<n;i++){uint8_t b=d[i]; for(int j=0;j<8;j++){uint8_t m=(c^b)&1;c>>=1;if(m)c^=0x8C;b>>=1;}} return c; }

// start a conversion (non-blocking: don't wait here)
void ds_start(void){
    ds_present = ow_reset();
    if(ds_present){ ow_wbyte(0xCC); ow_wbyte(0x44); }
}
// collect the result (call at least 750ms after ds_start)
void ds_collect(void){
    if(!ow_reset()){ ds_present=0; return; }
    ds_present=1;
    ow_wbyte(0xCC); ow_wbyte(0xBE);
    uint8_t sp[9]; for(int i=0;i<9;i++) sp[i]=ow_rbyte();
    if(crc8(sp,8)==sp[8]){
        temp_raw=(int16_t)((sp[1]<<8)|sp[0]);
        temperature_c=(float)temp_raw/16.0f;
    }
}

// ============================================================================
// MAIN
// ============================================================================
int main(void){
    // Enable FPU (float math would HardFault otherwise)
    *(volatile uint32_t*)0xE000ED88 |= (0xF << 20);
    __asm volatile("dsb"); __asm volatile("isb");

    // GPIOC for 1-Wire
    RCC_AHB1ENR |= (1 << 2);
    GPIOC_PUPDR &= ~(3u<<(OW_PIN*2)); GPIOC_PUPDR |= (1u<<(OW_PIN*2));
    GPIOC_OTYPER |= (1u<<OW_PIN);
    ow_rel();

    us_timer_init();
    ADC1_Init();
    SPI1_Init();
    UART2_Init();

    adxl_devid = ADXL_R(ADXL_DEVID);   // expect 0xE5
    ADXL_Init();

    uart_puts("PdM 4-sensor stream start\n");

    // kick off first temperature conversion
    ds_start();

    while(1){
        // ---- SCT current (ch0) ----
        uint32_t osum=0;
        for(int i=0;i<SAMPLES;i++){ osum+=ADC1_Read(0); }
        float dc=(float)osum/SAMPLES;
        double ss=0;
        for(int i=0;i<SAMPLES;i++){ float c=(float)ADC1_Read(0)-dc; ss+=(double)(c*c); }
        float rms=sqrtf((float)(ss/SAMPLES));
        current_rms = ((rms/4095.0f)*3.3f)*30.0f;   // change *30 to *20 or *10 if using that CT
        if(rms<35.0f) current_rms=0.0f;

        // ---- MAX4466 acoustic (ch1) ----
        uint32_t mosum=0;
        for(int i=0;i<MIC_SAMPLES;i++){ mosum+=ADC1_Read(1); }
        float mdc=(float)mosum/MIC_SAMPLES;
        double mss=0;
        for(int i=0;i<MIC_SAMPLES;i++){ float c=(float)ADC1_Read(1)-mdc; mss+=(double)(c*c); }
        acoustic_rms = sqrtf((float)(mss/MIC_SAMPLES));

        // ---- ADXL345 vibration ----
        double msum=0; float mags[VIB_SAMPLES];
        for(int i=0;i<VIB_SAMPLES;i++){
            ADXL_XYZ((int16_t*)&accel_x,(int16_t*)&accel_y,(int16_t*)&accel_z);
            float m=sqrtf((float)((int32_t)accel_x*accel_x+(int32_t)accel_y*accel_y+(int32_t)accel_z*accel_z));
            mags[i]=m; msum+=m;
        }
        float mmean=(float)(msum/VIB_SAMPLES);
        double vss=0;
        for(int i=0;i<VIB_SAMPLES;i++){ float d=mags[i]-mmean; vss+=(double)(d*d); }
        vibration_rms=sqrtf((float)(vss/VIB_SAMPLES));

        // ---- DS18B20: collect the conversion started last loop, then restart ----
        // The 3 sensors above took time; the conversion has had time to finish.
        ds_collect();
        ds_start();

        // ---- STREAM all four ----
        uart_putfloat(current_rms,2);   uart_putc(',');
        uart_putfloat(vibration_rms,2); uart_putc(',');
        uart_putfloat(acoustic_rms,1);  uart_putc(',');
        uart_putfloat(temperature_c,2); uart_putc('\n');
    }
}

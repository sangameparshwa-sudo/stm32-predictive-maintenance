/* Entry Point */
ENTRY(Reset_Handler)

/* Specify the memory areas for STM32F446RE */
MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 512K
  RAM (xrw)       : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Define output sections */
SECTIONS
{
  /* The vector table MUST go first into FLASH at absolute 0x08000000 */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector)) 
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  /* Initialized data sections go into RAM, load LMA copy from FLASH */
  .data :
  {
    . = ALIGN(4);
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    . = ALIGN(4);
  } >RAM AT> FLASH

  /* Uninitialized data section into RAM */
  .bss :
  {
    . = ALIGN(4);
    *(.bss)
    *(.bss*)
    . = ALIGN(4);
  } >RAM
}
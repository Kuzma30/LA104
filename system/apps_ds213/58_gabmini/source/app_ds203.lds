MEMORY
{
  null (rwx): ORIGIN = 0x00001000, LENGTH = 4K
  /* above the factory data of the DS203 (FPGA bitstream, calibration and
     device info at 0x0802C000..0x0803FFFF, LinearProgram refuses them);
     0x08012C00..0x0802BC00 (100 KB) was too small. Needs the 512 KB flash
     of the STM32F103VE (HW 2.6x / 2.7x). */
  rom (rx) : ORIGIN = 0x08040000, LENGTH = 144K
  /* the compiled translation (common/lang.h), written by the application */
  lang (r) : ORIGIN = 0x08064000, LENGTH = 16K
  /* OS RAM ends at 0x200043CC (no heap), 0x20004400..0x20005000 is free.
     The application runs on the OS stack: main() of the OS keeps 2.8 KB of
     buffers in its frame, so the app starts with SP = 0x2000ABF0 and the
     loader itself goes down to ~0x2000A900 while zeroing .bss. The region
     ends at 0x2000A600 to keep >= 1.5 KB for the stack. */
  ram (rwx) : ORIGIN = 0x20004400, LENGTH = 0x6200
  /* The STM32F103VE has 64 KB RAM, the OS uses it up to 0x2000B800 (stack
     top 0x2000B700): 0x2000C000..0x2000FFFF is free. The loader zeroes every
     section named .bss* (Execute.cpp), elfstrip keeps them. */
  ramhi (rwx) : ORIGIN = 0x2000C000, LENGTH = 16K
}

/*
failed 648 bytes at 0801f648
                    0801f7f4
                    0802af30
                    08029f30
                    0801f050
*/
SECTIONS
  {
    _addressLang = ORIGIN(lang);
    _addressLangEnd = ORIGIN(lang) + LENGTH(lang);
    .text : {
      _addressRomBegin = .;
      *(.entry)
      *(.text*)        /* Program code */
      *(.rodata*)      /* Read only data */
      _addressRomEnd = .;
    } >rom

    .data : {
      _addressRamBegin = .;
      *(.data*)        /* Data memory */
      *(.dynamic)
    } >ram

.bss_hi (NOLOAD) : {
    *(.bss_hi*)
    } >ramhi

  .bss : {
    *(.bss*)         /* Zero-filled run time allocate data memory */
    _addressRamEnd = .;
    } >ram

  .rel.plt : { *(.rel.plt)  } > rom
  .plt : { *(.plt) } > rom
  .got : { *(.got.plt) *(.got) } > ram /* relocation fixed by memory write! */
  .dynsym : { *(.dynsym) } > null
  .dynstr : { *(.dynstr) } > null

  }  


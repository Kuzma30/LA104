MEMORY
{
  null (rwx): ORIGIN = 0x00001000, LENGTH = 4K
  /* above the oscilloscope (0x08040000..0x08067FFF), below 67_freq: no flashing of the
     other application when switching between them; 512 KB flash (VE) */
  rom (rx) : ORIGIN = 0x08068000, LENGTH = 56K
  /* the compiled translation (common/lang.h), written by the application
     (LOGIC.TXT takes about 5 KB; 12 KB at 0x08075000 before the USB decoder) */
  lang (r) : ORIGIN = 0x08076000, LENGTH = 8K
  /* as the other applications: 0x20004400.., >= 1.5 KB stack below the OS */
  ram (rwx) : ORIGIN = 0x20004400, LENGTH = 0x6200
  /* RAM above the OS (64 KB RAM of the STM32F103VE), zeroed by the loader */
  ramhi (rwx) : ORIGIN = 0x2000C000, LENGTH = 16K
}
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


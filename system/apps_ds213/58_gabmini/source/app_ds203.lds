MEMORY
{
  null (rwx): ORIGIN = 0x00001000, LENGTH = 4K
  /* up to 0x0802BC00, the DS203 FPGA bitstream starts at 0x0802C000 */
  rom (rx) : ORIGIN = 0x08012c00, LENGTH = 100K
  /* OS RAM ends at 0x200043CC (no heap), 0x20004400..0x20005000 is free.
     The application runs on the OS stack: main() of the OS keeps 2.8 KB of
     buffers in its frame, so the app starts with SP = 0x2000ABF0 and the
     loader itself goes down to ~0x2000A900 while zeroing .bss. The region
     ends at 0x2000A600 to keep >= 1.5 KB for the stack. */
  ram (rwx) : ORIGIN = 0x20004400, LENGTH = 0x6200
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


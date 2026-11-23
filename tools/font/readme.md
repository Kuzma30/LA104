# Unicode glyphs of the OS font

`gen_fontuni.py` makes `system/os_host/source/bios/fontuni.h` (and the copy in
`system/os_platform/common/source/bios/`): the code points of the 8x14 CP437
font of `font.h` and extra glyphs (Cyrillic U+0401..U+045F, U+0490/0491, some
punctuation) taken from the console font `CyrSlav-VGA14.psf.gz` of the Debian
package console-setup (VGA 8x14 glyphs; the console fonts are public domain,
the source BDF font is (c) Dmitry Bolkhovityanov, free to use and modify).

    python3 gen_fontuni.py CyrSlav-VGA14.psf.gz > fontuni.h

`BIOS::LCD::Print` reads UTF-8: a valid sequence is a code point, any other
byte is drawn with the CP437 glyph as before (old texts with bytes >= 0x80
look the same). A code point without a glyph is drawn as `?`.

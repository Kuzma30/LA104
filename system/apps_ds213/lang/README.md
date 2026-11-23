# Languages of the DS203 applications

The language is chosen by the file `LANG.CFG` in the root of the disk: its
first word is the language code (`UK`: Ukrainian). Without the file, or with
`EN`, the applications are in English (built in).

The translations are text files on the disk, one per application:

    LANG.CFG               UK
    LANG/UK/LOGIC.TXT      logic analyzer (logic.elf)
    LANG/UK/OSC.TXT        oscilloscope (miniosc.elf)

Format (UTF-8), one string per line, the English text is the key:

    # comment
    "Trigger" = "Тригер"
    "no match #" = "немає збігу #"

Escapes `\"`, `\\`, `\n`, `\t`. A string without a translation (missing or
empty) stays English. A translated format of Printf must keep the same `%`
conversions in the same order. Keys with a context (`"gen:Pulse"`) are for
words with two meanings.

At its start an application compiles its file into a table in its own flash
area (logic: 0x08075000, 12 KB; oscilloscope: 0x08064000, 16 KB); the area
is written again only when the file changed (size and checksum are kept),
so the flash is not worn. An error is shown in the status line
(`no LANG/UK/LOGIC.TXT`, `translation too long`).

The screen font (OS, `BIOS::LCD::Print`) reads UTF-8: Latin, CP437 symbols,
Cyrillic (with Ukrainian Ґ Є І Ї), a few punctuation marks. Other
characters are drawn as `?`. A new language needs its letters in
`tools/font/gen_fontuni.py` (and the OS built again).

The width of a text is checked by the PC renderers of the manual (every
glyph outside the 400x240 screen is reported).

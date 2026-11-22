#!/bin/bash
# Logic analyzer for the DS203 (LA104 OS): build/66logic3.elf
set -e
cd "$(dirname "$0")"
mkdir -p build
cd build

arm-none-eabi-g++ -Wall -Os -Werror -fno-common -mcpu=cortex-m3 -mthumb -msoft-float -fno-exceptions -fno-rtti \
  -fno-threadsafe-statics -Wno-psabi -MD -D DS203 -D _ARM -D STM32F10X_HD -c \
  ../source/main.cpp ../source/fpga.cpp \
  -I../../../os_library/include/ -I../source/

arm-none-eabi-gcc -Os -fPIC -mcpu=cortex-m3 -mthumb -o output.elf -nostartfiles -T ../source/app_ds203.lds \
  ./main.o ./fpga.o -lbios_ds203 -L../../../os_library/build -lm

arm-none-eabi-objdump -d -S output.elf > output.asm
arm-none-eabi-readelf -lW output.elf | grep LOAD

../../../../tools/elfstrip/elfstrip output.elf 66logic3.elf

find . -type f -name '*.o' -delete
find . -type f -name '*.d' -delete

#https://developer.arm.com/open-source/gnu-toolchain/gnu-rm/downloads
# FPGA 2.64 tester, DS203 only (needs the FPGA image of system/fpga_ds203)

mkdir -p build
cd build

arm-none-eabi-g++ -Os -Werror -fno-common -mcpu=cortex-m3 -mthumb -msoft-float -fno-use-cxa-atexit -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wno-psabi -DDS203 -MD -D _ARM -D STM32F10X_HD -c ../source/main.cpp -I../../../os_library/include/ -I ../../../os_host/lib/CMSIS/Device/STM32F10x/Include -I ../../../os_host/lib/STM32F10x_StdPeriph_Driver/inc -I ../../../os_host/lib/CMSIS/Include
arm-none-eabi-gcc -fPIC -mcpu=cortex-m3 -mthumb -o output.elf -fno-use-cxa-atexit -nostartfiles -T ../source/app.lds ./main.o -lbios_ds203 -lm -L../../../os_library/build

arm-none-eabi-objdump -d -S output.elf > output.asm
arm-none-eabi-readelf -all output.elf > output.txt

find . -type f -name '*.o' -delete
find . -type f -name '*.d' -delete

../../../../tools/elfstrip/elfstrip output.elf 64fpgtst.elf

nm --print-size --size-sort -gC output.elf | grep " B " > symbols_ram.txt
nm --print-size --size-sort -gC output.elf | grep " T " > symbols_rom.txt
nm --print-size --size-sort -gC output.elf > symbols_all.txt

#include <stdint.h>
#include <stdbool.h>
#include "library/STM32F10x_StdPeriph_Driver/inc/misc.h"
#include "BIOS.h"

void Delay_mS(uint32_t mS);

extern void (* g_pfnVectors[76])(void);

#define GPIOD_BRR   (*((volatile uint32_t *)(0x40011400+0x14)))
#define GPIOD_BSRR  (*((volatile uint32_t *)(0x40011400+0x10)))
#define LCD_nRST_LOW()    GPIOD_BRR  = GPIO_Pin_11
#define LCD_nRST_HIGH()   GPIOD_BSRR = GPIO_Pin_11
#define LCD_RS_LOW()      GPIOD_BRR  = GPIO_Pin_12
#define LCD_RS_HIGH()     GPIOD_BSRR = GPIO_Pin_12
#define LCD_PORT *((volatile uint16_t*)0x60000000)

// LCD access. DS203 units were shipped with different LCD controllers, the
// factory SYS (LCD.c of DS203_SYS 1.5x, released by e-Design under the MIT
// license in github.com/matthiasbock/dso203) supports two of them:
//   ILI9327  - MIPI DCS commands (0x2A/0x2B window, 0x2C write, 0x2E read),
//              read ID 0xEF returns 02 04 93 27
//   R61509V  - 16 bit registers (0x0210..0x0213 window, 0x0200/0x0201 address,
//              0x0202 GRAM), device code register 0x0000 reads 0xB509
// The controller is detected at the first access and driven directly, the
// same way as SYS does it. An unknown controller (other hardware revisions,
// other SYS versions) is driven through the SYS routines as a fallback.

enum { LcdUnknown, LcdIli9327, LcdR61509, LcdSys };
static int gLcdType = LcdUnknown;
static bool gLcdWindowed = false;

static void LCD_WR_Ctrl(int Reg)
{
  LCD_RS_LOW();
  LCD_PORT = Reg;
  LCD_RS_HIGH();
}

static void LCD_WR_REG(int Reg, int Data)
{
  LCD_RS_LOW();
  LCD_PORT = Reg;
  LCD_RS_HIGH();
  LCD_PORT = Data;
}

static void LcdDetect(void)
{
  // ILI9327 the same way as SYS LCD_RD_Type
  LCD_WR_Ctrl(0xEF);
  uint32_t id = (LCD_PORT & 0xFF);
  id  = (LCD_PORT & 0xFF) << 24;
  id |= (LCD_PORT & 0xFF) << 16;
  id |= (LCD_PORT & 0xFF) << 8;
  id |= (LCD_PORT & 0xFF);
  if (id == 0x02049327)
  {
    gLcdType = LcdIli9327;
    LCD_WR_Ctrl(0x2C);
    return;
  }
  // R61509V device code (index 0x0000 is NOP on DCS controllers)
  LCD_WR_Ctrl(0x0000);
  uint16_t code1 = LCD_PORT;
  uint16_t code2 = LCD_PORT;
  if (code1 == 0xB509 || code2 == 0xB509)
  {
    gLcdType = LcdR61509;
    LCD_WR_Ctrl(0x0202);
    return;
  }
  gLcdType = LcdSys;
}

static inline int LcdType(void)
{
  if (gLcdType == LcdUnknown)
    LcdDetect();
  return gLcdType;
}

int LcdControllerType(void)
{
  return LcdType();
}

void Set_Pixel(uint_fast16_t Color)
{
  if (LcdType() == LcdSys)
    __LCD_SetPixl(Color);
  else
    LCD_PORT = Color;
}

void ReadStart(void)
{
  // R61509V: GRAM index, the first read after it is a dummy one
  if (LcdType() == LcdR61509)
  {
    LCD_WR_Ctrl(0x0202);
    (void)LCD_PORT;
  }
}

void ReadFinish(void)
{
  if (LcdType() == LcdR61509)
    LCD_WR_Ctrl(0x0202);
}

uint32_t ReadPixel()
{
  switch (LcdType())
  {
    case LcdIli9327:
    {
      uint16_t Data;
      LCD_WR_Ctrl(0x2E);
      Data = LCD_PORT;
      Data = LCD_PORT;
      LCD_WR_Ctrl(0x2C);
      return Data;
    }
    case LcdR61509:
      return LCD_PORT;
    default:
      return __LCD_GetPixl();
  }
}

// x1..x2-1 horizontal (0..399), y1..y2-1 vertical (0..239), as SYS LCD_Set_Block
static void SetBlockInclusive(int x1, int x2, int y1, int y2)
{
  switch (LcdType())
  {
    case LcdIli9327:
      LCD_WR_Ctrl(0x2A);
      LCD_PORT = (y1 >> 8);
      LCD_PORT = (y1 & 0xFF);
      LCD_PORT = (y2 >> 8);
      LCD_PORT = (y2 & 0xFF);
      LCD_WR_Ctrl(0x2B);
      LCD_PORT = (x1 >> 8);
      LCD_PORT = (x1 & 0xFF);
      LCD_PORT = (x2 >> 8);
      LCD_PORT = (x2 & 0xFF);
      LCD_WR_Ctrl(0x2C);
      break;
    case LcdR61509:
      LCD_WR_REG(0x0211, y2);
      LCD_WR_REG(0x0213, x2);
      LCD_WR_REG(0x0210, y1);
      LCD_WR_REG(0x0212, x1);
      LCD_WR_REG(0x0200, y1);
      LCD_WR_REG(0x0201, x1);
      LCD_WR_Ctrl(0x0202);
      break;
    default:
      __LCD_Set_Block(x1, x2, y1, y2);
  }
}

void Set_Block(int x1, int y1, int x2, int y2)
{
  // callers pass exclusive end coordinates
  SetBlockInclusive(x1, x2-1, y1, y2-1);
  gLcdWindowed = true;
}

void Set_Posi(uint_fast16_t x, uint_fast16_t y)
{
  // callers (Clear) may write past the end of a row, so the drawing window
  // has to be the whole screen again after a Set_Block
  if (gLcdWindowed)
  {
    SetBlockInclusive(0, 399, 0, 239);
    gLcdWindowed = false;
  }
  switch (LcdType())
  {
    case LcdIli9327:
      // SYS Point_SCR: row y to the end of the screen
      LCD_WR_Ctrl(0x2A);
      LCD_PORT = (0x00);
      LCD_PORT = (y & 0xFF);
      LCD_PORT = (0x00);
      LCD_PORT = (0xEF);
      LCD_WR_Ctrl(0x2B);
      LCD_PORT = (x >> 8);
      LCD_PORT = (x & 0xFF);
      LCD_PORT = (399 >> 8);
      LCD_PORT = (399 & 0xFF);
      LCD_WR_Ctrl(0x2C);
      break;
    case LcdR61509:
      LCD_WR_REG(0x0200, y);
      LCD_WR_REG(0x0201, x);
      LCD_WR_Ctrl(0x0202);
      break;
    default:
      __Point_SCR(x, y);
  }
}

void xBeep(bool b)
{
  #define TIM8_CCR2   (*((vu32 *)(0x40013400+0x38)))
  TIM8_CCR2 = 100 - (b ? 100 : 0)/2;
//  __Set(BEEP_VOLUME, b ? 100 : 0);
}

void EnableUsb(bool enable)
{
}

void HardwareInit()
{
  NVIC_SetVectorTable(NVIC_VectTab_FLASH, (uint32_t)g_pfnVectors - NVIC_VectTab_FLASH);
  __USB_Init();
  xBeep(false);
}

uint32_t GetKeys()
{
  uint32_t status = ~__Get(KEY_STATUS, 0);
  uint32_t out = 0;

  if (status & K_INDEX_D_STATUS ) out |= KeyLeft;
  if (status & K_INDEX_I_STATUS ) out |= KeyRight;
//  if (status & K_INDEX_S_STATUS ) out |= EKeyMask::KeyF5;
  if (status & K_ITEM_D_STATUS ) out |= KeyDown;
  if (status & K_ITEM_I_STATUS ) out |= KeyUp;
//  if (status & K_ITEM_S_STATUS ) out |= EKeyMask::KeyF6;
  if (status & KEY1_STATUS ) out |= KeyF1;
  if (status & KEY2_STATUS ) out |= KeyF2;
  if (status & KEY3_STATUS ) out |= KeyF3;
  if (status & KEY4_STATUS ) out |= KeyF4;

  return out;
}

// __ProgDiskPage of SYS 1.5x programs one page of the SPI flash: 256 bytes
// on the 2 MB disk (HW 2.6x) and 4096 bytes on the 8 MB disk (DS203HD).
// The FAT sector of the 2 MB disk is 512 bytes, writing it with a single
// call stored only its first half - files written by applications lost
// directory entries and FAT links (Wildcat Files.c also writes 2x 256 B).
#if defined(DS203HD)
#define DISK_PAGE_SIZE 4096
#define DISK_SECTOR_SIZE 4096
#else
#define DISK_PAGE_SIZE 256
#define DISK_SECTOR_SIZE 512
#endif

bool ExtFlashSecWr(uint8_t* pBuffer, uint32_t WriteAddr)
{
  for (int i = 0; i < DISK_SECTOR_SIZE; i += DISK_PAGE_SIZE)
    __ProgDiskPage(pBuffer + i, WriteAddr + i);
  return 1;
}

bool ExtFlashDataRd(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t Length)
{
  __ReadDiskData(pBuffer, ReadAddr, Length);
  return 1;
}

uintptr_t GetAttribute(enum EAttribute attribute)
{
  switch (attribute)
  {
    case DeviceType: return (uintptr_t)"DS203";
    case VersionDfu: return (uintptr_t)(char*)__Get(DFUVER, 0);
    case VersionHardware: return (uintptr_t)(char*)__Get(HDWVER, 0);
    case VersionSystem: return (uintptr_t)(char*)__Get(SYSVER, 0);
    case VersionFpga: return (uintptr_t)(char*)__Get(FPGAVER, 0);
    case SerialNumber: return (uintptr_t)(uint32_t)__GetDev_SN();
    case LicenseNumber: return (uintptr_t)0;
    case LicenseValid: return (uintptr_t)0;
    case DisplayType: return (uintptr_t)0;
    case DiskType: return (uintptr_t)0;
    case BatteryVoltage: return (uintptr_t)__Get(V_BATTERY, 0);
    case Charging: return (uintptr_t)__Get(USB_POWER, 0);
    case FlashReadRange: return (uintptr_t)0; // Uses native usb interface
    case FlashWriteRange: return (uintptr_t)0;
    case FlashAlertRange: return (uintptr_t)0;
    default: return 0;
  }
}

#include "source/bios/Bios.h"
#include "source/main/Execute.h"
#include "crc.h"
#include "tokenizer.h"

// LA104: the last application starts at power up. Its name is kept in
// LASTAPP.CFG on the disk, written only when it changes (no write for every
// start). F2 / F3 held at power up (LA104: F2) starts the shell instead; an
// application that does not load falls back to the shell. The flash is
// written only when a section differs (Execute.cpp compares the ELF with
// the flash before programming).
static const char* const LastAppFile = "LASTAPP.CFG";
static const char* const ShellFile = "shell.elf";

static bool SameName(const char* a, const char* b)
{
  for (;; a++, b++)
  {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
    if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
    if (ca != cb)
      return false;
    if (!ca)
      return true;
  }
}

// FAT access outside of the main loop: own shared buffer (stack; no
// application is loaded at that time)
static bool ReadLastApp(char* name, int nMax)
{
  uint8_t shared[BIOS::FAT::SharedBufferSize];
  uint8_t sector[BIOS::FAT::SectorSize];
  BIOS::FAT::SetSharedBuffer(shared);
  name[0] = 0;
  bool ok = false;
  if (BIOS::FAT::Open(LastAppFile, BIOS::FAT::IoRead) == BIOS::FAT::EOk)
  {
    if (BIOS::FAT::Read(sector) == BIOS::FAT::EOk)
    {
      int i = 0;
      while (i < nMax - 1 && sector[i] >= ' ' && sector[i] < 127)
      {
        name[i] = (char)sector[i];
        i++;
      }
      name[i] = 0;
      ok = i > 0;
    }
    BIOS::FAT::Close();
  }
  // the application must exist
  if (ok)
  {
    ok = BIOS::FAT::Open(name, BIOS::FAT::IoRead) == BIOS::FAT::EOk;
    if (ok)
      BIOS::FAT::Close();
  }
  BIOS::FAT::SetSharedBuffer(nullptr);
  return ok;
}

static void WriteLastApp(const char* name)
{
  char old[64];
  if (ReadLastApp(old, sizeof(old)) && SameName(old, name))
    return;
  uint8_t shared[BIOS::FAT::SharedBufferSize];
  uint8_t sector[BIOS::FAT::SectorSize];
  BIOS::FAT::SetSharedBuffer(shared);
  memset(sector, 0, sizeof(sector));
  int len = strlen(name);
  memcpy(sector, name, len);
  sector[len] = '\n';
  if (BIOS::FAT::Open(LastAppFile, BIOS::FAT::IoWrite) == BIOS::FAT::EOk)
  {
    BIOS::FAT::Write(sector);
    BIOS::FAT::Close(len + 1);
  }
  BIOS::FAT::SetSharedBuffer(nullptr);
}

// F2 / F3 (LA104: F2) held at power up: no automatic start
static bool SkipAutostart()
{
  uint32_t t0 = BIOS::SYS::GetTick();
  while (BIOS::SYS::GetTick() - t0 < 100)
  {
    BIOS::KEY::EKey key = BIOS::KEY::GetKey();
#ifdef LA104
    if (key == BIOS::KEY::F2)
#else
    if (key == BIOS::KEY::F2 || key == BIOS::KEY::F3)
#endif
      return true;
  }
  return false;
}

int main()
{                   
  BIOS::SYS::Init();
  BIOS::FAT::Init();

#if defined(LA104) || defined(DS213)
  int len = (uint8_t*)&_addressRomEnd-(uint8_t*)&_addressRomBegin;
  uint32_t check = crc32b((uint8_t*)&_addressRomBegin, len);
  if (check != 0)
  {
      BIOS::DBG::Print("CRC error!");
      while (BIOS::KEY::GetKey() != BIOS::KEY::F1);
  }
#endif

#ifndef DISABLE_USB
  BIOS::USB::Enable();
  BIOS::USB::InitializeMass();
#endif

  BIOS::OS::SetArgument((char*)"");

  char shell[64] = "shell.elf";

#ifdef LA104
  while (BIOS::KEY::GetKey() == BIOS::KEY::F3)
#endif
#ifdef DS213
  while (BIOS::KEY::GetKey() == BIOS::KEY::F4)
#endif
#ifdef DS203
  while (BIOS::KEY::GetKey() == BIOS::KEY::F4)
#endif
  {
      BIOS::DBG::Print("Safe mode, press F1 to continue.\n");
      while (BIOS::KEY::GetKey() != BIOS::KEY::F1);
  }

  // the last application, unless F2 / F3 is held
  char lastApp[64];             // copied by SetArgument (no static: OS RAM ends below 0x20004400)
  bool autostart = false;
  if (!SkipAutostart() && ReadLastApp(lastApp, sizeof(lastApp)))
  {
    BIOS::OS::SetArgument(lastApp);
    autostart = true;
  }

  while (1)
  {
    if (strlen(BIOS::OS::GetArgument()) == 0)
      BIOS::OS::SetArgument(shell);

    uint32_t address = 0;
    char filename[64];
    {
      CTokenizer tok(BIOS::OS::GetArgument());
      tok.GetToken(filename, 63);
      // remember an application started from the shell (not the shell)
      if (!autostart && !SameName(filename, ShellFile))
        WriteLastApp(filename);

      uint8_t gFlashSharedBuffer[BIOS::MEMORY::SharedBufferSize]; 
      BIOS::MEMORY::SetSharedBuffer(gFlashSharedBuffer);

      uint8_t gFatSharedBuffer[BIOS::FAT::SharedBufferSize]; 
      BIOS::FAT::SetSharedBuffer(gFatSharedBuffer);


      if (!BIOS::OS::LoadExecutable(filename, address, false))
        address = 0;    

      BIOS::MEMORY::SetSharedBuffer(nullptr);
      BIOS::FAT::SetSharedBuffer(nullptr);
    }

    if (!address && autostart)
    {
      // the last application is gone or broken: the shell
      autostart = false;
      BIOS::OS::SetArgument((char*)"");
      continue;
    }
    autostart = false;
    if (!address)
    {
      BIOS::DBG::Print("Press F1 to retry.\n");
      while (BIOS::KEY::GetKey() != BIOS::KEY::F1);
      continue;
    }
                           
    // keep argument - for file viewer
    BIOS::OS::HasArgument();
    BIOS::SYS::Execute(address);

    if (BIOS::OS::HasArgument())
      strcpy(shell, filename);
    else
      BIOS::OS::SetArgument((char*)"");
  }

  return 0;
}

extern "C" 
{

void Halt()
{
  BIOS::DBG::Print("Application halted");
  while(1);
}

void _HandleAssertion(const char* file, int line, const char* cond)
{
  // prevent sprintf buffer overflow
  BIOS::DBG::Print("\n\nAssertion failed in ");
  BIOS::DBG::Print(file);
  BIOS::DBG::Print(" [%d]: ", line);
  BIOS::DBG::Print(cond);
  BIOS::DBG::Print("\n");
  Halt();
}

void NMIException(void)
{
  BIOS::DBG::Print("NMIException");    
  Halt();
}

void MemManageException(void)
{
  BIOS::DBG::Print("MemManageException\n");    
  Halt();
}

void BusFaultException(void)
{
  BIOS::DBG::Print("BusFaultException\n");    
  Halt();
}

void UsageFaultException(void)
{
  BIOS::DBG::Print("UsageFaultException\n");    
  Halt();
}

#include "crash.h"

}
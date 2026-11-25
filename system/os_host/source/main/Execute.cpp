#include "Execute.h"
#include "library/elf/elf.h"
#include "framework/BufferedIo.h"
#include "gui/Gui.h"
#include "Shared.h"

// TODO: cleanup
//enum {DISABLE=0, ENABLE=1};
//extern "C" void USB_Connect(uint8_t Status);
uint8_t gBuffer[256];
bool gQuiet = false;
     
void Show(char* msg)
{
  static int x = 0;
  static bool flush = true;
  if (gQuiet)
    return;

  while (*msg)
  {
    char ch = *msg++;
    if (flush)
    {
      flush = false;
      x = 0;
      BIOS::LCD::Bar(0, BIOS::LCD::Height-16, BIOS::LCD::Width, BIOS::LCD::Height, RGB565(4040b0));
    }
    if (ch == '\n')
    {
      flush = true;
      continue;
    }          
    char show[2] = {ch, 0};
    x += BIOS::LCD::Print(x, BIOS::LCD::Height-16+1, RGB565(ffffff), RGB565(4040b0), show);
    if (x >= BIOS::LCD::Width)
    {
      x = 0;
      flush = true;
    }
  }
}

void Show(const char* msg)
{
  Show((char*)msg);
}

class CInterruptGuard
{
  uint32_t m_mask;

public:
  CInterruptGuard()
  {
    m_mask = BIOS::OS::DisableInterrupts();
  }
  ~CInterruptGuard()
  {
    BIOS::OS::EnableInterrupts(m_mask);
  }
};

bool Verify( CBufferedReader& f, Elf32_Shdr& elfSection )
{
	f.Seek( elfSection.offset );

	uint8_t* pMem = (uint8_t*)elfSection.addr;
	for (int i=0; i<(int)elfSection.size; i++, pMem++)
	{
		uint8_t bData;
		f >> bData;
		if ( bData != *pMem )
			return false;
	}
	return true;
}

bool VerifyZero( CBufferedReader& f, Elf32_Shdr& elfSection )
{
	uint8_t* pMem = (uint8_t*)elfSection.addr;
	for (int i=0; i<(int)elfSection.size; i++, pMem++)
	{
		if ( 0 != *pMem )
			return false;
	}
	return true;
}

void FlashRam( CBufferedReader& f, Elf32_Shdr& elfSection )
{
	f.Seek( elfSection.offset );
	uint8_t* pWriteTo = (uint8_t*)elfSection.addr;
	for (int i=0; i<(int)elfSection.size; i++)
	{
		uint8_t bData;
		f >> bData;
		*pWriteTo++ = bData;
	}

  if (!Verify(f, elfSection ))
  {
    BIOS::DBG::Print("Failed to verify %d bytes at %08x!", elfSection.size, elfSection.addr);
    _ASSERT(0);
  }
}

void ZeroRam( CBufferedReader& f, Elf32_Shdr& elfSection )
{
	// section length should be aligned to 32bits!?
	uint8_t* pWriteTo = (uint8_t*)elfSection.addr;
	for (int i=0; i<(int)elfSection.size; i++)
		*pWriteTo++ = 0;
}

void FlashRom( CBufferedReader& f, Elf32_Shdr& elfSection )
{
	int nLength = (int)elfSection.size;
	uint32_t dwAddr = elfSection.addr;

	f.Seek( elfSection.offset );
	BIOS::MEMORY::LinearStart();
	for ( int i=0; i<nLength; )
	{
		uint8_t* buffer = gBuffer;
		int nToLoad = nLength-i;
		if ( nToLoad > 256 )
			nToLoad = 256;
		f >> CStream(buffer, nToLoad);
		if ( !BIOS::MEMORY::LinearProgram( dwAddr, buffer, nToLoad) )
		{
			_ASSERT(!!!"LinearProgram failed");
		}
		i += nToLoad;
		dwAddr += nToLoad;
	}
	BIOS::MEMORY::LinearFinish();

  if (!Verify(f, elfSection ))
  {
    BIOS::DBG::Print("Failed to verify %d bytes at %08x!", elfSection.size, elfSection.addr);
    _ASSERT(0)
  }
}

void ZeroRom( CBufferedReader& f, Elf32_Shdr& elfSection )
{
	int nLength = (int)elfSection.size;
	uint32_t dwAddr = elfSection.addr;

	uint8_t buffer[64] = {0};

	BIOS::MEMORY::LinearStart();
	for ( int i=0; i<nLength; )
	{
		int nToLoad = nLength-i;
		if ( nToLoad > 64 )
			nToLoad = 64;
		if ( !BIOS::MEMORY::LinearProgram( dwAddr, buffer, nToLoad) )
		{
			_ASSERT(!!!"LinearProgram failed");
		}
		i += nToLoad;
		dwAddr += nToLoad;
	}
	BIOS::MEMORY::LinearFinish();
}

// a section of the application must not be written over the OS itself
// (an application linked for an older, smaller OS overwrote the running
// code and the OS crashed while flashing it)
extern unsigned long _addressRomBegin;
extern unsigned long _addressRomEnd;

static bool OverlapsOs( Elf32_Shdr& elfSection )
{
	if ( (elfSection.addr >> 24) != 0x08 || elfSection.size == 0 )
		return false;
	uint32_t nBegin = (uint32_t)&_addressRomBegin, nEnd = (uint32_t)&_addressRomEnd;
	if ( elfSection.addr >= nEnd || elfSection.addr + elfSection.size <= nBegin )
		return false;
	char message[80];
	sprintf(message, "\n%08x..%08x overlaps the OS (up to %08x):\nthe application needs a new build.\n",
		elfSection.addr, elfSection.addr + elfSection.size, nEnd);
	Show(message);
	BIOS::DBG::Print(message);
	return true;
}

//LINKERSECTION(".gbios")
bool /*CWndUserManager::*/FlashData( CBufferedReader& f, Elf32_Shdr& elfSection )
{
      	char message[64];

	if ( OverlapsOs( elfSection ) )
		return false;

	if ( Verify( f, elfSection ) )
	{
		sprintf(message, "Ignoring %d bytes at %08x.", elfSection.size, elfSection.addr);
		Show(message);
		return true;
	}

      	sprintf(message, "Flashing %d bytes at %08x.", elfSection.size, elfSection.addr);
      	Show(message);

#ifndef _WIN32
	if ( (elfSection.addr >> 24) == 0x20 )
	{
		FlashRam( f, elfSection );
	} else
	if ( (elfSection.addr >> 24) == 0x08 )
	{
		FlashRom( f, elfSection );
	} else
		_ASSERT(!!!"Unrecognized memory location");
#endif
	return true;
}

//LINKERSECTION(".gbios")
bool /*CWndUserManager::*/FlashBss( CBufferedReader& f, Elf32_Shdr& elfSection )
{
      	char message[64];

	if ( OverlapsOs( elfSection ) )
		return false;

	if ( VerifyZero( f, elfSection ) )
	{
	      	sprintf(message, "Ignoring %d bytes at %08x.", elfSection.size, elfSection.addr);
	      	Show(message);
		return true;
	}
	sprintf(message, "Filling %d bytes at %08x.", elfSection.size, elfSection.addr);
     	Show(message);
#ifndef _WIN32
	if ( (elfSection.addr >> 24) == 0x20 )
	{
		ZeroRam( f, elfSection );
	} else
	if ( (elfSection.addr >> 24) == 0x08 )
	{
		ZeroRom( f, elfSection );
	} else
		_ASSERT(!!!"Unrecognized memory location");
#endif
	return true;
}

bool BIOS::OS::LoadExecutable(char* filename, uint32_t& entry, bool quiet)
{
	/*
		all variables used in this routine + (used by BIOS functions) must be placed 
		at memory location not colliding with loaded image! Same limitation apply to the
		code area in flash occupied by this function!
	*/

	// TODO: load sections as gabuino does

        CInterruptGuard guard;
        gQuiet = quiet;

        char message[64];
        sprintf(message, "Executing ELF image '%s'\n", filename);
        Show(message);
	CBufferedReader fw;
	if (!fw.Open(filename))
        {
		sprintf(message, "Image '%s' not found!\n", filename);
		Show(message);
		return false;
        }

	Elf32_Ehdr elfHeader;
	Elf32_Shdr elfSection;

	fw >> CStream(&elfHeader, sizeof(Elf32_Ehdr));
	int nStringTableSectionOfs = elfHeader.shoff + elfHeader.shtrndx * sizeof(Elf32_Shdr);

	fw.Seek( nStringTableSectionOfs );
	fw >> CStream(&elfSection, sizeof(Elf32_Shdr));
	int nStringTableOfs = elfSection.offset;
	int nStringTableLen = elfSection.size;

	int nSymbolNamesOffset = -1;

	enum SecType {
		SecNone = 0,
		SecText = 1,
		SecData = 2,
		SecBss = 3,
		SecPlt = 4,
		SecGot = 5,
		SecDynamic = 6,
		SecDynStr = 7, // we need to load symbol list before DynSym
		SecDynSym = 8,
		SecRelPlt = 9, // process as last
		SecInterp = 10,
		SecStringTab = 11,
		SecInit = 12,
		SecStrTab = 13,
		SecSymTab = 14,
		SecRoData = 15
	};
	const char* arrSecNames[] = {"none", ".text", ".data", ".bss", ".plt", ".got", ".dynamic", ".dynstr", ".dynsym", ".rel.plt", ".interp", ".shstrtab", ".init_array", ".strtab", ".symtab", ".rodata"};

	int arrSectionIndex[COUNT(arrSecNames)];
	int arrSectionOffset[COUNT(arrSecNames)];
	for (int i=0; i<COUNT(arrSectionIndex); i++)
	{
		arrSectionIndex[i] = -1;
		arrSectionOffset[i] = -1;
	}
	//fw.Seek( elfHeader.shoff );
	for (int i=0; i<elfHeader.shnum; i++)
	{
		fw.Seek( elfHeader.shoff + i * sizeof(Elf32_Shdr) );
		fw >> CStream(&elfSection, sizeof(Elf32_Shdr));

		// get section name
		char strSectionName[15];
		int nSectionNameMaxLen = nStringTableLen - elfSection.name;
		if ( nSectionNameMaxLen > 14 )
			nSectionNameMaxLen = 14;
		strSectionName[14] = 0;

		fw.Seek(nStringTableOfs + elfSection.name);
		fw >> CStream( strSectionName, nSectionNameMaxLen );

		if ( strncmp( strSectionName, ".text", 5 ) == 0 )
		{
			sprintf(message, "%s>", strSectionName);
			Show(message);
			// flash code
			if ( !FlashData( fw, elfSection ) )
				return false;
			Show("\n");
			continue;
		}

		if ( strncmp( strSectionName, ".data", 5 ) == 0 )
		{
			sprintf(message, "%s>", strSectionName);
			Show(message);
			// flash data
			if ( !FlashData( fw, elfSection ) )
				return false;
			Show("\n");
			continue;
		}

		if ( strncmp( strSectionName, ".rodata", 7 ) == 0 )
		{
			sprintf(message, "%s>", strSectionName);
			Show(message);
			// flash data
			if ( !FlashData( fw, elfSection ) )
				return false;
			Show("\n");
			continue;
		}

		if ( strncmp( strSectionName, ".bss", 4 ) == 0 )
		{
			sprintf(message, "%s>", strSectionName);
			Show(message);
			// flash bss
			if ( !FlashBss( fw, elfSection ) )
				return false;
			Show("\n");
			continue;
		}

		SecType sectionType = SecNone;
		for (int j=0; j<COUNT(arrSecNames); j++)
			if ( strcmp( strSectionName, arrSecNames[j] ) == 0 )
			{
				sectionType = (SecType)j;
				break;
			}

		if ( sectionType > 0 )
		{
			arrSectionIndex[sectionType] = i;	
			arrSectionOffset[sectionType] = elfSection.offset;
		} else
		{
			sprintf(message, "%s> Unknown section name!", strSectionName);
			Show(message);
			_ASSERT( sectionType > 0 );
			return false;
		}
	}

	for ( int i=0; i<(int)COUNT(arrSectionIndex); i++)
	{
		if ( arrSectionIndex[i] == -1 )
			continue;

		fw.Seek( elfHeader.shoff + arrSectionIndex[i] * sizeof(Elf32_Shdr) );
		fw >> CStream(&elfSection, sizeof(Elf32_Shdr));

		sprintf(message, "%s>", arrSecNames[i]);
		Show(message);

		switch ( i )
		{
			case SecText:
			case SecData:
			{
				_ASSERT( 0 );
				if ( !FlashData( fw, elfSection ) )
					return false;
				break;	
			}
			case SecPlt:
			case SecGot:
			case SecDynamic:
			{
				if ( !FlashData( fw, elfSection ) )
					return false;
				break;
			}
			case SecBss:
			{
				_ASSERT( 0 );
				if ( !FlashBss( fw, elfSection ) )
					return false;
				break;
			}
			case SecDynStr:
			{
				nSymbolNamesOffset = elfSection.offset;
				break;
			}
			case SecRelPlt:
			{
				Show("Matching imports...");

				_ASSERT( arrSectionOffset[SecDynSym] != -1 );
				_ASSERT( arrSectionOffset[SecDynStr] != -1 );

				int nSymbolCount = elfSection.size/sizeof(Elf32_Rel);
				for (int i=0; i<nSymbolCount; i++)
				{
					Elf32_Rel elfRelocation;
					Elf32_Sym elfSymbol;

					fw.Seek( elfSection.offset + i * sizeof(Elf32_Rel) );
					fw >> CStream(&elfRelocation, sizeof(Elf32_Rel));

					_ASSERT( (elfRelocation.r_info & 0xff) == 0x16 );
					int nIndex = elfRelocation.r_info >> 8;
					
					fw.Seek( arrSectionOffset[SecDynSym] + nIndex*sizeof(Elf32_Sym) );
					fw >> CStream(&elfSymbol, sizeof(Elf32_Sym)); 

					_ASSERT(nSymbolNamesOffset > -1);
					fw.Seek( nSymbolNamesOffset + elfSymbol.st_name);

					char strSymbolName[128];
					fw >> CStream(&strSymbolName, sizeof(strSymbolName)); 

					uint32_t dwProcAddr = BIOS::SYS::GetProcAddress( strSymbolName );
					if (!dwProcAddr)
					{
                                          BIOS::DBG::Print("Symbol '");
                                          BIOS::DBG::Print(strSymbolName);
                                          BIOS::DBG::Print("' not found!\n");
                                          // not started (the OS goes on: the shell
                                          // after an automatic start, else F1 retries)
                                          return false;
                                        }

					// Execute only on ARM!
					/*
					0x20000E84 (GOT[2]) -> 0x20000DAC (PLT[0])
					0x20000E84 (GOT[2]) <- new address
					*/
					uint32_t* pRelocation = (uint32_t*)elfRelocation.r_offset;
					*pRelocation = dwProcAddr;
				}
				break;
			}
			case SecInit:
			{
				Show("InitArray");
				// last processed section before jumping to entry
				int nCount = elfSection.size/sizeof(uint32_t);
				fw.Seek( elfSection.offset );
				for (int i=0; i<nCount; i++)
				{
					uint32_t dwInitPtr;
					fw >> dwInitPtr;
#ifndef _WIN32
					typedef void (*TInitFunc)();
					TInitFunc InitFunc = (TInitFunc)dwInitPtr;
					InitFunc();
#endif
					Show(".");
				}			
				Show("\n");
				break;
			}
			case SecStrTab:
			{
				break;
			}
			case SecSymTab:
			{
				break;
			}
		}
		Show("\n");
	}

	sprintf(message, "Image loaded at 0x%08x\n", elfHeader.entry);
	Show(message);
	entry = elfHeader.entry;
	return true;
}

#pragma once
// Translations of the user interface (DS203 applications)
//
// The language is named in LANG.CFG on the disk (first word: UK, DE, ...;
// no file or EN: English, built in). The translation of an application is
// the UTF-8 text file LANG/<code>/<APP>.TXT, one string per line:
//
//   # comment
//   "Trigger" = "Тригер"
//   "no trigger" = "немає тригера"
//
// The English text is the key; \" \\ \n \t are escapes; a missing or empty
// translation keeps the English text. At the start the file is compiled
// into a table (sorted FNV-1a hashes of the keys, the UTF-8 texts) in an
// own flash area of the application: zero RAM at run time and stable
// pointers. The area is written only when the file changed (its size and
// checksum are kept in the header; a page is erased only when its contents
// differ), so the flash is not worn. The flash is written by own code (the
// OS of the DS203 does not export its flash functions).
//
// T("text") returns the translation or the text itself. Len() counts the
// characters of UTF-8 text the way the OS draws it (8 pixels each).
#include <stdint.h>
#include <string.h>

namespace Lang
{
  enum { Magic = 0x31474e4c };          // "LNG1"
  struct THeader
  {
    uint32_t magic;
    char code[4];
    uint32_t fileSize, fileSum;         // of LANG/<code>/<APP>.TXT
    uint32_t count;                     // entries
    uint32_t entries;                   // offset of the entries (from the area)
    uint32_t total;                     // bytes used
    uint32_t sum;                       // of the whole image after the header
  };
  struct TEntry { uint32_t hash, offset; };

  // the flash area (DS203: linker symbols of the area "lang")
#if defined(_ARM) && defined(DS203)
  extern "C" char _addressLang[], _addressLangEnd[];
  inline uint8_t* Area() { return (uint8_t*)_addressLang; }
  inline uint32_t AreaSize() { return (uint32_t)(_addressLangEnd - _addressLang); }
#elif defined(_ARM)
  inline uint8_t* Area() { return nullptr; }
  inline uint32_t AreaSize() { return 0; }
#else
  // PC renderers: an array instead of the flash
  inline uint8_t* Area() { static uint8_t a[32768]; return a; }
  inline uint32_t AreaSize() { return 32768; }
#endif

  inline const THeader*& Active() { static const THeader* h = nullptr; return h; }
  inline char* Status() { static char s[48]; return s; }

  inline uint32_t Hash(const char* s)
  {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
  }

  // the translation, or en
  inline const char* T(const char* en)
  {
    const THeader* hd = Active();
    if (!hd || !en || !*en)
      return en;
    const uint8_t* base = (const uint8_t*)hd;
    const TEntry* e = (const TEntry*)(base + hd->entries);
    uint32_t h = Hash(en);
    int lo = 0, hi = (int)hd->count - 1;
    while (lo <= hi)
    {
      int m = (lo + hi) >> 1;
      if (e[m].hash == h) return (const char*)base + e[m].offset;
      if (e[m].hash < h) lo = m + 1; else hi = m - 1;
    }
    return en;
  }

  // one character of UTF-8 text as the OS reads it (an invalid sequence:
  // one byte)
  inline int CharBytes(const char* s)
  {
    const uint8_t* p = (const uint8_t*)s;
    uint8_t b = p[0];
    if (b >= 0xc2 && b <= 0xdf && (p[1] & 0xc0) == 0x80) return 2;
    if (b >= 0xe0 && b <= 0xef && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 &&
        (b > 0xe0 || p[1] >= 0xa0)) return 3;
    if (b >= 0xf0 && b <= 0xf4 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) return 4;
    return 1;
  }
  inline int Len(const char* s)
  {
    int n = 0;
    while (*s) { s += CharBytes(s); n++; }
    return n;
  }
  inline int Width(const char* s) { return Len(s) * 8; }
  // cuts s after n characters
  inline void Cut(char* s, int n)
  {
    while (*s && n-- > 0) s += CharBytes(s);
    *s = 0;
  }
  // bytes of the first n characters
  inline int Bytes(const char* s, int n)
  {
    const char* p = s;
    while (*p && n-- > 0) p += CharBytes(p);
    return (int)(p - s);
  }
  // copies the translation of en to dst (size bytes, cut at a character)
  inline char* Copy(char* dst, const char* en, int size)
  {
    const char* s = T(en);
    int n = 0;
    while (s[n])
    {
      int b = CharBytes(s + n);
      if (n + b > size - 1) break;
      n += b;
    }
    memcpy(dst, s, n);
    dst[n] = 0;
    return dst + n;
  }

  // ------------------------------------------------------------ flash
  // own writer of the area (the OS of the DS203 does not export its flash
  // functions): a page (2 KB) is buffered in RAM and written back only
  // when its contents differ from the flash (erase + program)
  enum { PageSize = 2048 };
#ifdef _ARM
  inline volatile uint32_t& FlashReg(int off) { return *(volatile uint32_t*)(0x40022000 + off); }
  inline void FlashWait() { while (FlashReg(0x0c) & 1) {} }   // SR.BSY
  inline void FlashUnlock()
  {
    if (FlashReg(0x10) & 0x80)          // CR.LOCK
    {
      FlashReg(0x04) = 0x45670123;
      FlashReg(0x04) = 0xcdef89ab;
    }
  }
  inline void FlashLock() { FlashReg(0x10) |= 0x80; }
  inline void FlashErase(uint8_t* page)
  {
    FlashWait();
    FlashReg(0x0c) = 0x34;              // clear EOP, WRPRTERR, PGERR
    FlashReg(0x10) |= 0x02;             // PER
    FlashReg(0x14) = (uint32_t)page;
    FlashReg(0x10) |= 0x40;             // STRT
    FlashWait();
    FlashReg(0x10) &= ~0x02u;
  }
  inline void FlashProgram(uint8_t* dst, const uint8_t* src, int len)
  {
    FlashWait();
    FlashReg(0x10) |= 0x01;             // PG
    for (int i = 0; i < len; i += 2)
    {
      uint16_t v = src[i] | (src[i + 1] << 8);
      if (v != 0xffff)
      {
        *(volatile uint16_t*)(dst + i) = v;
        FlashWait();
      }
    }
    FlashReg(0x10) &= ~0x01u;
  }
#else
  inline void FlashUnlock() {}
  inline void FlashLock() {}
  inline void FlashErase(uint8_t* page) { memset(page, 0xff, PageSize); }
  inline void FlashProgram(uint8_t* dst, const uint8_t* src, int len)
  {
    for (int i = 0; i < len; i++) dst[i] &= src[i];
  }
#endif

  // ------------------------------------------------------------ loading
  // scratch (RAM free at the start): a flash page, a sector, a line and the
  // entries
  struct TLoader
  {
    uint8_t* flashBuf;                  // PageSize + 128
    int page;                           // page in flashBuf, -1: none
    uint8_t* sector;                    // 512
    char* line; int lineMax;
    TEntry* ent; int entMax; int n;
    uint32_t poolPos;                   // next byte in the area
    bool ok;
  };

  // writes the buffered page if it differs from the flash
  inline void Flush(TLoader& L)
  {
    if (L.page < 0)
      return;
    uint8_t* f = Area() + L.page * PageSize;
    if (memcmp(f, L.flashBuf, PageSize) != 0)
    {
      FlashUnlock();
      FlashErase(f);
      FlashProgram(f, L.flashBuf, PageSize);
      FlashLock();
      if (memcmp(f, L.flashBuf, PageSize) != 0)
        L.ok = false;
    }
    L.page = -1;
  }

  inline bool Program(TLoader& L, uint32_t pos, const void* data, int len)
  {
    if (pos + len > AreaSize()) { L.ok = false; return false; }
    const uint8_t* d = (const uint8_t*)data;
    while (len > 0)
    {
      int page = pos / PageSize, off = pos % PageSize;
      if (page != L.page)
      {
        Flush(L);
        memcpy(L.flashBuf, Area() + page * PageSize, PageSize);
        L.page = page;
      }
      int n = PageSize - off < len ? PageSize - off : len;
      memcpy(L.flashBuf + off, d, n);
      pos += n; d += n; len -= n;
    }
    return L.ok;
  }

  // reads LANG.CFG: the code (upper case), "" for English
  inline void ReadCode(char* code, uint8_t* sector)
  {
    memset(code, 0, 4);
    if (BIOS::FAT::Open("LANG.CFG", BIOS::FAT::IoRead) != BIOS::FAT::EOk)
      return;
    if (BIOS::FAT::Read(sector) == BIOS::FAT::EOk)
    {
      int i = 0, n = 0;
      while (i < 64 && (sector[i] == ' ' || sector[i] == '\t' || sector[i] == 0xef || sector[i] == 0xbb || sector[i] == 0xbf)) i++;
      while (n < 3 && ((sector[i] >= 'a' && sector[i] <= 'z') || (sector[i] >= 'A' && sector[i] <= 'Z')))
      {
        char c = (char)sector[i++];
        code[n++] = c >= 'a' ? c - 32 : c;
      }
      code[n] = 0;
      if (n == 2 && code[0] == 'E' && code[1] == 'N')
        code[0] = 0;
    }
    BIOS::FAT::Close();
  }

  // a line of the file: "key" = "value"
  inline const char* Unquote(const char* p, char* out, int max, int& len)
  {
    len = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return nullptr;
    p++;
    while (*p && *p != '"')
    {
      char c = *p++;
      if (c == '\\' && *p)
      {
        c = *p++;
        if (c == 'n') c = '\n';
        else if (c == 't') c = '\t';
      }
      if (len < max - 1) out[len++] = c;
    }
    if (*p != '"') return nullptr;
    out[len] = 0;
    return p + 1;
  }

  inline void ParseLine(TLoader& L, char* line)
  {
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return;               // comment, empty
    // the key and the value are unquoted in place (shorter than the line)
    char key[160], val[200];
    int kl, vl;
    const char* q = Unquote(p, key, sizeof(key), kl);
    if (!q) return;
    while (*q == ' ' || *q == '\t') q++;
    if (*q != '=') return;
    q = Unquote(q + 1, val, sizeof(val), vl);
    if (!q || vl == 0 || kl == 0) return;
    if (L.n >= L.entMax) { L.ok = false; return; }
    L.ent[L.n].hash = Hash(key);
    L.ent[L.n].offset = L.poolPos;
    L.n++;
    Program(L, L.poolPos, val, vl + 1);
    L.poolPos += vl + 1;
  }

  // reads the file; with parse: compiles it, else only its checksum
  inline bool ReadFile(TLoader& L, const char* path, bool parse, uint32_t& size, uint32_t& sum)
  {
    if (BIOS::FAT::Open(path, BIOS::FAT::IoRead) != BIOS::FAT::EOk)
      return false;
    size = BIOS::FAT::GetFileSize();
    sum = 2166136261u;
    int ll = 0;
    for (uint32_t pos = 0; pos < size; pos += 512)
    {
      if (BIOS::FAT::Read(L.sector) != BIOS::FAT::EOk) { BIOS::FAT::Close(); return false; }
      uint32_t n = size - pos < 512 ? size - pos : 512;
      for (uint32_t i = 0; i < n; i++)
      {
        uint8_t c = L.sector[i];
        sum ^= c; sum *= 16777619u;
        if (!parse) continue;
        if (c == '\n' || c == '\r')
        {
          L.line[ll] = 0;
          if (ll) ParseLine(L, L.line);
          ll = 0;
        }
        else if (ll < L.lineMax - 1)
          L.line[ll++] = (char)c;
      }
    }
    if (parse && ll) { L.line[ll] = 0; ParseLine(L, L.line); }
    BIOS::FAT::Close();
    return true;
  }

  // the checksum of the compiled image (after the header)
  inline uint32_t ImageSum(const THeader* h)
  {
    const uint8_t* p = (const uint8_t*)h + sizeof(THeader);
    uint32_t s = 2166136261u;
    for (uint32_t i = sizeof(THeader); i < h->total; i++) { s ^= *p++; s *= 16777619u; }
    return s;
  }

  inline bool Valid(const THeader* h)
  {
    return h->magic == Magic && h->total <= AreaSize() && h->total >= sizeof(THeader) &&
      h->entries + h->count * sizeof(TEntry) <= h->total && ImageSum(h) == h->sum;
  }

  // loads LANG/<code>/<app>.TXT; scratch: RAM free during the call (at
  // least 4 KB; the size limits the number of strings)
  inline bool Init(const char* app, uint8_t* scratch, int scratchSize)
  {
    Active() = nullptr;
    Status()[0] = 0;
    if (!Area() || AreaSize() < 1024)
      return false;
    TLoader L;
    L.flashBuf = scratch;
    L.page = -1;
    L.sector = scratch + 2176;
    L.line = (char*)scratch + 2176 + 512;
    L.lineMax = 400;
    L.ent = (TEntry*)(scratch + 2176 + 512 + 400);
    L.entMax = (scratchSize - (2176 + 512 + 400)) / (int)sizeof(TEntry);
    L.n = 0;
    L.ok = true;
    char code[4];
    ReadCode(code, L.sector);
    if (!code[0])
      return false;                      // English
    char path[40];
    strcpy(path, "LANG/");
    strcat(path, code);
    strcat(path, "/");
    strcat(path, app);
    strcat(path, ".TXT");
    uint32_t size, sum;
    if (!ReadFile(L, path, false, size, sum))
    {
      strcpy(Status(), "no ");
      strcat(Status(), path);
      return false;
    }
    const THeader* h = (const THeader*)Area();
    if (Valid(h) && h->fileSize == size && h->fileSum == sum && memcmp(h->code, code, 4) == 0)
    {
      Active() = h;                      // compiled before: no write
      return true;
    }
    // compile: the texts first (after the header), then the entries
    L.poolPos = sizeof(THeader);
    uint32_t size2, sum2;
    bool read = ReadFile(L, path, true, size2, sum2);
    // sorted by the hash, the first of equal keys stays
    for (int i = 1; i < L.n; i++)
    {
      TEntry e = L.ent[i];
      int j = i - 1;
      while (j >= 0 && L.ent[j].hash > e.hash) { L.ent[j + 1] = L.ent[j]; j--; }
      L.ent[j + 1] = e;
    }
    int m = 0;
    for (int i = 0; i < L.n; i++)
      if (m == 0 || L.ent[m - 1].hash != L.ent[i].hash) L.ent[m++] = L.ent[i];
    L.n = m;
    THeader hd;
    memset(&hd, 0, sizeof(hd));
    hd.entries = (L.poolPos + 3) & ~3u;
    if (hd.entries > L.poolPos)
    {
      uint8_t z[4] = { 0, 0, 0, 0 };
      Program(L, L.poolPos, z, hd.entries - L.poolPos);
    }
    Program(L, hd.entries, L.ent, L.n * sizeof(TEntry));
    hd.magic = Magic;
    memcpy(hd.code, code, 4);
    hd.fileSize = size;
    hd.fileSum = sum;
    hd.count = L.n;
    hd.total = hd.entries + L.n * sizeof(TEntry);
    // the checksum of the image as written: from the flash after LinearFinish
    hd.sum = 0;
    Program(L, 0, &hd, sizeof(hd));
    Flush(L);
    if (read && L.ok)
    {
      hd.sum = ImageSum(h);
      Program(L, 0, &hd, sizeof(hd));
      Flush(L);
    }
    if (!read || !L.ok || !Valid(h))
    {
      strcpy(Status(), L.n >= L.entMax ? "translation too long" : "translation error");
      return false;
    }
    Active() = h;
    return true;
  }

  inline const char* Code() { return Active() ? Active()->code : ""; }
}

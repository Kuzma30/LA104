// host test of common/lang.h: g++ -std=c++17 test_lang.cpp && ./a.out
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
namespace BIOS { namespace FAT {
  enum EResult { EOk, EFail }; enum { IoRead = 1, IoWrite = 2 };
  std::string files[2][2]; int cur = -1; size_t pos = 0;
  EResult Open(const char* n, int) { for (int i = 0; i < 2; i++) if (files[i][0] == n) { cur = i; pos = 0; return EOk; } cur = -1; return EFail; }
  EResult Read(uint8_t* p) { if (cur < 0) return EFail; std::string& d = files[cur][1]; memset(p, 0, 512); size_t n = d.size() - pos < 512 ? d.size() - pos : 512; memcpy(p, d.data() + pos, n); pos += n; return EOk; }
  EResult Close(int = -1) { cur = -1; return EOk; }
  uint32_t GetFileSize() { return cur < 0 ? 0 : (uint32_t)files[cur][1].size(); }
} }
#include "../lang.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); fails++; } } while (0)
static uint8_t scratch[16384];
int main()
{
  using namespace BIOS::FAT;
  // English: no LANG.CFG
  CHECK(!Lang::Init("APP", scratch, sizeof(scratch)));
  CHECK(strcmp(Lang::T("Trigger"), "Trigger") == 0);
  files[0][0] = "LANG.CFG"; files[0][1] = "\xef\xbb\xbfuk\r\n";
  CHECK(!Lang::Init("APP", scratch, sizeof(scratch)));
  CHECK(strcmp(Lang::Status(), "no LANG/UK/APP.TXT") == 0);
  std::string txt = "# comment\n\"Trigger\" = \"Тригер\"\r\n  \"a \\\"q\\\" b\"=\"в \\\"л\\\" г\"\n\"empty\" = \"\"\n"
    "bad line\n\"Trigger\" = \"second\"\n\"two\\nlines\" = \"два\\nрядки\"\n\"last\" = \"кінець\"";
  files[1][0] = "LANG/UK/APP.TXT"; files[1][1] = txt;
  CHECK(Lang::Init("APP", scratch, sizeof(scratch)));
  CHECK(strcmp(Lang::T("Trigger"), "Тригер") == 0);
  CHECK(strcmp(Lang::T("a \"q\" b"), "в \"л\" г") == 0);
  CHECK(strcmp(Lang::T("empty"), "empty") == 0);
  CHECK(strcmp(Lang::T("two\nlines"), "два\nрядки") == 0);
  CHECK(strcmp(Lang::T("last"), "кінець") == 0);
  CHECK(strcmp(Lang::T("unknown"), "unknown") == 0);
  CHECK(strcmp(Lang::Code(), "UK") == 0);
  // unchanged file: the area is not written again
  uint8_t copy[32768]; memcpy(copy, Lang::Area(), sizeof(copy));
  Lang::Area()[30000] = 0x5a;            // a byte outside the image
  CHECK(Lang::Init("APP", scratch, sizeof(scratch)));
  CHECK(Lang::Area()[30000] == 0x5a);
  // changed file: compiled again
  files[1][1] = txt + "\n\"new\" = \"нове\"\n";
  CHECK(Lang::Init("APP", scratch, sizeof(scratch)));
  CHECK(strcmp(Lang::T("new"), "нове") == 0 && strcmp(Lang::T("Trigger"), "Тригер") == 0);
  // too many strings for the scratch
  std::string big;
  for (int i = 0; i < 2000; i++) big += "\"k" + std::to_string(i) + "\" = \"v\"\n";
  files[1][1] = big;
  CHECK(!Lang::Init("APP", scratch, 4096));
  CHECK(strcmp(Lang::Status(), "translation too long") == 0);
  CHECK(strcmp(Lang::T("k1"), "k1") == 0);
  // characters
  CHECK(Lang::Len("abc") == 3 && Lang::Len("Тест") == 4 && Lang::Len(" \xe6s") == 3 && Lang::Len("Ґ€x") == 3);
  char s[32]; strcpy(s, "Привіт"); Lang::Cut(s, 3); CHECK(strcmp(s, "При") == 0);
  CHECK(Lang::Bytes("Ї1", 1) == 2);
  char d[8]; Lang::Copy(d, "ЖЖЖЖ", sizeof(d)); CHECK(strcmp(d, "ЖЖЖ") == 0);
  // EN switches back to English
  files[0][1] = "EN\n";
  CHECK(!Lang::Init("APP", scratch, sizeof(scratch)));
  CHECK(strcmp(Lang::T("Trigger"), "Trigger") == 0);
  printf(fails ? "%d failed\n" : "all passed\n", fails);
  return fails != 0;
}

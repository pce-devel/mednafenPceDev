/******************************************************************************/
/* Mednafen NEC PC-FX Emulation Module                                        */
/******************************************************************************/
/* debug.cpp:
**  Copyright (C) 2006-2016 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "pcfx.h"

#include <trio/trio.h>
#include <iconv.h>

#include "debug.h"
#include <mednafen/hw_cpu/v810/v810_cpuD.h>
#include "interrupt.h"
#include "timer.h"
#include "king.h"
#include "rainbow.h"
#include "soundbox.h"
#include "input.h"
#include <mednafen/cdrom/scsicd.h>


extern bool IsHSYNCBreakPoint();
extern bool IsVSYNCBreakPoint();


namespace MDFN_IEN_PCFX
{

static void (*CPUHook)(uint32, bool bpoint) = NULL;
static bool CPUHookContinuous = false;
static void (*LogFunc)(const char *, const char *);
static iconv_t sjis_ict = (iconv_t)-1;
bool PCFX_LoggingOn = false;

typedef struct __PCFX_BPOINT {
        uint32 A[2];
        int type;
        bool logical;
} PCFX_BPOINT;

static std::vector<PCFX_BPOINT> BreakPointsPC, BreakPointsRead, BreakPointsWrite, BreakPointsIORead, BreakPointsIOWrite;
static std::vector<PCFX_BPOINT> BreakPointsAux0Read, BreakPointsAux0Write;
static bool FoundBPoint = 0;

struct BTEntry
{
 uint32 from;
 uint32 to;
 uint32 branch_count;
 uint32 ecode;
};

#define NUMBT 24
static bool BTEnabled = false;
static BTEntry BTEntries[NUMBT];
static int BTIndex = 0;

static void AddBranchTrace(uint32 from, uint32 to, uint32 ecode)
{
 BTEntry *prevbt = &BTEntries[(BTIndex + NUMBT - 1) % NUMBT];

 //if(BTEntries[(BTIndex - 1) & 0xF] == PC) return;

 if(prevbt->from == from && prevbt->to == to && prevbt->ecode == ecode && prevbt->branch_count < 0xFFFFFFFF)
  prevbt->branch_count++;
 else
 {
  BTEntries[BTIndex].from = from;
  BTEntries[BTIndex].to = to;
  BTEntries[BTIndex].ecode = ecode;
  BTEntries[BTIndex].branch_count = 1;

  BTIndex = (BTIndex + 1) % NUMBT;
 }
}

void PCFXDBG_EnableBranchTrace(bool enable)
{
 BTEnabled = enable;
 if(!enable)
 {
  BTIndex = 0;
  memset(BTEntries, 0, sizeof(BTEntries));
 }
}

std::vector<BranchTraceResult> PCFXDBG_GetBranchTrace(void)
{
 BranchTraceResult tmp;
 std::vector<BranchTraceResult> ret;

 for(int x = 0; x < NUMBT; x++)
 {
  const BTEntry *bt = &BTEntries[(x + BTIndex) % NUMBT];

  tmp.count = bt->branch_count;
  trio_snprintf(tmp.from, sizeof(tmp.from), "%08x", bt->from);
  trio_snprintf(tmp.to, sizeof(tmp.to), "%08x", bt->to);

  tmp.code[0] = 0;

  if(bt->ecode >= 0xFFA0 && bt->ecode <= 0xFFBF)      // TRAP
  {
	trio_snprintf(tmp.code, sizeof(tmp.code), "TRAP");
  }
  else if(bt->ecode >= 0xFE00 && bt->ecode <= 0xFEFF)
  {
	trio_snprintf(tmp.code, sizeof(tmp.code), "INT%d", (bt->ecode >> 4) & 0xF);
  }
  else switch(bt->ecode)
  {
   case 0: break;
   default: trio_snprintf(tmp.code, sizeof(tmp.code), "e");
	    break;

   case 0xFFF0: // Reset
	trio_snprintf(tmp.code, sizeof(tmp.code), "R");
	break;

   case 0xFFD0: // NMI
	trio_snprintf(tmp.code, sizeof(tmp.code), "NMI");
	break;

   case 0xFFC0:	// Address trap
	trio_snprintf(tmp.code, sizeof(tmp.code), "ADTR");
	break;

   case 0xFF90:	// Illegal/invalid instruction code
	trio_snprintf(tmp.code, sizeof(tmp.code), "ILL");
	break;

   case 0xFF80:	// Zero division
	trio_snprintf(tmp.code, sizeof(tmp.code), "ZD");
	break;

   case 0xFF70:
	trio_snprintf(tmp.code, sizeof(tmp.code), "FIV");	// FIV
	break;

   case 0xFF68:
	trio_snprintf(tmp.code, sizeof(tmp.code), "FZD");	// FZD
	break;

   case 0xFF64:
	trio_snprintf(tmp.code, sizeof(tmp.code), "FOV");	// FOV
	break;

   case 0xFF62:
	trio_snprintf(tmp.code, sizeof(tmp.code), "FUD");	// FUD
	break;

   case 0xFF61:
	trio_snprintf(tmp.code, sizeof(tmp.code), "FPR");	// FPR
	break;

   case 0xFF60:
	trio_snprintf(tmp.code, sizeof(tmp.code), "FRO");	// FRO
	break;
  }

  ret.push_back(tmp);
 }
 return(ret);
}

template<bool write>
static void SimuVDC(bool which_vdc, bool addr, unsigned value = 0)
{
 VDC_SimulateResult result;

 if(write)
  fx_vdc_chips[which_vdc]->SimulateWrite16(addr, value, &result);
 else
  fx_vdc_chips[which_vdc]->SimulateRead16(addr, &result);

 if(result.ReadCount)
  PCFXDBG_CheckBP(BPOINT_AUX_READ, 0x80000 | (which_vdc << 16) | result.ReadStart, 0, result.ReadCount);

 if(result.WriteCount)
  PCFXDBG_CheckBP(BPOINT_AUX_WRITE, 0x80000 | (which_vdc << 16) | result.WriteStart, 0/*FIXME(HOW? :b)*/, result.WriteCount);

 if(result.RegReadDone)
  PCFXDBG_CheckBP(BPOINT_AUX_READ, 0xA0000 | (which_vdc << 16) | result.RegRWIndex, 0, 1);

 if(result.RegWriteDone)
  PCFXDBG_CheckBP(BPOINT_AUX_WRITE, 0xA0000 | (which_vdc << 16) | result.RegRWIndex, 0, 1);
}

void PCFXDBG_CheckBP(int type, uint32 address, uint32 value, unsigned int len)
{
 std::vector<PCFX_BPOINT>::iterator bpit, bpit_end;

 if(type == BPOINT_READ)
 {
  bpit = BreakPointsRead.begin();
  bpit_end = BreakPointsRead.end();

  if(MDFN_UNLIKELY(address >= 0xA4000000 && address <= 0xABFFFFFF))
  {
   SimuVDC<false>((bool)(address & 0x8000000), 1);
  }
 }
 else if(type == BPOINT_WRITE)
 {
  bpit = BreakPointsWrite.begin();
  bpit_end = BreakPointsWrite.end();

  if(MDFN_UNLIKELY(address >= 0xB4000000 && address <= 0xBBFFFFFF))
  {
   SimuVDC<true>((bool)(address & 0x8000000), 1, value);
  }
 }
 else if(type == BPOINT_IO_READ)
 {
  bpit = BreakPointsIORead.begin();
  bpit_end = BreakPointsIORead.end();

  if(address >= 0x400 && address <= 0x5FF)
  {
   SimuVDC<false>((bool)(address & 0x100), (bool)(address & 4));
  }
 }
 else if(type == BPOINT_IO_WRITE)
 {
  bpit = BreakPointsIOWrite.begin();
  bpit_end = BreakPointsIOWrite.end();

  if(address >= 0x400 && address <= 0x5FF)
  {
   SimuVDC<true>((bool)(address & 0x100), (bool)(address & 4), value);
  }
 }
 else if(type == BPOINT_AUX_READ)
 {
  bpit = BreakPointsAux0Read.begin();
  bpit_end = BreakPointsAux0Read.end();
 }
 else if(type == BPOINT_AUX_WRITE)
 {
  bpit = BreakPointsAux0Write.begin();
  bpit_end = BreakPointsAux0Write.end();
 }
 else
  return;

 for(; bpit != bpit_end; bpit++)
 {
  uint32 tmp_address = address;
  uint32 tmp_len = len;

  while(tmp_len--)
  {
   if(tmp_address >= bpit->A[0] && tmp_address <= bpit->A[1])
   {
    FoundBPoint = true;
    break;
   }
   tmp_address++;
  }
 }
}

enum
{
 SVT_NONE = 0,
 SVT_PTR,
 SVT_STRINGPTR,
 SVT_INT,
 SVT_UCHAR,
 SVT_LONG,
 SVT_ULONG,
};

typedef struct
{
 unsigned int number;
 const char *name;
 int arguments;
 int argument_types[16];
} syscall_t;

static const syscall_t SysDefs[] =
{
 {  0, "fsys_init", 3, { SVT_PTR, SVT_PTR, SVT_PTR} },
 {  1, "fsys_mount", 2, { SVT_PTR, SVT_PTR } },
 {  2, "fsys_ctrl", 4, { SVT_STRINGPTR, SVT_INT, SVT_PTR, SVT_INT } },
 {  3, "fsys_getfsys", 1, { SVT_PTR } },
 {  4, "fsys_format", 2, { SVT_PTR, SVT_PTR } },
 {  5, "fsys_diskfree", 1, { SVT_STRINGPTR } },
 {  6, "fsys_getblocks", 1, { SVT_PTR } },
 {  7, "fsys_open", 2, { SVT_STRINGPTR, SVT_INT } },
 {  8, "fsys_read", 3, { SVT_INT, SVT_PTR, SVT_INT } },
 {  9, "fsys_write", 3, { SVT_INT, SVT_PTR, SVT_INT } },
 { 10, "fsys_seek", 3, { SVT_INT, SVT_LONG, SVT_INT } },
 { 11, "fsys_htime", 2, { SVT_INT, SVT_LONG} },
 { 12, "fsys_close", 1, { SVT_INT } },
 { 13, "fsys_delete", 1, { SVT_STRINGPTR } },
 { 14, "fsys_rename", 2, { SVT_STRINGPTR } },
 { 15, "fsys_mkdir", 1, { SVT_STRINGPTR } },
 { 16, "fsys_rmdir", 1, { SVT_STRINGPTR } },
 { 17, "fsys_chdir", 1, { SVT_STRINGPTR } },
 { 18, "fsys_curdir", 1, { SVT_PTR } },
 { 19, "fsys_ffiles", 2, { SVT_PTR, SVT_PTR } },
 { 20, "fsys_nfiles", 1, { SVT_PTR } },
 { 21, "fsys_efiles", 1, { SVT_PTR } },
 { 22, "fsys_datetime", 1, { SVT_ULONG } },
 { 23, "fsys_m_init", 2, { SVT_PTR, SVT_PTR } },
 { 24, "fsys_malloc", 1, { SVT_INT } },
 { 25, "fsys_free", 1, { SVT_INT } },
 { 26, "fsys_setblock", 2, { SVT_PTR, SVT_INT } },
};

static void DoSyscallLog(void)
{
 uint32 ws = 0;
 unsigned int which = 0;
 unsigned int nargs = 0;
 const char *func_name = "<unknown>";
 char argsbuffer[2048];

 for(unsigned int i = 0; i < sizeof(SysDefs) / sizeof(syscall_t); i++)
 {
  if(SysDefs[i].number == PCFX_V810.GetPR(10))
  {
   nargs = SysDefs[i].arguments;
   func_name = SysDefs[i].name;
   which = i;
   break;
  }
 }

 {
  char *pos = argsbuffer;

  argsbuffer[0] = 0;

  pos += trio_sprintf(pos, "(");
  for(unsigned int i = 0; i < nargs; i++)
  {
   if(SysDefs[which].argument_types[i] == SVT_STRINGPTR)
   {
    uint8 quickiebuf[64 + 1];
    int qbuf_index = 0;
    bool error_thing = false;

    do
    {
     uint32 A = PCFX_V810.GetPR(6 + i) + qbuf_index;

     quickiebuf[qbuf_index] = 0;

     if(A >= 0x80000000 && A < 0xF0000000)
     {
      error_thing = true;
      break;
     }

     quickiebuf[qbuf_index] = mem_peekbyte(ws, A);
    } while(quickiebuf[qbuf_index] && ++qbuf_index < 64);

    if(qbuf_index == 64) 
     error_thing = true;

    quickiebuf[64] = 0;

    if(error_thing)
     pos += trio_sprintf(pos, "0x%08x, ", PCFX_V810.GetPR(6 + i));
    else
    {
	uint8 quickiebuf_utf8[64 * 6 + 1];
	char *in_ptr, *out_ptr;
	size_t ibl, obl;

	ibl = qbuf_index;
	obl = sizeof(quickiebuf_utf8) - 1;

	in_ptr = (char *)quickiebuf;
	out_ptr = (char *)quickiebuf_utf8;

	if(iconv(sjis_ict, (ICONV_CONST char **)&in_ptr, &ibl, &out_ptr, &obl) == (size_t) -1)
	{
	 pos += trio_sprintf(pos, "0x%08x, ", PCFX_V810.GetPR(6 + i));
	}
	else
	{
	 *out_ptr = 0;
	 pos += trio_sprintf(pos, "@0x%08x=\"%s\", ", PCFX_V810.GetPR(6 + i), quickiebuf_utf8);
	}
    }
   }
   else
    pos += trio_sprintf(pos, "0x%08x, ", PCFX_V810.GetPR(6 + i));
  }

  // Get rid of the trailing comma and space
  if(nargs)
   pos-=2;

  trio_sprintf(pos, ");");
 }

 PCFXDBG_DoLog("SYSCALL", "0x%02x, %s: %s", PCFX_V810.GetPR(10), func_name, argsbuffer);
}

static void CPUHandler(const v810_timestamp_t timestamp, uint32 PC)
{
 std::vector<PCFX_BPOINT>::iterator bpit;

 for(bpit = BreakPointsPC.begin(); bpit != BreakPointsPC.end(); bpit++)
 {
  if(PC >= bpit->A[0] && PC <= bpit->A[1])
  {
   FoundBPoint = true;
   break;
  }
 }
 FoundBPoint |=  IsVSYNCBreakPoint() | IsHSYNCBreakPoint();

 fx_vdc_chips[0]->ResetSimulate();
 fx_vdc_chips[1]->ResetSimulate();

 PCFX_V810.CheckBreakpoints(PCFXDBG_CheckBP, mem_peekhword, NULL);	// FIXME: mem_peekword

 if(PCFX_LoggingOn)
 {
  // FIXME:  There is a little race condition if a user turns on logging right between jump instruction and the first
  // instruction at 0xFFF0000C, in which case the call-from address will be wrong.
  static uint32 lastPC = ~0;

  if(PC == 0xFFF0000C)
  {
   static const char *font_sizes[6] =
   {
    "KANJI16x16", "KANJI12x12", "ANK8x16", "ANK6x12", "ANK8x8", "ANK8x12"
   };

   // FIXME, overflow possible and speed
   const uint32 pr7 = PCFX_V810.GetPR(7);
   const uint16 sc = PCFX_V810.GetPR(6) & 0xFFFF;
   const char* s = PCFXDBG_ShiftJIS_to_UTF8(sc);
   PCFXDBG_DoLog("ROMFONT", "0x%08x->0xFFF0000C, PR7=0x%08x=%s, PR6=0x%04x = %s", lastPC, pr7, (pr7 > 5) ? "?" : font_sizes[pr7], sc, s);
   for(const char* tmp = s; *tmp; tmp++)
   {
    const char c = *tmp;

    if(c != 0x1B)
     putchar(c);
   }
   fflush(stdout);
  }
  else if(PC == 0xFFF00008)
   DoSyscallLog();

  lastPC = PC;
 }

 CPUHookContinuous |= FoundBPoint;

 if(CPUHookContinuous && CPUHook)
 {
  ForceEventUpdates(timestamp);
  CPUHook(PC, FoundBPoint);
 }

 FoundBPoint = false;
}

static void RedoCPUHook(void)
{
 bool HappyTest;

 HappyTest = CPUHook || PCFX_LoggingOn || BreakPointsPC.size() || BreakPointsRead.size() || BreakPointsWrite.size() ||
		BreakPointsIOWrite.size() || BreakPointsIORead.size() || BreakPointsAux0Read.size() || BreakPointsAux0Write.size();

 PCFX_V810.SetCPUHook(HappyTest ? CPUHandler : NULL, BTEnabled ? AddBranchTrace : NULL);
}

static void PCFXDBG_FlushBreakPoints(int type)
{
 std::vector<PCFX_BPOINT>::iterator bpit;

 if(type == BPOINT_READ)
  BreakPointsRead.clear();
 else if(type == BPOINT_WRITE)
  BreakPointsWrite.clear();
 else if(type == BPOINT_IO_READ)
  BreakPointsIORead.clear();
 else if(type == BPOINT_IO_WRITE)
  BreakPointsIOWrite.clear();
 else if(type == BPOINT_AUX_READ)
  BreakPointsAux0Read.clear();
 else if(type == BPOINT_AUX_WRITE)
  BreakPointsAux0Write.clear();
 else if(type == BPOINT_PC)
  BreakPointsPC.clear();

 RedoCPUHook();
 KING_NotifyOfBPE(BreakPointsAux0Read.size(), BreakPointsAux0Write.size());
}

static void PCFXDBG_AddBreakPoint(int type, unsigned int A1, unsigned int A2, bool logical)
{
 PCFX_BPOINT tmp;

 tmp.A[0] = A1;
 tmp.A[1] = A2;
 tmp.type = type;

 if(type == BPOINT_READ)
  BreakPointsRead.push_back(tmp);
 else if(type == BPOINT_WRITE)
  BreakPointsWrite.push_back(tmp);
 else if(type == BPOINT_IO_READ)
  BreakPointsIORead.push_back(tmp);
 else if(type == BPOINT_IO_WRITE)
  BreakPointsIOWrite.push_back(tmp);
 else if(type == BPOINT_AUX_READ)
  BreakPointsAux0Read.push_back(tmp);
 else if(type == BPOINT_AUX_WRITE)
  BreakPointsAux0Write.push_back(tmp);
 else if(type == BPOINT_PC)
  BreakPointsPC.push_back(tmp);

 RedoCPUHook();
 KING_NotifyOfBPE(BreakPointsAux0Read.size(), BreakPointsAux0Write.size());
}

static uint16 dis_readhw(uint32 A)
{
 return(mem_peekhword(0, A));
}

static void PCFXDBG_Disassemble(uint32 &a, uint32 SpecialA, char *TextBuf)
{
 return(v810_dis(a, 1, TextBuf, dis_readhw));
}

static uint32 PCFXDBG_MemPeek(uint32 A, unsigned int bsize, bool hl, bool logical)
{
 uint32 ret = 0;
 uint32 ws = 0;

 for(unsigned int i = 0; i < bsize; i++)
 {
  A &= 0xFFFFFFFF;
  ret |= mem_peekbyte(ws, A) << (i * 8);
  A++;
 }

 return(ret);
}

static uint32 PCFXDBG_GetRegister(const unsigned int id, char *special, const uint32 special_len)
{
 switch(id >> 16)
 {
  case 0: return PCFX_V810.GetRegister(id & 0xFFFF, special, special_len);
  case 1: return PCFXIRQ_GetRegister(id & 0xFFFF, special, special_len);
  case 2: return FXTIMER_GetRegister(id & 0xFFFF, special, special_len);
  case 3: return FXINPUT_GetRegister(id & 0xFFFF, special, special_len);
 }

 return 0xDEADBEEF;
}

static void PCFXDBG_SetRegister(const unsigned int id, uint32 value)
{
 switch(id >> 16)
 {
  case 0: PCFX_V810.SetRegister(id & 0xFFFF, value); break;
  case 1: PCFXIRQ_SetRegister(id & 0xFFFF, value); break;
  case 2: FXTIMER_SetRegister(id & 0xFFFF, value); break;
  case 3: FXINPUT_SetRegister(id & 0xFFFF, value); break;
 }
}

static void PCFXDBG_SetCPUCallback(void (*callb)(uint32 PC, bool bpoint), bool continuous)
{
 CPUHook = callb;
 CPUHookContinuous = continuous;
 RedoCPUHook();
}

void PCFXDBG_DoLog(const char *type, const char *format, ...)
{
 if(LogFunc)
 {
  char *temp;

  va_list ap;
  va_start(ap, format);

  temp = trio_vaprintf(format, ap);
  LogFunc(type, temp);
  free(temp);

  va_end(ap);
 }
}

void PCFXDBG_SetLogFunc(void (*func)(const char *, const char *))
{
 LogFunc = func;

 PCFX_LoggingOn = func ? true : false;
 SCSICD_SetLog(func ? PCFXDBG_DoLog : NULL);
 KING_SetLogFunc(func ? PCFXDBG_DoLog : NULL);

 if(PCFX_LoggingOn)
 {
  if(sjis_ict == (iconv_t)-1)
   sjis_ict = iconv_open("UTF-8", "shift_jis");
 }
 else
 {
  if(sjis_ict != (iconv_t)-1)
  {
   iconv_close(sjis_ict);
   sjis_ict = (iconv_t)-1;
  }
 }
 RedoCPUHook();
}

char *PCFXDBG_ShiftJIS_to_UTF8(const uint16 sjc)
{
 static char ret[16];
 char inbuf[3];
 char *in_ptr, *out_ptr;
 size_t ibl, obl;

 if(sjc < 256)
 {
  inbuf[0] = sjc;
  inbuf[1] = 0;
  ibl = 1;
 }
 else
 {
  inbuf[0] = sjc >> 8;
  inbuf[1] = sjc >> 0;
  inbuf[2] = 0;
  ibl = 2;
 }

 in_ptr = inbuf;
 out_ptr = ret;  
 obl = 16;

 iconv(sjis_ict, (ICONV_CONST char **)&in_ptr, &ibl, &out_ptr, &obl);

 *out_ptr = 0;

 return(ret);
}

static const RegType PCFXRegs0[] =
{
	{ 0, 0, "-----V810-----", "", 0xFFFF },

	{ V810::GSREG_PC,      4, "PC",      "Program Counter",                               4 },
	{ V810::GSREG_PR +  1, 4, "R1",      "Program Register 1 (Workspace)",                4 },
	{ V810::GSREG_PR +  2, 4, "FP",      "Program Register 2 (Frame Pointer)",            4 },
	{ V810::GSREG_PR +  3, 4, "SP",      "Program Register 3 (Stack Pointer)",            4 },
	{ V810::GSREG_PR +  4, 4, "GP",      "Program Register 4 (Global Pointer)",           4 },
	{ V810::GSREG_PR +  5, 4, "TP",      "Program Register 5 (Text Pointer)",             4 },
	{ V810::GSREG_PR +  6, 4, "R6",      "Program Register 6 (Parameter 1)",              4 },
	{ V810::GSREG_PR +  7, 4, "R7",      "Program Register 7 (Parameter 2)",              4 },
	{ V810::GSREG_PR +  8, 4, "R8",      "Program Register 8 (Parameter 3)",              4 },
	{ V810::GSREG_PR +  9, 4, "R9",      "Program Register 9 (Parameter 4)",              4 },
	{ V810::GSREG_PR + 10, 3, "R10",     "Program Register 10 (Return 1)",                4 },
	{ V810::GSREG_PR + 11, 3, "R11",     "Program Register 11 (Return 2)",                4 },
	{ V810::GSREG_PR + 12, 3, "R12",     "Program Register 12 (Workspace)",               4 },
	{ V810::GSREG_PR + 13, 3, "R13",     "Program Register 13 (Workspace)",               4 },
	{ V810::GSREG_PR + 14, 3, "R14",     "Program Register 14 (Workspace)",               4 },
	{ V810::GSREG_PR + 15, 3, "R15",     "Program Register 15 (Workspace)",               4 },
	{ V810::GSREG_PR + 16, 3, "R16",     "Program Register 16 (Workspace)",               4 },
	{ V810::GSREG_PR + 17, 3, "R17",     "Program Register 17 (Workspace)",               4 },
	{ V810::GSREG_PR + 18, 3, "R18",     "Program Register 18 (Workspace)",               4 },
	{ V810::GSREG_PR + 19, 3, "R19",     "Program Register 19 (Workspace)",               4 },
	{ V810::GSREG_PR + 20, 3, "R20",     "Program Register 20 (Preserved)",               4 },
	{ V810::GSREG_PR + 21, 3, "R21",     "Program Register 21 (Preserved)",               4 },
	{ V810::GSREG_PR + 22, 3, "R22",     "Program Register 22 (Preserved)",               4 },
	{ V810::GSREG_PR + 23, 3, "R23",     "Program Register 23 (Preserved)",               4 },
	{ V810::GSREG_PR + 24, 3, "R24",     "Program Register 24 (Preserved)",               4 },
	{ V810::GSREG_PR + 25, 3, "R25",     "Program Register 25 (Preserved)",               4 },
	{ V810::GSREG_PR + 26, 3, "R26",     "Program Register 26 (String Dest Bit Offset)",  4 },
	{ V810::GSREG_PR + 27, 3, "R27",     "Program Register 27 (String Source Bit Offset)",4 },
	{ V810::GSREG_PR + 28, 3, "R28",     "Program Register 28 (String Length)",           4 },
	{ V810::GSREG_PR + 29, 3, "R29",     "Program Register 29 (String Dest)",             4 },
	{ V810::GSREG_PR + 30, 3, "R30",     "Program Register 30 (String Source)",           4 },
	{ V810::GSREG_PR + 31, 4, "LP",      "Program Register 31 (Link Pointer)",            4 },

	{ 0, 0, "-----SREG-----", "", 0xFFFF },

	{ V810::GSREG_SR +  0, 2, "EIPC",    "Exception/Interrupt PC",                        4 },
	{ V810::GSREG_SR +  1, 1, "EIPSW",   "Exception/Interrupt PSW",                       4 },
	{ V810::GSREG_SR +  2, 2, "FEPC",    "Fatal Error PC",                                4 },
	{ V810::GSREG_SR +  3, 1, "FEPSW",   "Fatal Error PSW",                               4 },
	{ V810::GSREG_SR +  4, 3, "ECR",     "Exception Cause Register",                      4 },
	{ V810::GSREG_SR +  5, 3, "PSW",     "Program Status Word",                           4 },
	{ V810::GSREG_SR +  6, 3, "PIR",     "Processor ID Register",                         4 },
	{ V810::GSREG_SR +  7, 2, "TKCW",    "Task Control Word",                             4 },
	{ V810::GSREG_SR + 24, 2, "CHCW",    "Cache Control Word",                            4 },
	{ V810::GSREG_SR + 25, 1, "ADTRE",   "Address Trap Register",                         4 },

	{ 0, 0, "----TSTAMP----", "", 0xFFFF },

	{ V810::GSREG_TIMESTAMP,             2, "TStamp",  "Timestamp",                       3 },

	{ 0, 0, "--------------", "", 0xFFFF },

	{ 0, 0, "", "", 0 },
};

static uint32 GetRegister_KING(const unsigned int id, char *special, const uint32 special_len)
{
 switch(id >> 16)
 {
  case 0: return KING_GetRegister(id & 0xFFFF, special, special_len);
  case 1: return MDFN_IEN_PCFX::SBoxDBG_GetRegister(id & 0xFFFF, special, special_len);
 }

 return 0xDEADBEEF;
}

static void SetRegister_KING(const unsigned int id, uint32 value)
{
 switch(id >> 16)
 {
  case 0: KING_SetRegister(id & 0xFFFF, value); break;
  case 1: MDFN_IEN_PCFX::SBoxDBG_SetRegister(id & 0xFFFF, value); break;
 }
} 

static const RegType KINGRegs0[] =
{
	{ 0, 0, "--KING-SYSTEM--", "", 0xFFFF },

	{ KING_GSREG_AR,       11, "AR",        "Active Register",                 1 },

	{ KING_GSREG_PAGESET,   4, "PAGESET",   "KRAM Page Settings",              2 },
	{ KING_GSREG_RTCTRL,    5, "RTCTRL",    "Rainbow Transfer Control",        2 },
	{ KING_GSREG_RKRAMA,    3, "RKRAMA",    "Rainbow Transfer K-RAM Address",  3 },
	{ KING_GSREG_RSTART,    5, "RSTART",    "Rainbow Transfer Start Position", 2 },
	{ KING_GSREG_RCOUNT,    5, "RCOUNT",    "Rainbow Transfer Block Count",    2 },
	{ KING_GSREG_RIRQLINE,  3, "RIRQLINE",  "Raster IRQ Line",                 2 },
	{ KING_GSREG_KRAMWA,    1, "KRAMWA",    "K-RAM Write Address",             4 },
	{ KING_GSREG_KRAMRA,    1, "KRAMRA",    "K-RAM Read Address",              4 },
	{ KING_GSREG_DMATA,     4, "DMATA",     "DMA Transfer Address",            3 },
	{ KING_GSREG_DMATS,     2, "DMATS",     "DMA Transfer Size",               4 },
	{ KING_GSREG_DMASTT,    5, "DMASTT",    "DMA Status",                      2 },

	{ 0, 0, "-----MPROG-----", "", 0xFFFF },

	{ KING_GSREG_MPROGADDR, 2, "MPROGADDR", "Micro-program Address",           2 },
	{ KING_GSREG_MPROGCTRL, 2, "MPROGCTRL", "Micro-program Control",           2 },

	{ KING_GSREG_MPROG0,    3, "Program0",    "Micro-program",                   2 },
	{ KING_GSREG_MPROG1,    3, "Program1",    "Micro-program",                   2 },
	{ KING_GSREG_MPROG2,    3, "Program2",    "Micro-program",                   2 },
	{ KING_GSREG_MPROG3,    3, "Program3",    "Micro-program",                   2 },
	{ KING_GSREG_MPROG4,    3, "Program4",    "Micro-program",                   2 },
	{ KING_GSREG_MPROG5,    3, "Program5",    "Micro-program",                   2 },
	{ KING_GSREG_MPROG6,    3, "Program6",    "Micro-program",                   2 },
	{ KING_GSREG_MPROG7,    3, "Program7",    "Micro-program",                   2 },
	{ KING_GSREG_MPROG8,    3, "Program8",    "Micro-program",                   2 },
	{ KING_GSREG_MPROG9,    3, "Program9",    "Micro-program",                   2 },
	{ KING_GSREG_MPROGA,    3, "ProgramA",    "Micro-program",                   2 },
	{ KING_GSREG_MPROGB,    3, "ProgramB",    "Micro-program",                   2 },
	{ KING_GSREG_MPROGC,    3, "ProgramC",    "Micro-program",                   2 },
	{ KING_GSREG_MPROGD,    3, "ProgramD",    "Micro-program",                   2 },
	{ KING_GSREG_MPROGE,    3, "ProgramE",    "Micro-program",                   2 },
	{ KING_GSREG_MPROGF,    3, "ProgramF",    "Micro-program",                   2 },

	{ 0, 0, "-----ADPCM-----", "", 0xFFFF },

	{ KING_GSREG_ADPCMCTRL, 2, "ADPCMCTRL", "ADPCM Control",                   2 },
	{ KING_GSREG_ADPCMBM0,  3, "ADPCMBM0",  "ADPCM Buffer Mode Ch0",           2 },
	{ KING_GSREG_ADPCMBM1,  3, "ADPCMBM1",  "ADPCM Buffer Mode Ch1",           2 },
	{ KING_GSREG_ADPCMPA0,  2, "ADPCMPA0",  "ADPCM PlayAddress Ch0",           0x100 | 18 },
	{ KING_GSREG_ADPCMPA1,  2, "ADPCMPA1",  "ADPCM PlayAddress Ch1",           0x100 | 18 },
	{ KING_GSREG_ADPCMSA0,  3, "ADPCMSA0",  "ADPCM Start Address Ch0",         2 },
	{ KING_GSREG_ADPCMSA1,  3, "ADPCMSA1",  "ADPCM Start Address Ch1",         2 },
	{ KING_GSREG_ADPCMIA0,  3, "ADPCMIA0",  "ADPCM Intermediate Address Ch0",  2 },
	{ KING_GSREG_ADPCMIA1,  3, "ADPCMIA1",  "ADPCM Intermediate Address Ch1",  2 },
	{ KING_GSREG_ADPCMEA0,  2, "ADPCMEA0",  "ADPCM End Address Ch0",           0x100 | 18 },
	{ KING_GSREG_ADPCMEA1,  2, "ADPCMEA1",  "ADPCM End Address Ch1",           0x100 | 18 },
	{ KING_GSREG_ADPCMStat, 4, "ADPCMStat", "ADPCM Status Register",           1 },
	{ KING_GSREG_Reg01,     8, "Reg01",     "KING Register 0x01",              1 },
	{ KING_GSREG_Reg02,     8, "Reg02",     "KING Register 0x02",              1 },
	{ KING_GSREG_Reg03,     8, "Reg03",     "KING Register 0x03",              1 },
	{ KING_GSREG_SUBCC,     8, "SUBCC",     "Sub-channel Control",             1 },

	{ 0, 0, "---------------", "", 0xFFFF },

	{ 0, 0, "", "", 0 },
};

static const RegType KINGRegs1[] =
{
	{ 0, 0, "--KING-BG--", "", 0xFFFF },

	{ KING_GSREG_BGMODE, 3, "Mode",   "Background Mode",                2 },
	{ KING_GSREG_BGPRIO, 3, "Prio",   "Background Priority",            2 },
	{ KING_GSREG_BGSCRM, 3, "ScrM",   "Background Scroll Mode",         2 },

	{ 0, 0, "----BG0----", "", 0xFFFF },

	{ KING_GSREG_BGSIZ0, 3, "Size",   "Background 0 Size",              2 },
	{ KING_GSREG_BGBAT0, 6, "BAT",    "Background 0 BAT Address",       1 },
	{ KING_GSREG_BGCG0,  7, "CG",     "Background 0 CG Address",        1 },
	{ KING_GSREG_BGBATS, 3, "SubBAT", "Background 0 SUB BAT Address",   1 },
	{ KING_GSREG_BGCGS,  4, "SubCG",  "Background 0 SUB CG Address",    1 },
	{ KING_GSREG_BGXSC0, 4, "XScr",   "Background 0 X Scroll",          0x100 | 11 },
	{ KING_GSREG_BGYSC0, 4, "YScr",   "Background 0 Y Scroll",          0x100 | 11 },

	{ 0, 0, "----BG1----", "", 0xFFFF },

	{ KING_GSREG_BGSIZ1, 5, "Size",   "Background 1 Size",              1 },
	{ KING_GSREG_BGBAT1, 6, "BAT",    "Background 1 BAT Address",       1 },
	{ KING_GSREG_BGCG1,  7, "CG",     "Background 1 CG Address",        1 },
	{ KING_GSREG_BGXSC1, 4, "XScr",   "Background 1 X Scroll",          0x100 | 10 },
	{ KING_GSREG_BGYSC1, 4, "YScr",   "Background 1 Y Scroll",          0x100 | 10 },

	{ 0, 0, "----BG2----", "", 0xFFFF },

	{ KING_GSREG_BGSIZ2, 5, "Size",   "Background 2 Size",              1 },
	{ KING_GSREG_BGBAT2, 6, "BAT",    "Background 2 BAT Address",       1 },
	{ KING_GSREG_BGCG2,  7, "CG",     "Background 2 CG Address",        1 },
	{ KING_GSREG_BGXSC2, 4, "XScr",   "Background 2 X Scroll",          0x100 | 10 },
	{ KING_GSREG_BGYSC2, 4, "YScr",   "Background 2 Y Scroll",          0x100 | 10 },

	{ 0, 0, "----BG3----", "", 0xFFFF },

	{ KING_GSREG_BGSIZ3, 5, "Size",   "Background 3 Size",              1 },
	{ KING_GSREG_BGBAT3, 6, "BAT",    "Background 3 BAT Address",       1 },
	{ KING_GSREG_BGCG3,  7, "CG",     "Background 3 CG Address",        1 },
	{ KING_GSREG_BGXSC3, 4, "XScr",   "Background 3 X Scroll",          0x100 | 10 },
	{ KING_GSREG_BGYSC3, 4, "YScr",   "Background 3 Y Scroll",          0x100 | 10 },

	{ 0, 0, "---AFFIN---", "", 0xFFFF },

	{ KING_GSREG_AFFINA, 6, "A",     "Background Affin Coefficient A", 2 },
	{ KING_GSREG_AFFINB, 6, "B",     "Background Affin Coefficient B", 2 },
	{ KING_GSREG_AFFINC, 6, "C",     "Background Affin Coefficient C", 2 },
	{ KING_GSREG_AFFIND, 6, "D",     "Background Affin Coefficient D", 2 },
	{ KING_GSREG_AFFINX, 6, "X",     "Background Affin Center X",      2 },
	{ KING_GSREG_AFFINY, 6, "Y",     "Background Affin Center Y",      2 },

	{ 0, 0, "---CDROM---", "", 0xFFFF },

	{ KING_GSREG_DB,     7, "DB",    "SCSI Data Bus",                  0x100 | 8 },
	{ KING_GSREG_BSY,    7, "BSY",   "SCSI BSY",                       0x100 | 1 },
	{ KING_GSREG_REQ,    7, "REQ",   "SCSI REQ",                       0x100 | 1 },
	{ KING_GSREG_ACK,    7, "ACK",   "SCSI ACK",                       0x100 | 1 },
	{ KING_GSREG_MSG,    7, "MSG",   "SCSI MSG",                       0x100 | 1 },
	{ KING_GSREG_IO,     8, "IO",    "SCSI IO",                        0x100 | 1 },
	{ KING_GSREG_CD,     8, "CD",    "SCSI CD",                        0x100 | 1 },
	{ KING_GSREG_SEL,    7, "SEL",   "SCSI SEL",                       0x100 | 1 },

	{ 0, 0, "---CD-DA---", "", 0xFFFF },

	{ (1 << 16) |  SBOX_GSREG_CDDA_LVOL,     3, "CDLVol",  "CD-DA Left Volume",         1 },
	{ (1 << 16) |  SBOX_GSREG_CDDA_RVOL,     3, "CDRVol",  "CD-DA Right Volume",        1 },

	{ 0, 0, "-----------", "", 0xFFFF },

	{ 0, 0, "", "", 0 },
};

static uint32 GetRegister_VCERAINBOW(const unsigned int id, char *special, const uint32 special_len)
{
 switch(id >> 16)
 {
  case 0: return FXVCE_GetRegister(id & 0xFFFF, special, special_len);
  case 1: return RAINBOW_GetRegister(id & 0xFFFF, special, special_len);
  case 2: return PCFXIRQ_GetRegister(id & 0xFFFF, special, special_len);
  case 3: return FXTIMER_GetRegister(id & 0xFFFF, special, special_len);
  case 4: return FXINPUT_GetRegister(id & 0xFFFF, special, special_len);
 }

 return 0xDEADBEEF;
}

static void SetRegister_VCERAINBOW(const unsigned int id, uint32 value)
{
 switch(id >> 16)
 {
  case 0: FXVCE_SetRegister(id & 0xFFFF, value); break;
  case 1: RAINBOW_SetRegister(id & 0xFFFF, value); break;
  case 2: PCFXIRQ_SetRegister(id & 0xFFFF, value); break;
  case 3: FXTIMER_SetRegister(id & 0xFFFF, value); break;
  case 4: FXINPUT_SetRegister(id & 0xFFFF, value); break;
 }
} 

static const RegType VCERAINBOWRegs[] =
{
	{ 0, 0, "-----IRQ-----", "", 0xFFFF },

	{ (2 << 16) | PCFXIRQ_GSREG_IPEND,   4, "IPEND",   "Interrupts Pending",            2 },
	{ (2 << 16) | PCFXIRQ_GSREG_IMASK,   4, "IMASK",   "Interrupt Mask",                2 },
	{ (2 << 16) | PCFXIRQ_GSREG_IPRIO0,  3, "IPRIO0",  "Interrupt Priority Register 0", 2 },
	{ (2 << 16) | PCFXIRQ_GSREG_IPRIO1,  3, "IPRIO1",  "Interrupt Priority Register 1", 2 },

	{ 0, 0, "----TIMER----", "", 0xFFFF },

	{ (3 << 16) | FXTIMER_GSREG_TCTRL,   4, "TCTRL",   "Timer Control",                 2 },
	{ (3 << 16) | FXTIMER_GSREG_TPRD,    5, "TPRD",    "Timer Period",                  2 },
	{ (3 << 16) | FXTIMER_GSREG_TCNTR,   2, "TCNTR",   "Timer Counter",                 3 },

	{ 0, 0, "-----PAD-----", "", 0xFFFF },

	{ (4 << 16) | FXINPUT_GSREG_KPCTRL0, 4, "KPCTRL0", "Keyport 0 Control",             1 },
	{ (4 << 16) | FXINPUT_GSREG_KPCTRL1, 4, "KPCTRL1", "Keyport 1 Control",             1 },

	{ 0, 0, "-----VCE-----", "", 0xFFFF },

	{ FXVCE_GSREG_Line,    6, "Line",    "VCE Frame Counter",                      0x100 |  9 },
	{ FXVCE_GSREG_PRIO0,   5, "PRIO0",   "VCE Priority 0",                         0x100 | 12 },
	{ FXVCE_GSREG_PRIO1,   4, "PRIO1",   "VCE Priority 1",                         2 },
	{ FXVCE_GSREG_PICMODE, 2, "PICMODE", "VCE Picture Mode",                       2 },
	{ FXVCE_GSREG_PALRWOF, 2, "PALRWOF", "VCE Palette R/W Offset",                 2 },
	{ FXVCE_GSREG_PALRWLA, 2, "PALRWLA", "VCE Palette R/W Latch",                  2 },
	{ FXVCE_GSREG_PALOFS0, 2, "PALOFS0", "VCE Palette Offset 0",                   2 } ,
	{ FXVCE_GSREG_PALOFS1, 2, "PALOFS1", "VCE Palette Offset 1",                   2 },
	{ FXVCE_GSREG_PALOFS2, 2, "PALOFS2", "VCE Palette Offset 2",                   2 },
	{ FXVCE_GSREG_PALOFS3, 4, "PALOFS3", "VCE Palette Offset 3",                   1 },
	{ FXVCE_GSREG_CCR,     6, "CCR",     "VCE Fixed Color Register",               2 },
	{ FXVCE_GSREG_BLE,     6, "BLE",     "VCE Cellophane Setting Register",        2 },
	{ FXVCE_GSREG_SPBL,    5, "SPBL",    "VCE Sprite Cellophane Setting Register", 2 },
	{ FXVCE_GSREG_COEFF0,  4, "COEFF0",  "VCE Cellophane Coefficient 0(1A)",       0x100 | 12 },
	{ FXVCE_GSREG_COEFF1,  4, "COEFF1",  "VCE Cellophane Coefficient 1(1B)",       0x100 | 12 },
	{ FXVCE_GSREG_COEFF2,  4, "COEFF2",  "VCE Cellophane Coefficient 2(2A)",       0x100 | 12 },
	{ FXVCE_GSREG_COEFF3,  4, "COEFF3",  "VCE Cellophane Coefficient 3(2B)",       0x100 | 12 },
	{ FXVCE_GSREG_COEFF4,  4, "COEFF4",  "VCE Cellophane Coefficient 4(3A)",       0x100 | 12 },
	{ FXVCE_GSREG_COEFF5,  4, "COEFF5",  "VCE Cellophane Coefficient 5(3B)",       0x100 | 12 },
	{ FXVCE_GSREG_CKeyY,   4, "CKeyY",   "VCE Chroma Key Y",                       2 },
	{ FXVCE_GSREG_CKeyU,   4, "CKeyU",   "VCE Chroma Key U",                       2 },
	{ FXVCE_GSREG_CKeyV,   4, "CKeyV",   "VCE Chroma Key V",                       2 },

	{ 0, 0, "---RAINBOW---", "", 0xFFFF },

	{ (1 << 16) | RAINBOW_GSREG_RSCRLL, 3, "RSCRLL",  "Rainbow Horizontal Scroll", 2 },
	{ (1 << 16) | RAINBOW_GSREG_RCTRL,  4, "RCTRL",   "Rainbow Control",           2 },
	{ (1 << 16) | RAINBOW_GSREG_RHSYNC, 5, "RHSYNC",  "Rainbow HSync?",            1 },
	{ (1 << 16) | RAINBOW_GSREG_RNRY,   5, "RNRY",    "Rainbow Null Run Y",        2 },
	{ (1 << 16) | RAINBOW_GSREG_RNRU,   5, "RNRU",    "Rainbow Null Run U",        2 },
	{ (1 << 16) | RAINBOW_GSREG_RNRV,   5, "RNRV",    "Rainbow Null Run V",        2 },

	{ 0, 0, "-------------", "", 0xFFFF },

	{ 0, 0, "", "", 0 },
};

static uint32 GetRegister_VDC(const unsigned int id, char *special, const uint32 special_len)
{
 return(fx_vdc_chips[(id >> 15) & 1]->GetRegister(id &~0x8000, special, special_len));
}

static void SetRegister_VDC(const unsigned int id, uint32 value)
{
 fx_vdc_chips[(id >> 15) & 1]->SetRegister(id &~0x8000, value);
} 


static const RegType VDCRegs[] =
{
	{ 0, 0, "---VDC-A---", "", 0xFFFF },

	{ 0x0000 | VDC::GSREG_SELECT, 3, "Select", "Register Select, VDC-A",         1 },
	{ 0x0000 | VDC::GSREG_STATUS, 3, "Status", "Status, VDC-A",                  1 },
	
	{ 0x0000 | VDC::GSREG_MAWR,   3, "MAWR",   "Memory Write Address, VDC-A",    2 },
	{ 0x0000 | VDC::GSREG_MARR,   3, "MARR",   "Memory Read Address, VDC-A",     2 },
	{ 0x0000 | VDC::GSREG_CR,     5, "CR",     "Control, VDC-A",                 2 },
	{ 0x0000 | VDC::GSREG_RCR,    4, "RCR",    "Raster Counter, VDC-A",          2 },
	{ 0x0000 | VDC::GSREG_BXR,    4, "BXR",    "X Scroll, VDC-A",                2 },
	{ 0x0000 | VDC::GSREG_BYR,    4 ,"BYR",    "Y Scroll, VDC-A",                2 },
	{ 0x0000 | VDC::GSREG_MWR,    4, "MWR",    "Memory Width, VDC-A",            2 },

	{ 0x0000 | VDC::GSREG_HSR,    4, "HSR",    "HSR, VDC-A",                     2 },
	{ 0x0000 | VDC::GSREG_HDR,    4, "HDR",    "HDR, VDC-A",                     2 },
	{ 0x0000 | VDC::GSREG_VSR,    4, "VSR",    "VSR, VDC-A",                     2 },
	{ 0x0000 | VDC::GSREG_VDR,    4, "VDR",    "VDR, VDC-A",                     2 },

	{ 0x0000 | VDC::GSREG_VCR,    4, "VCR",    "VCR, VDC-A",                     2 },
	{ 0x0000 | VDC::GSREG_DCR,    4, "DCR",    "DMA Control, VDC-A",             2 },
	{ 0x0000 | VDC::GSREG_SOUR,   3, "SOUR",   "VRAM DMA Source Address, VDC-A", 2 },
	{ 0x0000 | VDC::GSREG_DESR,   3, "DESR",   "VRAM DMA Dest Address, VDC-A",   2 },
	{ 0x0000 | VDC::GSREG_LENR,   3, "LENR",   "VRAM DMA Length, VDC-A", 2 },
	{ 0x0000 | VDC::GSREG_DVSSR,  2, "DVSSR",  "DVSSR Update Address, VDC-A",    2 },

	{ 0, 0, "---VDC-B---", "", 0xFFFF },

	{ 0x8000 | VDC::GSREG_SELECT, 3, "Select", "Register Select, VDC-B",         1 },
	{ 0x8000 | VDC::GSREG_STATUS, 3, "Status", "Status, VDC-B",                  1 },

	{ 0x8000 | VDC::GSREG_MAWR,   3, "MAWR",   "Memory Write Address, VDC-B",    2 },
	{ 0x8000 | VDC::GSREG_MARR,   3, "MARR",   "Memory Read Address, VDC-B",     2 },
	{ 0x8000 | VDC::GSREG_CR,     5, "CR",     "Control, VDC-B",                 2 },
	{ 0x8000 | VDC::GSREG_RCR,    4, "RCR",    "Raster Counter, VDC-B",          2 },
	{ 0x8000 | VDC::GSREG_BXR,    4, "BXR",    "X Scroll, VDC-B",                2 },
	{ 0x8000 | VDC::GSREG_BYR,    4 ,"BYR",    "Y Scroll, VDC-B",                2 },
	{ 0x8000 | VDC::GSREG_MWR,    4, "MWR",    "Memory Width, VDC-B",            2 },

	{ 0x8000 | VDC::GSREG_HSR,    4, "HSR",    "HSR, VDC-B",                     2 },
	{ 0x8000 | VDC::GSREG_HDR,    4, "HDR",    "HDR, VDC-B",                     2 },
	{ 0x8000 | VDC::GSREG_VSR,    4, "VSR",    "VSR, VDC-B",                     2 },
	{ 0x8000 | VDC::GSREG_VDR,    4, "VDR",    "VDR, VDC-B",                     2 },

	{ 0x8000 | VDC::GSREG_VCR,    4, "VCR",    "VCR, VDC-B",                     2 },
	{ 0x8000 | VDC::GSREG_DCR,    4, "DCR",    "DMA Control, VDC-B",             2 },
	{ 0x8000 | VDC::GSREG_SOUR,   3, "SOUR",   "VRAM DMA Source Address, VDC-B", 2 },
	{ 0x8000 | VDC::GSREG_DESR,   3, "DESR",   "VRAM DMA Dest Address, VDC-B",   2 },
	{ 0x8000 | VDC::GSREG_LENR,   3, "LENR",   "VRAM DMA Length, VDC-B",         2 },
	{ 0x8000 | VDC::GSREG_DVSSR,  2, "DVSSR",  "DVSSR Update Address, VDC-B",    2 },

	{ 0, 0, "-----------", "", 0xFFFF },

	{ 0, 0, "", "", 0 },
};

static const RegGroupType PCFXRegs0Group =
{
 NULL,
 PCFXRegs0,
 PCFXDBG_GetRegister,
 PCFXDBG_SetRegister,
};

static const RegGroupType KINGRegs0Group =
{
 NULL,
 KINGRegs0,
 GetRegister_KING,
 SetRegister_KING
};

static const RegGroupType KINGRegs1Group =
{
 NULL,
 KINGRegs1,
 GetRegister_KING,
 SetRegister_KING
};

static const RegGroupType VCERAINBOWRegsGroup =
{
 NULL,
 VCERAINBOWRegs,
 GetRegister_VCERAINBOW,
 SetRegister_VCERAINBOW
};

static const RegGroupType VDCRegsGroup =
{
 NULL,
 VDCRegs,
 GetRegister_VDC,
 SetRegister_VDC
};

void PCFXDBG_Init(void)
{
 MDFNDBG_AddRegGroup(&PCFXRegs0Group);
 MDFNDBG_AddRegGroup(&VCERAINBOWRegsGroup);
 MDFNDBG_AddRegGroup(&VDCRegsGroup);
 MDFNDBG_AddRegGroup(&KINGRegs1Group);
 MDFNDBG_AddRegGroup(&KINGRegs0Group);
}

static void ForceIRQ(int level)
{
 //v810_int(level);
}

DebuggerInfoStruct PCFXDBGInfo =
{
 false,
 "shift_jis",
 4,
 2,             // Instruction alignment(bytes)
 32,
 32,
 0x00000000,
 ~0U,           // ZP
 ~0U,           // SP
 NULL,          // GetStackPtr

 PCFXDBG_MemPeek,
 PCFXDBG_Disassemble,
 NULL,
 ForceIRQ,
 NULL,
 PCFXDBG_FlushBreakPoints,
 PCFXDBG_AddBreakPoint,
 PCFXDBG_SetCPUCallback,
 PCFXDBG_EnableBranchTrace,
 PCFXDBG_GetBranchTrace,
 KING_SetGraphicsDecode,
 PCFXDBG_SetLogFunc,
};

}

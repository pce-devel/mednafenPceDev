/******************************************************************************/
/* Mednafen Virtual Boy Emulation Module                                      */
/******************************************************************************/
/* debug.cpp:
**  Copyright (C) 2010-2016 Mednafen Team
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

#include "vb.h"
#include <mednafen/hw_cpu/v810/v810_cpuD.h>
#include <trio/trio.h>
#include <iconv.h>

#include "debug.h"
#include "timer.h"
//#include "input.h"
#include "vip.h"
#include "vsu.h"
#include "timer.h"

namespace MDFN_IEN_VB
{

MDFN_HIDE extern V810 *VB_V810;
MDFN_HIDE extern VSU *VB_VSU;

static void RedoCPUHook(void);
static void (*CPUHook)(uint32, bool bpoint) = NULL;
static bool CPUHookContinuous = false;
static void (*LogFunc)(const char *, const char *);
bool VB_LoggingOn = false;

typedef struct __VB_BPOINT {
        uint32 A[2];
        int type;
        bool logical;
} VB_BPOINT;

static std::vector<VB_BPOINT> BreakPointsPC, BreakPointsRead, BreakPointsWrite;
static bool FoundBPoint = 0;

struct BTEntry
{
 uint32 from;
 uint32 to;
 uint32 branch_count;
 uint32 ecode;
 bool valid;
};

#define NUMBT 24
static BTEntry BTEntries[NUMBT];
static int BTIndex;
static bool BTEnabled;

static void AddBranchTrace(uint32 from, uint32 to, uint32 ecode)
{
 BTEntry *prevbt = &BTEntries[(BTIndex + NUMBT - 1) % NUMBT];

 //if(BTEntries[(BTIndex - 1) & 0xF] == PC) return;

 if(prevbt->from == from && prevbt->to == to && prevbt->ecode == ecode && prevbt->branch_count < 0xFFFFFFFF && prevbt->valid)
  prevbt->branch_count++;
 else
 {
  BTEntries[BTIndex].from = from;
  BTEntries[BTIndex].to = to;
  BTEntries[BTIndex].ecode = ecode;
  BTEntries[BTIndex].branch_count = 1;
  BTEntries[BTIndex].valid = true;

  BTIndex = (BTIndex + 1) % NUMBT;
 }
}

void VBDBG_EnableBranchTrace(bool enable)
{
 BTEnabled = enable;
 if(!enable)
 {
  BTIndex = 0;
  memset(BTEntries, 0, sizeof(BTEntries));
 }

 RedoCPUHook();
}

std::vector<BranchTraceResult> VBDBG_GetBranchTrace(void)
{
 BranchTraceResult tmp;
 std::vector<BranchTraceResult> ret;

 for(int x = 0; x < NUMBT; x++)
 {
  const BTEntry *bt = &BTEntries[(x + BTIndex) % NUMBT];

  if(!bt->valid)
   continue;

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

   case 0xFFC0: // Address trap
        trio_snprintf(tmp.code, sizeof(tmp.code), "ADTR");
        break;

   case 0xFF90: // Illegal/invalid instruction code
        trio_snprintf(tmp.code, sizeof(tmp.code), "ILL");
        break;

   case 0xFF80: // Zero division
        trio_snprintf(tmp.code, sizeof(tmp.code), "ZD");
        break;

   case 0xFF70:
        trio_snprintf(tmp.code, sizeof(tmp.code), "FIV");       // FIV
        break;

   case 0xFF68:
        trio_snprintf(tmp.code, sizeof(tmp.code), "FZD");       // FZD
        break;

   case 0xFF64:
        trio_snprintf(tmp.code, sizeof(tmp.code), "FOV");       // FOV
        break;

   case 0xFF62:
        trio_snprintf(tmp.code, sizeof(tmp.code), "FUD");       // FUD
        break;

   case 0xFF61:
        trio_snprintf(tmp.code, sizeof(tmp.code), "FPR");       // FPR
        break;

   case 0xFF60:
        trio_snprintf(tmp.code, sizeof(tmp.code), "FRO");       // FRO
        break;
  }

  ret.push_back(tmp);
 }
 return(ret);
}


void VBDBG_CheckBP(int type, uint32 address, uint32 value, unsigned int len)
{
 std::vector<VB_BPOINT>::iterator bpit, bpit_end;

 if(type == BPOINT_READ || type == BPOINT_IO_READ)
 {
  bpit = BreakPointsRead.begin();
  bpit_end = BreakPointsRead.end();
 }
 else if(type == BPOINT_WRITE || type == BPOINT_IO_WRITE)
 {
  bpit = BreakPointsWrite.begin();
  bpit_end = BreakPointsWrite.end();
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

static uint16 MDFN_FASTCALL MemPeek8(v810_timestamp_t timestamp, uint32 A)
{
 uint8 ret;

 // TODO: VB_InDebugPeek(implement elsewhere)
 VB_InDebugPeek++;
 ret = MemRead8(timestamp, A);
 VB_InDebugPeek--;

 return(ret);
}

static uint16 MDFN_FASTCALL MemPeek16(v810_timestamp_t timestamp, uint32 A)
{
 uint16 ret;

 // TODO: VB_InDebugPeek(implement elsewhere)
 VB_InDebugPeek++;
 ret = MemRead16(timestamp, A);
 VB_InDebugPeek--;

 return(ret);
}

static void CPUHandler(const v810_timestamp_t timestamp, uint32 PC)
{
 std::vector<VB_BPOINT>::iterator bpit;

 for(bpit = BreakPointsPC.begin(); bpit != BreakPointsPC.end(); bpit++)
 {
  if(PC >= bpit->A[0] && PC <= bpit->A[1])
  {
   FoundBPoint = true;
   break;
  }
 }
 VB_V810->CheckBreakpoints(VBDBG_CheckBP, MemPeek16, NULL);

 CPUHookContinuous |= FoundBPoint;

 if(CPUHook && CPUHookContinuous)
 {
  ForceEventUpdates(timestamp);
  CPUHook(PC, FoundBPoint);
 }

 FoundBPoint = false;
}

static void RedoCPUHook(void)
{
 VB_V810->SetCPUHook((CPUHook || VB_LoggingOn || BreakPointsPC.size() || BreakPointsRead.size() || BreakPointsWrite.size()) ? CPUHandler : NULL,
	BTEnabled ? AddBranchTrace : NULL);
}

void VBDBG_FlushBreakPoints(int type)
{
 std::vector<VB_BPOINT>::iterator bpit;

 if(type == BPOINT_READ)
  BreakPointsRead.clear();
 else if(type == BPOINT_WRITE)
  BreakPointsWrite.clear();
 else if(type == BPOINT_PC)
  BreakPointsPC.clear();

 RedoCPUHook();
}

void VBDBG_AddBreakPoint(int type, unsigned int A1, unsigned int A2, bool logical)
{
 VB_BPOINT tmp;

 tmp.A[0] = A1;
 tmp.A[1] = A2;
 tmp.type = type;

 if(type == BPOINT_READ)
  BreakPointsRead.push_back(tmp);
 else if(type == BPOINT_WRITE)
  BreakPointsWrite.push_back(tmp);
 else if(type == BPOINT_PC)
  BreakPointsPC.push_back(tmp);

 RedoCPUHook();
}

static uint16 dis_readhw(uint32 A)
{
 int32 timestamp = 0;
 return(MemPeek16(timestamp, A));
}

void VBDBG_Disassemble(uint32 &a, uint32 SpecialA, char *TextBuf)
{
 return(v810_dis(a, 1, TextBuf, dis_readhw, true));
}

uint32 VBDBG_MemPeek(uint32 A, unsigned int bsize, bool hl, bool logical)
{
 uint32 ret = 0;
 int32 ws = 0;

 for(unsigned int i = 0; i < bsize; i++)
 {
  A &= 0xFFFFFFFF;
  //ret |= mem_peekbyte(A, ws) << (i * 8);
  ret |= MemRead8(ws, A) << (i * 8);
  A++;
 }

 return(ret);
}

static void GetAddressSpaceBytes_CPU(const char *name, uint32 Address, uint32 Length, uint8 *Buffer)
{
 while(Length--)
 {
  *Buffer = MemPeek8(0, Address);

  Address++;
  Buffer++;
 }
}

static void GetAddressSpaceBytes_RAM(const char *name, uint32 Address, uint32 Length, uint8 *Buffer)
{
 while(Length--)
 {
  *Buffer = MemPeek8(0, (0x5 << 24) | (uint16)Address);

  Address++;
  Buffer++;
 }
}

static void GetAddressSpaceBytes_VSUWD(const char *name, uint32 Address, uint32 Length, uint8 *Buffer)
{
 const unsigned int which = name[5] - '0';

 while(Length--)
 {
  *Buffer = VB_VSU->PeekWave(which, Address);

  Address++;
  Buffer++;
 }
}

static void PutAddressSpaceBytes_CPU(const char *name, uint32 Address, uint32 Length, uint32 Granularity, bool hl, const uint8 *Buffer)
{
 while(Length--)
 {
  int32 dummy_ts = 0;

  MemWrite8(dummy_ts, Address, *Buffer);

  Address++;
  Buffer++;
 }
}

static void PutAddressSpaceBytes_RAM(const char *name, uint32 Address, uint32 Length, uint32 Granularity, bool hl, const uint8 *Buffer)
{
 while(Length--)
 {
  int32 dummy_ts = 0;

  MemWrite8(dummy_ts, (0x5 << 24) | (uint16)Address, *Buffer);

  Address++;
  Buffer++;
 }
}

static void PutAddressSpaceBytes_VSUWD(const char *name, uint32 Address, uint32 Length, uint32 Granularity, bool hl, const uint8 *Buffer)
{
 const unsigned int which = name[5] - '0';

 while(Length--)
 {
  VB_VSU->PokeWave(which, Address, *Buffer);

  Address++;
  Buffer++;
 }
}

static uint32 VBDBG_GetRegister(const unsigned int id, char* special, const uint32 special_len)
{
 return VB_V810->GetRegister(id, special, special_len);
}

static void VBDBG_SetRegister(const unsigned int id, uint32 value)
{
 VB_V810->SetRegister(id, value);
}

void VBDBG_SetCPUCallback(void (*callb)(uint32 PC, bool bpoint), bool continuous)
{
 CPUHook = callb;
 CPUHookContinuous = continuous;
 RedoCPUHook();
}

void VBDBG_DoLog(const char *type, const char *format, ...)
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

void VBDBG_SetLogFunc(void (*func)(const char *, const char *))
{
 LogFunc = func;

 VB_LoggingOn = func ? true : false;

 if(VB_LoggingOn)
 {

 }
 else
 {

 }
 RedoCPUHook();
}

static const RegType V810Regs[] =
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


static const RegGroupType V810RegsGroup =
{
 NULL,
 V810Regs,
 VBDBG_GetRegister,
 VBDBG_SetRegister,
};

static uint32 MISC_GetRegister(const unsigned int id, char *special, const uint32 special_len)
{
 return(TIMER_GetRegister(id, special, special_len));
}

static void MISC_SetRegister(const unsigned int id, const uint32 value)
{
 TIMER_SetRegister(id, value);
}


static const RegType Regs_Misc[] =
{
	{ 0, 0, "------TIMER------", "", 0xFFFF },

	{ TIMER_GSREG_TCR,         12, "TCR",         "Timer Control Register",      1 },
	{ TIMER_GSREG_DIVCOUNTER,   3, "DivCounter",  "Timer Clock Divider Counter", 2 },
	{ TIMER_GSREG_RELOAD_VALUE, 2, "ReloadValue", "Timer Reload Value",          2 },
	{ TIMER_GSREG_COUNTER,      6, "Counter",     "Timer Counter Value",         2 },

	{ 0, 0, "-----------------", "", 0xFFFF },

	{ 0, 0, "", "", 0 },
};

static const RegGroupType RegsGroup_Misc =
{
        "Misc",
        Regs_Misc,
        MISC_GetRegister,
        MISC_SetRegister
};


static const RegType Regs_VIP[] =
{
	{ 0, 0, "-----VIP-----", "", 0xFFFF },

	{ VIP_GSREG_IPENDING,       1, "IPending",    "Interrupts Pending",          2 },
	{ VIP_GSREG_IENABLE,        2, "IEnable",     "Interrupts Enabled",          2 },

	{ VIP_GSREG_DPCTRL,         3, "DPCTRL",      "DPCTRL",                      2 },

	{ VIP_GSREG_BRTA,           7, "BRTA",        "BRTA",                        1 },
	{ VIP_GSREG_BRTB,           7, "BRTB",        "BRTB",                        1 },
	{ VIP_GSREG_BRTC,           7, "BRTC",        "BRTC",                        1 },
	{ VIP_GSREG_REST,           7, "REST",        "REST",                        1 },
	{ VIP_GSREG_FRMCYC,         5, "FRMCYC",      "FRMCYC",                      1 },
	{ VIP_GSREG_XPCTRL,         3, "XPCTRL",      "XPCTRL",                      2 },

	{ VIP_GSREG_SPT0,           5, "SPT0",        "SPT0",                        2 },
	{ VIP_GSREG_SPT1,           5, "SPT1",        "SPT1",                        2 },
	{ VIP_GSREG_SPT2,           5, "SPT2",        "SPT2",                        2 },
	{ VIP_GSREG_SPT3,           5, "SPT3",        "SPT3",                        2 },

	{ VIP_GSREG_GPLT0,          6, "GPLT0",       "GPLT0",                       1 },
	{ VIP_GSREG_GPLT1,          6, "GPLT1",       "GPLT1",                       1 },
	{ VIP_GSREG_GPLT2,          6, "GPLT2",       "GPLT2",                       1 },
	{ VIP_GSREG_GPLT3,          6, "GPLT3",       "GPLT3",                       1 },

	{ VIP_GSREG_JPLT0,          6, "JPLT0",       "JPLT0",                       1 },
	{ VIP_GSREG_JPLT1,          6, "JPLT1",       "JPLT1",                       1 },
	{ VIP_GSREG_JPLT2,          6, "JPLT2",       "JPLT2",                       1 },
	{ VIP_GSREG_JPLT3,          6, "JPLT3",       "JPLT3",                       1 },

	{ VIP_GSREG_BKCOL,          6, "BKCOL",       "BKCOL",                       1 },

	{ 0, 0, "-------------", "", 0xFFFF },

	{ 0, 0, "", "", 0 },
};

static const RegGroupType RegsGroup_VIP =
{
        "VIP",
        Regs_VIP,
        VIP_GetRegister,
        VIP_SetRegister
};


bool VBDBG_Init(void)
{
 BTEnabled = false;
 BTIndex = 0;
 memset(BTEntries, 0, sizeof(BTEntries));

 MDFNDBG_AddRegGroup(&V810RegsGroup);
 MDFNDBG_AddRegGroup(&RegsGroup_Misc);
 MDFNDBG_AddRegGroup(&RegsGroup_VIP);

 ASpace_Add(GetAddressSpaceBytes_CPU, PutAddressSpaceBytes_CPU, "cpu", "CPU Physical", 27);
 ASpace_Add(GetAddressSpaceBytes_RAM, PutAddressSpaceBytes_RAM, "ram", "RAM", 16);

 for(int x = 0; x < 5; x++)
 {
     AddressSpaceType newt;
     char tmpname[128], tmpinfo[128];

     trio_snprintf(tmpname, 128, "vsuwd%d", x);
     trio_snprintf(tmpinfo, 128, "VSU Wave Data %d", x);

     newt.GetAddressSpaceBytes = GetAddressSpaceBytes_VSUWD;
     newt.PutAddressSpaceBytes = PutAddressSpaceBytes_VSUWD;

     newt.name = std::string(tmpname);
     newt.long_name = std::string(tmpinfo);
     newt.TotalBits = 5;
     newt.NP2Size = 0;

     newt.IsWave = true;
     newt.WaveFormat = ASPACE_WFMT_UNSIGNED;
     newt.WaveBits = 6;
     ASpace_Add(newt); //PSG_GetAddressSpaceBytes, PSG_PutAddressSpaceBytes, tmpname, tmpinfo, 5);
 }



 return(true);
}

}

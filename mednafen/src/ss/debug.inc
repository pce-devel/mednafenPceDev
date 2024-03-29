/******************************************************************************/
/* Mednafen Sega Saturn Emulation Module                                      */
/******************************************************************************/
/* debug.inc:
**  Copyright (C) 2015-2019 Mednafen Team
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

#ifdef WANT_DEBUGGER
static struct DBGS
{
 enum { NUMBT = 24 };

 struct
 {
  uint32 from;
  uint32 to;
  uint32 branch_count;
  int exception;	// -1 on no exception.
  unsigned vecnum;
  bool valid;
 } BTEntries[2][NUMBT];

 bool BTEnabled;
 unsigned BTIndex[2];

 struct SS_BPOINT
 {
  uint32 A[2];
 };

 std::vector<SS_BPOINT> BreakPointsRead, BreakPointsWrite, BreakPointsPC;

 void (*CPUHook)(uint32, bool);
 bool CPUHookContinuous;

 bool FoundBPoint;

 uint32 CurPC[2];

 unsigned ActiveCPU;

 sscpu_timestamp_t eff_ts;
 bool inss;
} DBG;

static MDFN_COLD MDFN_NOWARN_UNUSED void DBG_Break(void)
{
 DBG.FoundBPoint = true;
}

static MDFN_COLD void DBG_CheckReadBP(unsigned len, uint32 addr)
{
 for(auto const& bp : DBG.BreakPointsRead)
 {
  for(uint32 ta = addr; ta != (addr + len); ta++)	// TODO: Optimize
  {
   if(ta >= bp.A[0] && ta <= bp.A[1])
   {
    DBG.FoundBPoint = true;
    return;
   }
  }
 }
}

static MDFN_COLD void DBG_CheckWriteBP(unsigned len, uint32 addr)
{
 for(auto const& bp : DBG.BreakPointsWrite)
 {
  for(uint32 ta = addr; ta != (addr + len); ta++)	// TODO: Optimize
  {
   if(ta >= bp.A[0] && ta <= bp.A[1])
   {
    DBG.FoundBPoint = true;
    return;
   }
  }
 }
}

enum
{
 ASPACE_PHYSICAL = 0,
 ASPACE_BACKUP,
 ASPACE_WORKRAML,
 ASPACE_WORKRAMH,
 ASPACE_SCSPRAM,
 ASPACE_DSPPROG,
 ASPACE_VDP1VRAM,
 ASPACE_VDP1FB0,
 ASPACE_VDP1FB1,
 ASPACE_VDP2VRAM
};

template<unsigned id>
static MDFN_COLD void GetAddressSpaceBytes(const char *name, uint32 Address, uint32 Length, uint8 *Buffer)
{
 while(MDFN_LIKELY(Length--))
 {
  switch(id)
  {
   default:
	break;

   case ASPACE_PHYSICAL:
	//TODO:
	//Address &= 0x07FFFFFF;
	//*Buffer = SH7095_BusPeek<uint8>(Address);
	break;

   case ASPACE_BACKUP:
	Address &= 0x7FFF;
	*Buffer = BackupRAM[Address];
	break;

   case ASPACE_WORKRAML:
	Address &= 0xFFFFF;
	*Buffer = ne16_rbo_be<uint8>(WorkRAML, Address);
	break;

   case ASPACE_WORKRAMH:
	Address &= 0xFFFFF;
	*Buffer = ne16_rbo_be<uint8>(WorkRAMH, Address);
	break;

   case ASPACE_SCSPRAM:
	Address &= 0x7FFFF;
	*Buffer = SOUND_PeekRAM(Address);
	break;

   case ASPACE_DSPPROG:
	Address &= 0x3FF;
	*Buffer = SCU_DSP_PeekProgRAM(Address >> 2) >> (((Address & 0x3) ^ 0x3) << 3);
	break;

   case ASPACE_VDP1VRAM:
	Address &= 0x7FFFF;
	*Buffer = VDP1::PeekVRAM(Address);
	break;

   case ASPACE_VDP1FB0:
   case ASPACE_VDP1FB1:
	Address &= 0x3FFFF;
	*Buffer = VDP1::PeekFB(id == ASPACE_VDP1FB1, Address);
	break;

   case ASPACE_VDP2VRAM:
	Address &= 0x7FFFF;
	*Buffer = VDP2::PeekVRAM(Address);
	break;
  }
  Address++;
  Buffer++;
 }
}

template<unsigned id>
static MDFN_COLD void PutAddressSpaceBytes(const char *name, uint32 Address, uint32 Length, uint32 Granularity, bool hl, const uint8 *Buffer)
{
 while(MDFN_LIKELY(Length--))
 {
  switch(id)
  {
   default:
	break;

   case ASPACE_PHYSICAL:
	// TODO:
	//Address &= 0x07FFFFFF;
	//*Buffer = SH7095_BusPoke<uint8>(Address, *Buffer);
	break;

   case ASPACE_BACKUP:
	Address &= 0x7FFF;
	BackupRAM[Address] = *Buffer;
	BackupRAM_Dirty = true;
	break;

   case ASPACE_WORKRAML:
	Address &= 0xFFFFF;
	ne16_wbo_be<uint8>(WorkRAML, Address, *Buffer);
	break;

   case ASPACE_WORKRAMH:
	Address &= 0xFFFFF;
	ne16_wbo_be<uint8>(WorkRAMH, Address, *Buffer);
	break;

   case ASPACE_SCSPRAM:
	Address &= 0x7FFFF;
	SOUND_PokeRAM(Address, *Buffer);
	break;

   case ASPACE_DSPPROG:
	// TODO:
	break;

   case ASPACE_VDP1VRAM:
	Address &= 0x7FFFF;
	VDP1::PokeVRAM(Address, *Buffer);
	break;

   case ASPACE_VDP1FB0:
   case ASPACE_VDP1FB1:
	Address &= 0x3FFFF;
	VDP1::PokeFB(id == ASPACE_VDP1FB1, Address, *Buffer);
	break;

   case ASPACE_VDP2VRAM:
	Address &= 0x7FFFF;
	VDP2::PokeVRAM(Address, *Buffer);
	break;
  }
  Address++;
  Buffer++;
 }
}


static MDFN_COLD uint32 DBG_MemPeek(uint32 A, unsigned int bsize, bool hl, bool logical)
{
/*
 uint32 ret = 0;

 for(unsigned int i = 0; i < bsize; i++)
  ret |= CPU->PeekMem8(A + i) << (i * 8);

 return(ret);
*/
 return 0xAA;
}


static MDFN_COLD void DBG_FlushBreakPoints(int type)
{
 if(type == BPOINT_READ)
  DBG.BreakPointsRead.clear();
 else if(type == BPOINT_WRITE)
  DBG.BreakPointsWrite.clear();
 else if(type == BPOINT_PC)
  DBG.BreakPointsPC.clear();
}

static MDFN_COLD void DBG_AddBreakPoint(int type, unsigned int A1, unsigned int A2, bool logical)
{
 DBGS::SS_BPOINT tmp;

 tmp.A[0] = A1;
 tmp.A[1] = A2;

 if(type == BPOINT_READ)
  DBG.BreakPointsRead.push_back(tmp);
 else if(type == BPOINT_WRITE)
  DBG.BreakPointsWrite.push_back(tmp);
 else if(type == BPOINT_PC)
  DBG.BreakPointsPC.push_back(tmp);
}

static MDFN_COLD void DBG_SetCPUCallback(void (*callb)(uint32 PC, bool bpoint), bool continuous)
{
 DBG.CPUHook = callb;
 DBG.CPUHookContinuous = continuous;
}

static MDFN_COLD void DBG_EnableBranchTrace(bool enable)
{
 if((DBG.BTEnabled ^ enable) & DBG.BTEnabled)
 {
  for(unsigned which = 0; which < 2; which++)
   for(unsigned i = 0; i < DBGS::NUMBT; i++)
    DBG.BTEntries[which][i].valid = false;
 }

 DBG.BTEnabled = enable;
}

static MDFN_COLD void DBG_AddBranchTrace(unsigned which, uint32 to, int exception, unsigned vecnum = 0)
{
 const uint32 from = DBG.CurPC[which];
 auto *prevbt = &DBG.BTEntries[which][(DBG.BTIndex[which] + DBGS::NUMBT - 1) % DBGS::NUMBT];

 //if(BTEntries[(BTIndex - 1) & 0xF] == PC) return;

 if(prevbt->from == from && prevbt->to == to && prevbt->exception == exception && prevbt->branch_count < 0xFFFFFFFF && prevbt->valid)
  prevbt->branch_count++;
 else
 {
  auto& bte = DBG.BTEntries[which][DBG.BTIndex[which]];
  bte.from = from;
  bte.to = to;
  bte.exception = exception;
  bte.vecnum = vecnum;
  bte.branch_count = 1;
  bte.valid = true;

  DBG.BTIndex[which] = (DBG.BTIndex[which] + 1) % DBGS::NUMBT;
 }
}

static MDFN_COLD std::vector<BranchTraceResult> DBG_GetBranchTrace(void)
{
 std::vector<BranchTraceResult> ret;
 BranchTraceResult tmp;

 for(unsigned x = 0; x < DBGS::NUMBT; x++)
 {
  char estr[32];
  const auto* bt = &DBG.BTEntries[DBG.ActiveCPU][(x + DBG.BTIndex[DBG.ActiveCPU]) % DBGS::NUMBT];

  if(!bt->valid)
   continue;

  tmp.count = bt->branch_count;
  trio_snprintf(tmp.from, sizeof(tmp.from), "%08x", bt->from);
  trio_snprintf(tmp.to, sizeof(tmp.to), "%08x", bt->to);

  trio_snprintf(estr, sizeof(estr), "UNK");

  switch(bt->exception)
  {
   case -1:
	estr[0] = 0;
	break;

   case SH7095::EXCEPTION_POWERON:
	trio_snprintf(estr, sizeof(estr), "PRST");
	break;

   case SH7095::EXCEPTION_RESET:
	trio_snprintf(estr, sizeof(estr), "MRST");
	break;

   case SH7095::EXCEPTION_ILLINSTR:
	trio_snprintf(estr, sizeof(estr), "ILL");
	break;

   case SH7095::EXCEPTION_ILLSLOT:
	trio_snprintf(estr, sizeof(estr), "ILLS");
	break;

   case SH7095::EXCEPTION_CPUADDR:
	trio_snprintf(estr, sizeof(estr), "CPUA");
	break;

   case SH7095::EXCEPTION_DMAADDR:
	trio_snprintf(estr, sizeof(estr), "DMAA");
	break;

   case SH7095::EXCEPTION_NMI:
	trio_snprintf(estr, sizeof(estr), "NMI");
	break;

   case SH7095::EXCEPTION_BREAK:
	trio_snprintf(estr, sizeof(estr), "BRK");
	break;

   case SH7095::EXCEPTION_TRAP:
	trio_snprintf(estr, sizeof(estr), "TRAP%02X", bt->vecnum);
	break;

   case SH7095::EXCEPTION_INT:
	trio_snprintf(estr, sizeof(estr), "INT%02X", bt->vecnum);
	break;
  }

  trio_snprintf(tmp.code, sizeof(tmp.code), "%s", estr);

  ret.push_back(tmp);
 }
 return(ret);
}

static INLINE void DBG_SetEffTS(const sscpu_timestamp_t eff_ts)
{
 DBG.eff_ts = eff_ts;
}

static INLINE bool DBG_InSlaveStep(void)
{
 return DBG.inss;
}

template<unsigned which>
static MDFN_COLD void DBG_CPUHandler(void)
{
 const uint32 PC = CPU[which].GetRegister(SH7095::GSREG_PC_ID, NULL, 0);

 if(which != DBG.ActiveCPU)
 {
  DBG.CurPC[which] = PC;
  return;
 }

#if 0
 if(LogFunc)
 {
  static const uint32 addr_mask[8] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x7FFFFFFF, 0x1FFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
  uint32 tpc = PC & addr_mask[PC >> 29];;

  if(MDFN_UNLIKELY(tpc <= 0xC0))
  {
   if(tpc == 0xA0 || tpc == 0xB0 || tpc == 0xC0)
   {
    const uint32 function = CPU->GetRegister(PS_CPU::GSREG_GPR + 9, NULL, 0);

    if(tpc != 0xB0 || function != 0x17)
    {
     char tmp[64];
     trio_snprintf(tmp, sizeof(tmp), "0x%02x:0x%02x", PC & 0xFF, function);
     LogFunc("BIOS", tmp);
    }
   }
  }
 }
#endif

 for(auto& bp : DBG.BreakPointsPC)
 {
  if(PC >= bp.A[0] && PC <= bp.A[1])
  {
   DBG.FoundBPoint = true;
   break;
  }
 }

 CPU[which].CheckRWBreakpoints(DBG_CheckReadBP, DBG_CheckWriteBP);
 //CPU->CheckBreakpoints(CheckCPUBPCallB, CPU->PeekMem32(PC));

 DBG.CPUHookContinuous |= DBG.FoundBPoint;

 if(DBG.CPUHookContinuous && DBG.CPUHook)
 {
  DBG.inss = which;
  ForceEventUpdates(DBG.eff_ts);
  DBG.CPUHook(PC, DBG.FoundBPoint);
  DBG.inss = false;
 }

 DBG.FoundBPoint = false;
 //
 //
 // No, no, this isn't right for dual-CPUs(FIXME):
 DBG.CurPC[which] = CPU[which].GetRegister(SH7095::GSREG_PC_ID, NULL, 0);
}


static MDFN_COLD bool DBG_NeedCPUHooks(void)
{
 return DBG.BTEnabled || DBG.CPUHook || DBG.BreakPointsPC.size() || DBG.BreakPointsRead.size() || DBG.BreakPointsWrite.size();
}

// TODO: Standard peek functions
static MDFN_COLD uint16 DBG_DisPeek16(uint32 A)
{
 return *(uint16*)(SH7095_FastMap[A >> SH7095_EXT_MAP_GRAN_BITS] + A);
}

static MDFN_COLD uint32 DBG_DisPeek32(uint32 A)
{
 uint32 ret;

 ret = *(uint16*)(SH7095_FastMap[A >> SH7095_EXT_MAP_GRAN_BITS] + A) << 16;
 A |= 2;
 ret |= *(uint16*)(SH7095_FastMap[A >> SH7095_EXT_MAP_GRAN_BITS] + A) << 0;

 return ret;
}

static MDFN_COLD void DBG_Disassemble(uint32 &A, uint32 SpecialA, char *TextBuf)
{
 if(A & 0x1)
 {
  strncpy(TextBuf, "UNALIGNED", 256);
  A &= ~0x1;
 }
 else
 {
  uint16 instr;

  if(A == CPU[DBG.ActiveCPU].GetRegister(SH7095::GSREG_PC_ID, NULL, 0))
   instr = CPU[DBG.ActiveCPU].GetRegister(SH7095::GSREG_PID, NULL, 0);
  else if(A == CPU[DBG.ActiveCPU].GetRegister(SH7095::GSREG_PC_IF, NULL, 0))
   instr = CPU[DBG.ActiveCPU].GetRegister(SH7095::GSREG_PIF, NULL, 0);
  else
   instr = DBG_DisPeek16(A);

  SH7095::Disassemble(instr, A + 4, TextBuf, DBG_DisPeek16, DBG_DisPeek32);
 }

 char* tlc = TextBuf;

 while((uint8)*tlc != 0)
 {
  *tlc = tolower(*tlc);
  tlc++;
 }

 A += 2;
}

static MDFN_COLD void DBG_ToggleSyntax(void)
{
 // Right, "syntax". ;)
 DBG.ActiveCPU = !DBG.ActiveCPU;
}
//
//
//

static const RegType DBG_Regs_CPU[] =
{
 { 0, 0, "----------------", "", 0xFFFF },

 { SH7095::GSREG_PC_ID,		6,	"PC",		"PC (Effective)", 4 },
 { SH7095::GSREG_RPC,		5,	"RPC",		"PC (Real)", 4 },

 { SH7095::GSREG_PID,		5,	"PID",		"Pipeline ID Buffer", 4 },
 { SH7095::GSREG_PIF,		5,	"PIF",		"Pipeline IF Buffer", 4 },

 { SH7095::GSREG_EP,		6,	"EP",		"Exception Pending", 4 },

 { SH7095::GSREG_R0,		6,	"R0",		"R0", 4 },
 { SH7095::GSREG_R1,		6,	"R1",		"R1",  4 },
 { SH7095::GSREG_R2,		6,	"R2",		"R2",  4 },
 { SH7095::GSREG_R3,		6,	"R3",		"R3",  4 },
 { SH7095::GSREG_R4,		6,	"R4",		"R4",  4 },
 { SH7095::GSREG_R5,		6,	"R5",		"R5",  4 },
 { SH7095::GSREG_R6,		6,	"R6",		"R6",  4 },
 { SH7095::GSREG_R7,		6,	"R7",		"R7",  4 },
 { SH7095::GSREG_R8,		6,	"R8",		"R8",  4 },
 { SH7095::GSREG_R9,		6,	"R9",		"R9",  4 },
 { SH7095::GSREG_R10,		5,	"R10",		"R10",  4 },
 { SH7095::GSREG_R11,		5,	"R11",		"R11",  4 },
 { SH7095::GSREG_R12,		5,	"R12",		"R12",  4 },
 { SH7095::GSREG_R13,		5,	"R13",		"R13",  4 },
 { SH7095::GSREG_R14,		5,	"R14",		"R14",  4 },
 { SH7095::GSREG_R15,		5,	"R15",		"R15/Stack Pointer", 4 },

 { 0, 0, "----------------", "", 0xFFFF },

 { SH7095::GSREG_SR,		6,	"SR",		"Status Register", 4 },
 { SH7095::GSREG_GBR,		5,	"GBR",		"Global Base Register", 4 },
 { SH7095::GSREG_VBR,		5,	"VBR",		"Vector Base Register", 4 },

 { 0, 0, "----------------", "", 0xFFFF },

 { SH7095::GSREG_MACH,		4,	"MACH",		"Multiply-and-Accumulate High", 4 },
 { SH7095::GSREG_MACL,		4,	"MACL",		"Multiply-and-Accumulate Low", 4 },

 { 0, 0, "----------------", "", 0xFFFF },

 { SH7095::GSREG_PR,		6,	"PR",		"Procedure Register", 4 },

 { 0, 0, "----------------", "", 0xFFFF },

 { SH7095::GSREG_NMIL,		10,	"NMIL",		"NMI Level(Input)", 1 },
 { SH7095::GSREG_IRL,		11,	"IRL",		"Interrupt Level(Input)", 1 },
 { SH7095::GSREG_IPRA,		8,	"IPRA",		"IPRA", 2 },
 { SH7095::GSREG_IPRB,		8,	"IPRB",		"IPRB", 2 },
 { SH7095::GSREG_VCRWDT,	6,	"VCRWDT",	"VCRWDT", 2 },
 { SH7095::GSREG_VCRA,		8,	"VCRA",		"VCRA", 2 },
 { SH7095::GSREG_VCRB,		8,	"VCRB",		"VCRB", 2 },
 { SH7095::GSREG_VCRC,		8,	"VCRC",		"VCRC", 2 },
 { SH7095::GSREG_VCRD,		8,	"VCRD",		"VCRD", 2 },
 { SH7095::GSREG_ICR,		9,	"ICR",		"ICR", 2 },

 { 0, 0, "----------------", "", 0xFFFF },

 { SH7095::GSREG_DVSR,		4,	"DVSR",		"Divisor", 4 },
 { SH7095::GSREG_DVDNT,		3,	"DVDNT",	"DVDNT", 4 },
 { SH7095::GSREG_DVDNTH,	2,	"DVDNTH",	"DVDNTH", 4 },
 { SH7095::GSREG_DVDNTL,	2,	"DVDNTL",	"DVDNTL", 4 },
 { SH7095::GSREG_DVDNTHS,	1,	"DVDNTHS",	"DVDNTH Shadow", 4 },
 { SH7095::GSREG_DVDNTLS,	1,	"DVDNTLS",	"DVDNTL Shadow", 4 },
 { SH7095::GSREG_VCRDIV,	6,	"VCRDIV",	"VCRDIV", 2 },
 { SH7095::GSREG_DVCR,		10,	"DVCR",		"DVCR", 1 },

 { 0, 0, "----------------", "", 0xFFFF },

 { SH7095::GSREG_WTCSR,		9,	"WTCSR",	"WTCSR", 1 },
 { SH7095::GSREG_WTCSRM,	8,	"WTCSRM",	"WTCSRM", 1 },
 { SH7095::GSREG_WTCNT,		9,	"WTCNT",	"WTCNT", 1 },
 { SH7095::GSREG_RSTCSR,	8,	"RSTCSR",	"RSTCSR", 1 },
 { SH7095::GSREG_RSTCSRM,	7,	"RSTCSRM",	"RSTCSRM", 1 },

 { 0, 0, "----------------", "", 0xFFFF },

 { 0, 0, "", "", 0 },
};

static const RegType DBG_Regs_CPUE_SCSP[] =
{
 { 0, 0, "------------", "", 0xFFFF },

 { SH7095::GSREG_DMAOR,		5,	"DMAOR",	"DMAOR", 1 },
 { SH7095::GSREG_DMAORM,	4,	"DMAORM",	"DMAORM", 1 },

 { 0, 0, "---DMACH0---", "", 0xFFFF },
 { SH7095::GSREG_DMA0_SAR,	1,	"SAR",		"SAR", 4 },
 { SH7095::GSREG_DMA0_DAR,	1,	"DAR",		"DAR", 4 },
 { SH7095::GSREG_DMA0_TCR,	1,	"TCR",		"TCR", 4 },
 { SH7095::GSREG_DMA0_CHCR,	4,	"CHCR",		"CHCR", 2 },
 { SH7095::GSREG_DMA0_CHCRM,	3,	"CHCRM",	"CHCRM", 2 },
 { SH7095::GSREG_DMA0_VCR,	7,	"VCR",		"VCR", 1 },
 { SH7095::GSREG_DMA0_DRCR,	6,	"DRCR",		"DRCR", 1 },

 { 0, 0, "---DMACH1---", "", 0xFFFF },
 { SH7095::GSREG_DMA1_SAR,	1,	"SAR",		"SAR", 4 },
 { SH7095::GSREG_DMA1_DAR,	1,	"DAR",		"DAR", 4 },
 { SH7095::GSREG_DMA1_TCR,	1,	"TCR",		"TCR", 4 },
 { SH7095::GSREG_DMA1_CHCR,	4,	"CHCR",		"CHCR", 2 },
 { SH7095::GSREG_DMA1_CHCRM,	3,	"CHCRM",	"CHCRM", 2 },
 { SH7095::GSREG_DMA1_VCR,	7,	"VCR",		"VCR", 1 },
 { SH7095::GSREG_DMA1_DRCR,	6,	"DRCR",		"DRCR", 1 },

 { 0, 0, "------------", "", 0xFFFF },

 { SH7095::GSREG_FRC,		5,	"FRC",		"FRC", 2 },
 { SH7095::GSREG_OCR0,		4,	"OCRA",		"OCRA", 2 },
 { SH7095::GSREG_OCR1,		4,	"OCRB",		"OCRB", 2 },
 { SH7095::GSREG_FICR,		4,	"FICR",		"FICR", 2 },
 { SH7095::GSREG_TIER,		6,	"TIER",		"TIER", 1 },
 { SH7095::GSREG_FTCSR,		5,	"FTCSR",	"FTCSR", 1 },
 { SH7095::GSREG_FTCSRM,	4,	"FTCSRM",	"FTCSRM", 1 },
 { SH7095::GSREG_TCR,		7,	"TCR",		"TCR", 1 },
 { SH7095::GSREG_TOCR,		6,	"TOCR",		"TOCR", 1 },
 { SH7095::GSREG_RWT,		7,	"RWT",		"R/W Temp", 1 },

 { 0, 0, "------------", "", 0xFFFF },

 { SH7095::GSREG_CCR,		7,	"CCR",		"Cache Control Register", 1 },
 { SH7095::GSREG_SBYCR,		5,	"SBYCR",	"Standby Control Register", 1 },

 { 0, 0, "------------", "", 0xFFFF },

 { (4 << 16) | SS_SCSP::GSREG_MVOL,	7,	"MVOL",		"Master Volume", 0x100 | 4 },
 { (4 << 16) | SS_SCSP::GSREG_DAC18B,	5,	"DAC18B",	"18-bit DAC Enable", 0x100 | 1 },
 { (4 << 16) | SS_SCSP::GSREG_MEM4MB,	5,	"Mem4Mb",	"4Mb Memory Enable", 0x100 | 1 },
 { (4 << 16) | SS_SCSP::GSREG_MSLC,	6,	"MSLC",		"Slot to Monitor", 0x100 | 5 },

 { (4 << 16) | SS_SCSP::GSREG_SCIEB,	3,	"SCIEB",	"SCIEB", 2 },
 { (4 << 16) | SS_SCSP::GSREG_SCIPD,	3,	"SCIPD",	"SCIPD", 2 },

 { (4 << 16) | SS_SCSP::GSREG_MCIEB,	3,	"MCIEB",	"MCIEB", 2 },
 { (4 << 16) | SS_SCSP::GSREG_MCIPD,	3,	"MCIPD",	"MCIPD", 2 },
/*
 { (6 << 16) | M68K::GSREG_PC,		2,	"PC",		"PC", 4 },
 { (6 << 16) | M68K::GSREG_SR,		6,	"SR",		"SR", 2 },
 { (6 << 16) | M68K::GSREG_SSP,		1,	"SSP",		"SSP", 4 },
 { (6 << 16) | M68K::GSREG_USP,		1,	"USP",		"USP", 4 },
*/
 { (4 << 16) | SS_SCSP::GSREG_RBC,	6,	"RBC",		"DSP Ringbuffer Control", 0x100 | 9 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG0,	2,	"EFREG0",	"DSP Effect Register 0", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG1,	2,	"EFREG1",	"DSP Effect Register 1", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG2,	2,	"EFREG2",	"DSP Effect Register 2", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG3,	2,	"EFREG3",	"DSP Effect Register 3", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG4,	2,	"EFREG4",	"DSP Effect Register 4", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG5,	2,	"EFREG5",	"DSP Effect Register 5", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG6,	2,	"EFREG6",	"DSP Effect Register 6", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG7,	2,	"EFREG7",	"DSP Effect Register 7", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG8,	2,	"EFREG8",	"DSP Effect Register 8", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREG9,	2,	"EFREG9",	"DSP Effect Register 9", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREGA,	2,	"EFREGA",	"DSP Effect Register A", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREGB,	2,	"EFREGB",	"DSP Effect Register B", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREGC,	2,	"EFREGC",	"DSP Effect Register C", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREGD,	2,	"EFREGD",	"DSP Effect Register D", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREGE,	2,	"EFREGE",	"DSP Effect Register E", 2 },
 { (4 << 16) | SS_SCSP::GSREG_EFREGF,	2,	"EFREGF",	"DSP Effect Register F", 2 },

 { 0, 0, "------------", "", 0xFFFF },

 { 0, 0, "", "", 0 },
};

static MDFN_COLD uint32 GetRegister(const unsigned id, char* const special, const uint32 special_len)
{
 switch(id >> 16)
 {
  case 0: return CPU[DBG.ActiveCPU].GetRegister((uint16)id, special, special_len);
  case 1: return CDB_GetRegister((uint16)id, special, special_len);
  case 2: return VDP1::GetRegister((uint16)id, special, special_len);
  case 3: return VDP2::GetRegister((uint16)id, special, special_len);
  case 4: return SOUND_GetSCSPRegister((uint16)id, special, special_len);
  case 5: return SCU_GetRegister((uint16)id, special, special_len);
  case 6: return SOUND_GetM68KRegister((uint16)id, special, special_len);
 }

 return 0;
}

static MDFN_COLD void SetRegister(const unsigned id, const uint32 value)
{
 switch(id >> 16)
 {
  case 0: CPU[DBG.ActiveCPU].SetRegister((uint16)id, value); break;
  case 1: CDB_SetRegister((uint16)id, value); break;
  case 2: VDP1::SetRegister((uint16)id, value); break;
  case 3: VDP2::SetRegister((uint16)id, value); break;
  case 4: SOUND_SetSCSPRegister((uint16)id, value); break;
  case 5: SCU_SetRegister((uint16)id, value); break;
  case 6: SOUND_SetM68KRegister((uint16)id, value); break;
 }
}

static const RegGroupType DBG_RegGroup_CPU =
{
 NULL,
 DBG_Regs_CPU,
 GetRegister,
 SetRegister
};

static const RegGroupType DBG_RegGroup_CPUE_SCSP =
{
 NULL,
 DBG_Regs_CPUE_SCSP,
 GetRegister,
 SetRegister
};


static const RegType DBG_Regs_SCU_CDB_VDP1[] =
{
 { 0, 0, "--------------", "", 0xFFFF },

 { (5 << 16) | SCU_GSREG_ILEVEL,	6,	"ILevel",	"IRL Output to SH2-M", 1 },
 { (5 << 16) | SCU_GSREG_IVEC,		8,	"IVec",		"Pending IRQ Vector Number", 1 },

 { 0, 0, "--------------", "", 0xFFFF },

 { (5 << 16) | SCU_GSREG_IASSERTED,	2,	"IAss",		"Status of Input IRQ Lines", 4 },
 { (5 << 16) | SCU_GSREG_IPENDING,	1,	"IPend",	"Pending IRQs", 4 },
 { (5 << 16) | SCU_GSREG_IMASK,		5,	"IMask",	"IRQ Mask", 2 },

 { 0, 0, "--------------", "", 0xFFFF },

 { (5 << 16) | SCU_GSREG_D0MD,		2,	"D0MD",		"D0MD", 4 },
 { (5 << 16) | SCU_GSREG_D1MD,		2,	"D1MD",		"D1MD", 4 },
 { (5 << 16) | SCU_GSREG_D2MD,		2,	"D2MD",		"D2MD", 4 },

 { 0, 0, "--------------", "", 0xFFFF },

 { (5 << 16) | SCU_GSREG_ASR0_CS0,	2,	"ASR0_CS0",	"A-bus CS0 Config", 2 },
 { (5 << 16) | SCU_GSREG_ASR0_CS1,	2,	"ASR0_CS1",	"A-bus CS1 Config", 2 },
 { (5 << 16) | SCU_GSREG_ASR1_CS2,	2,	"ASR1_CS2",	"A-bus CS2 Config", 2 },
 { (5 << 16) | SCU_GSREG_ASR1_CSD,	2,	"ASR1_CSD",	"A-bus Dummy Config", 2 },

 { (5 << 16) | SCU_GSREG_AREF,		8,	"AREF",		"A-bus Refresh Config", 1 },

 { (5 << 16) | SCU_GSREG_RSEL,		9,	"RSEL",		"SDRAM Size Config", 0x100 | 1 },

 { 0, 0, "--------------", "", 0xFFFF },

 { (5 << 16) | SCU_GSREG_TENBL,		8,	"TENBL",	"Timers Enable", 0x100 | 1 },

 { (5 << 16) | SCU_GSREG_T0CNT,		6,	"T0CNT",	"Timer0 Counter", 0x100 | 10 },
 { (5 << 16) | SCU_GSREG_T0CMP,		6,	"T0CMP",	"Timer0 Compare Value", 0x100 | 10 },
 { (5 << 16) | SCU_GSREG_T0MET,		8,	"T0MET",	"Timer0 Met", 0x100 | 1 },

 { (5 << 16) | SCU_GSREG_T1RLV,		6,	"T1RLV",	"Timer1 Reload Value", 0x100 | 9 },
 { (5 << 16) | SCU_GSREG_T1CNT,		6,	"T1CNT",	"Timer1 Counter", 0x100 | 9 },
 { (5 << 16) | SCU_GSREG_T1MOD,		8,	"T1MOD",	"Timer1 Mode", 0x100 | 1 },
 { (5 << 16) | SCU_GSREG_T1MET,		8,	"T1MET",	"Timer1 Met", 0x100 | 1 },

 { 0, 0, "-----DSP:-----", "", 0xFFFF },

 { (5 << 16) | SCU_GSREG_DSP_EXEC,	9,	"EXEC",		"Executing", 0x100 | 1 },
 { (5 << 16) | SCU_GSREG_DSP_PAUSE,	8,	"PAUSE",	"Paused", 0x100 | 1 },
 { (5 << 16) | SCU_GSREG_DSP_PC,	10,	"PC",		"Program Counter", 1 },
 { (5 << 16) | SCU_GSREG_DSP_END,	10,	"END",		"End Flag", 0x100 | 1 },

 { 0, 0, "--------------", "", 0xFFFF },

 { (1 << 16) | CDB_GSREG_HIRQ,		6,	"HIRQ",		"HIRQ", 2 },
 { (1 << 16) | CDB_GSREG_HIRQ_MASK,	5,	"HIRQM",	"HIRQ Mask", 2 },

 { (1 << 16) | CDB_GSREG_CDATA0,	4,	"CDATA0",	"Command Data 0", 2 },
 { (1 << 16) | CDB_GSREG_CDATA1,	4,	"CDATA1",	"Command Data 1", 2 },
 { (1 << 16) | CDB_GSREG_CDATA2,	4,	"CDATA2",	"Command Data 2", 2 },
 { (1 << 16) | CDB_GSREG_CDATA3,	4,	"CDATA3",	"Command Data 3", 2 },

 { (1 << 16) | CDB_GSREG_RESULT0,	6,	"RES0",		"Result Data 0", 2 },
 { (1 << 16) | CDB_GSREG_RESULT1,	6,	"RES1",		"Result Data 1", 2 },
 { (1 << 16) | CDB_GSREG_RESULT2,	6,	"RES2",		"Result Data 2", 2 },
 { (1 << 16) | CDB_GSREG_RESULT3,	6,	"RES3",		"Result Data 3", 2 },

 { 0, 0, "--------------", "", 0xFFFF },

 { (2 << 16) | VDP1::GSREG_SYSCLIPX,	4,	"SClipX",	"SysClipX", 2 },
 { (2 << 16) | VDP1::GSREG_SYSCLIPY,	4,	"SClipY",	"SysClipY", 2 },
 { (2 << 16) | VDP1::GSREG_USERCLIPX0,	3,	"UClipX0",	"UserClipX0", 2 },
 { (2 << 16) | VDP1::GSREG_USERCLIPY0,	3,	"UClipY0",	"UserClipY0", 2 },
 { (2 << 16) | VDP1::GSREG_USERCLIPX1,	3,	"UClipX1",	"UserClipX1", 2 },
 { (2 << 16) | VDP1::GSREG_USERCLIPY1,	3,	"UClipY1",	"UserClipY1", 2 },
 { (2 << 16) | VDP1::GSREG_LOCALX,	4,	"LocalX",	"LocalX", 2 },
 { (2 << 16) | VDP1::GSREG_LOCALY,	4,	"LocalY",	"LocalY", 2 },

 { (2 << 16) | VDP1::GSREG_TVMR,	8,	"TVMR",		"TVMR", 1 },
 { (2 << 16) | VDP1::GSREG_FBCR,	8,	"FBCR",		"FBCR", 1 },
 { (2 << 16) | VDP1::GSREG_EWDR,	6,	"EWDR",		"EWDR", 2 },
 { (2 << 16) | VDP1::GSREG_EWLR,	6,	"EWLR",		"EWLR", 2 },
 { (2 << 16) | VDP1::GSREG_EWRR,	6,	"EWRR",		"EWRR", 2 },

 { 0, 0, "--------------", "", 0xFFFF },

 { 0, 0, "", "", 0 },
};


static const RegGroupType DBG_RegGroup_SCU_CDB_VDP1 =
{
 NULL,
 DBG_Regs_SCU_CDB_VDP1,
 GetRegister,
 SetRegister
};

//
//

static const RegType DBG_Regs_VDP2[] =
{
 { 0, 0, "-----------", "", 0xFFFF },

 { (3 << 16) | VDP2::GSREG_LINE,	3,	"Line",		"Line", 2 },
 { (3 << 16) | VDP2::GSREG_DON,		7,	"DON",		"Display On", 0x100 | 1 },
 { (3 << 16) | VDP2::GSREG_BM,		8,	"BM",		"Border Mode", 0x100 | 1 },
 { (3 << 16) | VDP2::GSREG_IM,		8,	"IM",		"Interlace Mode", 0x100 | 2 },
 { (3 << 16) | VDP2::GSREG_VRES,	6,	"VRES",		"Vertical Resolution", 0x100 | 2 },
 { (3 << 16) | VDP2::GSREG_HRES,	6,	"HRES",		"Horizontal Resolution", 0x100 | 3 },

 { 0, 0, "-----------", "", 0xFFFF },

 { (3 << 16) | VDP2::GSREG_RAMCTL,	1,	"RAMCTL",	"RAM Control", 2 },
 { (3 << 16) | VDP2::GSREG_CYCA0,	1,	"A0",		"CYCA0", 4 },
 { (3 << 16) | VDP2::GSREG_CYCA1,	1,	"A1",		"CYCA1", 4 },
 { (3 << 16) | VDP2::GSREG_CYCB0,	1,	"B0",		"CYCB0", 4 },
 { (3 << 16) | VDP2::GSREG_CYCB1,	1,	"B1",		"CYCB1", 4 },

 { 0, 0, "-----------", "", 0xFFFF },

 { (3 << 16) | VDP2::GSREG_BGON,	3,	"BGON",		"BGON", 2 },
 { (3 << 16) | VDP2::GSREG_MZCTL,	2,	"MZCTL",	"MZCTL", 2 },
 { (3 << 16) | VDP2::GSREG_SFSEL,	2,	"SFSEL",	"SFSEL", 2 },
 { (3 << 16) | VDP2::GSREG_SFCODE,	1,	"SFCODE",	"SFCODE", 2 },
 { (3 << 16) | VDP2::GSREG_CHCTLA,	1,	"CHCTLA",	"CHCTLA", 2 },
 { (3 << 16) | VDP2::GSREG_CHCTLB,	1,	"CHCTLB",	"CHCTLB", 2 },

 { 0, 0, "-----------", "", 0xFFFF },

 { (3 << 16) | VDP2::GSREG_SCXIN0,	1,	"SCXIN0",	"SCXIN0", 2 },
 { (3 << 16) | VDP2::GSREG_SCXDN0,	1,	"SCXDN0",	"SCXDN0", 2 },
 { (3 << 16) | VDP2::GSREG_SCYIN0,	1,	"SCYIN0",	"SCYIN0", 2 },
 { (3 << 16) | VDP2::GSREG_SCYDN0,	1,	"SCYDN0",	"SCYDN0", 2 },
 { (3 << 16) | VDP2::GSREG_ZMXIN0,	1,	"ZMXIN0",	"ZMXIN0", 2 },
 { (3 << 16) | VDP2::GSREG_ZMXDN0,	1,	"ZMXDN0",	"ZMXDN0", 2 },
 { (3 << 16) | VDP2::GSREG_ZMYIN0,	1,	"ZMYIN0",	"ZMYIN0", 2 },
 { (3 << 16) | VDP2::GSREG_ZMYDN0,	1,	"ZMYDN0",	"ZMYDN0", 2 },

 { (3 << 16) | VDP2::GSREG_SCXIN1,	1,	"SCXIN1",	"SCXIN1", 2 },
 { (3 << 16) | VDP2::GSREG_SCXDN1,	1,	"SCXDN1",	"SCXDN1", 2 },
 { (3 << 16) | VDP2::GSREG_SCYIN1,	1,	"SCYIN1",	"SCYIN1", 2 },
 { (3 << 16) | VDP2::GSREG_SCYDN1,	1,	"SCYDN1",	"SCYDN1", 2 },
 { (3 << 16) | VDP2::GSREG_ZMXIN1,	1,	"ZMXIN1",	"ZMXIN1", 2 },
 { (3 << 16) | VDP2::GSREG_ZMXDN1,	1,	"ZMXDN1",	"ZMXDN1", 2 },
 { (3 << 16) | VDP2::GSREG_ZMYIN1,	1,	"ZMYIN1",	"ZMYIN1", 2 },
 { (3 << 16) | VDP2::GSREG_ZMYDN1,	1,	"ZMYDN1",	"ZMYDN1", 2 },

 { (3 << 16) | VDP2::GSREG_SCYN3,	2,	"SCXN2",	"SCXN2", 2 },
 { (3 << 16) | VDP2::GSREG_SCYN3,	2,	"SCYN2",	"SCYN2", 2 },
 { (3 << 16) | VDP2::GSREG_SCYN3,	2,	"SCXN3",	"SCXN3", 2 },
 { (3 << 16) | VDP2::GSREG_SCYN3,	2,	"SCYN3",	"SCYN3", 2 },

 { (3 << 16) | VDP2::GSREG_ZMCTL,	2,	"ZMCTL",	"ZMCTL", 2 },
 { (3 << 16) | VDP2::GSREG_SCRCTL,	1,	"SCRCTL",	"SCRCTL", 2 },

 { 0, 0, "-----------", "", 0xFFFF },

 { 0, 0, "", "", 0 },
};

static const RegGroupType DBG_RegGroup_VDP2 =
{
 NULL,
 DBG_Regs_VDP2,
 GetRegister,
 SetRegister
};

//
//
//
static MDFN_COLD void DBG_Init(void)
{
 DBG.ActiveCPU = 0;
 DBG.BTEnabled = false;

 for(unsigned which = 0; which < 2; which++)
 {
  DBG.BTIndex[which] = 0;

  for(unsigned i = 0; i < DBGS::NUMBT; i++)
   DBG.BTEntries[which][i].valid = false;
 }

 DBG.CPUHook = NULL;
 DBG.CPUHookContinuous = false;
 DBG.FoundBPoint = false;

 DBG.eff_ts = 0;
 DBG.inss = false;

 MDFNDBG_AddRegGroup(&DBG_RegGroup_CPU);
 MDFNDBG_AddRegGroup(&DBG_RegGroup_CPUE_SCSP);
 MDFNDBG_AddRegGroup(&DBG_RegGroup_SCU_CDB_VDP1);
 MDFNDBG_AddRegGroup(&DBG_RegGroup_VDP2);
 //
 //
 //
 ASpace_Add(GetAddressSpaceBytes<ASPACE_PHYSICAL>, PutAddressSpaceBytes<ASPACE_PHYSICAL>, "physical", "SH-2 External Bus (TODO)", 27);
 ASpace_Add(GetAddressSpaceBytes<ASPACE_WORKRAML>, PutAddressSpaceBytes<ASPACE_WORKRAML>, "workraml", "Low Work RAM", 20);
 ASpace_Add(GetAddressSpaceBytes<ASPACE_WORKRAMH>, PutAddressSpaceBytes<ASPACE_WORKRAMH>, "workramh", "High Work RAM", 20);
 ASpace_Add(GetAddressSpaceBytes<ASPACE_SCSPRAM>, PutAddressSpaceBytes<ASPACE_SCSPRAM>, "scspram", "SCSP RAM", 19);

 ASpace_Add(GetAddressSpaceBytes<ASPACE_DSPPROG>, PutAddressSpaceBytes<ASPACE_DSPPROG>, "dspprog", "DSP Program RAM", 10);

 ASpace_Add(GetAddressSpaceBytes<ASPACE_BACKUP>, PutAddressSpaceBytes<ASPACE_BACKUP>, "backup", "Backup RAM", 15);

 ASpace_Add(GetAddressSpaceBytes<ASPACE_VDP1VRAM>, PutAddressSpaceBytes<ASPACE_VDP1VRAM>, "vdp1vram", "VDP1 VRAM", 19);
 ASpace_Add(GetAddressSpaceBytes<ASPACE_VDP1FB0>, PutAddressSpaceBytes<ASPACE_VDP1FB0>, "vdp1fb0",  "VDP1 FB0 RAM(raw, no 8bpp/rot addr bit swizzling)", 18);
 ASpace_Add(GetAddressSpaceBytes<ASPACE_VDP1FB1>, PutAddressSpaceBytes<ASPACE_VDP1FB1>, "vdp1fb1",  "VDP1 FB1 RAM(raw, no 8bpp/rot addr bit swizzling)", 18);
 ASpace_Add(GetAddressSpaceBytes<ASPACE_VDP2VRAM>, PutAddressSpaceBytes<ASPACE_VDP2VRAM>, "vdp2vram", "VDP2 VRAM", 19);
}

static MDFN_COLD void DBG_Kill(void)
{
 DBG.BreakPointsPC.clear();
 DBG.BreakPointsRead.clear();
 DBG.BreakPointsWrite.clear();
}

static DebuggerInfoStruct DBGInfo
{
 false,
 "SJIS",

 2,
 2,

 32,
 27,

 0x06004000,
 ~0U,        // ZP
 ~0U,        // SP
 NULL,       // GetStackPtr

 DBG_MemPeek,

 DBG_Disassemble,
 DBG_ToggleSyntax,

 NULL,
 NULL,

 DBG_FlushBreakPoints,
 DBG_AddBreakPoint,
 DBG_SetCPUCallback,

 DBG_EnableBranchTrace,
 DBG_GetBranchTrace,

 NULL,
 NULL,

 NULL,
 NULL,
};
#else
static INLINE void DBG_SetEffTS(const sscpu_timestamp_t eff_ts) { }
static INLINE bool DBG_InSlaveStep(void) { return false; }
template<unsigned which> static INLINE void DBG_CPUHandler(void) { }
static INLINE void DBG_AddBranchTrace(unsigned which, uint32 to, int exception, unsigned vecnum = 0) { }
static INLINE bool DBG_NeedCPUHooks(void) { return false; } 
static INLINE void DBG_Init(void) { }
static INLINE void DBG_Kill(void) { }
#endif

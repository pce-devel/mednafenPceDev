/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "pce.h"
#include "input.h"
#include "huc.h"

#include "vce.h"	// For debugging only(but not the debugger :b)

#include "input/gamepad.h"
#include "input/mouse.h"
#include "input/tsushinkb.h"

#include <trio/trio.h>

namespace MDFN_IEN_PCE
{

enum
{
 PCEINPUT_NONE = 0,
 PCEINPUT_GAMEPAD = 1,
 PCEINPUT_MOUSE = 2,
 PCEINPUT_TSUSHINKB = 3,
};

//PCE_Input_Device::PCE_Input_Device(int which)
//{
//
//}

PCE_Input_Device::~PCE_Input_Device()
{

}

void PCE_Input_Device::Power(int32 timestamp)
{


}

void PCE_Input_Device::TransformInput(uint8* data, const bool DisableSR)
{


}

void PCE_Input_Device::AdjustTS(int32 delta)
{

}

void PCE_Input_Device::Write(int32 timestamp, bool old_SEL, bool new_SEL, bool old_CLR, bool new_CLR)
{

}

uint8 PCE_Input_Device::Read(int32 timestamp)
{
 return(0xF);
}

void PCE_Input_Device::Update(const uint8* data)
{

}

int PCE_Input_Device::StateAction(StateMem *sm, int load, int data_only, const char *section_name)
{
 return(1);
}

static PCE_Input_Device *devices[5] = { NULL };
static bool MultiTapEnabled = true;
static int InputTypes[5] = { 0 };
static uint8 *data_ptr[5];
static bool SEL, CLR;
static uint8 read_index = 0;
static bool DisableSR = false;

enum
{
 MB128_STATE_IDLE       = 0,
 MB128_STATE_A1         = 1,
 MB128_STATE_A2         = 2,
 MB128_STATE_REQ        = 3,
 MB128_STATE_ADDR       = 4,
 MB128_STATE_LENBITS    = 5,
 MB128_STATE_READ       = 6,
 MB128_STATE_READTRAIL  = 7,
 MB128_STATE_WRITE      = 8,
 MB128_STATE_WRITETRAIL = 9,
};

static uint8  MB128_Shiftreg  = 0xff;
static bool   MB128_Active    = false;
static uint8  MB128_State     = MB128_STATE_IDLE;
static uint8  MB128_Bitnum    = 0;
static bool   MB128_CmdWrRd   = 0;
static uint32 MB128_Address   = 0;
static uint32 MB128_LenBits   = 0;
static uint8  MB128_RetVal    = 0;

static void RemakeDevices(int which = -1)
{
 int s = 0;
 int e = 5;

 if(which != -1)
 {
  s = which;
  e = which + 1;
 }

 for(int i = s; i < e; i++)
 {
  if(devices[i])
   delete devices[i];
  devices[i] = NULL;

  switch(InputTypes[i])
  {
   default:
   case PCEINPUT_NONE: break;
   case PCEINPUT_GAMEPAD: devices[i] = PCEINPUT_MakeGamepad(); break;
   case PCEINPUT_MOUSE: devices[i] = PCEINPUT_MakeMouse(); break;
   case PCEINPUT_TSUSHINKB: devices[i] = PCEINPUT_MakeTsushinKB(); break;
  }
 }
}


static void SyncSettings(void);

void PCEINPUT_SettingChanged(const char *name)
{
 SyncSettings();
}

void PCEINPUT_Init(void)
{
 SyncSettings();
 RemakeDevices();
}

void PCEINPUT_TransformInput(void)
{
 for(unsigned i = 0; i < 5; i++)
 {
  if(devices[i])
   devices[i]->TransformInput(data_ptr[i], DisableSR);
 }
}

void PCEINPUT_SetInput(unsigned port, const char *type, uint8 *ptr)
{
 assert(port < 5);

 if(!strcmp(type, "gamepad"))
  InputTypes[port] = PCEINPUT_GAMEPAD;
 else if(!strcmp(type, "mouse"))
  InputTypes[port] = PCEINPUT_MOUSE;
 else if(!strcmp(type, "tsushinkb"))
  InputTypes[port] = PCEINPUT_TSUSHINKB;
 else
  InputTypes[port] = PCEINPUT_NONE;

 data_ptr[port] = (uint8 *)ptr;

 RemakeDevices(port);
}

uint16 INPUT_HESHack(void)
{
 uint16 ret = 0;

 switch(InputTypes[0])
 {
  case PCEINPUT_GAMEPAD: ret = MDFN_de16lsb(data_ptr[0]);
			 break;
 }
 return(ret);
}

void INPUT_Frame(void)
{
 for(int i = 0; i < 5; i++)
  if(devices[i])
   devices[i]->Update(data_ptr[i]);
}

void INPUT_AdjustTS(int32 delta_timestamp)
{
 for(int i = 0; i < 5; i++)
  if(devices[i])
   devices[i]->AdjustTS(delta_timestamp);
}

static INLINE uint8 RealPortRead(int32 timestamp, int which)
{
 uint8 ret = 0xF;

 //printf("RealInputRead: %d %d\n", which, timestamp);
 if(devices[which])
  ret = devices[which]->Read(timestamp);

 return(ret);
}

static INLINE void RealPortWrite(int32 timestamp, int which, bool old_SEL, bool new_SEL, bool old_CLR, bool new_CLR)
{
 if(devices[which])
  devices[which]->Write(timestamp, old_SEL, new_SEL, old_CLR, new_CLR);
}

static INLINE uint8 MultiTapRead(int32 timestamp)
{
 uint8 ret = 0xF;

 if(read_index > 4)
  ret = 0;
 else
  ret = RealPortRead(timestamp, read_index);

 return(ret);
}

static INLINE void MultiTapWrite(int32 timestamp, bool old_SEL, bool new_SEL, bool old_CLR, bool new_CLR)
{
 for(int i = 0; i < 5; i++)
  RealPortWrite(timestamp, i, old_SEL, new_SEL, old_CLR, new_CLR);

 // SEL held high, CLR transitions from 0->1, reset counter.
 // Scratch that, only check if SEL is high on the write that changes CLR from 0 to 1, to fix "Strip Fighter".
 if(/*old_SEL &&*/ new_SEL && !old_CLR && new_CLR)
 {
  //puts("Reset read index");

  read_index = 0;
 }
 // CLR held low, SEL transitions from 0->1, increment counter.
 else if(!old_CLR && !new_CLR && !old_SEL && new_SEL)
 {
  //puts("Increment read index");
  if(read_index < 255)
   read_index++;
 }
}


static const uint8 MB128_IDENT = 0x04; // Ident bit in return value

static INLINE void MB128_Send(bool new_SEL)
{
 uint8 temp_byte;
 uint8 temp_mask;

 switch(MB128_State)
 {
   case MB128_STATE_A1:
     MB128_State   = MB128_STATE_A2;
     MB128_RetVal  = new_SEL ? MB128_IDENT : 0;
     break;

   case MB128_STATE_A2:
     MB128_State   = MB128_STATE_REQ;
     MB128_RetVal  = new_SEL ? MB128_IDENT : 0;
     break;
    
   case MB128_STATE_REQ:
     MB128_CmdWrRd = new_SEL;
     MB128_State   = MB128_STATE_ADDR;
     MB128_RetVal  = 0;
     MB128_Bitnum  = 0;
     MB128_Address = 0;
     break;
    
   case MB128_STATE_ADDR:
     MB128_Address |= new_SEL ? (1 << (MB128_Bitnum+7)) : 0;
     MB128_RetVal  = 0;
     MB128_Bitnum += 1;
     if (MB128_Bitnum == 10) {
       MB128_Bitnum  = 0;
       MB128_LenBits = 0;
       MB128_State   = MB128_STATE_LENBITS;
       //printf("Addr = %6.6X\n", MB128_Address);
     }
     break;
    
   case MB128_STATE_LENBITS:
     MB128_LenBits |= new_SEL ? (1 << (MB128_Bitnum)) : 0;
     MB128_RetVal  = 0;
     MB128_Bitnum += 1;
     if (MB128_Bitnum == 20) {
       MB128_Bitnum  = 0;
       if (MB128_CmdWrRd) {   // Read
         MB128_State = (MB128_LenBits == 0) ? MB128_STATE_READTRAIL : MB128_STATE_READ;
       } else {
         MB128_State = (MB128_LenBits == 0) ? MB128_STATE_WRITETRAIL : MB128_STATE_WRITE;
       }
       //printf("Size: Bytes = %4.4X, Bits = %2.2X\n", (MB128_LenBits >> 3), (MB128_LenBits & 7));
     }
     break;
    
   case MB128_STATE_READ:
     MB128_RetVal = (HuC_PeekMB128(MB128_Address) & (1 << MB128_Bitnum)) ? 1 : 0;
     MB128_Bitnum  += 1;
     MB128_LenBits -= 1;
     if (MB128_LenBits == 0) {
       MB128_Bitnum = 0;
       MB128_State  = MB128_STATE_READTRAIL;
       break;
     }
     if (MB128_Bitnum == 8) {
       MB128_Bitnum = 0;
       MB128_Address++;
     }
     break;

   case MB128_STATE_WRITE:
     temp_mask = (1 << MB128_Bitnum);
     temp_byte = HuC_PeekMB128(MB128_Address) & ~temp_mask;
     HuC_PokeMB128(MB128_Address, temp_byte | (new_SEL ? temp_mask : 0));
     MB128_RetVal = 0;
     MB128_Bitnum  += 1;
     MB128_LenBits -= 1;
     if (MB128_LenBits == 0) {
       MB128_Bitnum = 0;
       MB128_State  = MB128_STATE_WRITETRAIL;
       break;
     }
     if (MB128_Bitnum == 8) {
       MB128_Bitnum = 0;
       MB128_Address++;
     }
     break;

   case MB128_STATE_WRITETRAIL:
     MB128_Bitnum += 1;
     if (MB128_Bitnum == 2) {
       MB128_RetVal  = 0;
     }
     if (MB128_Bitnum == 3) {
       MB128_Bitnum  = 0;
       MB128_State  = MB128_STATE_READTRAIL;
     }
     break;

   case MB128_STATE_READTRAIL:
     MB128_Bitnum += 1;
     if (MB128_Bitnum == 2) {
       MB128_RetVal  = 0;
     }
     if (MB128_Bitnum == 4) {
       MB128_Bitnum  = 0;
       MB128_CmdWrRd = 0;
       MB128_Address = 0;
       MB128_LenBits = 0;
       MB128_State  = MB128_STATE_IDLE;
       MB128_Active  = false;
     }
     break;
 }
}

static INLINE void MB128Write(int32 timestamp, bool old_SEL, bool new_SEL, bool old_CLR, bool new_CLR)
{

 if(!old_CLR && new_CLR)
 {
   if (MB128_Active) {
     MB128_Send(new_SEL);
   }
   else
   {
     MB128_Shiftreg = (MB128_Shiftreg>>1) | (new_SEL ? 0x80: 0x00);
     if (MB128_Shiftreg == 0xA8)
     {
       MB128_State = MB128_STATE_A1;
       MB128_Active = true;
       //printf("Active\n");
     }
   }
 }
}

uint8 INPUT_Read(int32 timestamp, unsigned int A)
{
 uint8 ret;

 //{
 // extern VCE *vce;
 // printf("Input Read: %04x, %d\n", A, vce->GetScanlineNo());
 //}

 if(HuC_IsMB128Available() && MB128_Active)
  ret = MB128_RetVal;

 else if(MultiTapEnabled && InputTypes[0] != PCEINPUT_TSUSHINKB)
  ret = MultiTapRead(timestamp);
 else
  ret = RealPortRead(timestamp, 0);

 //printf("Input read: %04x, %02x\n",A,ret);

 if(!PCE_IsCD)
  ret |= 0x80; // Set when CDROM is not attached

 //ret |= 0x40; // PC Engine if set, TG16 if clear.  Let's leave it clear, PC Engine games don't seem to mind if it's clear, but TG16 games barf if it's set.

 ret |= 0x30; // Always-set?

 return(ret);
}

void INPUT_Write(int32 timestamp, unsigned int A, uint8 V)
{
 //printf("Input Write: %04x, %02x\n", A, V);
 bool new_SEL = V & 0x1;
 bool new_CLR = V & 0x2;

 if(HuC_IsMB128Available())
  MB128Write(timestamp, SEL, new_SEL, CLR, new_CLR);

 if(MultiTapEnabled)
  MultiTapWrite(timestamp, SEL, new_SEL, CLR, new_CLR);
 else
  RealPortWrite(timestamp, 0, SEL, new_SEL, CLR, new_CLR);

 SEL = new_SEL;
 CLR = new_CLR;
}

void PCEINPUT_Power(int32 timestamp)
{
 read_index = 0;
 SEL = 0;
 CLR = 0;

 for(int i = 0; i < 5; i++)
  if(devices[i])
   devices[i]->Power(timestamp);
}

int INPUT_StateAction(StateMem *sm, int load, int data_only)
{
 SFORMAT StateRegs[] =
 {
  SFVAR(SEL),
  SFVAR(CLR),
  SFVAR(read_index),
  SFEND
 };

 int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "JOY");

 for(int i = 0; i < 5; i++)
 {
  if(devices[i])
  {
   char sn[8];
   trio_snprintf(sn, 8, "INP%d", i);
   ret &= devices[i]->StateAction(sm, load, data_only, sn);
  }
 }
 
 return(ret);
}

static const std::vector<InputDeviceInfoStruct> InputDeviceInfo =
{
 // None
 {
  "none",
  "none",
  NULL,
  IDII_Empty
 },

 // Gamepad
 {
  "gamepad",
  "Gamepad",
  NULL,
  PCE_GamepadIDII
 },

 // Mouse
 {
  "mouse",
  "Mouse",
  NULL,
  PCE_MouseIDII
 },
};

static const std::vector<InputDeviceInfoStruct> InputDeviceInfoPort1 =
{
 // None
 {
  "none",
  "none",
  NULL,
  IDII_Empty
 },

 // Gamepad
 {
  "gamepad",
  "Gamepad",
  NULL,
  PCE_GamepadIDII
 },

 // Mouse
 {
  "mouse",
  "Mouse",
  NULL,
  PCE_MouseIDII
 },

 // Tsushin Keyboard
 {
  "tsushinkb",
  "Tsushin Keyboard",
  NULL,
  PCE_TsushinKBIDII,
  InputDeviceInfoStruct::FLAG_KEYBOARD
 },
};

const std::vector<InputPortInfoStruct> PCEPortInfo =
{
 { "port1", "Port 1", InputDeviceInfoPort1, "gamepad" },
 { "port2", "Port 2", InputDeviceInfo, "gamepad" },
 { "port3", "Port 3", InputDeviceInfo, "gamepad" },
 { "port4", "Port 4", InputDeviceInfo, "gamepad" },
 { "port5", "Port 5", InputDeviceInfo, "gamepad" },
};

static void SyncSettings(void)
{
 MDFNGameInfo->mouse_sensitivity = MDFN_GetSettingF("pce.mouse_sensitivity");
 MultiTapEnabled = MDFN_GetSettingB("pce.input.multitap");
 DisableSR = MDFN_GetSettingB("pce.disable_softreset");
}

};

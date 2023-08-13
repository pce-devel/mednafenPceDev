/******************************************************************************/
/* Mednafen WonderSwan Emulation Module(based on Cygne)                       */
/******************************************************************************/
/* eeprom.cpp:
**  Copyright (C) 2002 Dox dox@space.pl
**  Copyright (C) 2007-2017 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License version 2.
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

#include "wswan.h"
#include "eeprom.h"
#include "gfx.h"
#include "memory.h"

namespace MDFN_IEN_WSWAN
{

static uint16_t wsGetEepromMask() {
 return wsIsColor() ? 0x7FF : 0x7F;
}

uint8 wsEEPROM[2048];
uint8 iEEPROM[0x800];

static uint8 iEEPROM_Command, EEPROM_Command;
static uint16 iEEPROM_Address, EEPROM_Address;
static uint16 iEEPROM_Data, EEPROM_Data;

uint8 WSwan_EEPROMRead(uint32 A)
{
 switch(A)
 {
  default: printf("Read: %04x\n", A); break;

  case 0xBA: return(iEEPROM_Data >> 0);
  case 0xBB: return(iEEPROM_Data >> 8);
  case 0xBC: return(iEEPROM_Address >> 0);
  case 0xBD: return(iEEPROM_Address >> 8);
  case 0xBE: return iEEPROM_Command | 0xFC;

  case 0xC4: return(EEPROM_Data >> 0);
  case 0xC5: return(EEPROM_Data >> 8);
  case 0xC6: return(EEPROM_Address >> 0);
  case 0xC7: return(EEPROM_Address >> 8);
  case 0xC8: return EEPROM_Command | 0xFC;
 }
 return(0);
}

static void WSwan_EEPROMCommand(uint8 cmd, uint8 *eeprom, uint16 size, uint16 *data_p, uint16 *address_p, uint8 *command_p)
{
 if(cmd & 0x80)
 {
  *command_p |= 0x80;
 }
 if((cmd & 0x70) != 0x40 && (cmd & 0x70) != 0x20 && (cmd & 0x70) != 0x10)
 {
  return;
 }

 *command_p &= 0xFC;

 uint16 data = 0xFFFF;
 if(cmd & 0x20)
 {
  data = *data_p;
 }
 uint16 address = ((*address_p) << 1) & (size - 1);
 uint8 opcode = (*address_p) >> (MDFN_log2(size) - 3);

 bool unlocked = !(*command_p & 0x08);
 if (eeprom == iEEPROM && (*command_p & 0x80) && address >= 0x30)
 {
  unlocked = false;
 }

 if((opcode & 0x1C) == 0x10)
 {
  // extended opcodes
  switch(opcode & 0x03)
  {
  case 0x00: // erase/write disable
    *command_p |= 0x08;
    break;
  case 0x01: // write all
    // TODO: Does this write only the program area when internal EEPROM is protected?
    if (unlocked) for(int i=0;i<size;i++) eeprom[i] = data >> ((i&1)*8);
    break;
  case 0x02: // erase all
    // TODO: Does this erase only the program area when internal EEPROM is protected?
    if (unlocked) memset(eeprom, 0xFF, size);
    break;
  case 0x03: // erase/write enable
    *command_p &= ~0x08;
    break;
  }
 }
 else if((opcode & 0x1C) == 0x18)
 {
  // read
  data = eeprom[address] | (eeprom[address | 1] << 8);
 }
 else if((opcode & 0x1C) == 0x14)
 {
  // write
  if (unlocked)
  {
    eeprom[address] = data;
    eeprom[address | 1] = data >> 8;
  }
 }
 else if((opcode & 0x1C) == 0x1C)
 {
  // erase
  if (unlocked)
  {
    eeprom[address] = 0xFF;
    eeprom[address | 1] = 0xFF;
  }
 }

 if(cmd & 0x70) *command_p |= 0x02;
 if(cmd & 0x10) *command_p |= 0x01;
 if(cmd & 0x10) *data_p = data;
}

void WSwan_EEPROMWrite(uint32 A, uint8 V)
{
 switch(A)
 {
  case 0xBA: iEEPROM_Data &= 0xFF00; iEEPROM_Data |= (V << 0); break;
  case 0xBB: iEEPROM_Data &= 0x00FF; iEEPROM_Data |= (V << 8); break;
  case 0xBC: iEEPROM_Address &= 0xFF00; iEEPROM_Address |= (V << 0); break;
  case 0xBD: iEEPROM_Address &= 0x00FF; iEEPROM_Address |= (V << 8); break;
  case 0xBE: WSwan_EEPROMCommand(V, iEEPROM, wsGetEepromMask() + 1, &iEEPROM_Data, &iEEPROM_Address, &iEEPROM_Command); break;

  case 0xC4: EEPROM_Data &= 0xFF00; EEPROM_Data |= (V << 0); break;
  case 0xC5: EEPROM_Data &= 0x00FF; EEPROM_Data |= (V << 8); break;
  case 0xC6: EEPROM_Address &= 0xFF00; EEPROM_Address |= (V << 0); break;
  case 0xC7: EEPROM_Address &= 0x00FF; EEPROM_Address |= (V << 8); break;
  case 0xC8: WSwan_EEPROMCommand(V, wsEEPROM, eeprom_size, &EEPROM_Data, &EEPROM_Address, &EEPROM_Command); break;
 }
}

void WSwan_EEPROMReset(void)
{
 iEEPROM_Command = EEPROM_Command = 0;
 iEEPROM_Address = EEPROM_Address = 0;
 iEEPROM_Data = EEPROM_Data = 0;
}

void WSwan_EEPROMLock(bool locked)
{
 iEEPROM_Command = (iEEPROM_Command & 0x7F) | (locked ? 0x80 : 0x00);
}

void WSwan_EEPROMInit(const char *Name, const uint16 BYear, const uint8 BMonth, const uint8 BDay, const uint8 Sex, const uint8 Blood)
{
 memset(wsEEPROM, 0, 2048);
 memset(iEEPROM, 0xFF, 0x800);

 // http://perfectkiosk.net/stsws.html#ieep
 // https://github.com/Godzil/splashbuilder

 memset(iEEPROM, 0x00, 0x60); // program data

 for(unsigned int x = 0; x < 16; x++) // owner name
 {
  uint8 zechar = 0;

  if(x < strlen(Name))
  {
   char tc = MDFN_azupper(Name[x]);
   if(tc == ' ') zechar = 0;
   else if(tc == '+') zechar = 0x27;
   else if(tc == '-') zechar = 0x28;
   else if(tc == '?') zechar = 0x29;
   else if(tc == '.') zechar = 0x2A;
   else if(tc >= '0' && tc <= '9') zechar = tc - '0' + 0x1;
   else if(tc >= 'A' && tc <= 'Z') zechar = tc - 'A' + 0xB;
  }
  iEEPROM[0x60 + x] = zechar;
 }

 #define  mBCD16(value) ( (((((value)%100) / 10) <<4)|((value)%10)) | ((((((value / 100)%100) / 10) <<4)|((value / 100)%10))<<8) )
 #define INT16_TO_BCD(A)  ((((((A) % 100) / 10) * 16 + ((A) % 10))) | (((((((A) / 100) % 100) / 10) * 16 + (((A) / 100) % 10))) << 8))   // convert INT16 --> BCD

 uint16 bcd_BYear = INT16_TO_BCD(BYear);

 iEEPROM[0x70] = (bcd_BYear >> 8) & 0xFF; // owner data
 iEEPROM[0x71] = (bcd_BYear >> 0) & 0xFF;
 iEEPROM[0x72] = mBCD(BMonth);
 iEEPROM[0x73] = mBCD(BDay);
 iEEPROM[0x74] = Sex;
 iEEPROM[0x75] = Blood;

 iEEPROM[0x7C] = 0x01; // cartridge changes
 iEEPROM[0x7D] = 0x01; // owner name changes
 iEEPROM[0x7E] = 0x01; // console boot count
 iEEPROM[0x7F] = 0x00;

 iEEPROM[0x83] = 0x03;
}

void WSwan_EEPROMStateAction(StateMem *sm, const unsigned load, const bool data_only)
{
 SFORMAT StateRegs[] =
 {
  SFVAR(iEEPROM_Command),
  SFVAR(iEEPROM_Address),
  SFVAR(EEPROM_Command),
  SFVAR(EEPROM_Address),
  SFVAR(iEEPROM),
  SFPTR8N(eeprom_size ? wsEEPROM : NULL, eeprom_size, "EEPROM"),
  SFEND
 };

 MDFNSS_StateAction(sm, load, data_only, StateRegs, "EEPR");

 if(load)
 {

 }
}

}

/******************************************************************************/
/* Mednafen - Multi-system Emulator                                           */
/******************************************************************************/
/* memdebugger.cpp:
**  Copyright (C) 2007-2021 Mednafen Team
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

#include "main.h"
#include "memdebugger.h"
#include "debugger.h"
#include "prompt.h"
#include <mednafen/FileStream.h>

#include <trio/trio.h>

extern bool WaitForVSYNC;
extern bool WaitForHSYNC;
extern int NeedRun;
extern void Debugger_GT_ResetHSync(void);
extern void Debugger_GT_ResetVSync(void);

void MemDebugger::ICV_Init(const char* newcode)
{
 iconv_t new_ict_game_to_utf8 = (iconv_t)-1, new_ict_utf8_to_game = (iconv_t)-1;

 if((new_ict_game_to_utf8 = iconv_open("UTF-8", newcode)) == (iconv_t)-1 || (new_ict_utf8_to_game = iconv_open(newcode, "UTF-8")) == (iconv_t)-1)
 {
  ErrnoHolder ene(errno);

  if(new_ict_game_to_utf8 != (iconv_t)-1)
   iconv_close(new_ict_game_to_utf8);

  if(new_ict_utf8_to_game != (iconv_t)-1)
   iconv_close(new_ict_utf8_to_game);

  if(errno == EINVAL)
   throw MDFN_Error(ene.Errno(), _("Character set \"%s\" not supported."), newcode);
  else
   throw MDFN_Error(ene.Errno(), _("iconv_open() error: %s"), ene.StrError());
 }

 GameCode = std::string(newcode);
 //
 //
 if(ict_game_to_utf8 != (iconv_t)-1)
 {
  iconv_close(ict_game_to_utf8);
  ict_game_to_utf8 = (iconv_t)-1;
 }

 if(ict_utf8_to_game != (iconv_t)-1)
 {
  iconv_close(ict_utf8_to_game);
  ict_utf8_to_game = (iconv_t)-1;
 }

 ict_game_to_utf8 = new_ict_game_to_utf8;
 ict_utf8_to_game = new_ict_utf8_to_game;
}

class MemDebuggerPrompt : public HappyPrompt
{
        public:

        MemDebuggerPrompt(MemDebugger* memdbg_in, const std::string &ptext, const std::string &zestring) : HappyPrompt(ptext, zestring), memdbg(memdbg_in)
        {

        }
        ~MemDebuggerPrompt()
        {

        }

        private:

        void TheEnd(const std::string &pstring);

	MemDebugger* memdbg;
};

void MemDebuggerPrompt::TheEnd(const std::string &pstring)
{
	memdbg->PromptFinish(pstring);
}


bool MemDebugger::DoBSSearch(const std::vector<uint8>& thebytes)
{
	const size_t byte_count = thebytes.size();
	const uint64 zemod = SizeCache[CurASpace];
        const uint32 start_a = ASpacePos[CurASpace] % zemod;
        uint32 a = start_a;
	bool found = false;
	std::vector<uint8> bbuffer(byte_count, 0);
	 
        do
        {
         ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), a, byte_count, bbuffer.data());
         if(!memcmp(bbuffer.data(), thebytes.data(), byte_count))
         {
          ASpacePos[CurASpace] = a;
          found = true;
          break;
         }
         a = (a + 1) % zemod;
        } while(a != start_a);
	 
	return found;
}

bool MemDebugger::DoRSearch(const std::vector<uint8>& thebytes)
{
	const size_t byte_count = thebytes.size();
	const uint64 zemod = SizeCache[CurASpace];
        const uint32 start_a = (ASpacePos[CurASpace] - 1) % zemod;
        uint32 a = start_a;
	bool found = false;
	std::vector<uint8> bbuffer(byte_count + 1, 0);

        do
        {
         ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), a, byte_count + 1, bbuffer.data());
         bool match = true;

         for(uint32 i = 1; i <= byte_count; i++)
         {
          if(((bbuffer[i] - bbuffer[i - 1]) & 0xFF) != thebytes[i - 1])
          {
           match = false;
           break;
          }
         }
         if(match)
         {
          found = true;
          ASpacePos[CurASpace] = (a + 1) % zemod;
          break;
         }
         a = (a + 1) % zemod;
        } while(a != start_a);
	 
	return found;
}

std::vector<uint8> MemDebugger::TextToBS(const std::string& text)
{
	const size_t text_len = text.size();
	std::vector<uint8> ret((text_len + 1) / 2, 0);
	size_t nib_count = 0;

        for(size_t x = 0; x < text_len; x++)
        {
         int c = MDFN_azlower(text[x]);

         if(c >= '0' && c <= '9')
          c -= '0';
         else if(c >= 'a' && c <= 'f')
         {
          c = c - 'a' + 0xa;
         }
         else if(c == ' ')
         {
          continue;
         }
         else
	  throw MDFN_Error(0, _("Invalid character '%c' in bytestring."), c);

         ret[nib_count >> 1] |= c << ((nib_count & 1) ? 0 : 4);
         nib_count++;
        }

        if(nib_count & 1)
	 throw MDFN_Error(0, _("Invalid number of characters in bytestring."));

	ret.resize(nib_count >> 1);

	return ret;
}

void MemDebugger::PromptFinish(const std::string &pstring)
{
 if(error_string)
 {
  free(error_string);
  error_string = nullptr;
 }

 const PromptType which = InPrompt;
 InPrompt = None;

 try
 {
         if(which == Goto || which == GotoDD)
         {
	  unsigned long long NewAddie;

	  if(trio_sscanf(pstring.c_str(), "%llx", &NewAddie) == 1)
	  {
	   ASpacePos[CurASpace] = (NewAddie * ASpace->Wordbytes);
	   Digitnum = ASpace->MaxDigit;

	   if(which == GotoDD)
	    GoGoPowerDD[CurASpace] = (NewAddie * ASpace->Wordbytes);
	  }
         }
	 else if(which == SetCharset)
	 {
	  ICV_Init(pstring.c_str());

	  MDFNI_SetSetting(std::string(CurGame->shortname) + "." + "debugger.memcharenc", pstring);
	 }
         else if(which == DumpMem)
         {
          uint32 A1, A2, tmpsize;
          char fname[256];
	  bool acceptable = false;

          if(trio_sscanf(pstring.c_str(), "%08x %08x %255[^\r\n]", &A1, &A2, fname) == 3)
	   acceptable = true;
          else if(trio_sscanf(pstring.c_str(), "%08x +%08x %255[^\r\n]", &A1, &tmpsize, fname) == 3)
	  {
	   acceptable = true;
	   A2 = A1 + tmpsize - 1;
	  }

	  if(!acceptable)
	   throw MDFN_Error(0, _("Invalid memory dump specification."));
	  else
          {           
	   FileStream fp(fname, FileStream::MODE_WRITE);
           uint8 write_buffer[256];
           uint64 a;

	   if (ASpace->Wordbytes == 2)
	   {
	    A1 = A1 * 2; // adjust offsets if representing word-size memory
	    A2 = A2 * 2 + 1;
	   }

           a = A1;

	   //printf("%08x %08x\n", A1, A2);

	   while(a <= A2)
	   {
            size_t to_write;
            to_write = A2 - a + 1;
            if(to_write > 256) to_write = 256;

	    ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), a, to_write, write_buffer);

	    fp.write(write_buffer, to_write);
	    a += to_write;
	   }

           fp.close();
          }
         }
         else if(which == LoadMem)
         {
          uint32 A1, A2, tmpsize;
          char fname[256];
          bool acceptable = false;

          if(trio_sscanf(pstring.c_str(), "%08x %08x %255[^\r\n]", &A1, &A2, fname) == 3)
           acceptable = true;
          else if(trio_sscanf(pstring.c_str(), "%08x +%08x %255[^\r\n]", &A1, &tmpsize, fname) == 3)
          {
           acceptable = true;
           A2 = A1 + tmpsize - 1;
          }

	  if(!acceptable)
	   throw MDFN_Error(0, _("Invalid memory load specification."));
          else
          {
           FileStream fp(fname, FileStream::MODE_READ);
	   uint8 read_buffer[256];
	   uint64 a;

	   if (ASpace->Wordbytes == 2)
	   {
	    A1 = A1 * 2; // adjust offsets if representing word-size memory
	    A2 = A2 * 2 + 1;
	   }

	   a = A1;

	   while(a <= A2)
	   {
	    size_t to_read; 
	    size_t read_len;

	    to_read = A2 - a + 1;
	    if(to_read > 256) to_read = 256;

	    read_len = fp.read(read_buffer, to_read, false);

	    if(read_len > 0)
             ASpace->PutAddressSpaceBytes(ASpace->name.c_str(), a, read_len, 1, true, read_buffer);

            a += read_len;

	    if(read_len != to_read)
	    {
	     error_string = trio_aprintf(_("Warning: unexpected EOF(short by %08llx byte(s))"), (unsigned long long)(A2 - a + 1));
	     error_time = -1;
	     break;
	    }
	   }
          }
         }
         else if(which == DumpHex)
         {
          uint32 A1, A2, tmpsize;
          char fname[256];
          bool acceptable = false;

          if(trio_sscanf(pstring.c_str(), "%08x %08x %255[^\r\n]", &A1, &A2, fname) == 3)
           acceptable = true;
          else if(trio_sscanf(pstring.c_str(), "%08x +%08x %255[^\r\n]", &A1, &tmpsize, fname) == 3)
          {
           acceptable = true;
           A2 = A1 + tmpsize - 1;
          }

          if (A2 < A1)
           acceptable = false;

          if(!acceptable)
           throw MDFN_Error(0, _("Invalid hex dump specification."));
          else
          {           
           FileStream fp(fname, FileStream::MODE_WRITE);
           uint8 byte_buffer[16];
           char line_buffer[256];

	   if (ASpace->Wordbytes == 2)
	   {
	    A1 = A1 * 2; // adjust offsets if representing word-size memory
	    A2 = A2 * 2 + 1;
	   }

           const uint64 zemod = SizeCache[CurASpace];

           while(A1 <= A2)
           {
            char *line_pointer = line_buffer;
            size_t to_write;
            to_write = A2 - A1 + 1;
            if(to_write > 8) to_write = 8;

            ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), A1, to_write, byte_buffer);

	    if (ASpace->Wordbytes == 2)
	    {
             line_pointer += trio_snprintf(line_pointer, line_buffer + sizeof(line_buffer) - line_pointer, " %0*X:  .dw ", (std::max<int>(12, 63 - MDFN_lzcount64(round_up_pow2(zemod))) + 3) / 4, (A1 / ASpace->Wordbytes) );
	    }
	    else
	    {
             line_pointer += trio_snprintf(line_pointer, line_buffer + sizeof(line_buffer) - line_pointer, " %0*X:  .db ", (std::max<int>(12, 63 - MDFN_lzcount64(round_up_pow2(zemod))) + 3) / 4, A1);
	    }

            for (size_t i = 0; i < to_write; ++i)
            {
             if (i != 0)
              line_pointer += trio_snprintf(line_pointer, line_buffer + sizeof(line_buffer) - line_pointer - 1, ",");

             if (ASpace->Wordbytes == 2)
	     {
              if (ASpace->Endianness == ENDIAN_LITTLE)
               line_pointer += trio_snprintf(line_pointer, line_buffer + sizeof(line_buffer) - line_pointer, " $%04X", ((byte_buffer[i+1] << 8) + byte_buffer[i]) );
	      else
               line_pointer += trio_snprintf(line_pointer, line_buffer + sizeof(line_buffer) - line_pointer, " $%04X", ((byte_buffer[i] << 8) + byte_buffer[i+1]) );

	      i++;
	     }
	     else
	     {
              line_pointer += trio_snprintf(line_pointer, line_buffer + sizeof(line_buffer) - line_pointer, " $%02X", byte_buffer[i]);
	     }
            }
            line_pointer += trio_snprintf(line_pointer, line_buffer + sizeof(line_buffer) - line_pointer, "\n");

            fp.write(line_buffer, strlen(line_buffer));
            A1 += to_write;
           }
          }
         }
	 else if(which == TextSearch)
	 {
          TS_String = pstring;
	  //
	  std::vector<uint8> thebytes;
          char *inbuf, *outbuf;
          size_t ibl, obl, obl_start;

          ibl = pstring.size();
          obl_start = obl = (ibl + 1) * 8; // Ugly maximum estimation!
          thebytes.resize(obl_start);

          inbuf = (char*)pstring.c_str();
          outbuf = (char*)thebytes.data();

	  if(ict_utf8_to_game != (iconv_t)-1)
	  {
	   iconv(ict_utf8_to_game, NULL, NULL, NULL, NULL);
	   if(iconv(ict_utf8_to_game, (ICONV_CONST char **)&inbuf, &ibl, &outbuf, &obl) == (size_t)-1)
	   {
	    ErrnoHolder ene(errno);

	    throw MDFN_Error(0, _("iconv() error: %s"), ene.StrError());
	   }
	  }

	  thebytes.resize(obl_start - obl);

	  if(!DoBSSearch(thebytes))
	  {
	   error_string = trio_aprintf(_("String not found."));
	   error_time = -1;
	   return;
	  }
	 }
	 else if(which == ByteStringSearch)
	 {
	  BSS_String = pstring;
	  //
	  std::vector<uint8> the_bytes = TextToBS(pstring);

	  if(!DoBSSearch(the_bytes))
          {
           error_string = trio_aprintf(_("Bytestring \"%s\" not found."), pstring.c_str());
           error_time = -1;
           return;
          }
	 }
	 else if(which == RelSearch)
	 {
	  RS_String = pstring;
	  //
	  std::vector<uint8> the_bytes = TextToBS(pstring);

	  if(!DoRSearch(the_bytes))
	  {
           error_string = trio_aprintf(_("Bytestring \"%s\" not found."), pstring.c_str());
           error_time = -1;
           return;
	  }
	 }
 }
 catch(std::exception& e)
 {
  error_string = trio_aprintf("%s", e.what());
  error_time = -1;
 }
}

// Call this function from the game thread.
void MemDebugger::SetActive(bool newia)
{
 if(CurGame->Debugger)
 {
  IsActive = newia;
  if(!newia)
  {
   InEditMode = false;
   Digitnum = ASpace->MaxDigit;
  }
 }
}

//
//
//
INLINE int32 MemDebugger::DrawWaveform(MDFN_Surface* surface, const int32 base_y, const uint32 hcenterw)
{
 const uint32 sample_color = surface->MakeColor(0x00,0xA0,0x00,0xFF);
 const int32 wf_size = SizeCache[CurASpace];
 uint8 waveform[wf_size];
 const int32 pcm_max = (1 << ASpace->WaveBits) - 1;
 int32 xo, yo;
 int32 area_w, area_h;

 xo = 0;
 yo = base_y;

 area_w = 2 + wf_size * 2 + 2;
 area_h = 2 + (pcm_max + 1) + 2;

 xo += (hcenterw - area_w) / 2;

 MDFN_DrawRect(surface, xo, yo, area_w, area_h, surface->MakeColor(0xA0,0xA0,0xA0,0xFF));
 MDFN_DrawFillRect(surface, xo + 1, yo + 1, area_w - 2, area_h - 2, surface->MakeColor(0x30,0x30,0x30,0xFF), surface->MakeColor(0,0,0,0xFF));
 xo += 2;
 yo += 2;

 //
 //
 //
 uint32* pixels = surface->pixels + xo + (yo * surface->pitchinpix);
 ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), 0, wf_size, waveform);

 for(int i = 0; MDFN_LIKELY(i < wf_size); i++)
 {
  int32 delta;
  int32 current;
  int32 previous;

  current = waveform[i];
  previous = waveform[(i + wf_size - 1) % wf_size];

  delta = current - previous;

  for(int y = previous; y != current; y += delta / abs(delta))
   pixels[i * 2 + 0 + ((pcm_max - y)) * surface->pitchinpix] = sample_color;

  pixels[i * 2 + 0 + ((pcm_max - current)) * surface->pitchinpix] = sample_color;
  pixels[i * 2 + 1 + ((pcm_max - current)) * surface->pitchinpix] = sample_color;
 }

 return base_y + area_h;
}

INLINE void MemDebugger::DrawAtCursorInfo(MDFN_Surface* surface, const int32 base_y, const uint32 hcenterw)
{
 static const int32 line_spacing = 16;
 static const unsigned fontid = MDFN_FONT_6x13_12x13;
 char cpstr[64];
 uint8 zebytes[8];
 uint32 tmpval;
 int32 width;
 int32 height;
 int32 tmpx;
 int32 tmpy;
 uint32 notsat = 0;
 uint32 cpplen;
 uint32 cpplen2;
 uint32 cplen;
 int32 mid_x = 400;
 int32 x = 4;
 int32 y = base_y;
 const uint64 asz = SizeCache[CurASpace];
 const int curpos_fw = (std::max<int>(12, 63 - MDFN_lzcount64(round_up_pow2(asz))) + 3) / 4;
 const uint32 curpos = ASpacePos[CurASpace] % asz;

 ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), curpos, 8, zebytes);

 x += 12;
 y += 4;
 trio_snprintf(cpstr, sizeof(cpstr), "< $%0*X / $%llX >", curpos_fw, (curpos / ASpace->Wordbytes), (unsigned long long)(asz / ASpace->Wordbytes) );
 cpplen = DrawText(surface, x, y, "Position:  ", surface->MakeColor(0xa0, 0xa0, 0xFF, 0xFF), fontid);
 cplen = DrawText(surface, x + cpplen, y, cpstr, surface->MakeColor(0xFF, 0xFF, 0xFF, 0xFF), fontid);

 //
 //
 //
 if(GoGoPowerDD[CurASpace])
 {
  char ggddstr[64];

  trio_snprintf(ggddstr, sizeof(ggddstr), "($%0*X+$%0*X)", curpos_fw, (unsigned)GoGoPowerDD[CurASpace], curpos_fw, (unsigned)(curpos - GoGoPowerDD[CurASpace]));
  DrawText(surface, x + cpplen + cplen + 8, y, ggddstr, surface->MakeColor(0xFF, 0x80, 0x80, 0xFF), fontid);
 }
 x += 12;
 y += line_spacing;

 if ( ASpace->Wordbytes == 1)
 {
  tmpval = zebytes[0];
  trio_snprintf(cpstr, sizeof(cpstr), "      $%02x  (%10u, %11d)", tmpval, (uint8)tmpval, (int8)tmpval);
  cpplen = DrawText(surface, x, y, "   1-byte:  ", surface->MakeColor(0xA0, 0xA0, 0xFF, 0xFF), fontid);
  DrawText(surface, x + cpplen, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
 }
 y += line_spacing;

 tmpval = zebytes[0] | (zebytes[1] << 8);
 trio_snprintf(cpstr, sizeof(cpstr), "    $%04x  (%10u, %11d)", tmpval, (uint16)tmpval, (int16)tmpval);
 cpplen = DrawText(surface, x, y, "LE 2-byte:  ", surface->MakeColor(0xA0, 0xA0, 0xFF, 0xFF), fontid);
 DrawText(surface, x + cpplen, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
 y += line_spacing;

 tmpval = zebytes[0] | (zebytes[1] << 8) | (zebytes[2] << 16) | (zebytes[3] << 24);
 trio_snprintf(cpstr, sizeof(cpstr), "$%08x  (%10u, %11d)", tmpval, (uint32)tmpval, (int32)tmpval);
 cpplen = DrawText(surface, x, y, "LE 4-byte:  ", surface->MakeColor(0xA0, 0xA0, 0xFF, 0xFF), fontid);
 DrawText(surface, x + cpplen, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
 y += line_spacing;

 tmpval = zebytes[1] | (zebytes[0] << 8);
 trio_snprintf(cpstr, sizeof(cpstr), "    $%04x  (%10u, %11d)", tmpval, (uint16)tmpval, (int16)tmpval);
 cpplen = DrawText(surface, x, y, "BE 2-byte:  ", surface->MakeColor(0xA0, 0xA0, 0xFF, 0xFF), fontid);
 DrawText(surface, x + cpplen, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
 y += line_spacing;

 tmpval = zebytes[3] | (zebytes[2] << 8) | (zebytes[1] << 16) | (zebytes[0] << 24);
 trio_snprintf(cpstr, sizeof(cpstr), "$%08x  (%10u, %11d)", tmpval, (uint32)tmpval, (int32)tmpval);
 cpplen = DrawText(surface, x, y, "BE 4-byte:  ", surface->MakeColor(0xA0, 0xA0, 0xFF, 0xFF), fontid);
 DrawText(surface, x + cpplen, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
 y += line_spacing;

 trio_snprintf(cpstr, sizeof(cpstr), "%s text:  ", GameCode.c_str());
 cpplen = DrawText(surface, x, y, cpstr, surface->MakeColor(0xA0, 0xA0, 0xFF, 0xFF), fontid);
 {
  char rawbuf[64];
  char textbuf[256];

  ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), curpos, 64, (uint8*)rawbuf);

  size_t ibl, obl, obl_start;
  char *inbuf, *outbuf;
  ibl = 64;
  obl_start = obl = 255; // Hehe, ugly maximum estimation!
  inbuf = rawbuf;
  outbuf = textbuf;

  if(ict_game_to_utf8 != (iconv_t)-1)
  {
   iconv(ict_game_to_utf8, NULL, NULL, NULL, NULL);
   iconv(ict_game_to_utf8, (ICONV_CONST char **)&inbuf, &ibl, &outbuf, &obl);
   textbuf[obl_start - obl] = 0;

   DrawText(surface, x + cpplen, y - 1, textbuf, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), MDFN_FONT_9x18_18x18);
  }
 }

 notsat = ASpace->PossibleSATB ? 0 : 1;

 tmpx = (zebytes[0] | (zebytes[1] << 8)) - 32;
 if ((tmpx < -32) || (tmpx > 1024))
  notsat = 1;

 tmpy = (zebytes[2] | (zebytes[3] << 8)) - 64;
 if ((tmpy < -64) || (tmpy > 1024))
  notsat = 1;


 if (notsat == 0)
 {
  x = mid_x;
  y = base_y;

  x += 12;
  y += 4;
  trio_snprintf(cpstr, sizeof(cpstr), "< $%0*X / $%llX >", curpos_fw, curpos, (unsigned long long)asz);
  cpplen = DrawText(surface, x, y, "SAT:  ", surface->MakeColor(0xa0, 0xa0, 0xFF, 0xFF), fontid);

  x += 12;
  y += line_spacing;

  tmpval = (zebytes[0] | (zebytes[1] << 8)) - 32;
  trio_snprintf(cpstr, sizeof(cpstr), "  %4d  ", tmpval);
  cpplen = DrawText(surface, x, y, "X-Position:   ", surface->MakeColor(0xA0, 0xA0, 0xFF, 0xFF), fontid);
  cpplen2 = DrawText(surface, x + cpplen, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
  width = ((zebytes[7] & 0x01) == 1) ? 32: 16;
  trio_snprintf(cpstr, sizeof(cpstr), "Pix: %d", width);
  cpplen2 = DrawText(surface, x + cpplen + cpplen2, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
  y += line_spacing;

  tmpval = (zebytes[2] | (zebytes[3] << 8)) - 64;
  trio_snprintf(cpstr, sizeof(cpstr), "  %4d  ", tmpval);
  cpplen = DrawText(surface, x, y, "Y-Position:   ", surface->MakeColor(0xA0, 0xA0, 0xFF, 0xFF), fontid);
  cpplen2 = DrawText(surface, x + cpplen, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
  height = 16;
  if ((zebytes[7] & 0x30) == 0x10) {
   height = 32;
  } else if ((zebytes[7] & 0x30) == 0x30) {
   height = 64;
  }
  trio_snprintf(cpstr, sizeof(cpstr), "Pix: %d", height);
  cpplen2 = DrawText(surface, x + cpplen + cpplen2, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
  y += line_spacing;

  tmpval = ((zebytes[4] | (zebytes[5] << 8)) & 0x7ff) << 5 ;
  trio_snprintf(cpstr, sizeof(cpstr), " $%04X  ", tmpval);
  cpplen = DrawText(surface, x, y, "Pattern Addr: ", surface->MakeColor(0xA0, 0xA0, 0xFF, 0xFF), fontid);
  DrawText(surface, x + cpplen, y, cpstr, surface->MakeColor(0xEF, 0xEF, 0xEF, 0xFF), fontid);
  y += line_spacing;

  if (((tmpy + height) <= 0) || ((tmpx + width) <= 0) || (tmpy > 240) || (tmpx > 512)) {
   cpplen = DrawText(surface, x, y, "Off Screen", surface->MakeColor(0xFF, 0x7F, 0x7F, 0xFF), fontid);
  }
  else if (((tmpx < 0) && ((tmpx+width) > 0))) {
   cpplen = DrawText(surface, x, y, "Partly On Screen", surface->MakeColor(0xEF, 0xFF, 0x8F, 0xFF), fontid);
  }
  else if (((tmpy < 0) && ((tmpy+height) > 0))) {
   cpplen = DrawText(surface, x, y, "Partly On Screen", surface->MakeColor(0xEF, 0xFF, 0x8F, 0xFF), fontid);
  }
  else if (((tmpy < 240) && ((tmpy+height) > 240))) {
   cpplen = DrawText(surface, x, y, "Partly On Screen", surface->MakeColor(0xEF, 0xFF, 0x8F, 0xFF), fontid);
  }
  else if ((tmpx+width) > 256) {
   cpplen = DrawText(surface, x, y, "Possibly On Screen", surface->MakeColor(0xFF, 0xDF, 0x7F, 0xFF), fontid);
  }
  else {
   cpplen = DrawText(surface, x, y, "On Screen", surface->MakeColor(0x7F, 0xFF, 0x7F, 0xFF), fontid);
  }

 }
}

static const unsigned addr_font = MDFN_FONT_6x13_12x13; //5x7;
static const int32 addr_left_padding = 4;
static const int32 addr_right_padding = 4;

static const int32 byte_vspacing = 18;

static const unsigned byte_hex_font = MDFN_FONT_6x13_12x13;
static const int32 byte_hex_font_width = 6;
static const int32 byte_hex_spacing = 3 + (byte_hex_font_width * 2);
static const int32 byte_hex_group_pad = 4; //3;
static const int32 byte_hex_right_padding = 4;

static const unsigned byte_char_font = MDFN_FONT_6x13_12x13;
static const int32 byte_char_y_adjust = 0;
static const int32 byte_char_font_width = 6;
static const int32 byte_char_spacing = byte_char_font_width + 1;

static const int32 byte_bpr = 32;	// Bytes per row.
static const int32 byte_minrows = 1;
static const int32 byte_maxrows = 24;
static const int32 byte_prerows = 8;


// Call this function from the game thread
void MemDebugger::Draw(MDFN_Surface *surface, const MDFN_Rect *rect, const MDFN_Rect *screen_rect)
{
 if(!IsActive)
  return;

 const uint32 curtime = Time::MonoMS();
 const MDFN_PixelFormat pf_cache = surface->format;
 int32 text_y = 2;
 const uint64 zemod = SizeCache[CurASpace];
 uint8 wordsize = ASpace->Wordbytes;
 uint8 endian = ASpace->Endianness;

 DrawText(surface, 0, text_y, ASpace->long_name, pf_cache.MakeColor(0x20, 0xFF, 0x20, 0xFF), MDFN_FONT_9x18_18x18, rect->w);
 text_y += 21;

 if(ASpace->IsWave && SizeCache[CurASpace] <= 128 && ASpace->WaveBits <= 6)
 {
  text_y += 4;
  text_y += DrawWaveform(surface, text_y, rect->w);
 }

 uint32 A;
 uint32 Ameow; // A meow for a cat

 if(SizeCache[CurASpace] <= (byte_bpr * byte_maxrows))
  A = 0;
 else
 if(ASpacePos[CurASpace] < (byte_bpr * byte_prerows))
  A = (SizeCache[CurASpace] - (byte_bpr * byte_prerows) + ASpacePos[CurASpace]) % SizeCache[CurASpace];
 else
  A = ASpacePos[CurASpace] - (byte_bpr * byte_prerows);

 Ameow = A - (A % byte_bpr);

 int numrows = zemod / byte_bpr;

 if(numrows > byte_maxrows)
  numrows = byte_maxrows;
 else if(numrows < byte_minrows)
  numrows = byte_minrows;

 for(int y = 0; y < numrows; y++)
 {
  uint8 byte_buffer[byte_bpr];
  char abuf[32];
  uint32 disp_addr;

  Ameow %= zemod;

  ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), Ameow, byte_bpr, byte_buffer);

  if (wordsize == 2)
   disp_addr = Ameow >> 1;
  else
   disp_addr = Ameow;

  trio_snprintf(abuf, 32, "%0*X:", (std::max<int>(12, 63 - MDFN_lzcount64(round_up_pow2(zemod))) + 3) / 4, disp_addr);

  uint32 alen = addr_left_padding;
  uint32 addr_color = pf_cache.MakeColor(0xA0, 0xA0, 0xFF, 0xFF);

  if(Ameow == (ASpacePos[CurASpace] & ~(byte_bpr-1)))
   addr_color = pf_cache.MakeColor(0xB0, 0xC0, 0xFF, 0xFF);

  alen += DrawText(surface, alen, text_y, abuf, addr_color, addr_font);
  alen += addr_right_padding;

  for(uint32 column = 0; column < byte_bpr; column++)
  {
   uint8 r_digit = 0;
   uint8 g_digit = 0;
   uint8 b_digit = 0;

   uint8 y_digit = 0;
   uint8 u_digit = 0;
   uint8 v_digit = 0;

   uint32 bcolor = pf_cache.MakeColor(0xFF, 0xFF, 0xFF, 0xFF);
   uint32 acolor = pf_cache.MakeColor(0xA0, 0xA0, 0xA0, 0xFF);

   uint32 r_color = pf_cache.MakeColor(0xFF, 0xA0, 0xA0, 0xFF);
   uint32 g_color = pf_cache.MakeColor(0xA0, 0xFF, 0xA0, 0xFF);
   uint32 b_color = pf_cache.MakeColor(0xA0, 0xA0, 0xFF, 0xFF);

   uint32 y_color = pf_cache.MakeColor(0xB0, 0xB0, 0xB0, 0xFF);
   uint32 u_color = pf_cache.MakeColor(0xA0, 0xA0, 0xFF, 0xFF);
   uint32 v_color = pf_cache.MakeColor(0xFF, 0xA0, 0xA0, 0xFF);
   uint32 x;

   if((wordsize == 2) && (!InTextArea) && ((column & 0x01) == 1)) // if in hex area, only do even-numbered spots
    column--;

   x = column;
   if (wordsize == 2)
     x = x>>1;

   if (y & 4)
   {
    if(!(x & 1))
    {
     bcolor = (x & 2) ?
      pf_cache.MakeColor(0xFF, 0x80, 0xFF, 0xFF):
      pf_cache.MakeColor(0xA0, 0xFF, 0xFF, 0xFF);
    }
   }
   else
   {
    if(!(x & 1))
    {
     bcolor = (x & 2) ?
      pf_cache.MakeColor(0xA0, 0xFF, 0xFF, 0xFF):
      pf_cache.MakeColor(0xFF, 0x80, 0xFF, 0xFF);
    }
   }

   char quickbuf[32];
   uint32 test_match_pos;
   char ascii_str[4];

   if (wordsize == 2)
   {
    ascii_str[2] = 0;
    ascii_str[0] = byte_buffer[(x*2)];
    ascii_str[1] = byte_buffer[(x*2)+1];

    if((uint8)ascii_str[0] < 0x20 || (uint8)ascii_str[0] >= 128)
     ascii_str[0] = '.';

    if((uint8)ascii_str[1] < 0x20 || (uint8)ascii_str[1] >= 128)
     ascii_str[1] = '.';
   }
   else
   {
    ascii_str[1] = 0;
    ascii_str[0] = byte_buffer[x];

    if((uint8)ascii_str[0] < 0x20 || (uint8)ascii_str[0] >= 128)
     ascii_str[0] = '.';
   }

   if ((ASpace->IsPalette) && (ASpace->PaletteType == PALETTE_PCE))
   {
    uint32 tmp_word = ((byte_buffer[(x*2)+1] << 8) | byte_buffer[(x*2)]);
    g_digit = ((tmp_word >> 6) & 0x07);
    r_digit = ((tmp_word >> 3) & 0x07);
    b_digit = (tmp_word & 0x07);
   }
   else if ((ASpace->IsPalette) && (ASpace->PaletteType == PALETTE_PCFX))
   {
    uint32 tmp_word = ((byte_buffer[(x*2)+1] << 8) | byte_buffer[(x*2)]);
    y_digit = ((tmp_word >> 8) & 0xff);
    u_digit = ((tmp_word >> 4) & 0x0f);
    v_digit = (tmp_word & 0x0f);
   }

   if (wordsize == 2)
   {
    if (endian == ENDIAN_LITTLE)
     trio_snprintf(quickbuf, 32, "%04X", ((byte_buffer[(x*2)+1] << 8) | byte_buffer[(x*2)]));
    else
     trio_snprintf(quickbuf, 32, "%04X", ((byte_buffer[(x*2)] << 8) | byte_buffer[(x*2)+1]));
   }
   else if (wordsize == 1)
   {
    trio_snprintf(quickbuf, 32, "%02X", byte_buffer[x]);
   }

   test_match_pos = ASpacePos[CurASpace] % zemod;

   if(Ameow == test_match_pos)
   {
    if(InEditMode)
    {
     if(curtime & 0x80)
     {
      int pix_offset = alen;

      if(InTextArea)
      {
       pix_offset += byte_bpr * byte_hex_spacing + byte_hex_right_padding + column * byte_char_spacing + ((byte_bpr - 1) / 4) * byte_hex_group_pad;
       DrawText(surface, pix_offset, text_y + byte_char_y_adjust, "▉", pf_cache.MakeColor(0xFF, 0xFF, 0xFF, 0xFF), byte_char_font); 
      }
      else
      {
       if (wordsize == 2)
       {
        pix_offset += ((3-Digitnum) * byte_hex_font_width) + (x*2) * byte_hex_spacing + (x / 4) * byte_hex_group_pad;
       }
       else
       {
        pix_offset += ((1-Digitnum) * byte_hex_font_width) + x * byte_hex_spacing + (x / 4) * byte_hex_group_pad;
       }
       DrawText(surface, pix_offset, text_y, "▉", pf_cache.MakeColor(0xFF, 0xFF, 0xFF, 0xFF), byte_hex_font);
      }

     }
    }
   }

   if ((Ameow == test_match_pos) ||
	((wordsize == 2) && ((Ameow & 0xFFFFFFFE) == (test_match_pos & 0xFFFFFFFE))))  
   {
    if(InTextArea)
    {
     acolor = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);
     bcolor = pf_cache.MakeColor(0xA0, 0xA0, 0xA0, 0xFF);
    }
    else
    {
     acolor = pf_cache.MakeColor(0xFF, 0x80, 0xFF, 0xFF);
     bcolor = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);

     r_color = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);
     g_color = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);
     b_color = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);
     y_color = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);
     u_color = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);
     v_color = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);
    }
   }

   // hex display
   if ((ASpace->IsPalette) && (ASpace->PaletteType == PALETTE_PCE))
   {
     uint32 x_offset = alen + (x*2) * byte_hex_spacing + ((x / 4) * byte_hex_group_pad);

     trio_snprintf(quickbuf, 32, "%1X", g_digit);
     DrawText(surface, x_offset + byte_hex_font_width, text_y, quickbuf, g_color, byte_hex_font);
     trio_snprintf(quickbuf, 32, "%1X", r_digit);
     DrawText(surface, x_offset + (2 * byte_hex_font_width), text_y, quickbuf, r_color, byte_hex_font);
     trio_snprintf(quickbuf, 32, "%1X", b_digit);
     DrawText(surface, x_offset + (3 * byte_hex_font_width), text_y, quickbuf, b_color, byte_hex_font);
   }
   else if ((ASpace->IsPalette) && (ASpace->PaletteType == PALETTE_PCFX))
   {
     uint32 x_offset = alen + (x*2) * byte_hex_spacing + ((x / 4) * byte_hex_group_pad);

     trio_snprintf(quickbuf, 32, "%2.2X", y_digit);
     DrawText(surface, x_offset, text_y, quickbuf, y_color, byte_hex_font);
     trio_snprintf(quickbuf, 32, "%1X", u_digit);
     DrawText(surface, x_offset + (2 * byte_hex_font_width), text_y, quickbuf, u_color, byte_hex_font);
     trio_snprintf(quickbuf, 32, "%1X", v_digit);
     DrawText(surface, x_offset + (3 * byte_hex_font_width), text_y, quickbuf, v_color, byte_hex_font);
   }
   else if (wordsize == 2)
   {
     DrawText(surface, alen + (x*2) * byte_hex_spacing + ((x / 4) * byte_hex_group_pad), text_y, quickbuf, bcolor, byte_hex_font);
   }
   else
   {
     DrawText(surface, alen + x * byte_hex_spacing + ((x / 4) * byte_hex_group_pad), text_y, quickbuf, bcolor, byte_hex_font);
   }

   // ASCII display
   if ((ASpace->IsPalette) && (ASpace->PaletteType == PALETTE_PCE))
   {
    uint32 pal_color = pf_cache.MakeColor(r_digit * 32, g_digit * 32, b_digit * 32, 0xFF);   // approximation
    uint32 border_color = pal_color;

    if ((Ameow == test_match_pos) ||
	((wordsize == 2) && ((Ameow & 0xFFFFFFFE) == (test_match_pos & 0xFFFFFFFE))))  
    {
     border_color = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);
    }

     MDFN_DrawFillRect(surface, alen + byte_bpr * byte_hex_spacing + byte_hex_right_padding + (x*2) * byte_char_spacing + ((byte_bpr - 1) / 4) * byte_hex_group_pad, text_y + byte_char_y_adjust, 2 * byte_char_spacing, byte_vspacing, border_color, pal_color, RectStyle::Rounded);
   }
   if ((ASpace->IsPalette) && (ASpace->PaletteType == PALETTE_PCFX))
   {
    int32 r_temp = (int) ((float)y_digit + (1.402f * ((v_digit << 4) - 128)));
    int32 g_temp = (int) ((float)y_digit - (0.344f * ((u_digit << 4) - 128)) - (0.714f * ((v_digit << 4) - 128)) );
    int32 b_temp = (int) ((float)y_digit + (1.772 * ((u_digit << 4) - 128)));
    if (r_temp > 255) r_temp = 255;
    if (g_temp > 255) g_temp = 255;
    if (b_temp > 255) b_temp = 255;
    if (r_temp < 0) r_temp = 0;
    if (g_temp < 0) g_temp = 0;
    if (b_temp < 0) b_temp = 0;

    uint32 pal_color = pf_cache.MakeColor(r_temp, g_temp, b_temp, 0xFF);   // approximation
    uint32 border_color = pal_color;

    if ((Ameow == test_match_pos) ||
	((wordsize == 2) && ((Ameow & 0xFFFFFFFE) == (test_match_pos & 0xFFFFFFFE))))  
    {
     border_color = pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF);
    }

     MDFN_DrawFillRect(surface, alen + byte_bpr * byte_hex_spacing + byte_hex_right_padding + (x*2) * byte_char_spacing + ((byte_bpr - 1) / 4) * byte_hex_group_pad, text_y + byte_char_y_adjust, 2 * byte_char_spacing, byte_vspacing, border_color, pal_color, RectStyle::Rounded);

   }
   else if (wordsize == 2)
   {
     DrawText(surface, alen + byte_bpr * byte_hex_spacing + byte_hex_right_padding + (x*2) * byte_char_spacing + ((byte_bpr - 1) / 4) * byte_hex_group_pad, text_y + byte_char_y_adjust, ascii_str, acolor, byte_char_font);
   }
   else
   {
     DrawText(surface, alen + byte_bpr * byte_hex_spacing + byte_hex_right_padding + x * byte_char_spacing + ((byte_bpr - 1) / 4) * byte_hex_group_pad, text_y + byte_char_y_adjust, ascii_str, acolor, byte_char_font);
   }
   Ameow++;

   if ((wordsize == 2) && (!InTextArea))
   {
     Ameow++;
     column++;
   }
  }
  text_y += byte_vspacing;
  if ((y & 7) == 7) text_y += 6;
 }

 DrawAtCursorInfo(surface, 23 - 3 + 3 * 6 + byte_maxrows * byte_vspacing, rect->w);
 
 if(InPrompt)
  myprompt->Draw(surface, rect);
 else if(myprompt)
 {
  delete myprompt;
  myprompt = NULL;
 }

 if(error_string)
 {
  if(error_time == -1)
   error_time = curtime;

  if((int32)(curtime - error_time) >= 4000)
  {
   free(error_string);
   error_string = NULL;
  }
  else
  {
   MDFN_DrawFillRect(surface, 0, (rect->h - 13 - 2), rect->w, 13 + 2, pf_cache.MakeColor(0xFF, 0xFF, 0xFF, 0xFF), pf_cache.MakeColor(0, 0, 0, 0xFF));
   DrawText(surface, 0, (rect->h - 13 - 1), error_string, pf_cache.MakeColor(0xFF, 0x00, 0x00, 0xFF), MDFN_FONT_6x13_12x13, rect->w);
  }
 }
}

void MemDebugger::AlignPos(int64 align)
{
 if (align == 1)
  return;

 ASpacePos[CurASpace] = ASpacePos[CurASpace] & (~(align >> 1));
}

void MemDebugger::ChangePos(int64 delta)
{
 int64 prevpos = ASpacePos[CurASpace];
 int64 newpos;

 newpos = prevpos + delta;

 while(newpos < 0)
  newpos += SizeCache[CurASpace];

 newpos %= SizeCache[CurASpace];
 ASpacePos[CurASpace] = newpos;

 Digitnum = ASpace->MaxDigit;
}

// Call this from the game thread
int MemDebugger::Event(const SDL_Event *event)
{
 uint8 wordsize = ASpace->Wordbytes;

 if(!InPrompt && myprompt)
 {
  delete myprompt;
  myprompt = NULL;
 }

 switch(event->type)
 {
  case SDL_TEXTINPUT:
	if(SDL_GetModState() & KMOD_LALT)
  	 break;

	if(PromptTAKC != SDLK_UNKNOWN)
	 break;

	if(InPrompt)
	 myprompt->InsertKBB(event->text.text);
	else if(InEditMode && InTextArea)
	{
	 uint8 to_write[512];
	 size_t ibl, obl, obl_start;
	 const char* inbuf;
	 char* outbuf;

	 ibl = strlen(event->text.text);
	 obl_start = obl = sizeof(to_write);

	 inbuf = event->text.text;
	 outbuf = (char*)to_write;

	 if(ict_utf8_to_game != (iconv_t)-1)
	 {
	  iconv(ict_utf8_to_game, NULL, NULL, NULL, NULL);
	  if(iconv(ict_utf8_to_game, (ICONV_CONST char **)&inbuf, &ibl, &outbuf, &obl) != (size_t)-1)
	  {
           unsigned to_write_len = obl_start - obl;

	   ASpace->PutAddressSpaceBytes(ASpace->name.c_str(), ASpacePos[CurASpace], to_write_len, 1, true, to_write);

           Digitnum = ASpace->MaxDigit;

	   ChangePos(to_write_len);
	  }
	 }
	}
	break;

  case SDL_KEYUP:
	if(PromptTAKC == event->key.keysym.sym)
	 PromptTAKC = SDLK_UNKNOWN;
	break;

  case SDL_KEYDOWN:
	if(PromptTAKC == event->key.keysym.sym && event->key.repeat)
	 PromptTAKC = SDLK_UNKNOWN;

	if(event->key.keysym.mod & KMOD_LALT)
	 break;
	//
	//
	//
	const unsigned ks = event->key.keysym.sym;

        if(InPrompt)
        {
         myprompt->Event(event);
        }
	else if(InEditMode && !InTextArea && ((ks >= SDLK_0 && ks <= SDLK_9)	|| 
	   (ks >= SDLK_a && ks <= SDLK_f)))
	{
         uint8 tc = 0;
         uint8 meowbuf[4];
         uint8 meowbyte = 0;
         uint32 meowword = 0;

         if(event->key.keysym.sym >= SDLK_0 && event->key.keysym.sym <= SDLK_9)
          tc = 0x0 + event->key.keysym.sym - SDLK_0;
         else if(event->key.keysym.sym >= SDLK_a && event->key.keysym.sym <= SDLK_f)
          tc = 0xA + event->key.keysym.sym - SDLK_a;

         if ((ASpace->IsPalette) && (ASpace->PaletteType == PALETTE_PCE))
	 {
          ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), ASpacePos[CurASpace], 2, &meowbuf[0]);
          meowword = ((meowbuf[1] << 8) + meowbuf[0]);
	  if (tc < 8)
	  {
           meowword &= ~(0x7 << (Digitnum * 3));
	   meowword |= tc << (Digitnum * 3);
           
           meowbuf[0] = (meowword & 0xff);
           meowbuf[1] = ((meowword >> 8) & 0xff);

           ASpace->PutAddressSpaceBytes(ASpace->name.c_str(), ASpacePos[CurASpace], 2, 1, true, &meowbuf[0]); // put it back
	  }
	 }
	 else if (wordsize == 2)
	 {
          ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), ASpacePos[CurASpace], 2, &meowbuf[0]);
          meowword = (ASpace->Endianness == ENDIAN_LITTLE) ? ((meowbuf[1] << 8) + meowbuf[0]) : ((meowbuf[0] << 8) + meowbuf[1]);
          meowword &= ~(0xF << (Digitnum * 4));
          meowword |= tc << (Digitnum * 4);
	  if (ASpace->Endianness == ENDIAN_LITTLE)
	  {
           meowbuf[0] = (meowword & 0xff);
           meowbuf[1] = ((meowword >> 8) & 0xff);
	  }
	  else
	  {
           meowbuf[0] = ((meowword >> 8) & 0xff);
           meowbuf[1] = (meowword & 0xff);
	  }
          ASpace->PutAddressSpaceBytes(ASpace->name.c_str(), ASpacePos[CurASpace], 2, 1, true, &meowbuf[0]); // put it back
         }
         else
	 {
          ASpace->GetAddressSpaceBytes(ASpace->name.c_str(), ASpacePos[CurASpace], 1, &meowbyte);
          meowbyte &= ~(0xF << (Digitnum * 4));
          meowbyte |= tc << (Digitnum * 4);
          ASpace->PutAddressSpaceBytes(ASpace->name.c_str(), ASpacePos[CurASpace], 1, 1, true, &meowbyte);
         }
	 

         if (Digitnum-- == 0)
         {
	    ChangePos(wordsize);
            Digitnum = ASpace->MaxDigit;
         }
	}
	else if(InEditMode && InTextArea && ks != SDLK_TAB && ks != SDLK_RETURN && ks != SDLK_INSERT && ks != SDLK_END && ks != SDLK_HOME &&
		ks != SDLK_PAGEUP && ks != SDLK_PAGEDOWN && ks != SDLK_UP && ks != SDLK_DOWN && ks != SDLK_LEFT && ks != SDLK_RIGHT)
	{
	 //
	}
	else switch(event->key.keysym.sym)
	{
	 default: break;

         case SDLK_MINUS: Debugger_GT_ModOpacity(-8);
                          break;
         case SDLK_EQUALS: Debugger_GT_ModOpacity(8);
                           break;
	 case SDLK_BACKSPACE:
		if(InEditMode && !InTextArea)
		{
	         if (++Digitnum > ASpace->MaxDigit)
	         {
		  ChangePos(-wordsize);
	          Digitnum = 0;
	         }
		}
		break;

	 case SDLK_SPACE:
		if(InEditMode && !InTextArea)
		{
	         if (Digitnum-- == 0)
	         {
		  ChangePos(wordsize);
                  Digitnum = ASpace->MaxDigit;
	         }
		}
		break;

	 case SDLK_TAB:
		InTextArea = !InTextArea;
		if (!InTextArea)
		 AlignPos(wordsize);
		if (ASpace->IsPalette)
		 InTextArea = false; 
                Digitnum = ASpace->MaxDigit;
		break;

	 case SDLK_d:
                InPrompt = DumpMem;
                myprompt = new MemDebuggerPrompt(this, "Dump Memory (start end filename)", "");
		PromptTAKC = event->key.keysym.sym;
		break;

	 case SDLK_l:
                InPrompt = LoadMem;
                myprompt = new MemDebuggerPrompt(this, "Load Memory (start end filename)", "");
		PromptTAKC = event->key.keysym.sym;
                break;

	 case SDLK_h:
                InPrompt = DumpHex;
                myprompt = new MemDebuggerPrompt(this, "Hex Dump (start end filename)", "");
		PromptTAKC = event->key.keysym.sym;
		break;

	 case SDLK_s:
		if(event->key.keysym.mod & KMOD_CTRL)
	        {
	         Debugger_GT_ResetVSync();
	         Debugger_GT_ResetHSync();
	         WaitForHSYNC = false;
	         WaitForVSYNC = true;
	         NeedRun = true;
	        }
	        else if(event->key.keysym.mod & KMOD_SHIFT)
	        {
	         Debugger_GT_ResetVSync();
	         Debugger_GT_ResetHSync();
	         WaitForHSYNC = true;
	         WaitForVSYNC = false;
	         NeedRun = true;
	        }
	        else
		{
	         if(SizeCache[CurASpace] > (1 << 24))
                 {
                  error_string = trio_aprintf(_("Address space is too large to search!"));
                  error_time = -1;
                 }
		 else
		 {
		  InPrompt = ByteStringSearch;
		  myprompt = new MemDebuggerPrompt(this, "Byte String Search", BSS_String);
		  PromptTAKC = event->key.keysym.sym;
		 }
		}
		break;

	 case SDLK_r:
		if(SizeCache[CurASpace] > (1 << 24))
                {
                 error_string = trio_aprintf(_("Address space is too large to search!"));
                 error_time = -1;
                }
                else
                {
		 InPrompt = RelSearch;
		 myprompt = new MemDebuggerPrompt(this, "Byte String Relative/Delta Search", RS_String);
		 PromptTAKC = event->key.keysym.sym;
		}
		break;

	 case SDLK_c:
		InPrompt = SetCharset;
		myprompt = new MemDebuggerPrompt(this, "Charset", GameCode);
		PromptTAKC = event->key.keysym.sym;
		break;

         case SDLK_t:
                if(ASpace->TotalBits > 24)
                {
                 error_string = trio_aprintf(_("Address space is too large to search!"));
                 error_time = -1;
                }
                else
                {
                 InPrompt = TextSearch;
                 myprompt = new MemDebuggerPrompt(this, "Text Search", TS_String);
		 PromptTAKC = event->key.keysym.sym;
                }
                break;

	 case SDLK_RETURN:
	 case SDLK_g:
	        if(event->key.keysym.mod & KMOD_SHIFT)
		{
                 InPrompt = GotoDD;
                 myprompt = new MemDebuggerPrompt(this, "Goto Address(DD)", "");
		 PromptTAKC = event->key.keysym.sym;
		}
		else
		{
		 InPrompt = Goto;
		 myprompt = new MemDebuggerPrompt(this, "Goto Address", "");
		 PromptTAKC = event->key.keysym.sym;
		}
		break;

	 case SDLK_INSERT:
		InEditMode = !InEditMode;
                Digitnum = ASpace->MaxDigit;
		break;

	 case SDLK_END: ASpacePos[CurASpace] = (SizeCache[CurASpace] - (byte_bpr * byte_maxrows / 2)) % SizeCache[CurASpace]; 
                        Digitnum = ASpace->MaxDigit;
			break;

	 case SDLK_HOME: ASpacePos[CurASpace] = 0;
                         Digitnum = ASpace->MaxDigit;
			 break;


	 case SDLK_PAGEUP: ChangePos(-byte_bpr * 16); break;
	 case SDLK_PAGEDOWN: ChangePos(byte_bpr * 16); break;
	 case SDLK_UP: ChangePos(-byte_bpr); break;
	 case SDLK_DOWN: ChangePos(byte_bpr); break;

	 case SDLK_LEFT:
		ChangePos(-wordsize);
		break;

	 case SDLK_RIGHT:
		ChangePos(wordsize);
		break;

	 case SDLK_COMMA: 
			if(CurASpace)
			 CurASpace--;
			else
			 CurASpace = AddressSpaces->size() - 1;

			ASpace = &(*AddressSpaces)[CurASpace];
                        Digitnum = ASpace->MaxDigit;
			if (ASpace->IsPalette)
			  InTextArea = false; 
			break;

	 case SDLK_SEMICOLON:
	 case SDLK_PERIOD:
			CurASpace = (CurASpace + 1) % AddressSpaces->size();
			ASpace = &(*AddressSpaces)[CurASpace];
                        Digitnum = ASpace->MaxDigit;
			if (ASpace->IsPalette)
			  InTextArea = false; 
			break;
	}
	break;
 }
 return(1);
}


// Called after a game is loaded.
MemDebugger::MemDebugger() : AddressSpaces(NULL), ASpace(NULL), IsActive(false), CurASpace(0),
			     Digitnum(1), InEditMode(false), InTextArea(false), error_string(NULL), error_time(-1),
			     ict_game_to_utf8((iconv_t)-1), ict_utf8_to_game((iconv_t)-1), InPrompt(None), myprompt(NULL), PromptTAKC(SDLK_UNKNOWN)
{
 if(CurGame->Debugger)
 {
  AddressSpaces = CurGame->Debugger->AddressSpaces;

  CurASpace = 0;
  ASpace = &(*AddressSpaces)[CurASpace];

  size_t num = AddressSpaces->size();

  ASpacePos.resize(num);
  SizeCache.resize(num);
  GoGoPowerDD.resize(num);

  for(size_t i = 0; i < num; i++)
  {
   uint64 tmpsize;

   tmpsize = (*AddressSpaces)[i].NP2Size;

   if(!tmpsize)
    tmpsize = (uint64)1 << (*AddressSpaces)[i].TotalBits;

   SizeCache[i] = tmpsize;
  }

  try
  {
   ICV_Init(MDFN_GetSettingS(std::string(CurGame->shortname) + "." + "debugger.memcharenc").c_str());
  }
  catch(std::exception& e)
  {
   try { ICV_Init(MDFNI_GetSettingDefault(std::string(CurGame->shortname) + "." + "debugger.memcharenc").c_str()); } catch(...) {}

   error_string = trio_aprintf("%s", e.what());
   error_time = -1;
  }
 }
}

MemDebugger::~MemDebugger()
{
 if(ict_game_to_utf8 != (iconv_t)-1)
 {
  iconv_close(ict_game_to_utf8);
  ict_game_to_utf8 = (iconv_t)-1;
 }

 if(ict_utf8_to_game != (iconv_t)-1)
 {
  iconv_close(ict_utf8_to_game);
  ict_utf8_to_game = (iconv_t)-1;
 }

 if(error_string)
 {
  free(error_string);
  error_string = NULL;
 }
}

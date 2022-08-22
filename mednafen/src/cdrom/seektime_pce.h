/*
 ============================================================================
 Name        : seektime.h
 Author      : Dave Shadoff
 Version     :
 Copyright   : (C) 2022 Dave Shadoff
 Description : Program to determine seek time, based on start and end sector numbers
 ============================================================================
 */

extern float get_pce_cd_seek_ms(int start_sector, int target_sector, unsigned transfer_rate);


/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef VIDEO_H
#define VIDEO_H

#define VMODE_TEXT80  0x03
#define VMODE_CGA4    0x04
#define VMODE_EGA16   0x0D

extern void __cdecl vid_setmode(unsigned char mode);

#endif

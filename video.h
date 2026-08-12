/*****************************************************************************

    File: "video.h"
    Date:  20/07/2023
    Copyright (C) 2023, Francisco J A Souza

    This file is part of EspAppleII Project.

    EspAppleII is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    EspAppleII is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*****************************************************************************/

//---------------------------------------------------------------------------

#ifndef Unit_VideoH
#define Unit_VideoH
//---------------------------------------------------------------------------

typedef enum {USA, France, Germany, UK, Denmark1, Sweden, Italy, Spain, Japan, Norway, Denmark2} Charset;

/* BINMANIP */
int bsave(char *filename);
int bload(char *filename);

/* Graphics flags */
#define HRG 1 /* 0001 */
#define PG2 2 /* 0010 */
#define GRX 4 /* 0100 */
#define SPL 8 /* 1000 */

extern int gm;

/* VIDEO */
void virtinit ();
void virtline (unsigned int rastline);
void opengraph(void);

#ifdef SAFE
#define GetAttrib(x) 0
#endif

#endif

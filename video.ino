/*****************************************************************************

    File: "video.ino"
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

#include "video.h"
#include "fabgl.h"
#include "font.h"

int gm;
static  Color COL_HGR0  = Color::Black;
static  Color COL_HGR1  = Color::BrightGreen;
static  Color COL_HGR2  = Color::BrightMagenta;
static  Color COL_HGR3  = Color::BrightWhite;
static  Color COL_HGR4  = Color::Black;
static  Color COL_HGR5  = Color::BrightYellow;
static  Color COL_HGR6  = Color::BrightBlue;
static  Color COL_HGR7  = Color::BrightWhite;

/* even columns */
static Color colourtabe[8]  = {COL_HGR0, COL_HGR0, COL_HGR2, COL_HGR3, COL_HGR0, COL_HGR1, COL_HGR3, COL_HGR3};

/* odd columns */
static Color colourtabo[8]  = {COL_HGR0, COL_HGR0, COL_HGR1, COL_HGR3, COL_HGR0, COL_HGR2, COL_HGR3, COL_HGR3};

/* even columns */
static Color colourtabe2[8] = {COL_HGR4, COL_HGR4, COL_HGR6, COL_HGR7, COL_HGR4, COL_HGR5, COL_HGR7, COL_HGR7};

/* odd columns */
static Color colourtabo2[8] = {COL_HGR4, COL_HGR4, COL_HGR5, COL_HGR7, COL_HGR4, COL_HGR6, COL_HGR7, COL_HGR7};

// Low-Resolution Graphics
//
//  0	Black	      #000000	  0,   0,   0
//  1	Magenta	    #930B7C	147,  11, 124
//  2	Dark Blue	  #1F35D3	 31,  53, 211
//  3	Purple	    #BB36FF	187,  54, 255
//  4	Dark Green	#00760C	  0, 118,  12
//  5	Grey 1	    #7E7E7E	126, 126, 126
//  6	Medium Blue	#07A8E0	  7, 168, 224
//  7	Light Blue	#9DACFF	157, 172, 255
//  8	Brown	      #624C00	 98,  76,   0
//  9	Orange	    #F9561D	249,  86,  29
// 10	Grey 2	    #7E7E7E	126, 126, 126
// 11	Pink	      #FF81EC	255, 129, 236
// 12	Light Green	#43C800	 67, 200,   0
// 13	Yellow	    #DCCD16	220, 205,  22
// 14	Aqua	      #5DF784	 93, 247, 132
// 15	White	      #FFFFFF	255, 255, 255

static unsigned int textAddr[24] = {
		    0x0000,	0x0080,	0x0100,	0x0180,	0x0200,	0x0280,	0x0300,	0x0380,
		    0x0028,	0x00a8,	0x0128,	0x01a8,	0x0228,	0x02a8,	0x0328,	0x03a8,
		    0x0050,	0x00d0,	0x0150,	0x01d0,	0x0250,	0x02d0,	0x0350,	0x03d0,
};

// High-Resolution Graphics
//
// 0	Black 1	#000000	  0,   0,   0
// 1	Green	  #68E043	101, 226,  67
// 2	Purple	#D660EF	214,  96, 239
// 3	White 1	#FFFFFF	255, 255, 255
// 4	Black 2	#000000	  0,   0,   0
// 5	Orange	#E6792E	226, 114,  43
// 6	Blue	  #4BB8F1	 75, 184, 241
// 7	White 2	#FFFFFF	255, 255, 255

static unsigned int hreslineaddr[8] = {
        0x0000, 0x0400, 0x0800, 0x0c00, 0x1000, 0x1400, 0x1800, 0x1c00
};        

static unsigned int hresaddr[24] = {
        0x0000, 0x0080, 0x0100, 0x0180, 0x0200, 0x0280, 0x0300, 0x0380,
        0x0028, 0x00a8, 0x0128, 0x01a8, 0x0228, 0x02a8, 0x0328, 0x03a8,
        0x0050, 0x00d0, 0x0150, 0x01d0, 0x0250, 0x02d0, 0x0350, 0x03d0
};

static unsigned short cachedHiresPage = 0;
static bool hiresCacheValid = false;
static unsigned short cachedTextPage = 0;
static bool textCacheValid = false;
static unsigned char cachedFlashChar = 0;
static unsigned short cachedLoresPage = 0;
static bool loresCacheValid = false;

// The Apple II lo-res values are composite-video colors. VGA16 cannot
// reproduce every shade exactly, but this preserves their color families and
// is far closer than treating every non-black value as white.
static Color const loresPalette[16] = {
  Color::Black,         Color::Magenta,       Color::Blue,          Color::BrightMagenta,
  Color::Green,         Color::White,         Color::Cyan,          Color::BrightBlue,
  Color::Yellow,        Color::BrightRed,     Color::BrightBlack,   Color::BrightMagenta,
  Color::BrightGreen,   Color::BrightYellow,  Color::BrightCyan,    Color::BrightWhite
};

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void virtreset() {

  gm = 0;
  textCacheValid = false;
  loresCacheValid = false;
  hiresCacheValid = false;
  virtsetmode();
} /* virtreset */

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void virtinit() {
  virtreset(); /* reset softswitches */
  virtsetmode(); /* set mode and page for rasterline */
} /* virtinit */

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void virtline(unsigned int rastline)
{
	int x, y;
	unsigned int addr;
	unsigned int val, val1, valBits;
	unsigned char bit, bytes;

	unsigned int displayMode = virtmodedown;
	if (virtsplit && rastline * 8 >= virtsplit)
		displayMode = modetext40;

	switch (displayMode)
	{
		case modetext40:
			/*text 40 column */
			if (cachedFlashChar != flashChar) {
				cachedFlashChar = flashChar;
				textCacheValid = false;
			}
			if (cachedTextPage != virttextpage) {
				cachedTextPage = virttextpage;
				textCacheValid = false;
			}
			addr = textAddr[rastline] + virttextpage;
			for (bytes = 0; bytes < 40; bytes++)
			{
				val = RAM[addr];
				if (!textCacheValid || val != RAM_TXT_BACK[addr - virttextpage])
				{ 
					RAM_TXT_BACK[addr - virttextpage] = val;
					// Draw the character's eight scan lines
					for (int line = 0; line < 8; line++)
					{
						if (val >= 128)   
						{
              // Normal characters
							valBits = AppleFont[(((val + (val - 128<' ' ? 64 : 0)) & 0x7f) << 3) | line];
						}
						else
						{
							if (val >= 64) {
                // Normal characters
                val1 = val - 0x40;  inverse = flashChar;
              }
							else
							//if (val < 64) 
              {
                // Inverse characters
                val1 = val + 0x40; inverse = 0xFF;
              } 
							valBits = AppleFont[(val1 << 3) | line] ^ inverse;
						}

						// Draw the character's seven pixels
						x = bytes * 7;
						y = (rastline *8) + line;
						for (bit = 128; bit > 1; bit = bit >> 1)
						{
							canvas.setPixel(x, y, (valBits & bit) ? COL_HGR7 : COL_HGR0);
							x++; /*next pixel */
						}	// for bit 
					}	// for line
				}	// for bytes
				addr++;
			}
			if (rastline == 23)
				textCacheValid = true;
			return;

		case modelres40:
			/*lores */
			if (cachedLoresPage != virttextpage) {
				cachedLoresPage = virttextpage;
				loresCacheValid = false;
			}
			addr = textAddr[rastline] + virttextpage;
			for (bytes = 0; bytes < 40; bytes++)
			{
			  val = RAM[addr];
				if (loresCacheValid && val == RAM_TXT_BACK[addr - virttextpage]) {
					addr++;
					continue;
				}
				RAM_TXT_BACK[addr - virttextpage] = val;
					// Draw the character's eight scan lines
				for (int line = 0; line < 8; line++)
				{
					if (line < 4)
						val1 = val & 0xf;
					else
						val1 = val >> 4;
					// Draw the character's seven pixels
					x = bytes * 7;
					y = (rastline *8) + line;
					for (bit = 0; bit < 7; bit++)
					{
						canvas.setPixel(x, y, loresPalette[val1]);
						x++; /*next pixel */
					}	// for bit 
				}	// for line
				addr++;
			}
			if (rastline == (virtsplit ? virtsplit / 8 - 1 : 23))
				loresCacheValid = true;
			return;			

		case modehres:
			/*hires */
			if (cachedHiresPage != virthrespage) {
				cachedHiresPage = virthrespage;
				hiresCacheValid = false;
			}
			for (int line = 0; line < 8; line++)
			{
				addr = hresaddr[rastline] + hreslineaddr[line] + virthrespage;
				bool previousByteChanged = false;
				for (bytes = 0; bytes < 40; bytes++)
				{
					unsigned int cacheIndex = addr - virthrespage;
					// Cache exactly the byte used for this draw. The CPU updates RAM on
					// the other core; rereading it after drawing could incorrectly mark
					// a newer, never-rendered value as clean.
					unsigned char currentByte = RAM[addr];
					bool byteChanged = !hiresCacheValid || currentByte != RAM_HGR_BACK[cacheIndex];
					bool nextByteChanged = bytes < 39 &&
						(!hiresCacheValid || RAM[addr + 1] != RAM_HGR_BACK[cacheIndex + 1]);

					// Artifact colors at a byte boundary depend on the neighboring
					// byte. Redraw this group if it or either neighbor changed.
					if (!byteChanged && !previousByteChanged && !nextByteChanged) {
						addr++;
						previousByteChanged = false;
						continue;
					}

					x = bytes * 7;
					y = (rastline *8) + line;
					val = (((unsigned char) RAM[addr - 1] & 0x60) >> 5) |
						    ((currentByte & 0x7f) << 2) |
						    (((unsigned char) RAM[addr + 1] & 0x3) << 9);
					if (bytes == 0)
						val &= 0x7fc; /* drop surrounding bits at left border */
					else if (bytes == 39)
						val &= 0x1ff; /* drop surrounding bits at right border */

					Color const * evenPalette = currentByte & 0x80 ? colourtabe2 : colourtabe;
					Color const * oddPalette = currentByte & 0x80 ? colourtabo2 : colourtabo;
					for (int pixel = 0; pixel < 7; pixel++) {
						Color const * palette = ((x + pixel) & 1) ? oddPalette : evenPalette;
						canvas.setPixel(x + pixel, y, palette[(val >> (pixel + 1)) & 7]);
					}

					RAM_HGR_BACK[cacheIndex] = currentByte;
					previousByteChanged = byteChanged;
					addr++;
				} /*for bytes */
			} /*for lines */
			if (rastline == (virtsplit ? virtsplit / 8 - 1 : 23))
				hiresCacheValid = true;
			return;
	} /*switch */
} /*virtline */ 

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void CheckVideoIO(word Address) {
  switch (Address) {
    //  CLRTEXT =  $C050 ;display graphics 
    case 0xc050 : {
      if (!(gm&GRX)) {
        gm|=GRX;
        virtsetmode();
      }
      return;
    }
    //  SETTEXT =  $C051 ;display text
    case 0xc051 : { 
      if (gm&GRX) {
        gm&=~GRX;
        virtsetmode();
      }
      return;
    }
    //  CLRMIXED = $C052 ;clear mixed mode- enable full graphics 
    case 0xc052 : { 
      if (gm&SPL) {
        gm&=~SPL;
        virtsetmode();
      }
      return;
    }
    //  SETMIXED = $C053 ;enable graphics/text mixed mode
    case 0xc053 : { 
      if (!(gm&SPL)) {
        gm|=SPL;
        virtsetmode();
      }
      return;
    }
    //  PAGE1 =    $C054 ;select text/graphics page1 
    case 0xc054 : { 
      if (gm&PG2) {
        gm &= ~PG2;
        virtsetmode();
      }
      return;
    }
    //  PAGE2 =    $C055 ;select text/graphics page2
    case 0xc055 : { 
      if (!(gm&PG2)) {
        gm |= PG2;
        virtsetmode();
      }
      return;
    }
    //  CLRHIRES = $C056 ;select Lo-res 
    case 0xc056 : { 
      if (gm&HRG) {
         gm&=~HRG;
         virtsetmode();
      }
      return;
    }
    //  SETHIRES = $C057 ;select Hi-res 
    case 0xc057 : { 
      if (!(gm&HRG)) {
         gm|=HRG;
         virtsetmode();
      }
      return;
    }
 }
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void virtsetmode() {

  unsigned int previousMode = virtmodedown;
  unsigned int previousSplit = virtsplit;
  unsigned int previousTextPage = virttextpage;
  unsigned int previousHiresPage = virthrespage;

  if (gm & GRX) {
    /* set the display modes for both parts of the screen */
    if (gm & HRG) {
        virtmodedown = modehres;
        virtsplit = (gm & SPL) ? 160 : 0;
    } else {
      virtmodedown = modelres40;
      virtsplit = (gm & SPL) ? 160 : 0;
    }
  } else {
    virtsplit = 0;
    virtmodedown = modetext40;
  }

  if (!(gm & PG2)) {
    /* set the visible page */
    virttextpage = 0x400;
    virthrespage = 0x2000;
  } else {
    virttextpage = 0x800;
    virthrespage = 0x4000;
  }

  // Another display mode overwrites the VGA framebuffer. Force one complete
  // HGR repaint when returning to graphics, even if Apple video RAM itself did
  // not change while text or lo-res was visible.
  if (previousMode != virtmodedown || previousSplit != virtsplit ||
      previousTextPage != virttextpage || previousHiresPage != virthrespage) {
    textCacheValid = false;
    loresCacheValid = false;
    hiresCacheValid = false;
  }


} /* virtsetmode */

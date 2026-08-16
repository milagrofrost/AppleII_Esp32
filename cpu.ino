/*****************************************************************************

    File: "cpu.ino"
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

// Address Modes
#define AD_IMP	0x01
#define AD_A 	  0x02
#define AD_ABS 	0x03
#define AD_ABSX	0x04
#define AD_ABSY	0x05
#define AD_IMM	0x06
#define AD_IND	0x07
#define AD_INDX	0x08
#define AD_INDY	0x09
#define AD_REL	0x0A
#define AD_ZPG	0x0B
#define AD_ZPGX	0x0C
#define AD_ZPGY	0x0D
#define AD_ZPIND 0x0E
#define AD_ABSINDX 0x0F

// SR Flag Modes
#define FL_Z 	  0x20
#define FL_ZN 	0xA0
#define FL_ZNC	0xB0
#define FL_ALL	0xF0

//Unimplemented ops
#define UNDF	0x00

//Other constants
#define SR_FIXED_BITS 0x20
#define SR_CARRY      0x01
#define SR_ZERO       0x02
#define SR_INT        0x04
#define SR_DEC        0x08
#define SR_BRK        0x10
#define SR_OVER       0x40
#define SR_NEG        0x80

//Stack pointer base address
#define STP_BASE       0x100

//high nibble SR flags, low nibble address mode
const unsigned char flags[] PROGMEM = {
	AD_IMP, FL_ZN|AD_INDX, UNDF, UNDF, UNDF, FL_ZN|AD_ZPG, FL_ZNC|AD_ZPG, UNDF, AD_IMP, FL_ZN|AD_IMM, FL_ZNC|AD_A, UNDF, UNDF, FL_ZN|AD_ABS, FL_ZNC|AD_ABS, UNDF,
	AD_REL, FL_ZN|AD_INDY, UNDF, UNDF, UNDF, FL_ZN|AD_ZPGX, FL_ZNC|AD_ZPGX, UNDF, AD_IMP, FL_ZN|AD_ABSY, UNDF, UNDF, UNDF, FL_ZN|AD_ABSX, FL_ZNC|AD_ABSX, UNDF,
	AD_ABS, FL_ZN|AD_INDX, UNDF, UNDF, FL_Z|AD_ZPG, FL_ZN|AD_ZPG, FL_ZNC|AD_ZPG, UNDF, AD_IMP, FL_ZN|AD_IMM, FL_ZNC|AD_A, UNDF, FL_Z|AD_ABS, FL_ZN|AD_ABS, FL_ZNC|AD_ABS, UNDF,
	AD_REL, FL_ZN|AD_INDY, UNDF, UNDF, UNDF, FL_ZN|AD_ZPGX, FL_ZNC|AD_ZPGX, UNDF, AD_IMP, FL_ZN|AD_ABSY, UNDF, UNDF, UNDF, FL_ZN|AD_ABSX, FL_ZNC|AD_ABSX, UNDF,
	AD_IMP, FL_ZN|AD_INDX, UNDF, UNDF, UNDF, FL_ZN|AD_ZPG, FL_ZNC|AD_ZPG, UNDF, AD_IMP, FL_ZN|AD_IMM, FL_ZNC|AD_A, UNDF, AD_ABS, FL_ZN|AD_ABS, FL_ZNC|AD_ABS, UNDF,
	AD_REL, FL_ZN|AD_INDY, UNDF, UNDF, UNDF, FL_ZN|AD_ZPGX, FL_ZNC|AD_ZPGX, UNDF, AD_IMP, FL_ZN|AD_ABSY, UNDF, UNDF, UNDF, FL_ZN|AD_ABSX, FL_ZNC|AD_ABSX, UNDF,
	AD_IMP, FL_ALL|AD_INDX, UNDF, UNDF, UNDF, FL_ALL|AD_ZPG, FL_ZNC|AD_ZPG, UNDF, FL_ZN|AD_IMP, FL_ALL|AD_IMM, FL_ZNC|AD_A,UNDF, AD_IND, FL_ALL|AD_ABS, FL_ZNC|AD_ABS, UNDF,
	AD_REL, FL_ALL|AD_INDY, UNDF, UNDF, UNDF, FL_ALL|AD_ZPGX, FL_ZNC|AD_ZPGX, UNDF, AD_IMP, FL_ALL|AD_ABSY, UNDF, UNDF, UNDF, FL_ALL|AD_ABSX, FL_ZNC|AD_ABSX, UNDF,
	UNDF, AD_INDX, UNDF, UNDF, AD_ZPG, AD_ZPG, AD_ZPG, UNDF, FL_ZN|AD_IMP, UNDF, FL_ZN|AD_IMP, UNDF, AD_ABS, AD_ABS, AD_ABS, UNDF,
	AD_REL, AD_INDY, UNDF, UNDF, AD_ZPGX, AD_ZPGX, AD_ZPGY, UNDF, FL_ZN|AD_IMP, AD_ABSY, AD_IMP, UNDF, UNDF, AD_ABSX, UNDF, UNDF,
	FL_ZN|AD_IMM, FL_ZN|AD_INDX, FL_ZN|AD_IMM, UNDF, FL_ZN|AD_ZPG, FL_ZN|AD_ZPG, FL_ZN|AD_ZPG, UNDF, FL_ZN|AD_IMP, FL_ZN|AD_IMM, FL_ZN|AD_IMP, UNDF, FL_ZN|AD_ABS, FL_ZN|AD_ABS, FL_ZN|AD_ABS, UNDF,
	AD_REL, FL_ZN|AD_INDY, UNDF, UNDF, FL_ZN|AD_ZPGX, FL_ZN|AD_ZPGX, FL_ZN|AD_ZPGY, UNDF, AD_IMP, FL_ZN|AD_ABSY, FL_ZN|AD_IMP, UNDF, FL_ZN|AD_ABSX, FL_ZN|AD_ABSX, FL_ZN|AD_ABSY, UNDF,
	FL_ZNC|AD_IMM, FL_ZNC|AD_INDX, UNDF, UNDF, FL_ZNC|AD_ZPG, FL_ZNC|AD_ZPG, FL_ZN|AD_ZPG, UNDF, FL_ZN|AD_IMP, FL_ZNC|AD_IMM, FL_ZN|AD_IMP, UNDF, FL_ZNC|AD_ABS, FL_ZNC|AD_ABS,	FL_ZN|AD_ABS, UNDF,
	AD_REL, FL_ZNC|AD_INDY, UNDF, UNDF, UNDF, FL_ZNC|AD_ZPGX, FL_ZN|AD_ZPGX, UNDF, AD_IMP, FL_ZNC|AD_ABSY, UNDF, UNDF, UNDF, FL_ZNC|AD_ABSX, FL_ZN|AD_ABSX, UNDF,
	FL_ZNC|AD_IMM, FL_ALL|AD_INDX, UNDF, UNDF, FL_ZNC|AD_ZPG, FL_ALL|AD_ZPG, FL_ZN|AD_ZPG, UNDF, FL_ZN|AD_IMP, FL_ALL|AD_IMM, AD_IMP, UNDF, FL_ZNC|AD_ABS, FL_ALL|AD_ABS,	FL_ZN|AD_ABS, UNDF,
	AD_REL, FL_ALL|AD_INDY, UNDF, UNDF, UNDF, FL_ALL|AD_ZPGX, FL_ZN|AD_ZPGX, UNDF, AD_IMP, FL_ALL|AD_ABSY, UNDF, UNDF, UNDF, FL_ALL|AD_ABSX, FL_ZN|AD_ABSX, UNDF
};

// NMOS 6502 base cycle counts. Page-crossing and taken-branch penalties are
// added separately by execCode(). Values for unsupported opcodes keep timing
// deterministic if software probes or executes one.
const unsigned char opcodeCycles[256] PROGMEM = {
  7,6,2,8,3,3,5,5,3,2,2,2,4,4,6,6,
  2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
  6,6,2,8,3,3,5,5,4,2,2,2,4,4,6,6,
  2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
  6,6,2,8,3,3,5,5,3,2,2,2,3,4,6,6,
  2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
  6,6,2,8,3,3,5,5,4,2,2,2,5,4,6,6,
  2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
  2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,
  2,6,2,6,4,4,4,4,2,5,2,5,5,5,5,5,
  2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,
  2,5,2,5,4,4,4,4,2,4,2,4,4,4,4,4,
  2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,
  2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
  2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,
  2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7
};

// CPU registers
unsigned short PC;
unsigned char STP = 0xFD, A = 0x00, X = 0x00, Y = 0x00, SR = SR_FIXED_BITS;

//Execution variables
unsigned char opcode, opflags;
unsigned short argument_addr;

//Temporary variables for flag generation
unsigned char value8;
unsigned short value16, value16_2, result;
#if ENABLE_CPU_TRACE
static unsigned int brkTraceCount = 0;
static unsigned int cmosUnhandledTraceCount = 0;
static unsigned int nmosCMOSOpcodeTraceCount = 0;
#endif

static bool IsHandledCMOSOpcode(unsigned char op) {
  switch (op) {
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x12: case 0x32: case 0x52: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
    case 0x34: case 0x3C: case 0x89:
    case 0x64: case 0x74: case 0x9C: case 0x9E:
    case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
    case 0x7C: case 0x80:
    case 0x02: case 0x22: case 0x42: case 0x62: case 0x82: case 0xC2: case 0xE2:
    case 0x44: case 0x54: case 0xD4: case 0xF4: case 0x5C: case 0xDC: case 0xFC:
    case 0xCB: case 0xDB:
      return true;
  }
  return (op & 0x0F) == 0x07 || (op & 0x0F) == 0x0F;
}

static unsigned char NMOSUndefinedAddressMode(unsigned char op) {
  // Stable NMOS 6502 undocumented NOPs still fetch their operand bytes. Many
  // Apple II loaders use $80 as a two-byte NOP; treating every undefined
  // opcode as implied desynchronizes the instruction stream.
  switch (op) {
    // Stable undocumented read/modify/write families: SLO, RLA, SRE, RRA,
    // DCP, and ISC all share the same seven addressing forms.
    case 0x03: case 0x23: case 0x43: case 0x63: case 0xC3: case 0xE3: return AD_INDX;
    case 0x07: case 0x27: case 0x47: case 0x67: case 0xC7: case 0xE7: return AD_ZPG;
    case 0x0F: case 0x2F: case 0x4F: case 0x6F: case 0xCF: case 0xEF: return AD_ABS;
    case 0x13: case 0x33: case 0x53: case 0x73: case 0xD3: case 0xF3: return AD_INDY;
    case 0x17: case 0x37: case 0x57: case 0x77: case 0xD7: case 0xF7: return AD_ZPGX;
    case 0x1B: case 0x3B: case 0x5B: case 0x7B: case 0xDB: case 0xFB: return AD_ABSY;
    case 0x1F: case 0x3F: case 0x5F: case 0x7F: case 0xDF: case 0xFF: return AD_ABSX;
    case 0x83: case 0xA3: return AD_INDX; // SAX/LAX
    case 0x87: case 0xA7: return AD_ZPG;
    case 0x8F: case 0xAF: return AD_ABS;
    case 0x97: case 0xB7: return AD_ZPGY;
    case 0xB3: return AD_INDY;
    case 0xBF: return AD_ABSY;
    case 0x0B: case 0x2B: case 0x4B: case 0x6B: case 0xCB: case 0xEB:
      return AD_IMM;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
      return AD_IMM;
    case 0x04: case 0x44: case 0x64:
      return AD_ZPG;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
      return AD_ZPGX;
    case 0x0C:
      return AD_ABS;
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
      return AD_ABSX;
  }
  return AD_IMP;
}

static unsigned char NMOSUndefinedCycles(unsigned char op) {
  // The base table contains the NMOS timings for stable undocumented
  // operations. NOPs below need explicit addressing-derived timings.
  if (NMOSUndefinedAddressMode(op) != AD_IMP &&
      op != 0x04 && op != 0x0C && op != 0x14 && op != 0x1C &&
      op != 0x34 && op != 0x3C && op != 0x44 && op != 0x54 &&
      op != 0x5C && op != 0x64 && op != 0x74 && op != 0x7C &&
      op != 0x80 && op != 0x82 && op != 0x89 && op != 0xC2 &&
      op != 0xD4 && op != 0xDC && op != 0xE2 && op != 0xF4 && op != 0xFC)
    return pgm_read_byte_near(opcodeCycles + op);
  switch (NMOSUndefinedAddressMode(op)) {
    case AD_ZPG: return 3;
    case AD_ZPGX: return 4;
    case AD_ABS: return 4;
    case AD_ABSX: return 4;
    default: return 2;
  }
}

static bool IsNMOSStableUndocumentedOpcode(unsigned char op) {
  switch (op) {
    case 0x03: case 0x07: case 0x0F: case 0x13: case 0x17: case 0x1B: case 0x1F:
    case 0x23: case 0x27: case 0x2F: case 0x33: case 0x37: case 0x3B: case 0x3F:
    case 0x43: case 0x47: case 0x4F: case 0x53: case 0x57: case 0x5B: case 0x5F:
    case 0x63: case 0x67: case 0x6F: case 0x73: case 0x77: case 0x7B: case 0x7F:
    case 0x83: case 0x87: case 0x8F: case 0x97:
    case 0xA3: case 0xA7: case 0xAF: case 0xB3: case 0xB7: case 0xBF:
    case 0xC3: case 0xC7: case 0xCF: case 0xD3: case 0xD7: case 0xDB: case 0xDF:
    case 0xE3: case 0xE7: case 0xEF: case 0xF3: case 0xF7: case 0xFB: case 0xFF:
    case 0x0B: case 0x2B: case 0x4B: case 0x6B: case 0xCB: case 0xEB:
      return true;
  }
  return false;
}

static bool IsNMOSUndocumentedRMWOpcode(unsigned char op) {
  unsigned char family = op & 0xE0;
  if (family == 0x80 || family == 0xA0) return false;
  switch (op & 0x1F) {
    case 0x03: case 0x07: case 0x0F: case 0x13:
    case 0x17: case 0x1B: case 0x1F:
      return true;
  }
  return false;
}

static unsigned char CMOSAddressMode(unsigned char op) {
  switch (op) {
    case 0x04: case 0x14: case 0x64: return AD_ZPG;
    case 0x0C: case 0x1C: case 0x9C: return AD_ABS;
    case 0x12: case 0x32: case 0x52: return FL_ZN | AD_ZPIND;
    case 0x72: case 0xF2: return FL_ALL | AD_ZPIND;
    case 0x92: return AD_ZPIND;
    case 0xB2: return FL_ZN | AD_ZPIND;
    case 0xD2: return FL_ZNC | AD_ZPIND;
    case 0x34: case 0x74: return AD_ZPGX;
    case 0x3C: case 0x9E: return AD_ABSX;
    case 0x7C: return AD_ABSINDX;
    case 0x80: return AD_REL;
    case 0x89: return AD_IMM;
    case 0x02: case 0x22: case 0x42: case 0x62:
    case 0x82: case 0xC2: case 0xE2: return AD_IMM;
    case 0x44: return AD_ZPG;
    case 0x54: case 0xD4: case 0xF4: return AD_ZPGX;
    case 0x5C: return AD_ABS;
    case 0xDC: case 0xFC: return AD_ABSX;
  }
  if ((op & 0x0F) == 0x07) return AD_ZPG; // RMB/SMB
  return AD_IMP;
}

static unsigned char CMOSCycles(unsigned char op) {
  switch (op) {
    case 0x04: case 0x14: return 5;
    case 0x0C: case 0x1C: return 6;
    case 0x12: case 0x32: case 0x52: case 0x72:
    case 0xB2: case 0xD2: case 0xF2: return 5;
    case 0x92: return 5;
    case 0x34: return 4; case 0x3C: return 4; case 0x89: return 2;
    case 0x64: return 3; case 0x74: return 4; case 0x9C: return 4; case 0x9E: return 5;
    case 0x1A: case 0x3A: case 0x5A: case 0xDA: return 3;
    case 0x7A: case 0xFA: return 4;
    case 0x7C: return 6; case 0x80: return 3;
  }
  if ((op & 0x0F) == 0x07) return 5;
  return 2;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void setflags() {
  // Mask out affected flags
  switch(opflags&0xF0) {
    case 0xA0: SR&=0x7D; break;
    case 0xB0: SR&=0x7C; break;
    case 0x30: SR&=0xFC; break;
    case 0xF0: SR&=0x3C; break;
    case 0x20: SR&=0xFD; break;
  }

  // Set various status flags
  if(opflags&0x80) SR |= (result&0x0080); //negative
  if(opflags&0x20) SR |= (((result&0xFF) == 0)?0x02:0); //zero
  if(opflags&0x10) SR |= ((result&0xFF00)?0x01:0); //carry
  if(opflags&0x40) SR |= ((result^((unsigned short)A))&(result^value16)&0x0080)>>1; 
}

static unsigned char ExecuteADC(unsigned char operand) {
  unsigned char accumulator = A;
  unsigned char carryIn = (SR & SR_CARRY) ? 1 : 0;
  unsigned short binary = (unsigned short) accumulator + operand + carryIn;
  bool overflow = (~(accumulator ^ operand) & (accumulator ^ binary) & 0x80) != 0;

  if (SR & SR_DEC) {
    unsigned short adjusted = binary;
    if ((accumulator & 0x0F) + (operand & 0x0F) + carryIn > 9)
      adjusted += 0x06;
    unsigned short lowAdjusted = adjusted;
    if (adjusted > 0x99)
      adjusted += 0x60;
    A = adjusted & 0xFF;
    SR &= ~(SR_CARRY | SR_ZERO | SR_OVER | SR_NEG);
    if (adjusted > 0xFF) SR |= SR_CARRY;
    if (overflow) SR |= SR_OVER;
    if (Is65C02Mode()) {
      if (A == 0) SR |= SR_ZERO;
      if (A & 0x80) SR |= SR_NEG;
    } else {
      // NMOS 6502 N/Z reflect the binary/intermediate ALU result rather than
      // the final BCD-adjusted accumulator.
      if ((binary & 0xFF) == 0) SR |= SR_ZERO;
      if (lowAdjusted & 0x80) SR |= SR_NEG;
    }
    return Is65C02Mode() ? 1 : 0;
  }

  value16 = operand;
  result = binary;
  setflags();
  A = result & 0xFF;
  return 0;
}

static unsigned char ExecuteSBC(unsigned char operand) {
  unsigned char accumulator = A;
  unsigned char carryIn = (SR & SR_CARRY) ? 1 : 0;
  int binary = (int) accumulator - operand - (carryIn ? 0 : 1);
  unsigned char binaryResult = binary & 0xFF;
  bool overflow = ((accumulator ^ binaryResult) & (accumulator ^ operand) & 0x80) != 0;

  if (SR & SR_DEC) {
    int low = (accumulator & 0x0F) - (operand & 0x0F) - (carryIn ? 0 : 1);
    int high = (accumulator >> 4) - (operand >> 4);
    if (low < 0) {
      low -= 6;
      high--;
    }
    if (high < 0)
      high -= 6;
    A = ((high << 4) | (low & 0x0F)) & 0xFF;
    SR &= ~(SR_CARRY | SR_ZERO | SR_OVER | SR_NEG);
    if (binary >= 0) SR |= SR_CARRY;
    if (overflow) SR |= SR_OVER;
    unsigned char flagResult = Is65C02Mode() ? A : binaryResult;
    if (flagResult == 0) SR |= SR_ZERO;
    if (flagResult & 0x80) SR |= SR_NEG;
    return Is65C02Mode() ? 1 : 0;
  }

  value16 = ((unsigned short) operand) ^ 0x00FF;
  result = (unsigned short) accumulator + value16 + carryIn;
  setflags();
  A = result & 0xFF;
  return 0;
}

static void SetNMOSZN(unsigned char value) {
  SR &= ~(SR_ZERO | SR_NEG);
  if (value == 0) SR |= SR_ZERO;
  if (value & 0x80) SR |= SR_NEG;
}

static bool ExecuteNMOSStableUndocumented(unsigned char op,
                                          unsigned char addressMode,
                                          unsigned char &extraCycles) {
  if (!IsNMOSStableUndocumentedOpcode(op)) return false;

  bool zeroPage = addressMode == AD_ZPG || addressMode == AD_ZPGX ||
                  addressMode == AD_ZPGY;
  unsigned char operand = zeroPage ? readPgz8(argument_addr)
                                   : read8(argument_addr);
  unsigned char family = op & 0xE0;

  // Combined read/modify/write operations. The memory modification happens
  // first, followed by the accumulator operation using the modified byte.
  if (IsNMOSUndocumentedRMWOpcode(op)) {
    unsigned char modified = operand;
    bool carryOut = false;
    if (family == 0x00 || family == 0x20) {       // SLO / RLA
      carryOut = (operand & 0x80) != 0;
      modified = (operand << 1) | (family == 0x20 && (SR & SR_CARRY) ? 1 : 0);
    } else if (family == 0x40 || family == 0x60) { // SRE / RRA
      carryOut = (operand & 0x01) != 0;
      modified = (operand >> 1) | (family == 0x60 && (SR & SR_CARRY) ? 0x80 : 0);
    } else if (family == 0xC0) {                  // DCP
      modified = operand - 1;
    } else if (family == 0xE0) {                  // ISC
      modified = operand + 1;
    }

    if (zeroPage) writePgz8(argument_addr, modified);
    else write8(argument_addr, modified);

    if (family == 0x00) {                         // SLO
      A |= modified;
      SR = (SR & ~SR_CARRY) | (carryOut ? SR_CARRY : 0);
      SetNMOSZN(A);
    } else if (family == 0x20) {                  // RLA
      A &= modified;
      SR = (SR & ~SR_CARRY) | (carryOut ? SR_CARRY : 0);
      SetNMOSZN(A);
    } else if (family == 0x40) {                  // SRE
      A ^= modified;
      SR = (SR & ~SR_CARRY) | (carryOut ? SR_CARRY : 0);
      SetNMOSZN(A);
    } else if (family == 0x60) {                  // RRA
      SR = (SR & ~SR_CARRY) | (carryOut ? SR_CARRY : 0);
      extraCycles += ExecuteADC(modified);
    } else if (family == 0xC0) {                  // DCP
      unsigned char difference = A - modified;
      SR &= ~(SR_CARRY | SR_ZERO | SR_NEG);
      if (A >= modified) SR |= SR_CARRY;
      SetNMOSZN(difference);
    } else {                                      // ISC
      extraCycles += ExecuteSBC(modified);
    }
    return true;
  }

  switch (op) {
    case 0x83: case 0x87: case 0x8F: case 0x97:   // SAX
      if (zeroPage) writePgz8(argument_addr, A & X);
      else write8(argument_addr, A & X);
      return true;

    case 0xA3: case 0xA7: case 0xAF:
    case 0xB3: case 0xB7: case 0xBF:              // LAX
      A = X = operand;
      SetNMOSZN(A);
      return true;

    case 0x0B: case 0x2B:                         // ANC
      A &= operand;
      SetNMOSZN(A);
      SR = (SR & ~SR_CARRY) | ((A & 0x80) ? SR_CARRY : 0);
      return true;

    case 0x4B: {                                  // ALR
      unsigned char combined = A & operand;
      SR = (SR & ~SR_CARRY) | ((combined & 1) ? SR_CARRY : 0);
      A = combined >> 1;
      SetNMOSZN(A);
      return true;
    }

    case 0x6B: {                                  // ARR
      unsigned char combined = A & operand;
      A = (combined >> 1) | ((SR & SR_CARRY) ? 0x80 : 0);
      SetNMOSZN(A);
      SR &= ~(SR_CARRY | SR_OVER);
      if (A & 0x40) SR |= SR_CARRY;
      if (((A >> 6) ^ (A >> 5)) & 1) SR |= SR_OVER;
      return true;
    }

    case 0xCB: {                                  // AXS/SBX immediate
      unsigned char combined = A & X;
      X = combined - operand;
      SR &= ~SR_CARRY;
      if (combined >= operand) SR |= SR_CARRY;
      SetNMOSZN(X);
      return true;
    }

    case 0xEB:                                    // unofficial SBC immediate
      extraCycles += ExecuteSBC(operand);
      return true;
  }
  return false;
}

/***************************************************************************************************************************************/
// Stack functions
/***************************************************************************************************************************************/
void push16(unsigned short pushval) {
  write8(STP_BASE + (STP--), (pushval>>8)&0xFF);
  write8(STP_BASE + (STP--), pushval&0xFF);
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void push8(unsigned char pushval) {
  write8(STP_BASE + (STP--), pushval);
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
unsigned short pull16() {
  unsigned char low = read8(STP_BASE + (++STP));
  unsigned char high = read8(STP_BASE + (++STP));
  value16 = low | ((unsigned short) high << 8);
  return value16;
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
unsigned char pull8() {
  return read8(STP_BASE + (++STP));
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void initCode() {
  // Load the reset vector
  PC = read16(0xFFFC);
  STP = 0xFD;
  virtinit();
  virtsetmode();
  InitDisk(6);
}

/***************************************************************************************************************************************/

/***************************************************************************************************************************************/
void execCode() {

    unsigned char instructionCycles;
    bool pageCrossed = false;

    // Get opcode / addressing mode
    opcode = read8(PC++);
    CPUInstructionOpcode = opcode;
    opflags = flags[opcode];
    instructionCycles = pgm_read_byte_near(opcodeCycles + opcode);
    bool cmosBitBranch = Is65C02Mode() && (opcode & 0x0F) == 0x0F;
    bool baseOpcodeUndefined = opflags == UNDF;
    if (Is65C02Mode() && baseOpcodeUndefined) {
      opflags = CMOSAddressMode(opcode);
      instructionCycles = CMOSCycles(opcode);
#if ENABLE_CPU_TRACE
      if (!IsHandledCMOSOpcode(opcode) && cmosUnhandledTraceCount < 24) {
        DEBUG_PRINTF("[CPU65] unhandled opcode=%02X at PC=%04X\n", opcode, (unsigned short)(PC - 1));
        cmosUnhandledTraceCount++;
      }
#endif
    } else if (!Is65C02Mode() && baseOpcodeUndefined) {
      opflags = NMOSUndefinedAddressMode(opcode);
      instructionCycles = NMOSUndefinedCycles(opcode);
    }

    if (cmosBitBranch) {
      argument_addr = read8(PC++); // zero-page byte to test
      value16_2 = read8(PC++);     // signed relative displacement
      if (value16_2 & 0x80) value16_2 |= 0xFF00;
      instructionCycles = 5;
    }
  
    // Addressing modes
    if (!cmosBitBranch) switch(opflags&0x0F) {
      case AD_IMP: case AD_A: argument_addr = 0xFFFF; break;
      case AD_ABS:
        argument_addr = read16(PC);
        PC += 2;
        break;
      case AD_ABSX:
        value16 = read16(PC);
        argument_addr = value16 + (unsigned short)X;
        pageCrossed = (value16 & 0xFF00) != (argument_addr & 0xFF00);
        PC += 2;
        break;
      case AD_ABSY:
        value16 = read16(PC);
        argument_addr = value16 + (unsigned short)Y;
        pageCrossed = (value16 & 0xFF00) != (argument_addr & 0xFF00);
        PC += 2;
        break;
      case AD_IMM:
        argument_addr = PC++;
        break;
      case AD_IND: {
        argument_addr = read16(PC);
        unsigned short indirectPointer = argument_addr;
        value16 = Is65C02Mode() ? argument_addr + 1
                                : (argument_addr&0xFF00) | ((argument_addr+1)&0x00FF);
        argument_addr = (unsigned short)read8(argument_addr) | ((unsigned short)read8(value16) << 8);
#if ENABLE_CPU_TRACE
        static unsigned char zeroIndirectTraceCount = 0;
        if ((argument_addr == 0 || read8(argument_addr) == 0x00) &&
            zeroIndirectTraceCount < 8) {
          DEBUG_PRINTF("[CPU] suspicious indirect target opcode=%02X at=%04X pointer=%04X target=%04X first=%02X\n",
                       opcode, (unsigned short) (PC - 1), indirectPointer,
                       argument_addr, read8(argument_addr));
          PrintIIeMemoryDiagnostic(indirectPointer);
          zeroIndirectTraceCount++;
        }
#endif
        PC+=2;
        break;
      }
      case AD_INDX:
        argument_addr = ((unsigned short)read8(PC++) + (unsigned short)X)&0xFF;
        value16 = (argument_addr&0xFF00) | ((argument_addr+1)&0x00FF); // Page wrap
        argument_addr = (unsigned short)read8(argument_addr) | ((unsigned short)read8(value16) << 8);
        break;
      case AD_INDY:
        argument_addr = (unsigned short)read8(PC++);
        value16 = (argument_addr&0xFF00) | ((argument_addr+1)&0x00FF); // Page wrap
        argument_addr = (unsigned short)read8(argument_addr) | ((unsigned short)read8(value16) << 8);
        value16 = argument_addr;
        argument_addr += Y;
        pageCrossed = (value16 & 0xFF00) != (argument_addr & 0xFF00);
        break;
      case AD_REL:
        argument_addr = (unsigned short)read8(PC++);
        argument_addr |= ((argument_addr&0x80)?0xFF00:0);
        break;
      case AD_ZPG:
        argument_addr = (unsigned short)read8(PC++);
        break;
      case AD_ZPGX:
        argument_addr = ((unsigned short)read8(PC++) + (unsigned short)X)&0xFF;
        break;
      case AD_ZPGY:
        argument_addr = ((unsigned short)read8(PC++) + (unsigned short)Y)&0xFF;
        break;
      case AD_ZPIND:
        value16 = read8(PC++);
        argument_addr = (unsigned short) readPgz8(value16) |
                        ((unsigned short) readPgz8((value16 + 1) & 0xFF) << 8);
        break;
      case AD_ABSINDX:
        value16 = read16(PC);
        PC += 2;
        value16 += X;
        argument_addr = (unsigned short) read8(value16) |
                        ((unsigned short) read8(value16 + 1) << 8);
        break;
      }

      if (pageCrossed) {
        switch (opcode) {
          case 0x11: case 0x19: case 0x1D:
          case 0x31: case 0x39: case 0x3D:
          case 0x51: case 0x59: case 0x5D:
          case 0x71: case 0x79: case 0x7D:
          case 0xB1: case 0xB9: case 0xBC: case 0xBD: case 0xBE:
          case 0xB3: case 0xBF: // undocumented LAX
          case 0xD1: case 0xD9: case 0xDD:
          case 0xF1: case 0xF9: case 0xFD:
          case 0x1C: case 0x3C: case 0x5C:
          case 0x7C: case 0xDC: case 0xFC:
            instructionCycles++;
            break;
        }
      }

      // CMOS-only opcodes must never reach the 65C02 handlers while running
      // either NMOS profile. Previously an undefined NMOS $80 executed BRA
      // with a stale argument address and jumped into arbitrary memory.
      if (!Is65C02Mode() && baseOpcodeUndefined &&
          ExecuteNMOSStableUndocumented(opcode, opflags & 0x0F,
                                        instructionCycles)) {
        cycle += instructionCycles;
        TotalCycles += instructionCycles;
        return;
      }

      if (!Is65C02Mode() && baseOpcodeUndefined) {
#if ENABLE_CPU_TRACE
        if (IsHandledCMOSOpcode(opcode) && nmosCMOSOpcodeTraceCount < 16) {
          DEBUG_PRINTF("[CPU] NMOS ignored 65C02 opcode=%02X at=%04X\n",
                       opcode, CPUInstructionStartPC);
          nmosCMOSOpcodeTraceCount++;
        }
#endif
        cycle += instructionCycles;
        TotalCycles += instructionCycles;
        return;
      }

      //opcodes
      switch(opcode) {
        // 65C02 additions
        case 0x04: case 0x0C: // TSB
          value8 = read8(argument_addr); result = A & value8;
          SR = (SR & ~SR_ZERO) | ((result & 0xFF) ? 0 : SR_ZERO);
          write8(argument_addr, value8 | A); break;
        case 0x14: case 0x1C: // TRB
          value8 = read8(argument_addr); result = A & value8;
          SR = (SR & ~SR_ZERO) | ((result & 0xFF) ? 0 : SR_ZERO);
          write8(argument_addr, value8 & ~A); break;
        case 0x12: case 0x32: case 0x52: case 0x72:
        case 0xB2: case 0xD2: case 0xF2: {
          value8 = read8(argument_addr);
          if (opcode == 0x12) { result = A | value8; A = result; setflags(); }
          else if (opcode == 0x32) { result = A & value8; A = result; setflags(); }
          else if (opcode == 0x52) { result = A ^ value8; A = result; setflags(); }
          else if (opcode == 0x72) { instructionCycles += ExecuteADC(value8); }
          else if (opcode == 0xB2) { A = value8; result = A; setflags(); }
          else if (opcode == 0xD2) { value16 = ((unsigned short)value8) ^ 0xFF; result = A + value16 + 1; setflags(); }
          else { instructionCycles += ExecuteSBC(value8); }
          break;
        }
        case 0x92: write8(argument_addr, A); break; // STA (zp)
        case 0x34: case 0x3C: case 0x89: // BIT new modes
          value8 = read8(argument_addr); result = A & value8;
          SR = (SR & ~SR_ZERO) | ((result & 0xFF) ? 0 : SR_ZERO);
          if (opcode != 0x89) SR = (SR & 0x3F) | (value8 & 0xC0);
          break;
        case 0x64: case 0x74: case 0x9C: case 0x9E: write8(argument_addr, 0); break;
        case 0x1A: result = ++A; opflags = FL_ZN; setflags(); break;
        case 0x3A: result = --A; opflags = FL_ZN; setflags(); break;
        case 0x5A: push8(Y); break;
        case 0x7A: Y = pull8(); result = Y; opflags = FL_ZN; setflags(); break;
        case 0xDA: push8(X); break;
        case 0xFA: X = pull8(); result = X; opflags = FL_ZN; setflags(); break;
        case 0x7C: PC = argument_addr; break;
        case 0x80: value16 = PC; PC += argument_addr; instructionCycles += ((value16 & 0xFF00) != (PC & 0xFF00)); break;
        case 0x07: case 0x17: case 0x27: case 0x37:
        case 0x47: case 0x57: case 0x67: case 0x77:
        case 0x87: case 0x97: case 0xA7: case 0xB7:
        case 0xC7: case 0xD7: case 0xE7: case 0xF7: {
          unsigned char mask = 1 << ((opcode >> 4) & 7);
          value8 = readPgz8(argument_addr);
          writePgz8(argument_addr, opcode & 0x80 ? value8 | mask : value8 & ~mask);
          break;
        }
        case 0x0F: case 0x1F: case 0x2F: case 0x3F:
        case 0x4F: case 0x5F: case 0x6F: case 0x7F:
        case 0x8F: case 0x9F: case 0xAF: case 0xBF:
        case 0xCF: case 0xDF: case 0xEF: case 0xFF: {
          unsigned char mask = 1 << ((opcode >> 4) & 7);
          bool set = (readPgz8(argument_addr) & mask) != 0;
          if (set == ((opcode & 0x80) != 0)) {
            value16 = PC; PC += value16_2;
            instructionCycles += 1 + ((value16 & 0xFF00) != (PC & 0xFF00));
          }
          break;
        }
        //ADC
        case 0x65:
          instructionCycles += ExecuteADC(readPgz8(argument_addr));
          break;
        case 0x69: case 0x75:
        case 0x6D: case 0x7D: case 0x79:
        case 0x61: case 0x71:
          instructionCycles += ExecuteADC(read8(argument_addr));
          break;
        //AND
        case 0x25: case 0x35:
          result = A&readPgz8(argument_addr);
          A = result&0xFF;
          setflags();
          break;
        case 0x29:
        case 0x2D: case 0x3D: case 0x39:
        case 0x21: case 0x31:
          result = A&read8(argument_addr);
          A = result&0xFF;
          setflags();
          break;
        //ASL A
        case 0x0A:
          value16 = (unsigned short)A;
          result = value16<<1;
          setflags();
          A = result&0xFF;
          break;
        //ASL
        case 0x06: case 0x16:
          value16 = readPgz8(argument_addr);
          result = value16<<1;
          setflags();
          write8(argument_addr, result&0xFF);
          break;
        case 0x0E: case 0x1E:
          value16 = read8(argument_addr);
          result = value16<<1;
          setflags();
          write8(argument_addr, result&0xFF);
          break;
        //BCC
        case 0x90:
          if(!(SR&SR_CARRY)) {
            value16 = PC;
            PC += argument_addr;
            instructionCycles += 1 + ((value16 & 0xFF00) != (PC & 0xFF00));
          }
          break;
        //BCS
        case 0xB0:
          if((SR&SR_CARRY)) {
            value16 = PC;
            PC += argument_addr;
            instructionCycles += 1 + ((value16 & 0xFF00) != (PC & 0xFF00));
          }
          break;
        //BEQ
        case 0xF0:
          if((SR&SR_ZERO)) {
            value16 = PC;
            PC += argument_addr;
            instructionCycles += 1 + ((value16 & 0xFF00) != (PC & 0xFF00));
          }
          break;
        //BNE
        case 0xD0:
          if(!(SR&SR_ZERO)) {
            value16 = PC;
            PC += argument_addr;
            instructionCycles += 1 + ((value16 & 0xFF00) != (PC & 0xFF00));
          }
          break;
        //BIT
        case 0x24:
          value8 = readPgz8(argument_addr);
          result = A & value8;
          setflags();
          SR = (SR&0x3F) | (value8&0xC0);
          break;
        case 0x2C:
          value8 = read8(argument_addr);
          result = A & value8;
          setflags();
          SR = (SR&0x3F) | (value8&0xC0);
          break;
        //BMI
        case 0x30:
          if((SR&SR_NEG)) {
            value16 = PC;
            PC += argument_addr;
            instructionCycles += 1 + ((value16 & 0xFF00) != (PC & 0xFF00));
          }
          break;
        //BPL
        case 0x10:
          if(!(SR&SR_NEG)) {
            value16 = PC;
            PC += argument_addr;
            instructionCycles += 1 + ((value16 & 0xFF00) != (PC & 0xFF00));
          }
          break;
        //BRK
        case 0x00:
#if ENABLE_CPU_TRACE
          if (brkTraceCount < 24) {
            unsigned short origin = PC - 1;
            unsigned short vector = read16(0xFFFE);
            DEBUG_PRINTF("[CPU] BRK at %04X next=%02X vector=%04X SP=%02X SR=%02X total=%llu base=%llu\n",
                         origin, read8(PC), vector, STP, SR,
                         TotalCycles, EmulationTimingBaseCycles);
            if (brkTraceCount == 0) {
              DEBUG_PRINT("[CPU] pre-BRK history:");
              for (int historyOffset = 0; historyOffset < 16; historyOffset++) {
                unsigned char historyIndex = (CPURecentIndex + historyOffset) & 0x0F;
                DEBUG_PRINTF(" %04X:%02X@%04X", CPURecentPC[historyIndex],
                             CPURecentOpcode[historyIndex], CPURecentArgument[historyIndex]);
              }
              DEBUG_PRINTLN();
            }
            brkTraceCount++;
          }
#endif
          PC++;
          push16(PC);
          push8(SR|SR_BRK);
          SR|=SR_INT;
          if (Is65C02Mode()) SR &= ~SR_DEC;
          PC = read16(0xFFFE);
          break;
        //BVC
        case 0x50:
          if(!(SR&SR_OVER)) {
            value16 = PC;
            PC += argument_addr;
            instructionCycles += 1 + ((value16 & 0xFF00) != (PC & 0xFF00));
          }
          break;
        //BVS
        case 0x70:
          if(SR&SR_OVER) {
            value16 = PC;
            PC += argument_addr;
            instructionCycles += 1 + ((value16 & 0xFF00) != (PC & 0xFF00));
          }
          break;
        //CLC
        case 0x18:
          SR&=0xFE;
          break;
        //CLD
        case 0xD8:
          SR&=0xF7;
          break;
        //CLI
        case 0x58:
          SR&=0xFB;
          break;
        //CLV
        case 0xB8:
          SR&=0xBF;
          break;
        //CMP
        case 0xC5: case 0xD5:
          value16 = ((unsigned short)readPgz8(argument_addr)) ^ 0x00FF;
          result = (unsigned short)A + value16 + (unsigned short)1;
          setflags();
          break;
        case 0xC9:
        case 0xCD: case 0xDD: case 0xD9:
        case 0xC1: case 0xD1:
          value16 = ((unsigned short)read8(argument_addr)) ^ 0x00FF;
          result = (unsigned short)A + value16 + (unsigned short)1;
          setflags();
          break;
        //CPX
        case 0xE4:
          value16 = ((unsigned short)readPgz8(argument_addr)) ^ 0x00FF;
          result = (unsigned short)X + value16 + (unsigned short)1;
          setflags();
          break;
        case 0xE0: case 0xEC:
          value16 = ((unsigned short)read8(argument_addr)) ^ 0x00FF;
          result = (unsigned short)X + value16 + (unsigned short)1;
          setflags();
          break;
        //CPY
        case 0xC4:
          value16 = ((unsigned short)readPgz8(argument_addr)) ^ 0x00FF;
          result = (unsigned short)Y + value16 + (unsigned short)1;
          setflags();
          break;
        case 0xC0: case 0xCC:
          value16 = ((unsigned short)read8(argument_addr)) ^ 0x00FF;
          result = (unsigned short)Y + value16 + (unsigned short)1;
          setflags();
          break;
        //DEC
        case 0xC6: case 0xD6:
          value16 = (unsigned short)readPgz8(argument_addr);
          result = value16 - 1;
          setflags();
          writePgz8(argument_addr, result&0xFF);
          break;
        case 0xCE: case 0xDE:
          value16 = (unsigned short)read8(argument_addr);
          result = value16 - 1;
          setflags();
          write8(argument_addr, result&0xFF);
          break;
        //DEX
        case 0xCA:
          result = --X;
          setflags();
          break;
        //DEY
        case 0x88:
          result = --Y;
          setflags();
          break;
        //EOR
        case 0x45: case 0x55:
          value8 = readPgz8(argument_addr);
          result = A^value8;
          setflags();
          A = result&0xFF;
          break;
        case 0x49:
        case 0x4D: case 0x5D: case 0x59:
        case 0x41: case 0x51:
          value8 = read8(argument_addr);
          result = A^value8;
          setflags();
          A = result&0xFF;
          break;
        //INC
        case 0xE6: case 0xF6:
          value16 = (unsigned short)readPgz8(argument_addr);
          result = value16 + 1;
          setflags();
          writePgz8(argument_addr, result&0xFF);	
          break;
        case 0xEE: case 0xFE:
          value16 = (unsigned short)read8(argument_addr);
          result = value16 + 1;
          setflags();
          write8(argument_addr, result&0xFF);	
          break;
        //INX
        case 0xE8:
          result = ++X;
          setflags();
          break;
        //INY
        case 0xC8:
          result = ++Y;
          setflags();	
          break;
        //JMP
        case 0x4C: case 0x6C:
          PC = argument_addr;
          break;
        //JSR
        case 0x20:
          push16(PC-1);
          PC = argument_addr;
          break;
        //LDA
        case 0xA5: case 0xB5:
          A = readPgz8(argument_addr);
          result = A;
          setflags();
          break;
        case 0xA9:
        case 0xAD: case 0xBD: case 0xB9:
        case 0xA1: case 0xB1:
          A = read8(argument_addr);
          result = A;
          setflags();
          break;
        //LDX
        case 0xA6: case 0xB6:
          X = readPgz8(argument_addr);
          result = X;
          setflags();
          break;
        case 0xA2:
        case 0xAE: case 0xBE:
          X = read8(argument_addr);
          result = X;
          setflags();
          break;
        //LDY
        case 0xA4: case 0xB4:
          Y = readPgz8(argument_addr);
          result = Y;
          setflags();
          break;
        case 0xA0:
        case 0xAC: case 0xBC:
          Y = read8(argument_addr);
          result = Y;
          setflags();
          break;
        //LSR A
        case 0x4A:
          value8 = A;
          result = value8 >> 1;
          result |= (value8&0x1)?0x8000:0;
          setflags();
          A = result&0xFF;
          break;
        //LSR
        case 0x46: case 0x56:
          value8 = readPgz8(argument_addr);
          result = value8 >> 1;
          result |= (value8&0x1)?0x8000:0;
          setflags();
          writePgz8(argument_addr, result&0xFF);
          break;
        case 0x4E: case 0x5E:
          value8 = read8(argument_addr);
          result = value8 >> 1;
          result |= (value8&0x1)?0x8000:0;
          setflags();
          write8(argument_addr, result&0xFF);
          break;
        //NOP
        case 0xEA:
          break;
        //ORA
        case 0x05: case 0x15:
          value8 = readPgz8(argument_addr);
          result = A | value8;
          setflags();
          A = result&0xFF;
          break;
        case 0x09:
        case 0x0D: case 0x1D: case 0x19:
        case 0x01: case 0x11:
          value8 = read8(argument_addr);
          result = A | value8;
          setflags();
          A = result&0xFF;
          break;
        //PHA
        case 0x48:
          push8(A);
          break;
        //PHP
        case 0x08:
          push8(SR|SR_BRK);
          break;
        //PLA
        case 0x68:
          result = pull8();
          setflags();
          A = result;
          break;
       //PLP
      case 0x28:
        SR = pull8() | SR_FIXED_BITS;
        break;
      //ROL A
      case 0x2A:
        value16 = (unsigned short)A;
        result = (value16 << 1) | (SR&SR_CARRY);
        setflags();	
        A = result&0xFF;
        break;
      //ROL
      case 0x26: case 0x36:
        value16 = (unsigned short)readPgz8(argument_addr);
        result = (value16 << 1) | (SR&SR_CARRY);
        setflags();
        writePgz8(argument_addr, result&0xFF);
        break;
      case 0x2E: case 0x3E:
        value16 = (unsigned short)read8(argument_addr);
        result = (value16 << 1) | (SR&SR_CARRY);
        setflags();
        write8(argument_addr, result&0xFF);
        break;
      //ROR A
      case 0x6A:
        value16 = (unsigned short)A;
        result = (value16 >> 1) | ((SR&SR_CARRY) << 7);
        result |= (value16&0x1)?0x8000:0;
        setflags();
        A = result&0xFF;
        break;
      //ROR
      case 0x66: case 0x76:
        value16 = (unsigned short)readPgz8(argument_addr);
        result = (value16 >> 1) | ((SR&SR_CARRY) << 7);
        result |= (value16&0x1)?0x8000:0;
        setflags();
        writePgz8(argument_addr, result&0xFF);
        break;
      case 0x6E: case 0x7E:
        value16 = (unsigned short)read8(argument_addr);
        result = (value16 >> 1) | ((SR&SR_CARRY) << 7);
        result |= (value16&0x1)?0x8000:0;
        setflags();
        write8(argument_addr, result&0xFF);
        break;
      //RTI
      case 0x40:
        SR = pull8();
        PC = pull16();
        break;
      //RTS
      case 0x60:
        PC = pull16() + 1;
        break;
      //SBC
      case 0xE5: case 0xF5:
        instructionCycles += ExecuteSBC(readPgz8(argument_addr));
        break;
      case 0xE9: 
      case 0xED: case 0xFD: case 0xF9:
      case 0xE1: case 0xF1:
        instructionCycles += ExecuteSBC(read8(argument_addr));
        break;
      //SEC
      case 0x38:
        SR |= SR_CARRY;
        break;
      //SED
      case 0xF8:
        SR |= SR_DEC;
        break;
      //SEI
      case 0x78:
        SR |= SR_INT;
        break;
      //STA
      case 0x85: case 0x95:
        writePgz8(argument_addr, A);
        break;
      case 0x8D:
      case 0x9D: case 0x99: case 0x81:
      case 0x91:
        write8(argument_addr, A);
        break;
      //STX
      case 0x86: case 0x96:
        writePgz8(argument_addr, X);
        break;
      case 0x8E:
        write8(argument_addr, X);
        break;
      //STY
      case 0x84: case 0x94:
        writePgz8(argument_addr, Y);
        break;
      case 0x8C:
        write8(argument_addr, Y);
        break;
      //TAX
      case 0xAA:
        X = A;
        result = A;
        setflags();
        break;
      //TAY
      case 0xA8:
        Y = A;
        result = A;
        setflags();
        break;
      //TSX
      case 0xBA:
        X = STP;
        result = STP;
        setflags();
        break;
      //TXA
      case 0x8A:
        A = X;
        result = X;
        setflags();
        break;
      //TXS
      case 0x9A:
        STP = X;
        result = X;
        setflags();
        break;
      //TYA
      case 0x98:
        A = Y;
        result = Y;
        setflags();
        break;
      }

    cycle += instructionCycles;
    TotalCycles += instructionCycles;
}

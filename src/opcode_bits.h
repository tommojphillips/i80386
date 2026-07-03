/* i80386_opcode_bits.h
 * Thomas J. Armytage 2026 ( https://github.com/tommojphillips/ )
 * opcode bit helper
 */


#ifndef I80386_OPCODE_BITS_H
#define I80386_OPCODE_BITS_H

#ifndef I80386_OPCODE
#define I80386_OPCODE
#endif

 /* byte/word operation. 0 = byte; 1 = word/dword depending on operand size */
#define W (I80386_OPCODE & 0x1)

/* byte/word operation. 0 = byte; 1 = word/dword */
#define WREG (I80386_OPCODE & 0x8) 

/* sign extend. 0 = word/dword; 1 = byte sign extended to word/dword */
#define S (I80386_OPCODE & 0x2)

/* segment register. es=b00; cs=b01; ss=b10; ds=b11 */
#define SR ((I80386_OPCODE >> 0x3) & 0x3)

/* extended segment register. es=b000, cs=b001, ss=b010, ds=b011, fs=b100, gs=b101 */
#define ESR ((I80386_OPCODE >> 0x3) & 0x7)

/* extended segment register. es=b000, cs=b001, ss=b010, ds=b011, fs=b100, gs=b101 */
#define SRE (I80386_OPCODE & 0x7)

/* 0 = (count = 1); 1 = (count = CL) */
#define VW (I80386_OPCODE & 0x2)

/* register direction (reg <- r/m) or (r/m <- reg) */
#define D (I80386_OPCODE & 0x2) 

/* zero (repz/repnz) */
#define Z (I80386_OPCODE & 0x1)

 /* Jump condition */
#define CCCC (I80386_OPCODE & 0x0F)
#define JCC_JO  0b0000
#define JCC_JNO 0b0001
#define JCC_JC  0b0010
#define JCC_JNC 0b0011
#define JCC_JZ  0b0100
#define JCC_JNZ 0b0101
#define JCC_JBE 0b0110
#define JCC_JA  0b0111
#define JCC_JS  0b1000
#define JCC_JNS 0b1001
#define JCC_JPE 0b1010
#define JCC_JPO 0b1011
#define JCC_JL  0b1100
#define JCC_JGE 0b1101
#define JCC_JLE 0b1110
#define JCC_JG  0b1111

#endif

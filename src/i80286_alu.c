/* alu.c
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 Arithmetic Logic Unit
 */

#include <stdint.h>

#include "i8086.h"
#include "i8086_alu.h"
#include "i8086_muldiv.h"
#include "sign_extend.h"

#define SF cpu->status.sf
#define CF cpu->status.cf
#define ZF cpu->status.zf
#define PF cpu->status.pf
#define OF cpu->status.of
#define AF cpu->status.af

#define SET_ZF(r) ZF = (r) == 0

#define SET_SF8(r) SF = ((r) & 0x80) >> 7
#define SET_ZF8(r) SET_ZF(r)

#define SET_PF8(r) { \
	uint8_t pf = (r);\
	pf ^= (pf >> 4); \
	pf ^= (pf >> 2); \
	pf ^= (pf >> 1); \
	PF = (~pf) & 1; }

#define SET_AF_ADD8(x,y,r) AF = (((x) ^ (y) ^ (r)) & 0x10) != 0
#define SET_AF_SUB8(x,y,r) AF = (((uint8_t)(x) ^ (uint8_t)(y) ^ (uint8_t)(r)) & 0x10) != 0
#define SET_OF_ADD8(x,y,r) OF = ((((r) ^ (x)) & ((r) ^ (y))) & 0x80) != 0
#define SET_OF_SUB8(x,y,r) OF = ((((uint8_t)(x) ^ (uint8_t)(y)) & ((uint8_t)(x) ^ (uint8_t)(r))) & 0x80) != 0
#define SET_CF_ADD8(r)     CF = (r) > 0xFF
#define SET_CF_SUB8(x,y)   CF = (y) > (x)

#define SET_SF16(r) SF = ((r) & 0x8000) >> 15
#define SET_ZF16(r) SET_ZF(r)

#define SET_PF16(r) SET_PF8(r & 0xFF)

#define SET_AF_ADD16(x,y,r) AF = (((x) ^ (y) ^ (r)) & 0x10) != 0
#define SET_AF_SUB16(x,y,r) AF = (((uint16_t)(x) ^ (uint16_t)(y) ^ (uint16_t)(r)) & 0x10) != 0
#define SET_OF_ADD16(x,y,r) OF = ((((r) ^ (x)) & ((r) ^ (y))) & 0x8000) != 0
#define SET_OF_SUB16(x,y,r) OF = ((((uint16_t)(x) ^ (uint16_t)(y)) & ((uint16_t)(x) ^ (uint16_t)(r))) & 0x8000) != 0
#define SET_CF_ADD16(r)     CF = (r) > 0xFFFF
#define SET_CF_SUB16(x,y)   CF = (y) > (x)

/* DBZ */
#define INT_DBZ 0 // ITC 0
extern void i8086_int(I8086* cpu, uint8_t type);

void alu_daa(I8086* cpu, uint8_t* x1) {
	uint8_t correction = 0;
	uint8_t af = AF;
	uint8_t cf = CF;

	uint16_t check_val = 0;
	if (af) {
		check_val = 0x9F;
	}
	else {
		check_val = 0x99;
	}

	if ((*x1 & 0x0F) > 9 || af) {
		correction |= 0x06;
		af = 1;
	}
	else {
		af = 0;
	}

	if (*x1 > check_val || cf) {
		correction |= 0x60;
		cf = 1;
	}
	else {
		cf = 0;
	}

	alu_add8(cpu, x1, correction);

	AF = af;
	CF = cf;
}
void alu_das(I8086* cpu, uint8_t* x1) {
	uint8_t correction = 0;
	uint8_t af = AF;
	uint8_t cf = CF;

	uint16_t check_val = 0;
	if (af) {
		check_val = 0x9F;
	}
	else {
		check_val = 0x99;
	}

	if ((*x1 & 0x0F) > 9 || af) {
		correction |= 6;
		af = 1;
	}

	if (*x1 > check_val || cf) {
		correction |= 0x60;
		cf = 1;
	}

	alu_sub8(cpu, x1, correction);

	AF = af;
	CF = cf;
}
void alu_aaa(I8086* cpu, uint8_t* l, uint8_t* h) {

	SF = *l >= 0x7A && *l <= 0xF9;
	OF = *l >= 0x7A && *l <= 0x7F;

	if ((*l & 0x0F) > 9 || AF) {
		*l = (*l + 6);
		*h += 1;
		AF = 1;
		CF = 1;
	}
	else {
		AF = 0;
		CF = 0;
	}

	SET_ZF8(*l);
	SET_PF8(*l);

	*l &= 0x0F;
}
void alu_aas(I8086* cpu, uint8_t* l, uint8_t* h) {

	SF = (!AF && *l > 0x7F) || (AF && (*l <= 0x05 || *l >= 0x86));
	OF = (AF && *l > 0x7F && *l <= 0x85);

	if ((*l & 0x0F) > 9 || AF) {
		*l -= 6;
		*h -= 1;
		AF = 1;
		CF = 1;
	}
	else {
		AF = 0;
		CF = 0;
	}

	SET_ZF8(*l);
	SET_PF8(*l);

	*l &= 0x0F;
}
void alu_aam(I8086* cpu, uint8_t* l, uint8_t* h, uint8_t divisor) {
	
	if (divisor != 0) {
		*h = (*l / divisor);
		*l = (*l % divisor);
		SET_PF8(*l);
		SET_SF8(*l);
		SET_ZF8(*l);
		AF = 0;
		CF = 0;
		OF = 0;
	}
	else {
		SET_PF8(0);
		SET_SF8(0);
		SET_ZF8(0);
		AF = 0;
		CF = 0;
		OF = 0;
		i8086_int(cpu, INT_DBZ);
	}	
}
void alu_aad(I8086* cpu, uint8_t* l, uint8_t* h, uint8_t divisor) {
	uint8_t product = *h * divisor;
	alu_add8(cpu, l, product);
	*h = 0;
}

/* 8bit alu */

void alu_add8(I8086* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = (*x1 + x2);
	SET_AF_ADD8(*x1, x2, tmp);
	SET_OF_ADD8(*x1, x2, tmp);
	SET_CF_ADD8(tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void alu_adc8(I8086* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = ((uint16_t)*x1 + (uint16_t)x2 + CF);
	SET_AF_ADD8(*x1, x2, tmp);
	SET_OF_ADD8(*x1, x2, tmp);
	SET_CF_ADD8(tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void alu_sub8(I8086* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = (*x1 - x2);
	SET_AF_SUB8(*x1, x2, tmp);
	SET_OF_SUB8(*x1, x2, tmp);
	SET_CF_SUB8(*x1, x2);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void alu_sbb8(I8086* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = ((uint16_t)*x1 - ((uint16_t)x2 + CF));
	SET_AF_SUB8(*x1, x2, tmp);
	SET_OF_SUB8(*x1, x2, tmp);
	SET_CF_SUB8(*x1, ((uint16_t)x2 + CF));
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}

void alu_and8(I8086* cpu, uint8_t* x1, uint8_t x2) {
	*x1 &= x2;
	CF = 0;
	OF = 0;
	AF = 0;
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void alu_xor8(I8086* cpu, uint8_t* x1, uint8_t x2) {
	*x1 ^= x2;
	CF = 0;
	OF = 0;
	AF = 0;
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void alu_or8(I8086* cpu, uint8_t* x1, uint8_t x2) {
	*x1 |= x2;
	CF = 0;
	OF = 0;
	AF = 0;
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}

void alu_cmp8(I8086* cpu, uint8_t  x1, uint8_t x2) {
	alu_sub8(cpu, &x1, x2); /* discard result */
}
void alu_test8(I8086* cpu, uint8_t x1, uint8_t x2) {
	alu_and8(cpu, &x1, x2); /* discard result */
}

void alu_rcl8(I8086* cpu, uint8_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		uint8_t cf = CF;
		CF = (*x1 >> 7) & 1;
		*x1 = (*x1 << 1) | cf;
	}
	if (count != 0) {
		OF = ((*x1 >> 7) & 1) ^ CF;
	}
}
void alu_rcr8(I8086* cpu, uint8_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		uint8_t cf = CF;
		CF = (*x1 & 1);
		*x1 = (*x1 >> 1) | (cf << 7);
	}
	if (count != 0) {
		OF = (*x1 >> 7) ^ ((*x1 >> 6) & 1);
	}
}
void alu_rol8(I8086* cpu, uint8_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		CF = (*x1 >> 7) & 1;
		*x1 = (*x1 << 1) | CF;
	}
	if (count != 0) {
		OF = ((*x1 >> 7) & 1) ^ CF;
	}
}
void alu_ror8(I8086* cpu, uint8_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		CF = (*x1 & 1);
		*x1 = (*x1 >> 1) | (CF << 7);
	}
	if (count != 0) {
		OF = (*x1 >> 7) ^ ((*x1 >> 6) & 1);
	}
}
void alu_shl8(I8086* cpu, uint8_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		CF = (*x1 >> 7) & 1;
		*x1 <<= 1;
	}
	if (count != 0) {
		OF = ((*x1 >> 7) & 1) ^ CF;
		AF = (*x1 & 0x10) != 0;
		SET_PF8(*x1);
		SET_SF8(*x1);
		SET_ZF8(*x1);
	}
}
void alu_shr8(I8086* cpu, uint8_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		OF = (*x1 >> 7) & 1;
		CF = (*x1 & 1);
		*x1 >>= 1;
	}
	if (count != 0) {
		AF = 0;
		SET_SF8(*x1);
		SET_PF8(*x1);
		SET_ZF8(*x1);
	}
}
void alu_sar8(I8086* cpu, uint8_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		CF = (*x1 & 1);
		uint8_t msb = (*x1 & 0x80);
		*x1 >>= 1;
		*x1 |= msb;
	}
	if (count != 0) {
		OF = 0;
		AF = 0;
		SET_SF8(*x1);
		SET_PF8(*x1);
		SET_ZF8(*x1);
	}
}
void alu_setmo8(I8086* cpu, uint8_t* x1, uint8_t count) {
	if (count != 0) {
		*x1 = 0xFF;
		CF = 0;
		AF = 0;
		OF = 0;
		SET_PF8(*x1);
		SET_SF8(*x1);
		SET_ZF8(*x1);
	}
}

void alu_inc8(I8086* cpu, uint8_t* x1) {
	uint16_t tmp = (*x1 + 1);
	SET_AF_ADD8(*x1, 1, tmp);
	SET_OF_ADD8(*x1, 1, tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void alu_dec8(I8086* cpu, uint8_t* x1) {
	uint16_t tmp = (*x1 - 1);
	SET_AF_SUB8(*x1, 1, tmp);
	SET_OF_SUB8(*x1, 1, tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}

void alu_mul8(I8086* cpu, uint8_t multiplicand, uint8_t multiplier, uint8_t* lo, uint8_t* hi) {
	mc_mul8(cpu, multiplicand, multiplier, 0, lo, hi);
}
void alu_imul8(I8086* cpu, uint8_t multiplicand, uint8_t multiplier, uint8_t* lo, uint8_t* hi) {
	mc_mul8(cpu, multiplicand, multiplier, 1, lo, hi);
}

void alu_div8(I8086* cpu, uint8_t dividend_lo, uint8_t dividend_hi, uint8_t divider, uint8_t* quotient, uint8_t* remainder) {
	uint16_t dividend = ((uint16_t)dividend_hi << 8) | dividend_lo;
	mc_div8(cpu, dividend, divider, 0, quotient, remainder);
}
void alu_idiv8(I8086* cpu, uint8_t dividend_lo, uint8_t dividend_hi, uint8_t divider, uint8_t* quotient, uint8_t* remainder) {
	uint16_t dividend = ((uint16_t)dividend_hi << 8) | dividend_lo;
	mc_div8(cpu, dividend, divider, 1, quotient, remainder);
}

void alu_neg8(I8086* cpu, uint8_t* x1) {
	uint8_t x2 = *x1;
	*x1 = 0;
	alu_sub8(cpu, x1, x2);
}

/* 16bit alu */

void alu_add16(I8086* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = (*x1 + x2);
	SET_AF_ADD16(*x1, x2, tmp);
	SET_OF_ADD16(*x1, x2, tmp);
	SET_CF_ADD16(tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void alu_adc16(I8086* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = ((uint32_t)*x1 + (uint32_t)x2 + CF);
	SET_AF_ADD16(*x1, x2, tmp);
	SET_OF_ADD16(*x1, x2, tmp);
	SET_CF_ADD16(tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void alu_sub16(I8086* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = (*x1 - x2);
	SET_AF_SUB16(*x1, x2, tmp);
	SET_OF_SUB16(*x1, x2, tmp);
	SET_CF_SUB16(*x1, x2);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void alu_sbb16(I8086* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = ((uint32_t)*x1 - ((uint32_t)x2 + CF));
	SET_AF_SUB16(*x1, x2, tmp);
	SET_OF_SUB16(*x1, x2, tmp);
	SET_CF_SUB16(*x1, ((uint32_t)x2 + CF));
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void alu_and16(I8086* cpu, uint16_t* x1, uint16_t x2) {
	*x1 &= x2;
	CF = 0;
	OF = 0;
	AF = 0;
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void alu_xor16(I8086* cpu, uint16_t* x1, uint16_t x2) {
	*x1 ^= x2;
	CF = 0;
	OF = 0;
	AF = 0;
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void alu_or16(I8086*  cpu, uint16_t* x1, uint16_t x2) {
	*x1 |= x2;
	CF = 0;
	OF = 0;
	AF = 0;
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void alu_cmp16(I8086* cpu, uint16_t  x1, uint16_t x2) {
	alu_sub16(cpu, &x1, x2); /* discard result */
}
void alu_test16(I8086* cpu, uint16_t x1, uint16_t x2) {
	alu_and16(cpu, &x1, x2); /* discard result */
}

void alu_rcl16(I8086* cpu, uint16_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		uint8_t cf = CF;
		CF = (*x1 >> 15) & 1;
		*x1 = (*x1 << 1) | cf;
	}
	if (count != 0) {
		OF = ((*x1 >> 15) & 1) ^ CF;
	}
}
void alu_rcr16(I8086* cpu, uint16_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		uint8_t cf = CF;
		CF = (*x1 & 1);		
		*x1 = (*x1 >> 1) | (cf << 15);
	}
	if (count != 0) {
		OF = (*x1 >> 15) ^ ((*x1 >> 14) & 1);
	}
}
void alu_rol16(I8086* cpu, uint16_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		CF = (*x1 >> 15) & 1;
		*x1 = (*x1 << 1) | CF;
	}
	if (count != 0) {
		OF = ((*x1 >> 15) & 1) ^ CF;
	}
}
void alu_ror16(I8086* cpu, uint16_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		CF = (*x1 & 1);
		*x1 = (*x1 >> 1) | (CF << 15);
	}
	if (count != 0) {
		OF = (*x1 >> 15) ^ ((*x1 >> 14) & 1);
	}
}
void alu_shl16(I8086* cpu, uint16_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		CF = (*x1 >> 15) & 1;
		*x1 <<= 1;	
	}
	if (count != 0) {
		OF = ((*x1 >> 15) & 1) ^ CF;
		AF = (*x1 & 0x10) != 0;
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}
void alu_shr16(I8086* cpu, uint16_t* x1, uint8_t count) {
	for (int i = 0; i < count; ++i) {
		OF = (*x1 >> 15) & 1;
		CF = (*x1 & 1);
		*x1 >>= 1;
	}
	if (count != 0) {
		AF = 0;
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}
void alu_sar16(I8086* cpu, uint16_t* x1, uint8_t count) {	
	for (int i = 0; i < count; ++i) {
		CF = (*x1 & 1);
		uint16_t msb = (*x1 & 0x8000);
		*x1 >>= 1;
		*x1 |= msb;
	}
	if (count != 0) {
		OF = 0;
		AF = 0;
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}
void alu_setmo16(I8086* cpu, uint16_t* x1, uint8_t count) {
	if (count != 0) {
		*x1 = 0xFFFF;
		CF = 0;
		AF = 0;
		OF = 0;
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}

void alu_inc16(I8086* cpu, uint16_t* x1) {
	uint32_t tmp = (*x1 + 1);
	SET_AF_ADD16(*x1, 1, tmp);
	SET_OF_ADD16(*x1, 1, tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void alu_dec16(I8086* cpu, uint16_t* x1) {
	uint32_t tmp = (*x1 - 1);
	SET_AF_SUB16(*x1, 1, tmp);
	SET_OF_SUB16(*x1, 1, tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void alu_mul16(I8086* cpu, uint16_t multiplicand, uint16_t multiplier, uint16_t* lo, uint16_t* hi) {
	mc_mul16(cpu, multiplicand, multiplier, 0, lo, hi);
}
void alu_imul16(I8086* cpu, uint16_t multiplicand, uint16_t multiplier, uint16_t* lo, uint16_t* hi) {
	mc_mul16(cpu, multiplicand, multiplier, 1, lo, hi);
}

void alu_div16(I8086* cpu, uint16_t dividend_lo, uint16_t dividend_hi, uint16_t divider, uint16_t* quotient, uint16_t* remainder) {
	uint32_t dividend = ((uint32_t)dividend_hi << 16) | dividend_lo;
	mc_div16(cpu, dividend, divider, 0, quotient, remainder);
}
void alu_idiv16(I8086* cpu, uint16_t dividend_lo, uint16_t dividend_hi, uint16_t divider, uint16_t* quotient, uint16_t* remainder) {
	uint32_t dividend = ((uint32_t)dividend_hi << 16) | dividend_lo;
	mc_div16(cpu, dividend, divider, 1, quotient, remainder);
}

void alu_neg16(I8086* cpu, uint16_t* x1) {
	uint16_t x2 = *x1;
	*x1 = 0;
	alu_sub16(cpu, x1, x2);
}

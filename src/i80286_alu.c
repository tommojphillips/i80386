/* i80286_alu.c
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 Arithmetic Logic Unit
 */

#include <stdint.h>

#include "i80286.h"
#include "i80286_alu.h"
#include "sign_extend.h"

#define SF cpu->psw.u.bits.sf
#define CF cpu->psw.u.bits.cf
#define ZF cpu->psw.u.bits.zf
#define PF cpu->psw.u.bits.pf
#define OF cpu->psw.u.bits.of
#define AF cpu->psw.u.bits.af

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
#define INT_DBZ 0 /* ITC 0 */
extern void i80286_int(I80286* cpu, uint8_t type);
extern void i80286_exception(I80286* cpu, uint8_t exception);

void i80286_alu_daa(I80286* cpu, uint8_t* x1) {
	uint8_t correction = 0;
	uint8_t af = AF;
	uint8_t cf = CF;

	if ((*x1 & 0x0F) > 9 || af) {
		correction |= 0x06;
		af = 1;
	}
	else {
		af = 0;
	}

	if (*x1 > 0x99 || cf) {
		correction |= 0x60;
		cf = 1;
	}
	else {
		cf = 0;
	}

	i80286_alu_add8(cpu, x1, correction);

	AF = af;
	CF = cf;
}
void i80286_alu_das(I80286* cpu, uint8_t* x1) {
	uint8_t correction = 0;
	uint8_t af = AF;
	uint8_t cf = CF;

	if ((*x1 & 0x0F) > 9 || af) {
		correction |= 6;
		af = 1;
	}

	if (*x1 > 0x99 || cf) {
		correction |= 0x60;
		cf = 1;
	}

	i80286_alu_sub8(cpu, x1, correction);

	AF = af;
	CF = cf;
}
void i80286_alu_aaa(I80286* cpu, uint8_t* l, uint8_t* h) {

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
void i80286_alu_aas(I80286* cpu, uint8_t* l, uint8_t* h) {

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
void i80286_alu_aam(I80286* cpu, uint8_t* l, uint8_t* h, uint8_t divisor) {
	/* fails aam 0. PF is incorrect when divisor = 0 */
	if (divisor != 0) {
		*h = (*l / divisor);
		*l = (*l % divisor);
		SET_PF8(*l);
		SET_SF8(*l);
		SET_ZF8(*l);
		AF = 0; /* 286 undefined */
		CF = 0; /* 286 undefined */
		OF = 0; /* 286 undefined */
	}
	else {
		PF = 0; /* may as well be undefined when divisor = 0. clearly the div microcode exits early and PF is set by some unknown algorithm */
		SF = 0;
		ZF = 0;
		AF = 0; /* 286 undefined */
		CF = 0; /* 286 undefined */
		OF = 0; /* 286 undefined */
		i80286_exception(cpu, INT_DBZ);
	}
}
void i80286_alu_aad(I80286* cpu, uint8_t* l, uint8_t* h, uint8_t divisor) {
	uint8_t product = *h * divisor;
	i80286_alu_add8(cpu, l, product);
	*h = 0;

	AF = 0; /* 286 undefined */
	CF = 0; /* 286 undefined */
	OF = 0; /* 286 undefined */
}

/* 8bit alu */

void i80286_alu_add8(I80286* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = (*x1 + x2);
	SET_AF_ADD8(*x1, x2, tmp);
	SET_OF_ADD8(*x1, x2, tmp);
	SET_CF_ADD8(tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80286_alu_adc8(I80286* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = ((uint16_t)*x1 + (uint16_t)x2 + CF);
	SET_AF_ADD8(*x1, x2, tmp);
	SET_OF_ADD8(*x1, x2, tmp);
	SET_CF_ADD8(tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80286_alu_sub8(I80286* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = (*x1 - x2);
	SET_AF_SUB8(*x1, x2, tmp);
	SET_OF_SUB8(*x1, x2, tmp);
	SET_CF_SUB8(*x1, x2);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80286_alu_sbb8(I80286* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = ((uint16_t)*x1 - ((uint16_t)x2 + CF));
	SET_AF_SUB8(*x1, x2, tmp);
	SET_OF_SUB8(*x1, x2, tmp);
	SET_CF_SUB8(*x1, ((uint16_t)x2 + CF));
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}

void i80286_alu_and8(I80286* cpu, uint8_t* x1, uint8_t x2) {
	*x1 &= x2;
	CF = 0;
	OF = 0;
	AF = 0; /* 286 undefined */
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80286_alu_xor8(I80286* cpu, uint8_t* x1, uint8_t x2) {
	*x1 ^= x2;
	CF = 0;
	OF = 0;
	AF = 0; /* 286 undefined */
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80286_alu_or8(I80286* cpu, uint8_t* x1, uint8_t x2) {
	*x1 |= x2;
	CF = 0;
	OF = 0;
	AF = 0; /* 286 undefined */
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}

void i80286_alu_cmp8(I80286* cpu, uint8_t  x1, uint8_t x2) {
	i80286_alu_sub8(cpu, &x1, x2); /* discard result */
}
void i80286_alu_test8(I80286* cpu, uint8_t x1, uint8_t x2) {
	i80286_alu_and8(cpu, &x1, x2); /* discard result */
}

void i80286_alu_rcl8(I80286* cpu, uint8_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		uint8_t cf = CF;
		CF = (*x1 >> 7) & 1;
		*x1 = (*x1 << 1) | cf;
	}
	if (count != 0) {
		OF = ((*x1 >> 7) & 1) ^ CF;
	}
}
void i80286_alu_rcr8(I80286* cpu, uint8_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		uint8_t cf = CF;
		CF = (*x1 & 1);
		*x1 = (*x1 >> 1) | (cf << 7);
	}
	if (count != 0) {
		OF = (*x1 >> 7) ^ ((*x1 >> 6) & 1);
	}
}
void i80286_alu_rol8(I80286* cpu, uint8_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		CF = (*x1 >> 7) & 1;
		*x1 = (*x1 << 1) | CF;
	}
	if (count != 0) {
		OF = ((*x1 >> 7) & 1) ^ CF;
	}
}
void i80286_alu_ror8(I80286* cpu, uint8_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		CF = (*x1 & 1);
		*x1 = (*x1 >> 1) | (CF << 7);
	}
	if (count != 0) {
		OF = (*x1 >> 7) ^ ((*x1 >> 6) & 1);
	}
}
void i80286_alu_shl8(I80286* cpu, uint8_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		CF = (*x1 >> 7) & 1;
		*x1 <<= 1;
	}
	if (count != 0) {
		OF = ((*x1 >> 7) & 1) ^ CF;
		AF = (*x1 & 0x10) != 0; /* 286 undefined */
		SET_PF8(*x1);
		SET_SF8(*x1);
		SET_ZF8(*x1);
	}
}
void i80286_alu_shr8(I80286* cpu, uint8_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		OF = (*x1 >> 7) & 1;
		CF = (*x1 & 1);
		*x1 >>= 1;
	}
	if (count != 0) {
		AF = 1; /* 286 undefined */
		SET_SF8(*x1);
		SET_PF8(*x1);
		SET_ZF8(*x1);
	}
}
void i80286_alu_sal8(I80286* cpu, uint8_t* x1, uint8_t count) {
	/* SAL is identical to SHL */
	i80286_alu_shl8(cpu, x1, count);
}
void i80286_alu_sar8(I80286* cpu, uint8_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		CF = (*x1 & 1);
		uint8_t msb = (*x1 & 0x80);
		*x1 >>= 1;
		*x1 |= msb;
	}
	if (count != 0) {
		OF = 0;
		AF = 1; /* 286 undefined */
		SET_SF8(*x1);
		SET_PF8(*x1);
		SET_ZF8(*x1);
	}
}

void i80286_alu_inc8(I80286* cpu, uint8_t* x1) {
	uint16_t tmp = (*x1 + 1);
	SET_AF_ADD8(*x1, 1, tmp);
	SET_OF_ADD8(*x1, 1, tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80286_alu_dec8(I80286* cpu, uint8_t* x1) {
	uint16_t tmp = (*x1 - 1);
	SET_AF_SUB8(*x1, 1, tmp);
	SET_OF_SUB8(*x1, 1, tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}

void i80286_alu_mul8(I80286* cpu, uint8_t multiplicand, uint8_t multiplier, uint16_t* product) {
	uint16_t p = multiplicand * multiplier;
	*product = p;

	if (p & 0xFF00) {
		CF = 1;
		OF = 1;
	}
	else {
		CF = 0;
		OF = 0;
	}
}
void i80286_alu_imul8(I80286* cpu, uint8_t multiplicand, uint8_t multiplier, uint16_t* product) {
	int16_t p = (int8_t)multiplicand * (int8_t)multiplier;
	*product = (uint16_t)p;

	if (p < -128 || p > 127) {
		CF = 1;
		OF = 1;
	}
	else {
		CF = 0;
		OF = 0;
	}
}

void i80286_alu_div8(I80286* cpu, uint16_t dividend, uint8_t divider, uint8_t* quotient, uint8_t* remainder) {
	if (divider == 0) {
		i80286_exception(cpu, INT_DBZ);
		return;
	}

	uint16_t q = dividend / divider;
	uint16_t r = dividend % divider;

	if (q > 0xFF) {
		i80286_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFF;
	*remainder = r & 0xFF;
}
void i80286_alu_idiv8(I80286* cpu, uint16_t dividend, uint8_t divider, uint8_t* quotient, uint8_t* remainder) {
	/* fails 4 tests: 
	 idx: 952  - 0038b4bacfb75535b5da175f619b0812b16d0601
	 idx: 1085 - de153d1e3812cdb2c9d25272844b4b28a5adc35f
	 idx: 2653 - dce03c62813266bf0ba50e3325fa3898132cad1f
	 idx: 4297 - 38a27640b8a9475f75998d2cab801d51eb8bb0b2 */
	if (divider == 0) {
		i80286_exception(cpu, INT_DBZ);
		return;
	}

	int16_t q = (int16_t)dividend / (int8_t)divider;
	int16_t r = (int16_t)dividend % (int8_t)divider;
	if (q < -128 || q > 127) {
		i80286_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFF;
	*remainder = r & 0xFF;
}

void i80286_alu_neg8(I80286* cpu, uint8_t* x1) {
	uint8_t x2 = *x1;
	*x1 = 0;
	i80286_alu_sub8(cpu, x1, x2);
}
void i80286_alu_not8(I80286* cpu, uint8_t* x1) {
	(void)cpu;
	*x1 = ~(*x1);
}

/* 16bit alu */

void i80286_alu_add16(I80286* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = (*x1 + x2);
	SET_AF_ADD16(*x1, x2, tmp);
	SET_OF_ADD16(*x1, x2, tmp);
	SET_CF_ADD16(tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80286_alu_adc16(I80286* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = ((uint32_t)*x1 + (uint32_t)x2 + CF);
	SET_AF_ADD16(*x1, x2, tmp);
	SET_OF_ADD16(*x1, x2, tmp);
	SET_CF_ADD16(tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80286_alu_sub16(I80286* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = (*x1 - x2);
	SET_AF_SUB16(*x1, x2, tmp);
	SET_OF_SUB16(*x1, x2, tmp);
	SET_CF_SUB16(*x1, x2);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80286_alu_sbb16(I80286* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = ((uint32_t)*x1 - ((uint32_t)x2 + CF));
	SET_AF_SUB16(*x1, x2, tmp);
	SET_OF_SUB16(*x1, x2, tmp);
	SET_CF_SUB16(*x1, ((uint32_t)x2 + CF));
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void i80286_alu_and16(I80286* cpu, uint16_t* x1, uint16_t x2) {
	*x1 &= x2;
	CF = 0;
	OF = 0;
	AF = 0; /* 286 undefined */
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80286_alu_xor16(I80286* cpu, uint16_t* x1, uint16_t x2) {
	*x1 ^= x2;
	CF = 0;
	OF = 0;
	AF = 0; /* 286 undefined */
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80286_alu_or16(I80286*  cpu, uint16_t* x1, uint16_t x2) {
	*x1 |= x2;
	CF = 0;
	OF = 0;
	AF = 0; /* 286 undefined */
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void i80286_alu_cmp16(I80286* cpu, uint16_t  x1, uint16_t x2) {
	i80286_alu_sub16(cpu, &x1, x2); /* discard result */
}
void i80286_alu_test16(I80286* cpu, uint16_t x1, uint16_t x2) {
	i80286_alu_and16(cpu, &x1, x2); /* discard result */
}

void i80286_alu_rcl16(I80286* cpu, uint16_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		uint8_t cf = CF;
		CF = (*x1 >> 15) & 1;
		*x1 = (*x1 << 1) | cf;
	}
	if (count != 0) {
		OF = ((*x1 >> 15) & 1) ^ CF;
	}
}
void i80286_alu_rcr16(I80286* cpu, uint16_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		uint8_t cf = CF;
		CF = (*x1 & 1);		
		*x1 = (*x1 >> 1) | (cf << 15);
	}
	if (count != 0) {
		OF = (*x1 >> 15) ^ ((*x1 >> 14) & 1);
	}
}
void i80286_alu_rol16(I80286* cpu, uint16_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		CF = (*x1 >> 15) & 1;
		*x1 = (*x1 << 1) | CF;
	}
	if (count != 0) {
		OF = ((*x1 >> 15) & 1) ^ CF;
	}
}
void i80286_alu_ror16(I80286* cpu, uint16_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		CF = (*x1 & 1);
		*x1 = (*x1 >> 1) | (CF << 15);
	}
	if (count != 0) {
		OF = (*x1 >> 15) ^ ((*x1 >> 14) & 1);
	}
}
void i80286_alu_shl16(I80286* cpu, uint16_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		CF = (*x1 >> 15) & 1;
		*x1 <<= 1;	
	}
	if (count != 0) {
		OF = ((*x1 >> 15) & 1) ^ CF;
		AF = (*x1 & 0x10) != 0; /* 286 undefined */
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}
void i80286_alu_shr16(I80286* cpu, uint16_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		OF = (*x1 >> 15) & 1;
		CF = (*x1 & 1);
		*x1 >>= 1;
	}
	if (count != 0) {
		AF = 1; /* 286 undefined */
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}
void i80286_alu_sal16(I80286* cpu, uint16_t* x1, uint8_t count) {
	/* SAL is identical to SHL */
	i80286_alu_shl16(cpu, x1, count);
}
void i80286_alu_sar16(I80286* cpu, uint16_t* x1, uint8_t count) {
	count &= 31;
	for (int i = 0; i < count; ++i) {
		CF = (*x1 & 1);
		uint16_t msb = (*x1 & 0x8000);
		*x1 >>= 1;
		*x1 |= msb;
	}
	if (count != 0) {
		OF = 0;
		AF = 1; /* 286 undefined */
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}

void i80286_alu_inc16(I80286* cpu, uint16_t* x1) {
	uint32_t tmp = (*x1 + 1);
	SET_AF_ADD16(*x1, 1, tmp);
	SET_OF_ADD16(*x1, 1, tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80286_alu_dec16(I80286* cpu, uint16_t* x1) {
	uint32_t tmp = (*x1 - 1);
	SET_AF_SUB16(*x1, 1, tmp);
	SET_OF_SUB16(*x1, 1, tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void i80286_alu_mul16(I80286* cpu, uint16_t multiplicand, uint16_t multiplier, uint32_t* product) {
	uint32_t p = multiplicand * multiplier;
	*product = p;

	if (p & 0xFFFF0000) {
		CF = 1;
		OF = 1;
	}
	else {
		CF = 0;
		OF = 0;
	}
}
void i80286_alu_imul16(I80286* cpu, uint16_t multiplicand, uint16_t multiplier, uint32_t* product) {
	int32_t p = (int16_t)multiplicand * (int16_t)multiplier;
	*product = (uint32_t)p;

	if (p < -32768 || p > 32767) {
		CF = 1;
		OF = 1;
	}
	else {
		CF = 0;
		OF = 0;
	}
}

void i80286_alu_div16(I80286* cpu, uint32_t dividend, uint16_t divider, uint16_t* quotient, uint16_t* remainder) {
	if (divider == 0) {
		i80286_exception(cpu, INT_DBZ);
		return;
	}

	uint32_t q = dividend / divider;
	uint32_t r = dividend % divider;

	if (q > 0xFFFF) {
		i80286_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFFFF;
	*remainder = r & 0xFFFF;
}
void i80286_alu_idiv16(I80286* cpu, uint32_t dividend, uint16_t divider, uint16_t* quotient, uint16_t* remainder) {
	if (divider == 0) {
		i80286_exception(cpu, INT_DBZ);
		return;
	}

	int32_t q = (int32_t)dividend / (int16_t)divider;
	int32_t r = (int32_t)dividend % (int16_t)divider;

	if (q < -32768 || q > 32767) {
		i80286_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFFFF;
	*remainder = r & 0xFFFF;
}

void i80286_alu_neg16(I80286* cpu, uint16_t* x1) {
	uint16_t x2 = *x1;
	*x1 = 0;
	i80286_alu_sub16(cpu, x1, x2);
}
void i80286_alu_not16(I80286* cpu, uint16_t* x1) {
	(void)cpu; 
	*x1 = ~(*x1);
}

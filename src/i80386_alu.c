/* i80386_alu.c
 * Thomas J. Armytage 2025-2026 ( https://github.com/tommojphillips/ )
 * Intel 80386 Arithmetic Logic Unit
 */

#include <stdint.h>

#include "i80386.h"
#include "i80386_alu.h"

#define SET_ZF(r) cpu->psw.zf = (r) == 0

#define SET_SF8(r) cpu->psw.sf = ((r) & 0x80) >> 7
#define SET_ZF8(r) SET_ZF(r)

#define SET_PF8(r) { \
	uint8_t pf = (r);\
	pf ^= (pf >> 4); \
	pf ^= (pf >> 2); \
	pf ^= (pf >> 1); \
	cpu->psw.pf = (~pf) & 1; }

#define SET_AF_ADD8(x,y,r) cpu->psw.af = (((x) ^ (y) ^ (r)) & 0x10) != 0
#define SET_AF_SUB8(x,y,r) cpu->psw.af = (((uint8_t)(x) ^ (uint8_t)(y) ^ (uint8_t)(r)) & 0x10) != 0
#define SET_OF_ADD8(x,y,r) cpu->psw.of = ((((r) ^ (x)) & ((r) ^ (y))) & 0x80) != 0
#define SET_OF_SUB8(x,y,r) cpu->psw.of = ((((uint8_t)(x) ^ (uint8_t)(y)) & ((uint8_t)(x) ^ (uint8_t)(r))) & 0x80) != 0
#define SET_CF_ADD8(r)     cpu->psw.cf = (r) > 0xFF
#define SET_CF_SUB8(x,y)   cpu->psw.cf = (y) > (x)

#define SET_SF16(r) cpu->psw.sf = ((r) & 0x8000) >> 15
#define SET_ZF16(r) SET_ZF(r)

#define SET_PF16(r) SET_PF8(r & 0xFF)

#define SET_AF_ADD16(x,y,r) cpu->psw.af = (((x) ^ (y) ^ (r)) & 0x10) != 0
#define SET_AF_SUB16(x,y,r) cpu->psw.af = (((uint16_t)(x) ^ (uint16_t)(y) ^ (uint16_t)(r)) & 0x10) != 0
#define SET_OF_ADD16(x,y,r) cpu->psw.of = ((((r) ^ (x)) & ((r) ^ (y))) & 0x8000) != 0
#define SET_OF_SUB16(x,y,r) cpu->psw.of = ((((uint16_t)(x) ^ (uint16_t)(y)) & ((uint16_t)(x) ^ (uint16_t)(r))) & 0x8000) != 0
#define SET_CF_ADD16(r)     cpu->psw.cf = (r) > 0xFFFF
#define SET_CF_SUB16(x,y)   cpu->psw.cf = (y) > (x)

#define SET_SF32(r) cpu->psw.sf = ((r) & 0x80000000) >> 31
#define SET_ZF32(r) SET_ZF(r)

#define SET_PF32(r) SET_PF8(r & 0xFF)

#define SET_AF_ADD32(x,y,r) cpu->psw.af = (((x) ^ (y) ^ (r)) & 0x10) != 0
#define SET_AF_SUB32(x,y,r) cpu->psw.af = (((uint32_t)(x) ^ (uint32_t)(y) ^ (uint32_t)(r)) & 0x10) != 0
#define SET_OF_ADD32(x,y,r) cpu->psw.of = ((((r) ^ (x)) & ((r) ^ (y))) & 0x80000000) != 0
#define SET_OF_SUB32(x,y,r) cpu->psw.of = ((((uint32_t)(x) ^ (uint32_t)(y)) & ((uint32_t)(x) ^ (uint32_t)(r))) & 0x80000000) != 0
#define SET_CF_ADD32(r)     cpu->psw.cf = (r) > 0xFFFFFFFF
#define SET_CF_SUB32(x,y)   cpu->psw.cf = (y) > (x)

/* DBZ */
#define INT_DBZ 0 /* ITC 0 */

extern void i80386_int(I80386* cpu, uint8_t type, int interrupt_type);
extern void i80386_exception(I80386* cpu, uint8_t exception);

void i80386_alu_daa(I80386* cpu, uint8_t* x1) {
	uint8_t correction = 0;
	uint8_t af = cpu->psw.af;
	uint8_t cf = cpu->psw.cf;

	if ((*x1 & 0x0F) > 0x9 || af) {
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

	i80386_alu_add8(cpu, x1, correction);

	cpu->psw.af = af;
	cpu->psw.cf = cf;
}
void i80386_alu_das(I80386* cpu, uint8_t* x1) {
	uint8_t correction = 0;
	uint8_t af = cpu->psw.af;
	uint8_t cf = cpu->psw.cf;

	if ((*x1 & 0x0F) > 0x9 || af) {
		correction |= 6;
		af = 1;
	}

	if (*x1 > 0x99 || cf) {
		correction |= 0x60;
		cf = 1;
	}

	i80386_alu_sub8(cpu, x1, correction);

	cpu->psw.af = af;
	cpu->psw.cf = cf;
}
void i80386_alu_aaa(I80386* cpu, uint16_t* x1) {
	if ((*x1 & 0x0F) > 9U || cpu->psw.af) {
		*x1 += 0x106;
		cpu->psw.af = 1;
		cpu->psw.cf = 1;
	}
	else {
		cpu->psw.af = 0;
		cpu->psw.cf = 0;
	}

	SET_ZF8(*x1 & 0xFF);
	SET_PF8(*x1 & 0xFF);

	*x1 &= 0xFF0F;
#if 0
	cpu->psw.sf = *l >= 0x7A && *l <= 0xF9;
	cpu->psw.of = *l >= 0x7A && *l <= 0x7F;

	if ((*l & 0x0F) > 9 || cpu->psw.af) {
		*l = (*l + 6);
		*h += 1;
		cpu->psw.af = 1;
		cpu->psw.cf = 1;
	}
	else {
		cpu->psw.af = 0;
		cpu->psw.cf = 0;
	}

	SET_ZF8(*l);
	SET_PF8(*l);

	*l &= 0x0F;
#endif
}
void i80386_alu_aas(I80386* cpu, uint16_t* x1) {
	if ((*x1 & 0x0F) > 9U || cpu->psw.af) {
		*x1 -= 0x106;
		cpu->psw.af = 1;
		cpu->psw.cf = 1;
	}
	else {
		cpu->psw.af = 0;
		cpu->psw.cf = 0;
	}

	SET_ZF8(*x1 & 0xFF);
	SET_PF8(*x1 & 0xFF);

	*x1 &= 0xFF0F;
#if 0
	cpu->psw.sf = (!cpu->psw.af && *l > 0x7F) || (cpu->psw.af && (*l <= 0x05 || *l >= 0x86));
	cpu->psw.of = (cpu->psw.af && *l > 0x7F && *l <= 0x85);

	if ((*l & 0x0F) > 9 || cpu->psw.af) {
		*l -= 6;
		*h -= 1;
		cpu->psw.af = 1;
		cpu->psw.cf = 1;
	}
	else {
		cpu->psw.af = 0;
		cpu->psw.cf = 0;
	}

	SET_ZF8(*l);
	SET_PF8(*l);

	*l &= 0x0F;
#endif
}
void i80386_alu_aam(I80386* cpu, uint16_t* x1, uint8_t divisor) {
	if (divisor != 0) {
		*x1 = (((*x1 & 0xFF) / divisor) << 8) | ((*x1 & 0xFF) % divisor);
		SET_PF8(*x1 & 0xFF);
		SET_SF8(*x1 & 0xFF);
		SET_ZF8(*x1 & 0xFF);
	}
	else {
		cpu->psw.pf = 0;
		cpu->psw.sf = 0;
		cpu->psw.zf = 0;
		i80386_exception(cpu, INT_DBZ);
	}
#if 0
	/* fails aam 0. PF is incorrect when divisor = 0 */
	if (divisor != 0) {
		*h = (*l / divisor);
		*l = (*l % divisor);
		SET_PF8(*l);
		SET_SF8(*l);
		SET_ZF8(*l);
		cpu->psw.af = 0; /* 286 undefined */
		cpu->psw.cf = 0; /* 286 undefined */
		cpu->psw.of = 0; /* 286 undefined */
	}
	else {
		cpu->psw.pf = 0; /* may as well be undefined when divisor = 0. clearly the div microcode exits early and PF is set by some unknown algorithm */
		cpu->psw.sf = 0;
		cpu->psw.zf = 0;
		cpu->psw.af = 0; /* 286 undefined */
		cpu->psw.cf = 0; /* 286 undefined */
		cpu->psw.of = 0; /* 286 undefined */
		i80386_exception(cpu, INT_DBZ);
	}
#endif
}
void i80386_alu_aad(I80386* cpu, uint16_t* x1, uint8_t divisor) {
	uint8_t product = (*x1 >> 8) * divisor;
	uint8_t tmp = *x1 & 0xFF;
	i80386_alu_add8(cpu, &tmp, product);
	*x1 = tmp;
#if 0
	uint8_t product = *h * divisor;
	i80386_alu_add8(cpu, l, product);
	*h = 0;

	cpu->psw.af = 0; /* 286 undefined */
	cpu->psw.cf = 0; /* 286 undefined */
	cpu->psw.of = 0; /* 286 undefined */
#endif
}

/* 8bit alu */

void i80386_alu_add8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = (*x1 + x2);
	SET_AF_ADD8(*x1, x2, tmp);
	SET_OF_ADD8(*x1, x2, tmp);
	SET_CF_ADD8(tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80386_alu_adc8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = ((uint16_t)*x1 + (uint16_t)x2 + cpu->psw.cf);
	SET_AF_ADD8(*x1, x2, tmp);
	SET_OF_ADD8(*x1, x2, tmp);
	SET_CF_ADD8(tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80386_alu_sub8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = (*x1 - x2);
	SET_AF_SUB8(*x1, x2, tmp);
	SET_OF_SUB8(*x1, x2, tmp);
	SET_CF_SUB8(*x1, x2);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80386_alu_sbb8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	uint16_t tmp = ((uint16_t)*x1 - ((uint16_t)x2 + cpu->psw.cf));
	SET_AF_SUB8(*x1, x2, tmp);
	SET_OF_SUB8(*x1, x2, tmp);
	SET_CF_SUB8(*x1, ((uint16_t)x2 + cpu->psw.cf));
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}

void i80386_alu_and8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	*x1 &= x2;
	cpu->psw.cf = 0;
	cpu->psw.of = 0;
	cpu->psw.af = 0; /* 286 undefined */
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80386_alu_xor8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	*x1 ^= x2;
	cpu->psw.cf = 0;
	cpu->psw.of = 0;
	cpu->psw.af = 0; /* 286 undefined */
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80386_alu_or8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	*x1 |= x2;
	cpu->psw.cf = 0;
	cpu->psw.of = 0;
	cpu->psw.af = 0; /* 286 undefined */
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}

void i80386_alu_cmp8(I80386* cpu, uint8_t*  x1, uint8_t x2) {
	uint8_t x0 = *x1; 
	i80386_alu_sub8(cpu, &x0, x2); /* discard result */
}
void i80386_alu_test8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	uint8_t x0 = *x1;
	i80386_alu_and8(cpu, &x0, x2); /* discard x0 */
}

void i80386_alu_rcl8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		uint8_t cf = cpu->psw.cf;
		cpu->psw.cf = (*x1 >> 7U) & 1U;
		*x1 = (*x1 << 1U) | cf;
	}
	if (x2 != 0) {
		cpu->psw.of = ((*x1 >> 7U) & 1U) ^ cpu->psw.cf;
	}
}
void i80386_alu_rcr8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		uint8_t cf = cpu->psw.cf;
		cpu->psw.cf = (*x1 & 1U);
		*x1 = (*x1 >> 1U) | (cf << 7U);
	}
	if (x2 != 0) {
		cpu->psw.of = (*x1 >> 7U) ^ ((*x1 >> 6U) & 1U);
	}
}
void i80386_alu_rol8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 >> 7U) & 1U;
		*x1 = (*x1 << 1) | cpu->psw.cf;
	}
	if (x2 != 0) {
		cpu->psw.of = ((*x1 >> 7U) & 1U) ^ cpu->psw.cf;
	}
}
void i80386_alu_ror8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 & 1U);
		*x1 = (*x1 >> 1) | (cpu->psw.cf << 7U);
	}
	if (x2 != 0) {
		cpu->psw.of = (*x1 >> 7U) ^ ((*x1 >> 6U) & 1U);
	}
}
void i80386_alu_shl8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 >> 7U) & 1U;
		*x1 <<= 1;
	}
	if (x2 != 0) {
		cpu->psw.of = ((*x1 >> 7) & 1U) ^ cpu->psw.cf;
		cpu->psw.af = (*x1 & 0x10) != 0; /* 286 undefined */
		SET_PF8(*x1);
		SET_SF8(*x1);
		SET_ZF8(*x1);
	}
}
void i80386_alu_shr8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	x2 &= 0x1F;
	if (x2 == 1U) {
		cpu->psw.of = (*x1 >> 7U) & 1U;
	}
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 & 1U);
		*x1 >>= 1U;
	}
	if (x2 != 0) {
		cpu->psw.af = 1; /* 286 undefined */
		SET_SF8(*x1);
		SET_PF8(*x1);
		SET_ZF8(*x1);
	}
}
void i80386_alu_sal8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	/* SAL is identical to SHL */
	i80386_alu_shl8(cpu, x1, x2);
}
void i80386_alu_sar8(I80386* cpu, uint8_t* x1, uint8_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 & 1);
		uint8_t msb = (*x1 & 0x80);
		*x1 >>= 1;
		*x1 |= msb;
	}
	if (x2 != 0) {
		cpu->psw.of = 0;
		cpu->psw.af = 1; /* 286 undefined */
		SET_SF8(*x1);
		SET_PF8(*x1);
		SET_ZF8(*x1);
	}
}

void i80386_alu_inc8(I80386* cpu, uint8_t* x1) {
	uint16_t tmp = (*x1 + 1);
	SET_AF_ADD8(*x1, 1, tmp);
	SET_OF_ADD8(*x1, 1, tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}
void i80386_alu_dec8(I80386* cpu, uint8_t* x1) {
	uint16_t tmp = (*x1 - 1);
	SET_AF_SUB8(*x1, 1, tmp);
	SET_OF_SUB8(*x1, 1, tmp);
	*x1 = (tmp & 0xFF);
	SET_SF8(*x1);
	SET_PF8(*x1);
	SET_ZF8(*x1);
}

void i80386_alu_mul8(I80386* cpu, uint8_t multiplicand, uint8_t multiplier, uint16_t* product) {
	uint16_t p = (uint16_t)multiplicand * multiplier;
	*product = p;

	if (p & 0xFF00) {
		cpu->psw.cf = 1;
		cpu->psw.of = 1;
	}
	else {
		cpu->psw.cf = 0;
		cpu->psw.of = 0;
	}
}
void i80386_alu_imul8(I80386* cpu, uint8_t multiplicand, uint8_t multiplier, uint16_t* product) {
	int16_t p = (int8_t)multiplicand * (int8_t)multiplier;
	*product = (uint16_t)p;

	if (p < -128 || p > 127) {
		cpu->psw.cf = 1;
		cpu->psw.of = 1;
	}
	else {
		cpu->psw.cf = 0;
		cpu->psw.of = 0;
	}
}

void i80386_alu_div8(I80386* cpu, uint16_t dividend, uint8_t divider, uint8_t* quotient, uint8_t* remainder) {
	if (divider == 0) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	uint16_t q = dividend / divider;
	uint16_t r = dividend % divider;

	if (q > 0xFF) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFF;
	*remainder = r & 0xFF;
}
void i80386_alu_idiv8(I80386* cpu, uint16_t dividend, uint8_t divider, uint8_t* quotient, uint8_t* remainder) {
	if (divider == 0) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	int16_t q = (int16_t)dividend / (int8_t)divider;
	int16_t r = (int16_t)dividend % (int8_t)divider;
	if (q < -128 || q > 127) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFF;
	*remainder = r & 0xFF;
}

void i80386_alu_neg8(I80386* cpu, uint8_t* x1) {
	uint8_t x2 = *x1;
	*x1 = 0;
	i80386_alu_sub8(cpu, x1, x2);
}
void i80386_alu_not8(I80386* cpu, uint8_t* x1) {
	(void)cpu;
	*x1 = ~(*x1);
}

/* 16bit alu */

void i80386_alu_add16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = (*x1 + x2);
	SET_AF_ADD16(*x1, x2, tmp);
	SET_OF_ADD16(*x1, x2, tmp);
	SET_CF_ADD16(tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80386_alu_adc16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = ((uint32_t)*x1 + (uint32_t)x2 + cpu->psw.cf);
	SET_AF_ADD16(*x1, x2, tmp);
	SET_OF_ADD16(*x1, x2, tmp);
	SET_CF_ADD16(tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80386_alu_sub16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = (*x1 - x2);
	SET_AF_SUB16(*x1, x2, tmp);
	SET_OF_SUB16(*x1, x2, tmp);
	SET_CF_SUB16(*x1, x2);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80386_alu_sbb16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	uint32_t tmp = ((uint32_t)*x1 - ((uint32_t)x2 + cpu->psw.cf));
	SET_AF_SUB16(*x1, x2, tmp);
	SET_OF_SUB16(*x1, x2, tmp);
	SET_CF_SUB16(*x1, ((uint32_t)x2 + cpu->psw.cf));
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void i80386_alu_and16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	*x1 &= x2;
	cpu->psw.cf = 0;
	cpu->psw.of = 0;
	cpu->psw.af = 0; /* 286 undefined */
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80386_alu_xor16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	*x1 ^= x2;
	cpu->psw.cf = 0;
	cpu->psw.of = 0;
	cpu->psw.af = 0; /* 286 undefined */
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80386_alu_or16(I80386*  cpu, uint16_t* x1, uint16_t x2) {
	*x1 |= x2;
	cpu->psw.cf = 0;
	cpu->psw.of = 0;
	cpu->psw.af = 0; /* 286 undefined */
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void i80386_alu_cmp16(I80386* cpu, uint16_t*  x1, uint16_t x2) {
	uint16_t x0 = *x1; 
	i80386_alu_sub16(cpu, &x0, x2); /* discard x0 */
}
void i80386_alu_test16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	uint16_t x0 = *x1;
	i80386_alu_and16(cpu, &x0, x2); /* discard x0 */
}

void i80386_alu_rcl16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		uint8_t cf = cpu->psw.cf;
		cpu->psw.cf = (*x1 >> 15U) & 1U;
		*x1 = (*x1 << 1U) | cf;
	}
	if (x2 != 0) {
		cpu->psw.of = ((*x1 >> 15U) & 1U) ^ cpu->psw.cf;
	}
}
void i80386_alu_rcr16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		uint8_t cf = cpu->psw.cf;
		cpu->psw.cf = (*x1 & 1U);		
		*x1 = (*x1 >> 1U) | (cf << 15U);
	}
	if (x2 != 0) {
		cpu->psw.of = (*x1 >> 15) ^ ((*x1 >> 14) & 1);
	}
}
void i80386_alu_rol16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 >> 15U) & 1U;
		*x1 = (*x1 << 1U) | cpu->psw.cf;
	}
	if (x2 != 0) {
		cpu->psw.of = ((*x1 >> 15U) & 1U) ^ cpu->psw.cf;
	}
}
void i80386_alu_ror16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 & 1U);
		*x1 = (*x1 >> 1) | (cpu->psw.cf << 15U);
	}
	if (x2 != 0) {
		cpu->psw.of = (*x1 >> 15U) ^ ((*x1 >> 14U) & 1U);
	}
}
void i80386_alu_shl16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 >> 15U) & 1U;
		*x1 <<= 1U;	
	}
	if (x2 != 0) {
		cpu->psw.of = ((*x1 >> 15U) & 1U) ^ cpu->psw.cf;
		cpu->psw.af = (*x1 & 0x10) != 0; /* 286 undefined */
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}
void i80386_alu_shr16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	x2 &= 0x1F;
	if (x2 == 1) {
		cpu->psw.of = (*x1 >> 15U) & 1U;
	}
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 & 1U);
		*x1 >>= 1U;
	}
	if (x2 != 0) {
		cpu->psw.af = 1; /* 286 undefined */
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}
void i80386_alu_sal16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	/* SAL is identical to SHL */
	i80386_alu_shl16(cpu, x1, x2);
}
void i80386_alu_sar16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	x2 &= 0x1F;
	for (int i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 & 1);
		uint16_t msb = (*x1 & 0x8000);
		*x1 >>= 1;
		*x1 |= msb;
	}
	if (x2 != 0) {
		cpu->psw.of = 0;
		cpu->psw.af = 1; /* 286 undefined */
		SET_SF16(*x1);
		SET_PF16(*x1);
		SET_ZF16(*x1);
	}
}
void i80386_alu_shld16(I80386* cpu, uint16_t* x1, uint16_t x2, uint8_t x3) {
	if (x3 == 0) {
		return;
	}
	cpu->psw.cf = (*x1 >> (16U - x3)) & 1U;
	*x1 = (*x1 << x3) | (x2 >> (16U - x3));
	if (x3 == 1) {
		cpu->psw.of = ((*x1 >> 15U) & 1U) ^ cpu->psw.cf;
	}
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80386_alu_shrd16(I80386* cpu, uint16_t* x1, uint16_t x2, uint8_t x3) {
	if (x3 == 0) {
		return;
	}
	cpu->psw.cf = (*x1 >> (x3 - 1U)) & 1U;
	*x1 = (*x1 >> x3) | (x2 << (16U - x3));
	if (x3 == 1) {
		cpu->psw.of = ((*x1 >> 15U) & 1U) ^ ((*x1 >> 14U) & 1U);
	}
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void i80386_alu_bt16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	cpu->psw.cf = (*x1 >> x2) & 1U;
}
void i80386_alu_bts16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	i80386_alu_bt16(cpu, x1, x2);
	*x1 |= (1U << x2);
}
void i80386_alu_btr16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	i80386_alu_bt16(cpu, x1, x2);
	*x1 &= ~(1U << x2);
}
void i80386_alu_btc16(I80386* cpu, uint16_t* x1, uint16_t x2) {
	i80386_alu_bt16(cpu, x1, x2);
	*x1 ^= (1U << x2);
}

void i80386_alu_inc16(I80386* cpu, uint16_t* x1) {
	uint32_t tmp = (*x1 + 1U);
	SET_AF_ADD16(*x1, 1U, tmp);
	SET_OF_ADD16(*x1, 1U, tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}
void i80386_alu_dec16(I80386* cpu, uint16_t* x1) {
	uint32_t tmp = (*x1 - 1U);
	SET_AF_SUB16(*x1, 1U, tmp);
	SET_OF_SUB16(*x1, 1U, tmp);
	*x1 = (tmp & 0xFFFF);
	SET_SF16(*x1);
	SET_PF16(*x1);
	SET_ZF16(*x1);
}

void i80386_alu_mul16(I80386* cpu, uint16_t multiplicand, uint16_t multiplier, uint32_t* product) {
	uint32_t p = (uint32_t)multiplicand * multiplier;
	*product = p;

	if (p & 0xFFFF0000) {
		cpu->psw.cf = 1;
		cpu->psw.of = 1;
	}
	else {
		cpu->psw.cf = 0;
		cpu->psw.of = 0;
	}
}
void i80386_alu_imul16(I80386* cpu, uint16_t multiplicand, uint16_t multiplier, uint32_t* product) {
	int32_t p = (int16_t)multiplicand * (int16_t)multiplier;
	*product = (uint32_t)p;

	if (p < -32768 || p > 32767) {
		cpu->psw.cf = 1;
		cpu->psw.of = 1;
	}
	else {
		cpu->psw.cf = 0;
		cpu->psw.of = 0;
	}
}

void i80386_alu_div16(I80386* cpu, uint32_t dividend, uint16_t divider, uint16_t* quotient, uint16_t* remainder) {
	if (divider == 0) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	uint32_t q = dividend / divider;
	uint32_t r = dividend % divider;

	if (q > 0xFFFF) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFFFF;
	*remainder = r & 0xFFFF;
}
void i80386_alu_idiv16(I80386* cpu, uint32_t dividend, uint16_t divider, uint16_t* quotient, uint16_t* remainder) {
	if (divider == 0) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	int32_t q = (int32_t)dividend / (int16_t)divider;
	int32_t r = (int32_t)dividend % (int16_t)divider;

	if (q < -32768 || q > 32767) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFFFF;
	*remainder = r & 0xFFFF;
}

void i80386_alu_neg16(I80386* cpu, uint16_t* x1) {
	uint16_t x2 = *x1;
	*x1 = 0;
	i80386_alu_sub16(cpu, x1, x2);
}
void i80386_alu_not16(I80386* cpu, uint16_t* x1) {
	(void)cpu; 
	*x1 = ~(*x1);
}

/* 32bit alu */

void i80386_alu_add32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	uint64_t tmp = (uint64_t)*x1 + x2;
	SET_AF_ADD32(*x1, x2, tmp);
	SET_OF_ADD32(*x1, x2, tmp);
	SET_CF_ADD32(tmp);
	*x1 = (tmp & 0xFFFFFFFF);
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}
void i80386_alu_adc32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	uint64_t tmp = (uint64_t)*x1 + x2 + cpu->psw.cf;
	SET_AF_ADD32(*x1, x2, tmp);
	SET_OF_ADD32(*x1, x2, tmp);
	SET_CF_ADD32(tmp);
	*x1 = (tmp & 0xFFFFFFFF);
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}
void i80386_alu_sub32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	uint64_t tmp = (uint64_t)*x1 - x2;
	SET_AF_SUB32(*x1, x2, tmp);
	SET_OF_SUB32(*x1, x2, tmp);
	SET_CF_SUB32(*x1, x2);
	*x1 = (tmp & 0xFFFFFFFF);
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}
void i80386_alu_sbb32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	uint64_t tmp = (uint64_t)*x1 - ((uint64_t)x2 + cpu->psw.cf);
	SET_AF_SUB32(*x1, x2, tmp);
	SET_OF_SUB32(*x1, x2, tmp);
	SET_CF_SUB32(*x1, ((uint64_t)x2 + cpu->psw.cf));
	*x1 = (tmp & 0xFFFFFFFF);
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}

void i80386_alu_and32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	*x1 &= x2;
	cpu->psw.cf = 0;
	cpu->psw.of = 0;
	cpu->psw.af = 0; /* 286 undefined */
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}
void i80386_alu_xor32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	*x1 ^= x2;
	cpu->psw.cf = 0;
	cpu->psw.of = 0;
	cpu->psw.af = 0; /* 286 undefined */
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}
void i80386_alu_or32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	*x1 |= x2;
	cpu->psw.cf = 0;
	cpu->psw.of = 0;
	cpu->psw.af = 0; /* 286 undefined */
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}

void i80386_alu_cmp32(I80386* cpu, uint32_t*  x1, uint32_t x2) {
	uint32_t x0 = *x1; 
	i80386_alu_sub32(cpu, &x0, x2); /* discard x0 */
}
void i80386_alu_test32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	uint32_t x0 = *x1;
	i80386_alu_and32(cpu, &x0, x2); /* discard x0 */
}

void i80386_alu_rcl32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	x2 &= 0x1F;
	for (uint32_t i = 0; i < x2; ++i) {
		uint8_t cf = cpu->psw.cf;
		cpu->psw.cf = (*x1 >> 31U) & 1U;
		*x1 = (*x1 << 1U) | cf;
	}
	if (x2 != 0) {
		cpu->psw.of = ((*x1 >> 31U) & 1U) ^ cpu->psw.cf;
	}
}
void i80386_alu_rcr32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	x2 &= 0x1F;
	for (uint32_t i = 0; i < x2; ++i) {
		uint8_t cf = cpu->psw.cf;
		cpu->psw.cf = (*x1 & 1U);
		*x1 = (*x1 >> 1U) | (cf << 31U);
	}
	if (x2 != 0) {
		cpu->psw.of = (*x1 >> 31U) ^ ((*x1 >> 30U) & 1U);
	}
}
void i80386_alu_rol32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	x2 &= 0x1F;
	for (uint32_t i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 >> 31U) & 1U;
		*x1 = (*x1 << 1U) | cpu->psw.cf;
	}
	if (x2 != 0) {
		cpu->psw.of = ((*x1 >> 31U) & 1U) ^ cpu->psw.cf;
	}
}
void i80386_alu_ror32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	x2 &= 0x1F;
	for (uint32_t i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 & 1U);
		*x1 = (*x1 >> 1U) | (cpu->psw.cf << 31U);
	}
	if (x2 != 0) {
		cpu->psw.of = (*x1 >> 31U) ^ ((*x1 >> 30U) & 1U);
	}
}
void i80386_alu_shl32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	x2 &= 0x1F;
	for (uint32_t i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 >> 31U) & 1U;
		*x1 <<= 1;
	}
	if (x2 != 0) {
		cpu->psw.of = ((*x1 >> 31U) & 1U) ^ cpu->psw.cf;
		cpu->psw.af = (*x1 & 0x10) != 0; /* 286 undefined */
		SET_SF32(*x1);
		SET_PF32(*x1);
		SET_ZF32(*x1);
	}
}
void i80386_alu_shr32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	x2 &= 0x1F;
	if (x2 == 1) {
		cpu->psw.of = (*x1 >> 31U) & 1U;
	}
	for (uint32_t i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 & 1U);
		*x1 >>= 1;
	}
	if (x2 != 0) {
		cpu->psw.af = 1; /* 286 undefined */
		SET_SF32(*x1);
		SET_PF32(*x1);
		SET_ZF32(*x1);
	}
}
void i80386_alu_sal32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	/* SAL is identical to SHL */
	i80386_alu_shl32(cpu, x1, x2);
}
void i80386_alu_sar32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	x2 &= 0x1F;
	for (uint32_t i = 0; i < x2; ++i) {
		cpu->psw.cf = (*x1 & 1U);
		uint32_t msb = (*x1 & 0x80000000);
		*x1 >>= 1;
		*x1 |= msb;
	}
	if (x2 != 0) {
		cpu->psw.of = 0;
		cpu->psw.af = 1; /* 286 undefined */
		SET_SF32(*x1);
		SET_PF32(*x1);
		SET_ZF32(*x1);
	}
}
void i80386_alu_shld32(I80386* cpu, uint32_t* x1, uint32_t x2, uint8_t x3) {
	if (x3 == 0) {
		return;
	}
	cpu->psw.cf = (*x1 >> (32U - x3)) & 1U;
	*x1 = (*x1 << x3) | (x2 >> (32U - x3));
	if (x3 == 1) {
		cpu->psw.of = ((*x1 >> 31U) & 1U) ^ cpu->psw.cf;
	}
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}
void i80386_alu_shrd32(I80386* cpu, uint32_t* x1, uint32_t x2, uint8_t x3) {
	if (x3 == 0) {
		return;
	}
	cpu->psw.cf = (*x1 >> (x3 - 1U)) & 1U;
	*x1 = (*x1 >> x3) | (x2 << (32U - x3));
	if (x3 == 1) {
		cpu->psw.of = ((*x1 >> 31U) & 1U) ^ ((*x1 >> 30U) & 1U);
	}
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}

void i80386_alu_bt32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	cpu->psw.cf = (*x1 >> x2) & 1U;
}
void i80386_alu_bts32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	i80386_alu_bt32(cpu, x1, x2);
	*x1 |= (1U << x2);
}
void i80386_alu_btr32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	i80386_alu_bt32(cpu, x1, x2);
	*x1 &= ~(1U << x2);
}
void i80386_alu_btc32(I80386* cpu, uint32_t* x1, uint32_t x2) {
	i80386_alu_bt32(cpu, x1, x2);
	*x1 ^= (1U << x2);
}

void i80386_alu_inc32(I80386* cpu, uint32_t* x1) {
	uint64_t tmp = (uint64_t)*x1 + 1U;
	SET_AF_ADD32(*x1, 1U, tmp);
	SET_OF_ADD32(*x1, 1U, tmp);
	*x1 = (tmp & 0xFFFFFFFF);
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}
void i80386_alu_dec32(I80386* cpu, uint32_t* x1) {
	uint64_t tmp = (uint64_t)*x1 - 1U;
	SET_AF_SUB32(*x1, 1U, tmp);
	SET_OF_SUB32(*x1, 1U, tmp);
	*x1 = (tmp & 0xFFFFFFFF);
	SET_SF32(*x1);
	SET_PF32(*x1);
	SET_ZF32(*x1);
}

void i80386_alu_mul32(I80386* cpu, uint32_t multiplicand, uint32_t multiplier, uint64_t* product) {
	uint64_t p = (uint64_t)multiplicand * multiplier;
	if (p & 0xFFFFFFFF00000000) {
		cpu->psw.cf = 1;
		cpu->psw.of = 1;
	}
	else {
		cpu->psw.cf = 0;
		cpu->psw.of = 0;
	}

	if (product) {
		*product = p;
	}
}
void i80386_alu_imul32(I80386* cpu, uint32_t multiplicand, uint32_t multiplier, uint64_t* product) {
	int64_t p = (int64_t)multiplicand * multiplier;

	if (p < -2147483648i64 || p > 2147483647i64) {
		cpu->psw.cf = 1;
		cpu->psw.of = 1;
	}
	else {
		cpu->psw.cf = 0;
		cpu->psw.of = 0;
	}

	if (product) {
		*product = (uint64_t)p;
	}
}

void i80386_alu_div32(I80386* cpu, uint64_t dividend, uint32_t divider, uint32_t* quotient, uint32_t* remainder) {
	if (divider == 0) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	uint64_t q = dividend / divider;
	uint64_t r = dividend % divider;

	if (q > 0xFFFFFFFF) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFFFFFFFF;
	*remainder = r & 0xFFFFFFFF;
}
void i80386_alu_idiv32(I80386* cpu, uint64_t dividend, uint32_t divider, uint32_t* quotient, uint32_t* remainder) {
	if (divider == 0) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	int64_t q = (int64_t)dividend / (int64_t)divider;
	int64_t r = (int64_t)dividend % (int64_t)divider;

	if (q < -2147483648i64 || q > 2147483647i64) {
		i80386_exception(cpu, INT_DBZ);
		return;
	}

	*quotient = q & 0xFFFFFFFF;
	*remainder = r & 0xFFFFFFFF;
}

void i80386_alu_neg32(I80386* cpu, uint32_t* x1) {
	uint32_t x2 = *x1;
	*x1 = 0;
	i80386_alu_sub32(cpu, x1, x2);
}
void i80386_alu_not32(I80386* cpu, uint32_t* x1) {
	(void)cpu;
	*x1 = ~(*x1);
}

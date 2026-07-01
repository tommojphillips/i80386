/* alu.h
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 Arithmetic Logic Unit
 */

#ifndef I8086_ALU_H
#define I8086_ALU_H

#include <stdint.h>

typedef struct I8086 I8086;

#ifdef __cplusplus
extern "C" {
#endif

void alu_daa(I8086* cpu, uint8_t* x1);
void alu_das(I8086* cpu, uint8_t* x1);
void alu_aaa(I8086* cpu, uint8_t* l, uint8_t* h);
void alu_aas(I8086* cpu, uint8_t* l, uint8_t* h);
void alu_aad(I8086* cpu, uint8_t* l, uint8_t* h, uint8_t divisor);
void alu_aam(I8086* cpu, uint8_t* l, uint8_t* h, uint8_t divisor);

/* 8bit ALU */

void alu_and8(I8086* cpu, uint8_t* x1, uint8_t x2);
void alu_xor8(I8086* cpu, uint8_t* x1, uint8_t x2);
void alu_or8(I8086*  cpu, uint8_t* x1, uint8_t x2);
void alu_add8(I8086* cpu, uint8_t* x1, uint8_t x2);
void alu_adc8(I8086* cpu, uint8_t* x1, uint8_t x2);
void alu_sub8(I8086* cpu, uint8_t* x1, uint8_t x2);
void alu_sbb8(I8086* cpu, uint8_t* x1, uint8_t x2);
void alu_cmp8(I8086* cpu, uint8_t  x1, uint8_t x2);
void alu_test8(I8086* cpu, uint8_t x1, uint8_t x2);

void alu_rcl8(I8086* cpu, uint8_t* x1, uint8_t count);
void alu_rcr8(I8086* cpu, uint8_t* x1, uint8_t count);
void alu_rol8(I8086* cpu, uint8_t* x1, uint8_t count);
void alu_ror8(I8086* cpu, uint8_t* x1, uint8_t count);
void alu_shl8(I8086* cpu, uint8_t* x1, uint8_t count);
void alu_shr8(I8086* cpu, uint8_t* x1, uint8_t count);
void alu_sar8(I8086* cpu, uint8_t* x1, uint8_t count);

void alu_inc8(I8086* cpu, uint8_t* x1);
void alu_dec8(I8086* cpu, uint8_t* x1);

void alu_mul8(I8086* cpu, uint8_t multiplicand, uint8_t multiplier, uint8_t* lo, uint8_t* hi);
void alu_imul8(I8086* cpu, uint8_t multiplicand, uint8_t multiplier, uint8_t* lo, uint8_t* hi);
void alu_div8(I8086* cpu, uint8_t dividend_lo, uint8_t dividend_hi, uint8_t divider, uint8_t* quotient, uint8_t* remainder);
void alu_idiv8(I8086* cpu, uint8_t dividend_lo, uint8_t dividend_hi, uint8_t divider, uint8_t* quotient, uint8_t* remainder);

void alu_neg8(I8086* cpu, uint8_t* x1);

void alu_setmo8(I8086* cpu, uint8_t* x1, uint8_t count);

/* 16bit ALU */

void alu_and16(I8086* cpu, uint16_t* x1, uint16_t x2);
void alu_xor16(I8086* cpu, uint16_t* x1, uint16_t x2);
void alu_or16(I8086*  cpu, uint16_t* x1, uint16_t x2);
void alu_add16(I8086* cpu, uint16_t* x1, uint16_t x2);
void alu_adc16(I8086* cpu, uint16_t* x1, uint16_t x2);
void alu_sub16(I8086* cpu, uint16_t* x1, uint16_t x2);
void alu_sbb16(I8086* cpu, uint16_t* x1, uint16_t x2);
void alu_cmp16(I8086* cpu, uint16_t  x1, uint16_t x2);
void alu_test16(I8086* cpu, uint16_t x1, uint16_t x2);

void alu_rcl16(I8086* cpu, uint16_t* x1, uint8_t count);
void alu_rcr16(I8086* cpu, uint16_t* x1, uint8_t count);
void alu_rol16(I8086* cpu, uint16_t* x1, uint8_t count);
void alu_ror16(I8086* cpu, uint16_t* x1, uint8_t count);
void alu_shl16(I8086* cpu, uint16_t* x1, uint8_t count);
void alu_shr16(I8086* cpu, uint16_t* x1, uint8_t count);
void alu_sar16(I8086* cpu, uint16_t* x1, uint8_t count);

void alu_inc16(I8086* cpu, uint16_t* x1);
void alu_dec16(I8086* cpu, uint16_t* x1);

void alu_mul16(I8086* cpu, uint16_t multiplicand, uint16_t multiplier, uint16_t* lo, uint16_t* hi);
void alu_imul16(I8086* cpu, uint16_t multiplicand, uint16_t multiplier, uint16_t* lo, uint16_t* hi);
void alu_div16(I8086* cpu, uint16_t dividend_lo, uint16_t dividend_hi, uint16_t divider, uint16_t* quotient, uint16_t* remainder);
void alu_idiv16(I8086* cpu, uint16_t dividend_lo, uint16_t dividend_hi, uint16_t divider, uint16_t* quotient, uint16_t* remainder);

void alu_neg16(I8086* cpu, uint16_t* x1);

void alu_setmo16(I8086* cpu, uint16_t* x1, uint8_t count);

#ifdef __cplusplus
};
#endif

#endif

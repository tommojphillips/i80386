/* i80286_alu.h
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 Arithmetic Logic Unit
 */

#ifndef I80286_ALU_H
#define I80286_ALU_H

#include <stdint.h>

typedef struct I80286 I80286;

#ifdef __cplusplus
extern "C" {
#endif

void i80286_alu_daa(I80286* cpu, uint8_t* x1);
void i80286_alu_das(I80286* cpu, uint8_t* x1);
void i80286_alu_aaa(I80286* cpu, uint8_t* l, uint8_t* h);
void i80286_alu_aas(I80286* cpu, uint8_t* l, uint8_t* h);
void i80286_alu_aad(I80286* cpu, uint8_t* l, uint8_t* h, uint8_t divisor);
void i80286_alu_aam(I80286* cpu, uint8_t* l, uint8_t* h, uint8_t divisor);

/* 8bit ALU */

void i80286_alu_and8(I80286* cpu, uint8_t* x1, uint8_t x2);
void i80286_alu_xor8(I80286* cpu, uint8_t* x1, uint8_t x2);
void i80286_alu_or8(I80286*  cpu, uint8_t* x1, uint8_t x2);
void i80286_alu_add8(I80286* cpu, uint8_t* x1, uint8_t x2);
void i80286_alu_adc8(I80286* cpu, uint8_t* x1, uint8_t x2);
void i80286_alu_sub8(I80286* cpu, uint8_t* x1, uint8_t x2);
void i80286_alu_sbb8(I80286* cpu, uint8_t* x1, uint8_t x2);
void i80286_alu_cmp8(I80286* cpu, uint8_t  x1, uint8_t x2);
void i80286_alu_test8(I80286* cpu, uint8_t x1, uint8_t x2);

void i80286_alu_rcl8(I80286* cpu, uint8_t* x1, uint8_t count);
void i80286_alu_rcr8(I80286* cpu, uint8_t* x1, uint8_t count);
void i80286_alu_rol8(I80286* cpu, uint8_t* x1, uint8_t count);
void i80286_alu_ror8(I80286* cpu, uint8_t* x1, uint8_t count);
void i80286_alu_shl8(I80286* cpu, uint8_t* x1, uint8_t count);
void i80286_alu_shr8(I80286* cpu, uint8_t* x1, uint8_t count);
void i80286_alu_sal8(I80286* cpu, uint8_t* x1, uint8_t count);
void i80286_alu_sar8(I80286* cpu, uint8_t* x1, uint8_t count);

void i80286_alu_inc8(I80286* cpu, uint8_t* x1);
void i80286_alu_dec8(I80286* cpu, uint8_t* x1);

void i80286_alu_mul8(I80286* cpu, uint8_t multiplicand, uint8_t multiplier, uint16_t* product);
void i80286_alu_imul8(I80286* cpu, uint8_t multiplicand, uint8_t multiplier, uint16_t* product);
void i80286_alu_div8(I80286* cpu, uint16_t dividend, uint8_t divider, uint8_t* quotient, uint8_t* remainder);
void i80286_alu_idiv8(I80286* cpu, uint16_t dividend, uint8_t divider, uint8_t* quotient, uint8_t* remainder);

void i80286_alu_neg8(I80286* cpu, uint8_t* x1);
void i80286_alu_not8(I80286* cpu, uint8_t* x1);

/* 16bit ALU */

void i80286_alu_and16(I80286* cpu, uint16_t* x1, uint16_t x2);
void i80286_alu_xor16(I80286* cpu, uint16_t* x1, uint16_t x2);
void i80286_alu_or16(I80286*  cpu, uint16_t* x1, uint16_t x2);
void i80286_alu_add16(I80286* cpu, uint16_t* x1, uint16_t x2);
void i80286_alu_adc16(I80286* cpu, uint16_t* x1, uint16_t x2);
void i80286_alu_sub16(I80286* cpu, uint16_t* x1, uint16_t x2);
void i80286_alu_sbb16(I80286* cpu, uint16_t* x1, uint16_t x2);
void i80286_alu_cmp16(I80286* cpu, uint16_t  x1, uint16_t x2);
void i80286_alu_test16(I80286* cpu, uint16_t x1, uint16_t x2);

void i80286_alu_rcl16(I80286* cpu, uint16_t* x1, uint8_t count);
void i80286_alu_rcr16(I80286* cpu, uint16_t* x1, uint8_t count);
void i80286_alu_rol16(I80286* cpu, uint16_t* x1, uint8_t count);
void i80286_alu_ror16(I80286* cpu, uint16_t* x1, uint8_t count);
void i80286_alu_shl16(I80286* cpu, uint16_t* x1, uint8_t count);
void i80286_alu_shr16(I80286* cpu, uint16_t* x1, uint8_t count);
void i80286_alu_sal16(I80286* cpu, uint16_t* x1, uint8_t count);
void i80286_alu_sar16(I80286* cpu, uint16_t* x1, uint8_t count);

void i80286_alu_inc16(I80286* cpu, uint16_t* x1);
void i80286_alu_dec16(I80286* cpu, uint16_t* x1);

void i80286_alu_mul16(I80286* cpu, uint16_t multiplicand, uint16_t multiplier, uint32_t* product);
void i80286_alu_imul16(I80286* cpu, uint16_t multiplicand, uint16_t multiplier, uint32_t* product);
void i80286_alu_div16(I80286* cpu, uint32_t dividend, uint16_t divider, uint16_t* quotient, uint16_t* remainder);
void i80286_alu_idiv16(I80286* cpu, uint32_t dividend, uint16_t divider, uint16_t* quotient, uint16_t* remainder);

void i80286_alu_neg16(I80286* cpu, uint16_t* x1);
void i80286_alu_not16(I80286* cpu, uint16_t* x1);

#ifdef __cplusplus
};
#endif

#endif

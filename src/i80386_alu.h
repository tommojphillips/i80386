/* i80386_alu.h
 * Thomas J. Armytage 2025-2026 ( https://github.com/tommojphillips/ )
 * Intel 80386 Arithmetic Logic Unit
 */

#ifndef I80386_ALU_H
#define I80386_ALU_H

#include <stdint.h>

typedef struct I80386 I80386;

typedef void(*I80386_ALU8_FUNC)(I80386* cpu, uint8_t* lhs, uint8_t rhs);
typedef void(*I80386_ALU16_FUNC)(I80386* cpu, uint16_t* lhs, uint16_t rhs);
typedef void(*I80386_ALU32_FUNC)(I80386* cpu, uint32_t* lhs, uint32_t rhs);

#ifdef __cplusplus
extern "C" {
#endif

void i80386_alu_daa(I80386* cpu, uint8_t* x1);
void i80386_alu_das(I80386* cpu, uint8_t* x1);
void i80386_alu_aaa(I80386* cpu, uint16_t* x1);
void i80386_alu_aas(I80386* cpu, uint16_t* x1);
void i80386_alu_aad(I80386* cpu, uint16_t* x1, uint8_t divisor);
void i80386_alu_aam(I80386* cpu, uint16_t* x1, uint8_t divisor);

/* 8bit ALU */

void i80386_alu_and8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_xor8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_or8(I80386*  cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_add8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_adc8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_sub8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_sbb8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_cmp8(I80386* cpu, uint8_t*  x1, uint8_t x2);
void i80386_alu_test8(I80386* cpu, uint8_t* x1, uint8_t x2);

void i80386_alu_rcl8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_rcr8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_rol8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_ror8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_shl8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_shr8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_sal8(I80386* cpu, uint8_t* x1, uint8_t x2);
void i80386_alu_sar8(I80386* cpu, uint8_t* x1, uint8_t x2);

void i80386_alu_inc8(I80386* cpu, uint8_t* x1);
void i80386_alu_dec8(I80386* cpu, uint8_t* x1);

void i80386_alu_neg8(I80386* cpu, uint8_t* x1);
void i80386_alu_not8(I80386* cpu, uint8_t* x1);

void i80386_alu_mul8(I80386* cpu, uint8_t multiplicand, uint8_t multiplier, uint16_t* product);
void i80386_alu_imul8(I80386* cpu, uint8_t multiplicand, uint8_t multiplier, uint16_t* product);
void i80386_alu_div8(I80386* cpu, uint16_t dividend, uint8_t divider, uint8_t* quotient, uint8_t* remainder);
void i80386_alu_idiv8(I80386* cpu, uint16_t dividend, uint8_t divider, uint8_t* quotient, uint8_t* remainder);

/* 16bit ALU */

void i80386_alu_and16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_xor16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_or16(I80386*  cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_add16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_adc16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_sub16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_sbb16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_cmp16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_test16(I80386* cpu, uint16_t* x1, uint16_t x2);

void i80386_alu_rcl16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_rcr16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_rol16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_ror16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_shl16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_shr16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_sal16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_sar16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_shld16(I80386* cpu, uint16_t* x1, uint16_t x2, uint8_t x3);
void i80386_alu_shrd16(I80386* cpu, uint16_t* x1, uint16_t x2, uint8_t x3);

void i80386_alu_bt16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_bts16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_btr16(I80386* cpu, uint16_t* x1, uint16_t x2);
void i80386_alu_btc16(I80386* cpu, uint16_t* x1, uint16_t x2);

void i80386_alu_inc16(I80386* cpu, uint16_t* x1);
void i80386_alu_dec16(I80386* cpu, uint16_t* x1);

void i80386_alu_neg16(I80386* cpu, uint16_t* x1);
void i80386_alu_not16(I80386* cpu, uint16_t* x1);

void i80386_alu_mul16(I80386* cpu, uint16_t multiplicand, uint16_t multiplier, uint32_t* product);
void i80386_alu_imul16(I80386* cpu, uint16_t multiplicand, uint16_t multiplier, uint32_t* product);
void i80386_alu_div16(I80386* cpu, uint32_t dividend, uint16_t divider, uint16_t* quotient, uint16_t* remainder);
void i80386_alu_idiv16(I80386* cpu, uint32_t dividend, uint16_t divider, uint16_t* quotient, uint16_t* remainder);

/* 32bit ALU */

void i80386_alu_and32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_xor32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_or32(I80386*  cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_add32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_adc32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_sub32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_sbb32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_cmp32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_test32(I80386* cpu, uint32_t* x1, uint32_t x2);

void i80386_alu_rcl32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_rcr32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_rol32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_ror32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_shl32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_shr32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_sal32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_sar32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_shld32(I80386* cpu, uint32_t* x1, uint32_t x2, uint8_t x3);
void i80386_alu_shrd32(I80386* cpu, uint32_t* x1, uint32_t x2, uint8_t x3);

void i80386_alu_bt32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_bts32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_btr32(I80386* cpu, uint32_t* x1, uint32_t x2);
void i80386_alu_btc32(I80386* cpu, uint32_t* x1, uint32_t x2);

void i80386_alu_inc32(I80386* cpu, uint32_t* x1);
void i80386_alu_dec32(I80386* cpu, uint32_t* x1);

void i80386_alu_neg32(I80386* cpu, uint32_t* x1);
void i80386_alu_not32(I80386* cpu, uint32_t* x1);

void i80386_alu_mul32(I80386* cpu, uint32_t multiplicand, uint32_t multiplier, uint64_t* product);
void i80386_alu_imul32(I80386* cpu, uint32_t multiplicand, uint32_t multiplier, uint64_t* product);
void i80386_alu_div32(I80386* cpu, uint64_t dividend, uint32_t divider, uint32_t* quotient, uint32_t* remainder);
void i80386_alu_idiv32(I80386* cpu, uint64_t dividend, uint32_t divider, uint32_t* quotient, uint32_t* remainder);


#ifdef __cplusplus
};
#endif

#endif

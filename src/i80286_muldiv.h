/* i8086_muldiv.h
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 MUL/DIV Microcode Emulation
 */

#ifndef I8086_MULDIV_H
#define I8086_MULDIV_H

#include <stdint.h>

typedef struct I8086 I8086;

#ifdef __cplusplus
extern "C" {
#endif

void mc_div8(I8086* cpu, uint16_t dividend, uint8_t divisor, uint8_t is_signed, uint8_t* out_quotient, uint8_t* out_remainder);
void mc_mul8(I8086* cpu, uint8_t multiplicand, uint8_t multiplier, uint8_t is_signed, uint8_t* product_lo, uint8_t* product_hi);

void mc_div16(I8086* cpu, uint32_t dividend, uint16_t divisor, uint8_t is_signed, uint16_t* out_quotient, uint16_t* out_remainder);
void mc_mul16(I8086* cpu, uint16_t multiplicand, uint16_t multiplier, uint8_t is_signed, uint16_t* product_lo, uint16_t* product_hi);

#ifdef __cplusplus
};
#endif

#endif
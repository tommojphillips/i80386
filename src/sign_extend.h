/* sign_extend.h
 * Thomas J. Armytage 2025-2026 ( https://github.com/tommojphillips/ )
 * Sign Extend
 */

#ifndef SIGN_EXTEND_H
#define SIGN_EXTEND_H

#include <stdint.h>

uint16_t sign_extend8_16(uint8_t value);
uint32_t sign_extend8_32(uint8_t value);
uint32_t sign_extend16_32(uint16_t value);

#endif

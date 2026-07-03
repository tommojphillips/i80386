/* sign_extend.c
 * Thomas J. Armytage 2025-2026 ( https://github.com/tommojphillips/ )
 * Sign Extend
 */

#include <stdint.h>

uint16_t sign_extend8_16(uint8_t value) {
	uint16_t s = value;
	if (value & 0x80) {
		s |= 0xFF00;
	}
	return s;
}
uint32_t sign_extend8_32(uint8_t value) {
	uint32_t s = value;
	if (value & 0x80) {
		s |= 0xFFFFFF00;
	}
	return s;
}
uint32_t sign_extend16_32(uint16_t value) {
	uint32_t s = value;
	if (value & 0x8000) {
		s |= 0xFFFF0000;
	}
	return s;
}

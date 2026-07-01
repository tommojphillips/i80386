/* i8086_mnem.h
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 Mnemonics/Disassembler
 */

#ifndef I8086_MNEM_H
#define I8086_MNEM_H

#include <stdint.h>

#include "i8086.h"

/* I8086 CPU State */
typedef struct I8086_MNEM {
	char str[32];
	char addressing_str[32];
	char ea_str[32];
	I8086 const* state;     // CPU state
	uint16_t counter;       // instruction length
	uint16_t segment;		// CS
	uint16_t offset;		// IP
	uint8_t opcode;         // opcode
	uint8_t segment_prefix; // segement override index
	uint8_t internal_flags; // rep prefix
	I8086_MOD_RM modrm;     // modrm structure

	uint8_t has_ea;
	uint8_t step_over_has_target;
	uint8_t step_into_has_target;
	
	uint16_t ea_segment;
	uint16_t ea_offset;

	uint16_t step_over_segment;
	uint16_t step_over_offset;

	uint16_t step_into_segment;
	uint16_t step_into_offset;
} I8086_MNEM;

#ifdef __cplusplus
extern "C" {
#endif

/* Disassemble Opcode at CS:IP */
int i8086_mnem(I8086_MNEM* cpu);

/* Disassemble Opcode at seg:offset */
int i8086_mnem_at(I8086_MNEM* mnem, uint16_t seg, uint16_t offset);

void i8086_mnem_get_modrm(I8086_MNEM* mnem, char* buf, const char* fmt);

uint20_t i8086_mnem_get_step_over_target(I8086_MNEM* mnem);
uint20_t i8086_mnem_get_step_into_target(I8086_MNEM* mnem);

#ifdef __cplusplus
};
#endif
#endif

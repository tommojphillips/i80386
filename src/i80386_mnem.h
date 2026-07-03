/* i80286_mnem.h
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 80286 Mnemonics/Disassembler
 */

#ifndef I80286_MNEM_H
#define I80286_MNEM_H

#include <stdint.h>

#include "i80286.h"

/* I80286 CPU State */
typedef struct I80286_MNEM {
	char str[32];
	char addressing_str[32];
	I80286 const* state;     /* CPU state */
	uint16_t counter;        /* instruction length */
	uint16_t segment;		 /* CS */
	uint16_t offset;		 /* IP */
	uint8_t opcode;          /* opcode */
	uint8_t segment_prefix;  /* segement override index */
	uint8_t internal_flags;  /* rep prefix */
	I80286_MOD_RM modrm;     /* modrm structure */

	uint16_t step_over_has_target;
	uint16_t step_over_segment;
	uint16_t step_over_offset;
} I80286_MNEM;

#ifdef __cplusplus
extern "C" {
#endif

/* Disassemble Opcode at CS:IP */
int i80286_mnem(I80286_MNEM* cpu);

/* Disassemble Opcode at seg:offset */
int i80286_mnem_at(I80286_MNEM* mnem, uint16_t seg, uint16_t offset);

uint32_t i80286_mnem_get_step_over_target(I80286_MNEM* mnem);

#ifdef __cplusplus
};
#endif
#endif

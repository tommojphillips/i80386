/* i80386_mnem.h
 * Thomas J. Armytage 2025-2026 ( https://github.com/tommojphillips/ )
 * Intel 80386 Mnemonics/Disassembler
 */

#ifndef I80386_MNEM_H
#define I80386_MNEM_H

#include <stdint.h>

#include "i80386.h"

#pragma warning(push)
#pragma warning(disable : 4201)  /* C4201 - nameless struct */

 /* I80386 Mnemonic function pointers */
typedef struct I80386_MNEM_FUNCS {
	I80386_READ_MEMORY_BYTE exe_mem_byte;  /* fetch opcode byte */
	I80386_READ_MEMORY_BYTE read_mem_byte; /* read mem byte */
	void* user_param;
} I80386_MNEM_FUNCS;

/* I80386 CPU State */
typedef struct I80386_MNEM {
	char str[256];
	char addressing_segment[32];
	char addressing_offset[128];
	char addressing_str[32];
	I80386 const* state;     /* CPU state */
	uint16_t counter;        /* instruction length */
	I80386_SEGMENT_REGISTER sdescriptor; /* CS base */
	union {
		uint16_t offset;     /* IP */
		uint32_t offset32;   /* EIP */
	};
	uint8_t opcode;          /* opcode */
	uint8_t segment_prefix;  /* segement override index */
	uint8_t internal_flags;  /* rep prefix */
	uint8_t operand_size;
	uint8_t addressing_size;
	I80386_MOD_RM modrm;     /* modrm structure */
	I80386_SIB sib;          /* sib structure */

	uint8_t step_over_has_target;
	I80386_LOGICAL_ADDRESS step_over_address;
	uint8_t step_into_has_target;
	I80386_LOGICAL_ADDRESS step_into_address;	
	I80386_EFFECTIVE_ADDRESS effective_address; 
	I80386_MNEM_FUNCS funcs; /* cpu memory function pointers */
} I80386_MNEM;

#pragma warning(pop) /* C4201 - nameless struct */

#ifdef __cplusplus
extern "C" {
#endif

/* Disassemble Opcode at CS:EIP */
int i80386_mnem(I80386_MNEM* mnem);
/* Disassemble Opcode at CS:EIP+offset */
int i80386_mnem_at(I80386_MNEM* mnem, uint32_t offset);
/* Disassemble Opcode at base+offset */
int i80386_mnem_at_bo(I80386_MNEM* mnem, uint32_t base, uint32_t offset);
/* Disassemble Opcode at seg:offset */
int i80386_mnem_at_so(I80386_MNEM* mnem, uint16_t selector, uint32_t offset);

uint32_t i80386_mnem_get_step_over_target(I80386_MNEM* mnem);
uint32_t i80386_mnem_get_step_into_target(I80386_MNEM* mnem);

int i80386_mnem_segment_translation(const I80386_MNEM* mnem, uint32_t base, uint32_t offset, uint32_t* linear_address);
int i80386_mnem_page_translation(const I80386_MNEM* mnem, uint32_t linear_address, uint32_t* physical_address);
int i80386_mnem_address_translation(const I80386_MNEM* mnem, uint32_t base, uint32_t offset, uint32_t* linear_address, uint32_t* physical_address);

#ifdef __cplusplus
};
#endif
#endif

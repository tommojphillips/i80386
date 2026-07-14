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

#define I80386_MNEM_MAX_TOKENS 32
typedef enum {
	MNEM_TOKEN_MNEMONIC,
	MNEM_TOKEN_GENERAL_REGISTER,
	MNEM_TOKEN_CONTROL_REGISTER,
	MNEM_TOKEN_DEBUG_REGISTER,
	MNEM_TOKEN_TEST_REGISTER,
	MNEM_TOKEN_IMMEDIATE,
	MNEM_TOKEN_NUMBER,
	MNEM_TOKEN_MEMORY,
	MNEM_TOKEN_SEGMENT,
	MNEM_TOKEN_PREFIX,
	MNEM_TOKEN_OPERATOR,
	MNEM_TOKEN_RELATIVE_ADDRESS,
	MNEM_TOKEN_ABSOLUTE_ADDRESS,
} MNEM_TOKEN_TYPE;

typedef struct {
	const char* text;
	MNEM_TOKEN_TYPE type;
	int operand_size;
	int len;
	uint32_t number;
} MNEM_TOKEN;

typedef struct {
	MNEM_TOKEN tokens[I80386_MNEM_MAX_TOKENS];
	int token_count;
} MNEM_RENDER_LINE;

 /* I80386 Mnemonic function pointers */
typedef struct I80386_MNEM_FUNCS {
	I80386_READ_MEMORY_BYTE exe_mem_byte;  /* fetch opcode byte */
	I80386_READ_MEMORY_BYTE read_mem_byte; /* read mem byte */
	void* user_param;
} I80386_MNEM_FUNCS;

/* I80386 CPU State */
typedef struct I80386_MNEM {
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
	MNEM_RENDER_LINE line;
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

/* Read descriptor table entry pointed by selector - Returns 1 if success, otherwise 0
 cpu:      Cpu instance
 selector: Input selector
 entry:    Output descriptor table entry */
int i80386_mnem_read_descriptor_table_entry(const I80386_MNEM* mnem, uint16_t selector, I80386_DESCRIPTOR_TABLE_ENTRY* entry);

/* Resolve base from segment selector
 cpu:        Cpu instance
 selector:   Input selector
 base:       Output resolved base */
int i80386_mnem_resolve_segment_selector(const I80386_MNEM* mnem, uint16_t selector, uint32_t* base);

int i80386_mnem_segment_translation(const I80386_MNEM* mnem, uint32_t base, uint32_t offset, uint32_t* linear_address);
int i80386_mnem_page_translation(const I80386_MNEM* mnem, uint32_t linear_address, uint32_t* physical_address);
int i80386_mnem_address_translation(const I80386_MNEM* mnem, uint32_t base, uint32_t offset, uint32_t* linear_address, uint32_t* physical_address);

#ifdef __cplusplus
};
#endif
#endif

/* i80386.c
 * Thomas J. Armytage 2025-2026 ( https://github.com/tommojphillips/ )
 * Intel 80386 CPU (Intel 80386EX KU80C386EX33)
 */

#include <stdint.h>

#include "i80386.h"
#include "i80386_alu.h"
#include "sign_extend.h"

#define I80386_OPCODE cpu->opcode
#include "opcode_bits.h"

/* Get default or override segment index */
#define GET_SEG_OVERRIDE(seg) ((cpu->segment_override != 0xFF) ? cpu->segment_override : seg)

#define INT_DBZ      0 /* ITC 0 */
#define INT_TRAP     1 /* ITC 1 */
#define INT_NMI      2 /* ITC 2 */
#define INT_3        3 /* ITC 3 */
#define INT_OVERFLOW 4 /* ITC 4 */
#define INT_BOUNDS   5 /* ITC 5 */

#define EXCEPTION_UD   6 /* #UD */
#define EXCEPTION_DF   8 /* #DF */
#define EXCEPTION_TS  10 /* #TS */
#define EXCEPTION_NP  11 /* #NP */
#define EXCEPTION_SS  12 /* #SS */
#define EXCEPTION_GP  13 /* #GP */
#define EXCEPTION_PF  18 /* #PF */
#define EXCEPTION_TF 255 /* Triple Faulted */

/* Internal flag F1. Signals that a rep prefix is in use for this decode cycle */
#define F1  (cpu->internal_flags & INTERNAL_FLAG_F1)

/* Internal flag F1Z. Signals which rep (repz/repnz) is in use for this decode cycle */
#define F1Z (cpu->internal_flags & INTERNAL_FLAG_F1Z)

#define RO 0 /* Readonly */
#define RW 1 /* Read/Write */
#define WB 1 /* Writeback */

#define ZE 0 /* Zero-extend */
#define SE 1 /* Sign-extend */

#define SR_TYPE_DATA   0
#define SR_TYPE_STACK  1
#define SR_TYPE_CODE   2
#define SR_TYPE_LDT    3
#define SR_TYPE_TR     4
#define SR_TYPE_TSS    5
#define SR_TYPE_SYS    6

void i80386_exception(I80386* cpu, uint8_t exception);
void i80386_exception_code(I80386* cpu, uint8_t exception, uint16_t code);
void i80386_page_fault(I80386* cpu, uint32_t address, int present, int is_write);

static void read_dword_physical(const I80386* cpu, uint32_t address, uint32_t* value);
static void write_dword_physical(const I80386* cpu, uint32_t address, uint32_t value);

static int i80386_check_instruction_len(I80386* cpu) {
	if (cpu->instruction_len > 15) {
		i80386_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	return 1;
}
static int i80386_check_condition(I80386* cpu) {
	switch (CCCC) {
		case JCC_JO:
			if (cpu->psw.of) return 1;
			break;
		case JCC_JNO:
			if (!cpu->psw.of) return 1;
			break;
		case JCC_JC:
			if (cpu->psw.cf) return 1;
			break;
		case JCC_JNC:
			if (!cpu->psw.cf) return 1;
			break;
		case JCC_JZ:
			if (cpu->psw.zf) return 1;
			break;
		case JCC_JNZ:
			if (!cpu->psw.zf) return 1;
			break;
		case JCC_JBE:
			if (cpu->psw.cf || cpu->psw.zf) return 1;
			break;
		case JCC_JA:
			if (!cpu->psw.cf && !cpu->psw.zf) return 1;
			break;
		case JCC_JS:
			if (cpu->psw.sf) return 1;
			break;
		case JCC_JNS:
			if (!cpu->psw.sf) return 1;
			break;
		case JCC_JPE:
			if (cpu->psw.pf) return 1;
			break;
		case JCC_JPO:
			if (!cpu->psw.pf) return 1;
			break;
		case JCC_JL:
			if (cpu->psw.sf != cpu->psw.of) return 1;
			break;
		case JCC_JGE:
			if (cpu->psw.sf == cpu->psw.of) return 1;
			break;
		case JCC_JLE:
			if (cpu->psw.zf || cpu->psw.sf != cpu->psw.of) return 1;
			break;
		case JCC_JG:
			if (!cpu->psw.zf && cpu->psw.sf == cpu->psw.of) return 1;
			break;
	}
	return 0;
}

static void i80386_rollback_instruction(I80386* cpu) {
	cpu->eip -= cpu->instruction_len;
	cpu->instruction_len = 0;
}

static int segment_read_check(I80386* cpu, uint8_t sreg_index, uint32_t offset, uint32_t size) {
	if (offset > cpu->segment_registers[sreg_index].desc.limit - (size - 1)) {
		return 0;
	}
	if (cpu->segment_registers[sreg_index].desc.ar.e && !cpu->segment_registers[sreg_index].desc.ar.rw) {
		return 0;
	}
	return 1;
}
static int segment_write_check(I80386* cpu, uint8_t sreg_index, uint32_t offset, uint32_t size) {
	if (offset > cpu->segment_registers[sreg_index].desc.limit - (size - 1)) {
		return 0;
	}
	if (cpu->segment_registers[sreg_index].desc.ar.e || !cpu->segment_registers[sreg_index].desc.ar.rw) {
		return 0;
	}
	return 1;
}

/* 8 bit memory r/w */

static int read_byte_physical(const I80386* cpu, uint32_t address, uint8_t* value) {
	*value = cpu->funcs.read_mem_byte(cpu->funcs.user_param, address);
	return 1;
}
static int write_byte_physical(const I80386* cpu, uint32_t address, uint8_t value) {
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address, value);
	return 1;
}
static int read_byte_logical(I80386* cpu, uint32_t base, uint32_t offset, uint8_t* value) {
	uint32_t physical_address = 0;
	if (!i80386_address_translation(cpu, base, offset, 0, &physical_address)) {
		return 0;
	}
	read_byte_physical(cpu, physical_address, value);
	return 1;
}
static int write_byte_logical(I80386* cpu, uint32_t base, uint32_t offset, uint8_t value) {
	uint32_t physical_address = 0;
	if (!i80386_address_translation(cpu, base, offset, 1, &physical_address)) {
		return 0;
	}
	write_byte_physical(cpu, physical_address, value);
	return 1;
}
static int read_byte_ea(I80386* cpu, const I80386_EFFECTIVE_ADDRESS* ea, uint8_t* value) {
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, ea->segment_index, ea->logical_address.offset, 1)) {
		i80386_exception(cpu, ea->stack_address ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[ea->segment_index].desc.base, ea->logical_address.offset, 0, &physical_address)) {
		return 0;
	}
	read_byte_physical(cpu, physical_address, value);
	return 1;
}
static int write_byte_ea(I80386* cpu, const I80386_EFFECTIVE_ADDRESS* ea, uint8_t value) {
	uint32_t physical_address = 0;
	if (!segment_write_check(cpu, ea->segment_index, ea->logical_address.offset, 1)) {
		i80386_exception(cpu, ea->stack_address ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[ea->segment_index].desc.base, ea->logical_address.offset, 1, &physical_address)) {
		return 0;
	}
	write_byte_physical(cpu, physical_address, value);
	return 1;
}
static int read_byte_sreg(I80386* cpu, uint8_t segment_index, uint32_t offset, uint8_t* value) {
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, segment_index, offset, 1)) {
		i80386_exception(cpu, cpu->effective_address.stack_address || segment_index == SEG_SS ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[segment_index].desc.base, offset, 0, &physical_address)) {
		return 0;
	}
	read_byte_physical(cpu, physical_address, value);
	return 1;
}
static int write_byte_sreg(I80386* cpu, uint8_t segment_index, uint32_t offset, uint8_t value) {
	uint32_t physical_address = 0;
	if (!segment_write_check(cpu, segment_index, offset, 1)) {
		i80386_exception(cpu, cpu->effective_address.stack_address || segment_index == SEG_SS ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[segment_index].desc.base, offset, 1, &physical_address)) {
		return 0;
	}
	write_byte_physical(cpu, physical_address, value);
	return 1;
}
static int fetch_byte(I80386* cpu, uint8_t* value) {
	/* Code fetch 8bit byte */
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, SEG_CS, cpu->eip, 1)) {
		i80386_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->cs.desc.base, cpu->eip, 0, &physical_address)) {
		return 0;
	}

	cpu->eip += 1U;
	cpu->instruction_len += 1U;
	if (!i80386_check_instruction_len(cpu)) {
		return 0;
	}

	read_byte_physical(cpu, physical_address, value);
	return 1;
}

/* 16 bit memory r/w */

static void read_word_physical(const I80386* cpu, uint32_t address, uint16_t* value) {
	*value = cpu->funcs.read_mem_byte(cpu->funcs.user_param, address) | 
		((uint16_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, address + 1) << 8);
}
static void write_word_physical(const I80386* cpu, uint32_t address, uint16_t value) {
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address, value & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address + 1, (value >> 8) & 0xFF);
}
static int read_word_logical(I80386* cpu, uint32_t base, uint32_t offset, uint16_t* value) {
	uint32_t physical_address = 0;
	if (!i80386_address_translation(cpu, base, offset, 0, &physical_address)) {
		return 0;
	}
	read_word_physical(cpu, physical_address, value);
	return 1;
}
static int write_word_logical(I80386* cpu, uint32_t base, uint32_t offset, uint16_t value) {
	uint32_t physical_address = 0;
	if (!i80386_address_translation(cpu, base, offset, 1, &physical_address)) {
		return 0;
	}
	write_word_physical(cpu, physical_address, value);
	return 1;
}
static int read_word_ea(I80386* cpu, const I80386_EFFECTIVE_ADDRESS* ea, uint16_t* value) {
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, ea->segment_index, ea->logical_address.offset, 2)) {
		i80386_exception(cpu, ea->stack_address ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, ea->logical_address.base, ea->logical_address.offset, 0, &physical_address)) {
		return 0;
	}
	read_word_physical(cpu, physical_address, value);
	return 1;
}
static int write_word_ea(I80386* cpu, const I80386_EFFECTIVE_ADDRESS* ea, uint16_t value) {
	uint32_t physical_address = 0;
	if (!segment_write_check(cpu, ea->segment_index, ea->logical_address.offset, 2)) {
		i80386_exception(cpu, ea->stack_address ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, ea->logical_address.base, ea->logical_address.offset, 1, &physical_address)) {
		return 0;
	}
	write_word_physical(cpu, physical_address, value);
	return 1;
}
static int read_word_sreg(I80386* cpu, uint8_t segment_index, uint32_t offset, uint16_t* value) {
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, segment_index, offset, 2)) {
		i80386_exception(cpu, cpu->effective_address.stack_address || segment_index == SEG_SS ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[segment_index].desc.base, offset, 0, &physical_address)) {
		return 0;
	}
	read_word_physical(cpu, physical_address, value);
	return 1;
}
static int write_word_sreg(I80386* cpu, uint8_t segment_index, uint32_t offset, uint16_t value) {
	uint32_t physical_address = 0;
	if (!segment_write_check(cpu, segment_index, offset, 2)) {
		i80386_exception(cpu, cpu->effective_address.stack_address || segment_index == SEG_SS ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[segment_index].desc.base, offset, 1, &physical_address)) {
		return 0;
	}
	write_word_physical(cpu, physical_address, value);
	return 1;
}
static int fetch_word(I80386* cpu, uint16_t* value) {
	/* Code fetch 16bit word */
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, SEG_CS, cpu->eip, 2)) {
		i80386_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->cs.desc.base, cpu->eip, 0, &physical_address)) {
		return 0;
	}

	cpu->eip += 2U;
	cpu->instruction_len += 2U;
	if (!i80386_check_instruction_len(cpu)) {
		return 0;
	}

	read_word_physical(cpu, physical_address, value);
	return 1;	
}

/* 32 bit memory r/w */

static void read_dword_physical(const I80386* cpu, uint32_t physical_address, uint32_t* value) {
	*value = cpu->funcs.read_mem_byte(cpu->funcs.user_param, physical_address)                  |
		((uint32_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, physical_address + 1) << 8)  |
		((uint32_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, physical_address + 2) << 16) |
		((uint32_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, physical_address + 3) << 24);
}
static void write_dword_physical(const I80386* cpu, uint32_t physical_address, uint32_t value) {
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, physical_address, value & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, physical_address + 1, (value >> 8) & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, physical_address + 2, (value >> 16) & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, physical_address + 3, (value >> 24) & 0xFF);
}
static int read_dword_logical(I80386* cpu, uint32_t base, uint32_t offset, uint32_t* value) {
	uint32_t physical_address = 0;
	if (!i80386_address_translation(cpu, base, offset, 0, &physical_address)) {
		return 0;
	}
	read_dword_physical(cpu, physical_address, value);
	return 1;
}
static int write_dword_logical(I80386* cpu, uint32_t base, uint32_t offset, uint32_t value) {
	uint32_t physical_address = 0;
	if (!i80386_address_translation(cpu, base, offset, 1, &physical_address)) {
		return 0;
	}
	write_dword_physical(cpu, physical_address, value);
	return 1;
}
static int read_dword_ea(I80386* cpu, const I80386_EFFECTIVE_ADDRESS* ea, uint32_t* value) {
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, ea->segment_index, ea->logical_address.offset, 4)) {
		i80386_exception(cpu, ea->stack_address ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, ea->logical_address.base, ea->logical_address.offset, 0, &physical_address)) {
		return 0;
	}
	read_dword_physical(cpu, physical_address, value);
	return 1;
}
static int write_dword_ea(I80386* cpu, const I80386_EFFECTIVE_ADDRESS* ea, uint32_t value) {
	uint32_t physical_address = 0;
	if (!segment_write_check(cpu, ea->segment_index, ea->logical_address.offset, 4)) {
		i80386_exception(cpu, ea->stack_address ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, ea->logical_address.base, ea->logical_address.offset, 1, &physical_address)) {
		return 0;
	}
	write_dword_physical(cpu, physical_address, value);
	return 1;
}
static int read_dword_sreg(I80386* cpu, uint8_t segment_index, uint32_t offset, uint32_t* value) {
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, segment_index, offset, 4)) {
		i80386_exception(cpu, cpu->effective_address.stack_address || segment_index == SEG_SS ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[segment_index].desc.base, offset, 0, &physical_address)) {
		return 0;
	}
	read_dword_physical(cpu, physical_address, value);
	return 1;
}
static int write_dword_sreg(I80386* cpu, uint8_t segment_index, uint32_t offset, uint32_t value) {
	uint32_t physical_address = 0;
	if (!segment_write_check(cpu, segment_index, offset, 4)) {
		i80386_exception(cpu, cpu->effective_address.stack_address || segment_index == SEG_SS ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[segment_index].desc.base, offset, 0, &physical_address)) {
		return 0;
	}
	write_dword_physical(cpu, physical_address, value);
	return 1;
}
static int fetch_dword(I80386* cpu, uint32_t* value) {
	/* Code fetch 32bit dword */
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, SEG_CS, cpu->eip, 4)) {
		i80386_exception(cpu, EXCEPTION_GP);
		return 0;
	}

	if (!i80386_address_translation(cpu, cpu->cs.desc.base, cpu->eip, 0, &physical_address)) {
		return 0;
	}
	
	cpu->eip += 4U;
	cpu->instruction_len += 4U;
	if (!i80386_check_instruction_len(cpu)) {
		return 0;
	}

	read_dword_physical(cpu, physical_address, value);
	return 1;
}

/* 64 bit memory r/w */

static void read_qword_physical(const I80386* cpu, uint32_t address, uint64_t* value) {
	*value = cpu->funcs.read_mem_byte(cpu->funcs.user_param, address)                  |
		((uint64_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, address + 1) << 8)  |
		((uint64_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, address + 2) << 16) |
		((uint64_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, address + 3) << 24) |
		((uint64_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, address + 4) << 32) |
		((uint64_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, address + 5) << 40) |
		((uint64_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, address + 6) << 48) |
		((uint64_t)cpu->funcs.read_mem_byte(cpu->funcs.user_param, address + 7) << 56);
}
static void write_qword_physical(const I80386* cpu, uint32_t address, uint64_t value) {
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address, value & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address + 1, (value >> 8) & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address + 2, (value >> 16) & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address + 3, (value >> 24) & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address + 4, (value >> 32) & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address + 5, (value >> 40) & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address + 6, (value >> 48) & 0xFF);
	cpu->funcs.write_mem_byte(cpu->funcs.user_param, address + 7, (value >> 56) & 0xFF);
}
static int read_qword_logical(I80386* cpu, uint32_t base, uint32_t offset, uint64_t* value) {
	uint32_t physical_address = 0;
	if (!i80386_address_translation(cpu, base, offset, 0, &physical_address)) {
		return 0;
	}
	read_qword_physical(cpu, physical_address, value);
	return 1;
}
static int write_qword_logical(I80386* cpu, uint32_t base, uint32_t offset, uint64_t value) {
	uint32_t physical_address = 0;
	if (!i80386_address_translation(cpu, base, offset, 1, &physical_address)) {
		return 0;
	}
	write_qword_physical(cpu, physical_address, value);
	return 1;
}
static int read_qword_ea(I80386* cpu, const I80386_EFFECTIVE_ADDRESS* ea, uint64_t* value) {
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, ea->segment_index, ea->logical_address.offset, 8)) {
		i80386_exception(cpu, ea->stack_address ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, ea->logical_address.base, ea->logical_address.offset, 0, &physical_address)) {
		return 0;
	}
	read_qword_physical(cpu, physical_address, value);
	return 1;
}
static int write_qword_ea(I80386* cpu, const I80386_EFFECTIVE_ADDRESS* ea, uint64_t value) {
	uint32_t physical_address = 0;
	if (!segment_write_check(cpu, ea->segment_index, ea->logical_address.offset, 8)) {
		i80386_exception(cpu, ea->stack_address ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, ea->logical_address.base, ea->logical_address.offset, 1, &physical_address)) {
		return 0;
	}
	write_qword_physical(cpu, physical_address, value);
	return 1;
}
static int read_qword_sreg(I80386* cpu, uint8_t segment_index, uint32_t offset, uint64_t* value) {
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, segment_index, offset, 8)) {
		i80386_exception(cpu, cpu->effective_address.stack_address || segment_index == SEG_SS ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[segment_index].desc.base, offset, 0, &physical_address)) {
		return 0;
	}
	read_qword_physical(cpu, physical_address, value);
	return 1;
}
static int write_qword_sreg(I80386* cpu, uint8_t segment_index, uint32_t offset, uint64_t value) {
	uint32_t physical_address = 0;
	if (!segment_write_check(cpu, segment_index, offset, 8)) {
		i80386_exception(cpu, cpu->effective_address.stack_address || segment_index == SEG_SS ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->segment_registers[segment_index].desc.base, offset, 1, &physical_address)) {
		return 0;
	}
	write_qword_physical(cpu, physical_address, value);
	return 1;
}
static int fetch_qword(I80386* cpu, uint64_t* value) {
	/* Code fetch 64bit qword */
	uint32_t physical_address = 0;
	if (!segment_read_check(cpu, SEG_CS, cpu->eip, 8)) {
		i80386_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	if (!i80386_address_translation(cpu, cpu->cs.desc.base, cpu->eip, 0, &physical_address)) {
		return 0;
	}

	cpu->eip += 8U;
	cpu->instruction_len += 8U;
	if (!i80386_check_instruction_len(cpu)) {
		return 0;
	}

	read_qword_physical(cpu, physical_address, value);
	return 1;
}

/* 8 bit register r/w */
static uint8_t reg8_read(const I80386* cpu, uint8_t reg) {
	if (reg & 0x4) {
		return cpu->general_registers[reg & 0x3].r8.h;
	}
	else {
		return cpu->general_registers[reg & 0x3].r8.l;
	}
}
static void reg8_write(I80386* cpu, uint8_t reg, uint8_t v) {
	if (reg & 0x4) {
		cpu->general_registers[reg & 0x3].r8.h = v;
	}
	else {
		cpu->general_registers[reg & 0x3].r8.l = v;
	}
}

/* 16 bit register r/w */
static uint16_t reg16_read(const I80386* cpu, uint8_t reg) {
	return cpu->general_registers[reg & 0x7].r16;
}
static void reg16_write(I80386* cpu, uint8_t reg, uint16_t v) {
	cpu->general_registers[reg & 0x7].r16 = v;
}

/* 32 bit register r/w */
static uint32_t reg32_read(const I80386* cpu, uint8_t reg) {
	return cpu->general_registers[reg & 0x7].r32;
}
static void reg32_write(I80386* cpu, uint8_t reg, uint32_t v) {
	cpu->general_registers[reg & 0x7].r32 = v;
}

/* SIB */
static int sib_fetch(I80386* cpu) {
	cpu->sib.byte = 0;
	if (!fetch_byte(cpu, &cpu->sib.byte)) {
		return 0;
	}
	return 1;
}
static int sib_check(I80386* cpu) {
	if (cpu->address_size && cpu->modrm.mod != 0b11 && cpu->modrm.rm == 0b100) {
		return sib_fetch(cpu);
	}
	return 1;
}

/* Mod R/M */
static uint16_t modrm_get_base_offset(const I80386* cpu, I80386_MOD_RM modrm) {
	switch (modrm.rm) {
		case 0b000: /* base rel indexed - BX + SI */
			return (cpu->bx + cpu->si);
		case 0b001: /* base rel indexed - BX + DI */
			return (cpu->bx + cpu->di);
		case 0b010: /* base rel indexed stack - BP + SI */
			return (cpu->bp + cpu->si);
		case 0b011: /* base rel indexed stack - BP + DI */
			return (cpu->bp + cpu->di);
		case 0b100: /* implied SI */
			return cpu->si;
		case 0b101: /* implied DI */
			return cpu->di;
		case 0b110: /* implied BP */
			return cpu->bp;
		case 0b111: /* implied BX */
			return cpu->bx;
	}
	return 0;
}
static void modrm_set_effective_address_descriptor(const I80386* cpu, uint8_t segment_index, I80386_EFFECTIVE_ADDRESS* effective_address) {
	effective_address->valid = 1;
	effective_address->stack_address = (segment_index == SEG_SS);
	effective_address->segment_index = segment_index;
	effective_address->logical_address.base = cpu->segment_registers[segment_index].desc.base;
}
static int modrm_get_effective_address(I80386* cpu, I80386_EFFECTIVE_ADDRESS* effective_address) {
	if (!i80386_modrm_get_offset(cpu, cpu->address_size, cpu->modrm, cpu->sib, &cpu->effective_address, fetch_byte, fetch_word, fetch_dword, cpu)) {
		return 0;
	}
	if (!i80386_modrm_get_segment(cpu, cpu->address_size, cpu->modrm, cpu->sib, &cpu->effective_address, cpu->segment_override)) {
		return 0;
	}
	if (effective_address) {
		effective_address->valid = cpu->effective_address.valid;
		effective_address->stack_address = cpu->effective_address.stack_address;
		effective_address->segment_index = cpu->effective_address.segment_index;
		effective_address->logical_address.offset = cpu->effective_address.logical_address.offset;
		effective_address->logical_address.base = cpu->effective_address.logical_address.base;
	}
	return 1;
}
static int modrm_get_seg_index(I80386* cpu, const I80386_MOD_RM modrm, uint8_t* sr) {
	if (modrm.reg > SEG_GS) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0; /* error - 386 doesnt wrap segment registers like the 8086 */
	}
	*sr = modrm.reg;
	return 1;
}
static int fetch_modrm(I80386* cpu) {
	cpu->modrm.byte = 0;
	if (!fetch_byte(cpu, &cpu->modrm.byte)) {
		return 0;
	}
	if (!sib_check(cpu)) {
		return 0;
	}
	return 1;
}
static int modrm_get_rm(I80386* cpu, I80386_OPERAND* op) {
	if (cpu->modrm.mod == 0b11) {
		op->type = OPERAND_TYPE_GENERAL_REGISTER;
		op->reg.index = cpu->modrm.rm;
		return 1;
	}
	else {
		int r = modrm_get_effective_address(cpu, &op->mem.ea);
		op->type = OPERAND_TYPE_MEMORY;
		return r;
	}
}
static int modrm_add_offset_rm(I80386* cpu, I80386_OPERAND* op, int32_t offset) {
	op->mem.ea.logical_address.offset += offset;
	if (!cpu->address_size) {
		op->mem.ea.logical_address.offset &= 0xFFFF;
	}

	if (op->mem.ea.logical_address.offset > cpu->segment_registers[op->mem.ea.segment_index].desc.limit) {
		i80386_exception(cpu, op->mem.ea.stack_address ? EXCEPTION_SS : EXCEPTION_GP);
		return 0;
	}
	return 1;
}

/* 8 bit Mod R/M */

static int modrm_read_rm8(I80386* cpu, const I80386_OPERAND* op, uint8_t* value) {
	if (op->type == OPERAND_TYPE_GENERAL_REGISTER) {
		*value = reg8_read(cpu, op->reg.index);
		return 1;
	}
	else {
		return read_byte_ea(cpu, &op->mem.ea, value);
	}
}
static int  modrm_write_rm8(I80386* cpu, const I80386_OPERAND* op, uint8_t value) {
	if (op->type == OPERAND_TYPE_GENERAL_REGISTER) {
		reg8_write(cpu, op->reg.index, value);
		return 1;
	}
	else {
		return write_byte_ea(cpu, &op->mem.ea, value);
	}
}
static uint8_t modrm_read_reg8(const I80386* cpu) {
	return reg8_read(cpu, cpu->modrm.reg);
}
static void modrm_write_reg8(I80386* cpu, uint8_t value) {
	reg8_write(cpu, cpu->modrm.reg, value);
}

/* 16 bit Mod R/M */

static int modrm_read_rm16(I80386* cpu, const I80386_OPERAND* op, uint16_t* value) {
	if (op->type == OPERAND_TYPE_GENERAL_REGISTER) {
		*value = reg16_read(cpu, op->reg.index);
		return 1;
	}
	else {
		return read_word_ea(cpu, &op->mem.ea, value);
	}
}
static int modrm_write_rm16(I80386* cpu, const I80386_OPERAND* op, uint16_t value) {
	if (op->type == OPERAND_TYPE_GENERAL_REGISTER) {
		reg16_write(cpu, op->reg.index, value);
		return 1;
	}
	else {
		return write_word_ea(cpu, &op->mem.ea, value);
	}
}
static uint16_t modrm_read_reg16(const I80386* cpu) {
	return reg16_read(cpu, cpu->modrm.reg);
}
static void modrm_write_reg16(I80386* cpu, uint16_t value) {
	reg16_write(cpu, cpu->modrm.reg, value);
}

/* 32 bit Mod R/M */

static int modrm_read_rm32(I80386* cpu, const I80386_OPERAND* op, uint32_t* value) {
	if (op->type == OPERAND_TYPE_GENERAL_REGISTER) {
		*value = reg32_read(cpu, op->reg.index);
		return 1;
	}
	else {
		return read_dword_ea(cpu, &op->mem.ea, value);
	}
}
static int modrm_write_rm32(I80386* cpu, const I80386_OPERAND* op, uint32_t value) {
	if (op->type == OPERAND_TYPE_GENERAL_REGISTER) {
		reg32_write(cpu, op->reg.index, value);
		return 1;
	}
	else {
		return write_dword_ea(cpu, &op->mem.ea, value);
	}
}
static uint32_t modrm_read_reg32(I80386* cpu) {
	return reg32_read(cpu, cpu->modrm.reg);
}
static void modrm_write_reg32(I80386* cpu, uint32_t value) {
	reg32_write(cpu, cpu->modrm.reg, value);
}

/* Execute binary operation */

static int exec_bin_Gb_Eb(I80386* cpu, void (*op)(I80386*, uint8_t*, uint8_t), int wb) {
	/* Gb<->Eb */
	I80386_OPERAND rm = {0};
	uint8_t reg = 0;
	uint8_t tmp = 0;
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}
	if (!modrm_read_rm8(cpu, &rm, &tmp)) {
		return 0;
	}
	reg = modrm_read_reg8(cpu);
	
	if (D) {
		/* Gb,Eb */
		op(cpu, &reg, tmp);
		if (wb) {
			modrm_write_reg8(cpu, reg);
		}
	}
	else {
		/* Eb,Gb */
		op(cpu, &tmp, reg);
		if (wb) {
			modrm_write_rm8(cpu, &rm, tmp);
		}
	}
	return 1;
}
static int exec_bin_EbIb(I80386* cpu, I80386_ALU8_FUNC op, int wb) {
	/* Eb,Ib */
	I80386_OPERAND rm = { 0 };
	uint8_t tmp = 0;
	uint8_t imm = 0;
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}
	if (!modrm_read_rm8(cpu, &rm, &tmp)) {
		return 0;
	}
	if (!fetch_byte(cpu, &imm)) {
		return 0;
	}
	op(cpu, &tmp, imm);
	if (wb) {
		modrm_write_rm8(cpu, &rm, tmp);
	}
	return 1;
}

static int exec_bin_Gw_Ew(I80386* cpu, void (*op)(I80386*, uint16_t*, uint16_t), int wb) {
	/* Gw<->Ew */
	I80386_OPERAND rm = { 0 };
	uint16_t reg = 0;
	uint16_t tmp = 0;
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}
	if (!modrm_read_rm16(cpu, &rm, &tmp)) {
		return 0;
	}
	reg = modrm_read_reg16(cpu);

	if (D) {
		/* Gw,Ew */
		op(cpu, &reg, tmp);
		if (wb) {
			modrm_write_reg16(cpu, reg);
		}
	}
	else {
		/* Ew,Gw */
		op(cpu, &tmp, reg);
		if (wb) {
			modrm_write_rm16(cpu, &rm, tmp);
		}
	}
	return 1;
}
static int exec_bin_EwIw(I80386* cpu, void (*op)(I80386*, uint16_t*, uint16_t), int wb) {
	/* Ew,Iw */
	I80386_OPERAND rm = { 0 };
	uint16_t imm = 0;
	uint16_t tmp = 0;		
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}
	if (!fetch_word(cpu, &imm)) {
		return 0;
	}
	if (!modrm_read_rm16(cpu, &rm, &tmp)) {
		return 0;
	}

	op(cpu, &tmp, imm);
	if (wb) {
		modrm_write_rm16(cpu, &rm, tmp);
	}
	return 1;
}
static int exec_bin_EwIb(I80386* cpu, void (*op)(I80386*, uint16_t*, uint16_t), int wb, int se) {
	/* Ew,Ib */
	I80386_OPERAND rm = { 0 };
	uint8_t imm = 0;
	uint16_t tmp = 0;
	uint16_t tmp2 = 0;
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}
	if (!fetch_byte(cpu, &imm)) {
		return 0;
	}
	if (!modrm_read_rm16(cpu, &rm, &tmp)) {
		return 0;
	}

	if (se) {
		tmp2 = sign_extend8_16(imm);
	}
	else {
		tmp2 = tmp;
	}

	op(cpu, &tmp, tmp2);
	if (wb) {
		modrm_write_rm16(cpu, &rm, tmp);
	}
	return 1;
}

static int exec_bin_Gd_Ed(I80386* cpu, void (*op)(I80386*, uint32_t*, uint32_t), int wb) {
	/* Gd<->Ed */
	I80386_OPERAND rm = { 0 };
	uint32_t reg = 0;
	uint32_t tmp = 0;
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}
	if (!modrm_read_rm32(cpu, &rm, &tmp)) {
		return 0;
	}
	reg = modrm_read_reg32(cpu);

	if (D) {
		/* Gd,Ed */
		op(cpu, &reg, tmp);
		if (wb) {
			modrm_write_reg32(cpu, reg);
		}
	}
	else {
		/* Ed,Gd */
		op(cpu, &tmp, reg);
		if (wb) {
			modrm_write_rm32(cpu, &rm, tmp);
		}
	}
	return 1;
}
static int exec_bin_EdId(I80386* cpu, void (*op)(I80386*, uint32_t*, uint32_t), int wb) {
	/* Ed,Id */
	I80386_OPERAND rm = { 0 };
	uint32_t imm = 0;
	uint32_t tmp = 0;
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}
	if (!fetch_dword(cpu, &imm)) {
		return 0;
	}
	if (!modrm_read_rm32(cpu, &rm, &tmp)) {
		return 0;
	}

	op(cpu, &tmp, imm);
	if (wb) {
		modrm_write_rm32(cpu, &rm, tmp);
	}
	return 1;
}
static int exec_bin_EdIb(I80386* cpu, void (*op)(I80386*, uint32_t*, uint32_t), int wb, int se) {
	/* Ed,Ib */
	I80386_OPERAND rm = { 0 };
	uint8_t imm = 0;
	uint32_t tmp = 0;
	uint32_t tmp2 = 0;
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}
	if (!fetch_byte(cpu, &imm)) {
		return 0;
	}
	if (!modrm_read_rm32(cpu, &rm, &tmp)) {
		return 0;
	}

	if (se) {
		tmp2 = sign_extend8_32(imm);
	}
	else {
		tmp2 = tmp;
	}

	op(cpu, &tmp, tmp2);
	if (wb) {
		modrm_write_rm32(cpu, &rm, tmp);
	}
	return 1;
}

static int exec_bin_load_segment(I80386* cpu, uint8_t segment_index) {
	/* Mp */
	uint16_t selector = 0;

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}

	if (!fetch_modrm(cpu)) {
		return 0;
	}

	/* The mod R/M byte may refer only to memory */
	if (cpu->modrm.mod == 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}

	if (!modrm_get_effective_address(cpu, NULL)) {
		return 0;
	}

	if (cpu->operand_size) {
		/* r32, m16:32 */
		uint32_t offset = 0;

		if (!cpu->address_size) {
			cpu->effective_address.logical_address.offset &= 0xFFFF;
		}
		if (!read_dword_ea(cpu, &cpu->effective_address, &offset)) {
			return 0;
		}

		cpu->effective_address.logical_address.offset += 4;
		if (!cpu->address_size) {
			cpu->effective_address.logical_address.offset &= 0xFFFF;
		}
		if (!read_word_ea(cpu, &cpu->effective_address, &selector)) {
			return 0;
		}

		if (!i80386_load_segment_register(cpu, &cpu->segment_registers[segment_index], segment_index, selector)) {
			return 0;
		}
		modrm_write_reg32(cpu, offset);
	}
	else {
		/* r16, m16:16 */
		uint16_t offset = 0;

		if (!cpu->address_size) {
			cpu->effective_address.logical_address.offset &= 0xFFFF;
		}
		if (!read_word_ea(cpu, &cpu->effective_address, &offset)) {
			return 0;
		}

		cpu->effective_address.logical_address.offset += 2;
		if (!cpu->address_size) {
			cpu->effective_address.logical_address.offset &= 0xFFFF;
		}
		if (!read_word_ea(cpu, &cpu->effective_address, &selector)) {
			return 0;
		}

		if (!i80386_load_segment_register(cpu, &cpu->segment_registers[segment_index], segment_index, selector)) {
			return 0;
		}
		modrm_write_reg16(cpu, offset);
	}
	return 1;
}
static int exec_bin_store_descriptor_table_register(I80386* cpu, I80386_DESCRIPTOR_TABLE_REGISTER* dtr) {
	uint16_t limit = 0;
	uint16_t base_lo = 0;
	uint8_t  base_hi = 0;

	/* The mod R/M byte may refer only to memory */
	if (cpu->modrm.mod == 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}
	if (!modrm_get_effective_address(cpu, NULL)) {
		return 0;
	}

	limit = dtr->limit & 0xFFFF;
	base_lo = dtr->base & 0xFFFF;
	base_hi = (dtr->base >> 16) & 0xFF;

	if (!write_word_ea(cpu, &cpu->effective_address, limit)) {
		return 0;
	}
	cpu->effective_address.logical_address.offset += 2;
	if (!write_word_ea(cpu, &cpu->effective_address, base_lo)) {
		return 0;
	}
	cpu->effective_address.logical_address.offset += 2;
	if (!write_byte_ea(cpu, &cpu->effective_address, base_hi)) {
		return 0;
	}
	return 1;
}
static int exec_bin_load_descriptor_table_register(I80386* cpu, I80386_DESCRIPTOR_TABLE_REGISTER* dtr) {
	uint16_t limit = 0;
	uint32_t base = 0;

	/* The mod R/M byte may refer only to memory */
	if (cpu->modrm.mod == 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}

	/* Must be ring 0 */
	if (cpu->cpl != 0) {
		i80386_exception(cpu, EXCEPTION_GP);
		return 0;
	}

	if (!modrm_get_effective_address(cpu, NULL)) {
		return 0;
	}

	if (!read_word_ea(cpu, &cpu->effective_address, &limit)) {
		return 0;
	}
	cpu->effective_address.logical_address.offset += 2;
	if (!read_dword_ea(cpu, &cpu->effective_address, &base)) {
		return 0;
	}

	dtr->base = base;
	dtr->limit = limit;
	return 1;
}
static int exec_bin_bit_test(I80386* cpu, I80386_ALU16_FUNC op16, I80386_ALU32_FUNC op32, int wb) {
	/* bit test - Ev,Gv */
	I80386_OPERAND rm = { 0 };

	if (!fetch_modrm(cpu)) {
		return 0;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}

	/* lock raises #UD */
	if (cpu->lock_prefix && (!wb || (wb && rm.type != OPERAND_TYPE_MEMORY))) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}

	if (cpu->operand_size) {
		uint32_t value = 0;
		uint32_t index = 0;
		uint32_t bit = 0;
		int32_t dword_offset = 0;

		index = modrm_read_reg32(cpu);
		bit = index & 0x1F;
		dword_offset = ((int32_t)index >> 5);

		if (rm.type == OPERAND_TYPE_MEMORY) {
			if (!modrm_add_offset_rm(cpu, &rm, dword_offset * 4)) {
				return 0;
			}
		}

		if (!modrm_read_rm32(cpu, &rm, &value)) {
			return 0;
		}

		if (op32) {
			op32(cpu, &value, bit);
		}
		if (wb) {
			modrm_write_rm32(cpu, &rm, value);
		}
	}
	else {
		uint16_t value = 0;
		uint16_t index = 0;
		uint16_t bit = 0;
		int16_t word_offset = 0;

		index = modrm_read_reg16(cpu);
		bit = index & 0xF;
		word_offset = ((int16_t)index >> 4);

		if (rm.type == OPERAND_TYPE_MEMORY) {
			if (!modrm_add_offset_rm(cpu, &rm, word_offset * 2)) {
				return 0;
			}
		}

		if (!modrm_read_rm16(cpu, &rm, &value)) {
			return 0;
		}

		if (op16) {
			op16(cpu, &value, bit);
		}
		if (wb) {
			modrm_write_rm16(cpu, &rm, value);
		}
	}
	return 1;
}
static int exec_bin_bit_search(I80386* cpu, int r) {
	/* bit search forward/reverse - Ev,Gv */
	I80386_OPERAND rm = { 0 };

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}

	if (!fetch_modrm(cpu)) {
		return 0;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return 0;
	}

	if (cpu->operand_size) {
		uint32_t src;

		if (!modrm_read_rm32(cpu, &rm, &src)) {
			return 0;
		}

		/* check if destination is undefined */
		if (src == 0) {
			cpu->psw.zf = 1;
			return 0;
		}

		cpu->psw.zf = 0;

		if (r) {
			for (uint32_t bit = 31; bit >= 1; --bit) {
				if (src & (1U << bit)) {
					modrm_write_reg32(cpu, bit);
					break;
				}
			}
		}
		else {
			for (uint32_t bit = 0; bit < 32; ++bit) {
				if (src & (1U << bit)) {
					modrm_write_reg32(cpu, bit);
					break;
				}
			}
		}
	}
	else {
		uint16_t src;

		if (!modrm_read_rm16(cpu, &rm, &src)) {
			return 0;
		}

		/* check if destination is undefined */
		if (src == 0) {
			cpu->psw.zf = 1;
			return 0;
		}

		cpu->psw.zf = 0;

		if (r) {
			for (uint16_t bit = 15; bit >= 1; --bit) {
				if (src & (1U << bit)) {
					modrm_write_reg16(cpu, bit);
					break;
				}
			}
		}
		else {
			for (uint16_t bit = 0; bit < 16; ++bit) {
				if (src & (1U << bit)) {
					modrm_write_reg16(cpu, bit);
					break;
				}
			}
		}
	}
	return 1;
}
static int exec_bin_rm_reg(I80386* cpu, I80386_ALU8_FUNC op8, I80386_ALU16_FUNC op16, I80386_ALU32_FUNC op32, int wb) {
	if (!fetch_modrm(cpu)) {
		return 0;
	}

	/* lock prefix raises #UD if instruction is not read-modify-write memory */
	if (cpu->lock_prefix && (!wb || cpu->modrm.mod == 0b11 || D)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}
	
	if (W) {
		/* Ev<->Gv */
		if (cpu->operand_size) {
			exec_bin_Gd_Ed(cpu, op32, wb);
		}
		else {
			exec_bin_Gw_Ew(cpu, op16, wb);
		}
	}
	else {
		/* Eb<->Gb */
		exec_bin_Gb_Eb(cpu, op8, wb);
	}

	return 1;
}
static int exec_bin_accum_imm(I80386* cpu, I80386_ALU8_FUNC op8, I80386_ALU16_FUNC op16, I80386_ALU32_FUNC op32) {
	if (W) {
		if (cpu->operand_size) {
			/* EAX,Id */
			uint32_t imm = 0;
			if (!fetch_dword(cpu, &imm)) {
				return 0;
			}
			op32(cpu, &cpu->eax, imm);
		}
		else {
			/* AX,Iw */
			uint16_t imm = 0;
			if (!fetch_word(cpu, &imm)) {
				return 0;
			}
			op16(cpu, &cpu->ax, imm);
		}
	}
	else {
		/* AL,Ib */
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return 0;
		}
		op8(cpu, &cpu->al, imm);
	}

	return 1;
}
static int exec_bin_rm_imm(I80386* cpu, I80386_ALU8_FUNC op8, I80386_ALU16_FUNC op16, I80386_ALU32_FUNC op32, int wb, int se) {
	/* Group 1 */
	
	/* lock prefix throws #UD if not read-modify-write memory */
	if (cpu->lock_prefix && (!wb || cpu->modrm.mod == 0b11)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}

	if (W) {
		if (se && S) {
			/* Ev,Ib */
			if (cpu->operand_size) {
				exec_bin_EdIb(cpu, op32, wb, SE);
			}
			else {
				exec_bin_EwIb(cpu, op16, wb, SE);
			}
		}
		else {
			/* Ev,Iv */
			if (cpu->operand_size) {
				exec_bin_EdId(cpu, op32, wb);
			}
			else {
				exec_bin_EwIw(cpu, op16, wb);
			}
		}
	}
	else {
		/* Eb,Ib */
		exec_bin_EbIb(cpu, op8, wb);
	}

	return 1;
}
static int exec_bin_grp2_cl(I80386* cpu, I80386_ALU8_FUNC op8, I80386_ALU16_FUNC op16, I80386_ALU32_FUNC op32) {
	/* Group 2 - CL */

	/* lock prefix throws #UD if not read-modify-write memory */
	if (cpu->lock_prefix && cpu->modrm.mod == 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}

	uint8_t count = 1;
	if (VW) {
		count = cpu->cl;
	}

	if (W) {
		if (cpu->operand_size) {
			I80386_OPERAND rm = { 0 };
			uint32_t tmp = 0;
			if (!modrm_get_rm(cpu, &rm)) {
				return 0;
			}
			if (!modrm_read_rm32(cpu, &rm, &tmp)) {
				return 0;
			}
			op32(cpu, &tmp, count);
			modrm_write_rm32(cpu, &rm, tmp);
		}
		else {
			I80386_OPERAND rm = { 0 };
			uint16_t tmp = 0;
			if (!modrm_get_rm(cpu, &rm)) {
				return 0;
			}
			if (!modrm_read_rm16(cpu, &rm, &tmp)) {
				return 0;
			}
			op16(cpu, &tmp, count);
			modrm_write_rm16(cpu, &rm, tmp);
		}
	}
	else {
		I80386_OPERAND rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return 0;
		}
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return 0;
		}
		op8(cpu, &tmp, count);
		modrm_write_rm8(cpu, &rm, tmp);
	}

	return 1;
}
static int exec_bin_grp2_imm(I80386* cpu, I80386_ALU8_FUNC op8, I80386_ALU16_FUNC op16, I80386_ALU32_FUNC op32) {
	/* Group 2 - IMM */

	/* lock prefix throws #UD if not read-modify-write memory */
	if (cpu->lock_prefix && cpu->modrm.mod == 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return 0;
	}

	uint8_t count = 0;
	if (W) {
		if (cpu->operand_size) {
			I80386_OPERAND rm = { 0 };
			uint32_t tmp = 0;
			if (!modrm_get_rm(cpu, &rm)) {
				return 0;
			}
			if (!fetch_byte(cpu, &count)) {
				return 0;
			}
			if (!modrm_read_rm32(cpu, &rm, &tmp)) {
				return 0;
			}
			op32(cpu, &tmp, count);
			modrm_write_rm32(cpu, &rm, tmp);
		}
		else {
			I80386_OPERAND rm = { 0 };
			uint16_t tmp = 0;
			if (!modrm_get_rm(cpu, &rm)) {
				return 0;
			}
			if (!fetch_byte(cpu, &count)) {
				return 0;
			}
			if (!modrm_read_rm16(cpu, &rm, &tmp)) {
				return 0;
			}
			op16(cpu, &tmp, count);
			modrm_write_rm16(cpu, &rm, tmp);
		}
	}
	else {
		I80386_OPERAND rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return 0;
		}
		if (!fetch_byte(cpu, &count)) {
			return 0;
		}
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return 0;
		}
		op8(cpu, &tmp, count);
		modrm_write_rm8(cpu, &rm, tmp);
	}
	return 1;
}

/* Stack functions */

static int push_op16(I80386* cpu, I80386_OPERAND* op16) {
	uint16_t tmp = 0;
	cpu->effective_address.stack_address = 1;
	if(!modrm_read_rm16(cpu, op16, &tmp)) {
		return 0;
	}
	cpu->sp -= 2;
	return write_word_sreg(cpu, SEG_SS, cpu->sp, tmp);
}
static int push_word(I80386* cpu, uint16_t value) {
	cpu->effective_address.stack_address = 1;
	if (cpu->address_size) {
		cpu->esp -= 2;
		return write_word_sreg(cpu, SEG_SS, cpu->esp, value);
	}
	else {
		cpu->sp -= 2;
		return write_word_sreg(cpu, SEG_SS, cpu->sp, value);
	}
}
static int push_word_at(I80386* cpu, uint32_t* offset, uint16_t value) {
	*offset = (*offset) - 2;
	cpu->effective_address.stack_address = 1;
	return write_word_sreg(cpu, SEG_SS, *offset, value);
}
static int push_word_align(I80386* cpu, uint16_t value) {
	cpu->effective_address.stack_address = 1;
	if (cpu->address_size) {
		cpu->esp -= 4;
		return write_word_sreg(cpu, SEG_SS, cpu->esp, value);
	}
	else {
		cpu->sp -= 4;
		return write_word_sreg(cpu, SEG_SS, cpu->sp, value);
	}
}
static int push_word_align_at(I80386* cpu, uint32_t* offset, uint16_t value) {
	*offset = (*offset) - 4;
	cpu->effective_address.stack_address = 1;
	return write_word_sreg(cpu, SEG_SS, *offset, value);
}
static int pop_op16(I80386* cpu, I80386_OPERAND* op16) {
	uint16_t tmp = 0;
	cpu->effective_address.stack_address = 1;
	if (cpu->address_size) {
		if (!read_word_sreg(cpu, SEG_SS, cpu->esp, &tmp)) {
			return 0;
		}
		cpu->esp += 2;
	}
	else {
		if (!read_word_sreg(cpu, SEG_SS, cpu->sp, &tmp)) {
			return 0;
		}
		cpu->sp += 2;
	}
	return modrm_write_rm16(cpu, op16, tmp);
}
static int pop_word(I80386* cpu, uint16_t* value) {
	cpu->effective_address.stack_address = 1;
	if (cpu->address_size) {
		if (!read_word_sreg(cpu, SEG_SS, cpu->esp, value)) {
			return 0;
		}
		cpu->esp += 2;
	}
	else {
		if (!read_word_sreg(cpu, SEG_SS, cpu->sp, value)) {
			return 0;
		}
		cpu->sp += 2;
	}
	return 1;
}
static int pop_word_at(I80386* cpu, uint32_t* offset, uint16_t* value) {
	cpu->effective_address.stack_address = 1;
	if (!read_word_sreg(cpu, SEG_SS, *offset, value)) {
		return 0;
	}
	*offset = (*offset) + 2;
	return 1;
}
static int pop_word_align(I80386* cpu, uint16_t* value) {
	/* 32-bit pop, high-order 16-bits discarded */
	uint32_t tmp = 0;
	cpu->effective_address.stack_address = 1;
	if (cpu->address_size) {
		if (!read_dword_sreg(cpu, SEG_SS, cpu->esp, &tmp)) {
			return 0;
		}
		cpu->esp += 4;
	}
	else {
		if (!read_dword_sreg(cpu, SEG_SS, cpu->sp, &tmp)) {
			return 0;
		}
		cpu->sp += 4;
	}
	*value = tmp & 0xFFFF;
	return 1;
}
static int pop_word_align_at(I80386* cpu, uint32_t* offset, uint16_t* value) {
	/* 32-bit pop, high-order 16-bits discarded */
	uint32_t tmp = 0;
	cpu->effective_address.stack_address = 1;
	if (!read_dword_sreg(cpu, SEG_SS, *offset, &tmp)) {
		return 0;
	}
	*value = tmp & 0xFFFF;
	*offset = (*offset) + 4;
	return 1;
}
static int stack_peek_word(I80386* cpu, int i, uint16_t* value) {
	if (cpu->address_size) {
		return read_word_sreg(cpu, SEG_SS, cpu->esp + i, value);
	}
	else {
		return read_word_sreg(cpu, SEG_SS, cpu->sp + i, value);
	}
}

static int push_op32(I80386* cpu, I80386_OPERAND* op32) {
	uint32_t tmp = 0;
	if(!modrm_read_rm32(cpu, op32, &tmp)) {
		return 0;
	}
	cpu->effective_address.stack_address = 1;

	if (cpu->address_size) {
		cpu->esp -= 4;
		return write_dword_sreg(cpu, SEG_SS, cpu->esp, tmp);
	}
	else {
		cpu->sp -= 4;
		return write_dword_sreg(cpu, SEG_SS, cpu->sp, tmp);
	}
}
static int push_dword(I80386* cpu, uint32_t value) {
	cpu->effective_address.stack_address = 1;
	if (cpu->address_size) {
		cpu->esp -= 4;
		return write_dword_sreg(cpu, SEG_SS, cpu->esp, value);
	}
	else {
		cpu->sp -= 4;
		return write_dword_sreg(cpu, SEG_SS, cpu->sp, value);
	}
}
static int push_dword_at(I80386* cpu, uint32_t* offset, uint32_t value) {
	*offset = (*offset) - 4;
	cpu->effective_address.stack_address = 1;
	return write_dword_sreg(cpu, SEG_SS, *offset, value);
}
static int pop_op32(I80386* cpu, I80386_OPERAND* op32) {
	uint32_t tmp = 0;
	cpu->effective_address.stack_address = 1;
	if (cpu->address_size) {
		if (!read_dword_sreg(cpu, SEG_SS, cpu->esp, &tmp)) {
			return 0;
		}
		cpu->esp += 4;
	}
	else {
		if (!read_dword_sreg(cpu, SEG_SS, cpu->sp, &tmp)) {
			return 0;
		}
		cpu->sp += 4;
	}
	return modrm_write_rm32(cpu, op32, tmp);
}
static int pop_dword(I80386* cpu, uint32_t* value) {
	cpu->effective_address.stack_address = 1;

	if (cpu->address_size) {
		if (!read_dword_sreg(cpu, SEG_SS, cpu->esp, value)) {
			return 0;
		}
		cpu->esp += 4;
	}
	else {
		if (!read_dword_sreg(cpu, SEG_SS, cpu->sp, value)) {
			return 0;
		}
		cpu->sp += 4;
	}
	return 1;
}
static int pop_dword_at(I80386* cpu, uint32_t* offset, uint32_t* value) {
	cpu->effective_address.stack_address = 1;
	if (!read_dword_sreg(cpu, SEG_SS, *offset, value)) {
		return 0;
	}
	*offset = (*offset) + 4;
	return 1;
}
static int stack_peek_dword(I80386* cpu, int i, uint32_t* value) {
	if (cpu->address_size) {
		return read_dword_sreg(cpu, SEG_SS, cpu->esp + i, value);
	}
	else {
		return read_dword_sreg(cpu, SEG_SS, cpu->sp + i, value);
	}
}

/* Gate functions */
static int i80386_stack_switch(I80386* cpu, int dpl, int param_count, uint8_t type, I80386_SEGMENT_REGISTER* new_ss, uint32_t* new_esp) {
	/* switch stacks */
	uint16_t old_selector = cpu->ss.selector;
	uint32_t old_base = cpu->ss.desc.base;
	uint32_t old_esp = cpu->esp;

	if ((cpu->tss.stacks[dpl].ss & 0xFFF8) == 0) {
		i80386_exception_code(cpu, EXCEPTION_SS, 0);
		return 0;
	}

	if (!i80386_load_segment_register(cpu, new_ss, SEG_SS, cpu->tss.stacks[dpl].ss)) {
		return 0;
	}
	*new_esp = cpu->tss.stacks[dpl].esp;

	cpu->effective_address.stack_address = 1;

	switch (type) {
		case I80386_GATE_TYPE_CALL_286:
			*new_esp = (*new_esp) - 2;
			if (!write_word_logical(cpu, new_ss->desc.base, *new_esp, old_selector)) {
		return 0;
	}
			*new_esp = (*new_esp) - 2;
			if (!write_word_logical(cpu, new_ss->desc.base, *new_esp, old_esp & 0xFFFF)) {
				return 0;
			}
			if (param_count > 0) {
				for (int i = param_count - 1; i >= 0; --i) {
					uint16_t v = 0;
					if (!read_word_logical(cpu, old_base, old_esp + (i * 2), &v)) {
						return 0;
					}
					*new_esp = (*new_esp) - 2;
					if (!write_word_logical(cpu, new_ss->desc.base, *new_esp, v)) {
						return 0;
					}
				}
			}
			break;
		case I80386_GATE_TYPE_CALL_386:
			*new_esp = (*new_esp) - 4;
			if (!write_word_logical(cpu, new_ss->desc.base, *new_esp, old_selector)) {
				return 0;
			}
			*new_esp = (*new_esp) - 4;
			if (!write_dword_logical(cpu, new_ss->desc.base, *new_esp, old_esp)) {
				return 0;
			}
			if (param_count > 0) {
				for (int i = param_count - 1; i >= 0; --i) {
					uint32_t v = 0;
					if (!read_dword_logical(cpu, old_base, old_esp + (i * 4), &v)) {
						return 0;
					}
					*new_esp = (*new_esp) - 4;
					if (!write_dword_logical(cpu, new_ss->desc.base, *new_esp, v)) {
						return 0;
					}
				}
			}
			break;
	}
	return 1;
}
static int i80386_int_gate(I80386* cpu, const I80386_GATE* gate, int interrupt_type) {
	I80386_DESCRIPTOR_TABLE_ENTRY target = { 0 };
	uint8_t rpl = 0;
	uint8_t cpl = 0;
	uint32_t final_esp = cpu->esp;
	I80386_SEGMENT_REGISTER sreg = { 0 };

	if (gate->present == 0) {
		i80386_exception_code(cpu, EXCEPTION_NP, gate->selector);
		return 0;
	}
	
	if ((gate->selector & 0xFFF8) == 0) {
		i80386_exception_code(cpu, EXCEPTION_GP, 0);
		return 0;
	}

	rpl = gate->selector & 3;
	cpl = cpu->cpl > rpl ? cpu->cpl : rpl;

	/* Hardware interrupts ignore gate DPL */
	if (interrupt_type == I80386_INTERRUPT_TYPE_SOFTWARE && cpl > gate->dpl) {
		i80386_exception_code(cpu, EXCEPTION_GP, 0);
		return 0;
	}

	if (!i80386_read_descriptor_table_entry(cpu, gate->selector, &target)) {
		return 0;
	}
	if (!target.ar.present) {
		i80386_exception_code(cpu, EXCEPTION_NP, gate->selector);
		return 0;
	}
	if (!target.ar.s) {
		i80386_exception_code(cpu, EXCEPTION_GP, gate->selector);
		return 0;
	}
	if (!target.ar.e) {
		i80386_exception_code(cpu, EXCEPTION_GP, gate->selector);
		return 0;
	}

	if (target.ar.dpl < cpl) {
		/* switch stacks */
		if (!i80386_stack_switch(cpu, target.ar.dpl, 0, gate->type, &sreg, &final_esp)) {
			return 0;
		}
	}
	else {
		i80386_copy_segment_descriptor(&sreg, &cpu->ss);
	}

	/* Push frame */
	if (gate->type == I80386_GATE_TYPE_INT_286 || gate->type == I80386_GATE_TYPE_TRAP_286) {
		final_esp -= 2;
		if (!write_word_logical(cpu, sreg.desc.base, final_esp, cpu->psw.word)) {
			return 0;
		}
		final_esp -= 2;
		if (!write_word_logical(cpu, sreg.desc.base, final_esp, cpu->cs.selector)) {
			return 0;
		}
		if (!push_word_at(cpu, &final_esp, cpu->ip)) {
			return 0;
		}
	}
	else if (gate->type == I80386_GATE_TYPE_INT_386 || gate->type == I80386_GATE_TYPE_TRAP_386) {
		final_esp -= 4;
		if (!write_dword_logical(cpu, sreg.desc.base, final_esp, cpu->eflags.dword)) {
			return 0;
		}
		final_esp -= 4;
		if (!write_dword_logical(cpu, sreg.desc.base, final_esp, cpu->cs.selector)) {
			return 0;
		}
		if (!push_dword_at(cpu, &final_esp, cpu->eip)) {
			return 0;
		}
	}

	/* commit register values */
	if (!i80386_load_segment_register(cpu, &cpu->cs, SEG_CS, gate->selector)) {
		return 0;
	}
	if (target.ar.dpl < cpl) {
		i80386_copy_segment_descriptor(&cpu->ss, &sreg);
	}
	if (gate->type == I80386_GATE_TYPE_INT_286 || gate->type == I80386_GATE_TYPE_TRAP_286) {
		cpu->esp = final_esp & 0xFFFF;
		cpu->eip = gate->offset_lo; /* clear upper 16bits */
	}
	else if (gate->type == I80386_GATE_TYPE_INT_386 || gate->type == I80386_GATE_TYPE_TRAP_386) {
		cpu->esp = final_esp;
		cpu->eip = gate->offset_lo | gate->offset_hi << 16;
	}

	/* clear if/tf flags */
	cpu->psw.tf = 0;
	if (gate->type == I80386_GATE_TYPE_INT_286 || gate->type == I80386_GATE_TYPE_INT_386) {
		cpu->psw.in = 0;
	}
	cpu->halt = 0; /* int clears hlt state */

	return 1;
}
static int i80386_call_gate(I80386* cpu, const I80386_DESCRIPTOR_TABLE_ENTRY* entry, uint16_t selector) {
	I80386_GATE call_gate = { 0 };
	uint8_t rpl = selector & 3;
	uint8_t cpl = cpu->cpl > rpl ? cpu->cpl : rpl;
	I80386_DESCRIPTOR_TABLE_ENTRY target = { 0 };
	uint32_t new_esp = 0;
	I80386_SEGMENT_REGISTER sreg = { 0 };

	call_gate.qword = entry->qword;

	if (!call_gate.present) {
		i80386_exception_code(cpu, EXCEPTION_NP, selector);
		return 0;
	}

	if (cpl > call_gate.dpl) {
		i80386_exception_code(cpu, EXCEPTION_GP, selector);
		return 0;
	}

	if ((call_gate.selector & 0xFFF8) == 0) {
		i80386_exception_code(cpu, EXCEPTION_GP, selector);
		return 0;
	}

	if (!i80386_read_descriptor_table_entry(cpu, call_gate.selector, &target)) {
		return 0;
	}
	if (!target.ar.present) {
		i80386_exception_code(cpu, EXCEPTION_NP, selector);
		return 0;
	}
	if (!target.ar.s) {
		i80386_exception_code(cpu, EXCEPTION_GP, selector);
		return 0;
	}
	if (!target.ar.e) {
		i80386_exception_code(cpu, EXCEPTION_GP, selector);
		return 0;
	}

	if (target.ar.dpl < cpl) {
		/* switch stacks */
		if (!i80386_stack_switch(cpu, target.ar.dpl, call_gate.param_count, call_gate.type, &sreg, &new_esp)) {
			return 0;
		}
	}
	else {
		new_esp = cpu->esp;
	}

	if (!push_word_align_at(cpu, &new_esp, cpu->cs.selector)) {
		return 0;
	}
	if (!push_dword_at(cpu, &new_esp, cpu->eip)) {
		return 0;
	}

	if (!i80386_load_segment_register(cpu, &cpu->cs, SEG_CS, call_gate.selector)) {
		return 0;
	}
	if (call_gate.type == I80386_GATE_TYPE_CALL_286) {
		cpu->eip = call_gate.offset_lo; /* clear upper 16bits */
	}
	else if (call_gate.type == I80386_GATE_TYPE_CALL_386) {
		cpu->eip = call_gate.offset_lo | (call_gate.offset_hi << 16);
	}

	if (target.ar.dpl < cpl) {
		i80386_copy_segment_descriptor(&cpu->ss, &sreg);
	}
	cpu->esp = new_esp;
	cpu->cpl = target.ar.dpl;
	return 1;
}
static int i80386_task_gate(I80386* cpu, const I80386_DESCRIPTOR_TABLE_ENTRY* entry, I80386_TASK_SWITCH_REASON reason) {
	I80386_GATE gate = { 0 };

	/* Vaildate TASK descriptor */
	gate.qword = entry->qword;
	if (!gate.present) {
		i80386_exception_code(cpu, EXCEPTION_NP, gate.selector);
		return 0;
	}
	if ((gate.selector & 0xFFF8) == 0) {
		i80386_exception_code(cpu, EXCEPTION_GP, gate.selector);
		return 0;
	}

	/* Task switch */
	return i80386_task_switch(cpu, gate.selector, reason);
}

/* TLB functions */

static void i80386_flush_tlb(I80386* cpu) {
	/* flush all pages from the tlb */
	for (int i = 0; i < cpu->tlb.count; ++i) {
		cpu->tlb.entries[i].valid = 0;
	}
	cpu->tlb.count = 0;
	cpu->tlb.index = 0;
}
static int i80386_flush_tlb_page(I80386* cpu, uint32_t linear_page) {
	/* flush selected page from the tlb */
	for (int i = 0; i < cpu->tlb.count; ++i) {
		if (cpu->tlb.entries[i].linear_page == linear_page) {
			cpu->tlb.entries[i].valid = 0;
			return 1;
		}
	}
	return 0;
}
static int i80386_search_tlb(const I80386* cpu, uint32_t linear_page, const I80386_TLB_ENTRY** tlb) {
	/* search for a page in the tlb */
	for (int i = 0; i < cpu->tlb.count; ++i) {
		if (cpu->tlb.entries[i].valid && cpu->tlb.entries[i].linear_page == linear_page) {
			*tlb = &cpu->tlb.entries[i];
			return 1;
		}
	}
	return 0;
}
static void i80386_insert_tlb(I80386* cpu, uint32_t linear_page, I80386_PAGE_TABLE_ENTRY pde, I80386_PAGE_TABLE_ENTRY pte) {
	/* insert a page in the tlb */
	int insert_index = -1;

	for (int i = 0; i < I80386_TLB_SIZE; ++i) {
		/* update existing entry if exists */
		if (cpu->tlb.entries[i].valid && cpu->tlb.entries[i].linear_page == linear_page) {
			insert_index = i;
			break;
		}
		/* replace the first invalid entry */
		if (insert_index == -1 && !cpu->tlb.entries[i].valid) {
			insert_index = i;
			if (i > cpu->tlb.count) {
				cpu->tlb.count = i;
			}
			else if (i == cpu->tlb.count) {
				cpu->tlb.count++;
			}
		}
	}

	/* no free entry? replace the oldest entry */
	if (insert_index == -1) {
		insert_index = cpu->tlb.index;
		cpu->tlb.index = (cpu->tlb.index + 1) % I80386_TLB_SIZE;
	}

	cpu->tlb.entries[insert_index].linear_page = linear_page;
	cpu->tlb.entries[insert_index].physical_page = pte.page_frame_address;
	cpu->tlb.entries[insert_index].rw = pde.rw & pte.rw;
	cpu->tlb.entries[insert_index].us = pde.us & pte.us;
	cpu->tlb.entries[insert_index].valid = 1;
}

/* Task functions */

static int i80386_task_load_tss(I80386* cpu, const I80386_TASK_STATE_SEGMENT* tss) {
	/* Load CPU State from tss image */

	I80386_SEGMENT_REGISTER es = { 0 };
	I80386_SEGMENT_REGISTER cs = { 0 };
	I80386_SEGMENT_REGISTER ss = { 0 };
	I80386_SEGMENT_REGISTER ds = { 0 };
	I80386_SEGMENT_REGISTER fs = { 0 };
	I80386_SEGMENT_REGISTER gs = { 0 };
	I80386_SEGMENT_REGISTER ldtr = { 0 };

	if (!i80386_load_segment_register(cpu, &es, SEG_ES, tss->es & 0xFFFF)) {
		return 0;
	}
	if (!i80386_load_segment_register(cpu, &cs, SEG_CS, tss->cs & 0xFFFF)) {
		return 0;
	}
	if (!i80386_load_segment_register(cpu, &ss, SEG_SS, tss->ss & 0xFFFF)) {
		return 0;
	}
	if (!i80386_load_segment_register(cpu, &ds, SEG_DS, tss->ds & 0xFFFF)) {
		return 0;
	}
	if (!i80386_load_segment_register(cpu, &fs, SEG_FS, tss->fs & 0xFFFF)) {
		return 0;
	}
	if (!i80386_load_segment_register(cpu, &gs, SEG_GS, tss->gs & 0xFFFF)) {
		return 0;
	}
	if (!i80386_load_segment_register(cpu, &ldtr, SEG_LDT, tss->ldt & 0xFFFF)) {
		return 0;
	}

	cpu->cr3 = tss->cr3;
	cpu->eip = tss->eip;
	cpu->eflags.dword = tss->eflags;
	cpu->eax = tss->eax;
	cpu->ecx = tss->ecx;
	cpu->edx = tss->edx;
	cpu->ebx = tss->ebx;
	cpu->esp = tss->esp;
	cpu->ebp = tss->ebp;
	cpu->esi = tss->esi;
	cpu->edi = tss->edi;

	i80386_copy_segment_descriptor(&cpu->es, &es);
	i80386_copy_segment_descriptor(&cpu->cs, &cs);
	i80386_copy_segment_descriptor(&cpu->ss, &ss);
	i80386_copy_segment_descriptor(&cpu->ds, &ds);
	i80386_copy_segment_descriptor(&cpu->fs, &fs);
	i80386_copy_segment_descriptor(&cpu->gs, &gs);
	i80386_copy_segment_descriptor(&cpu->ldtr, &ldtr);

	i80386_flush_tlb(cpu);
	return 1;
}
static int i80386_task_store_tss(const I80386* cpu, I80386_TASK_STATE_SEGMENT* tss) {
	/* Store CPU State to tss image */
	tss->cr3 = cpu->cr3;
	tss->eip = cpu->eip;
	tss->eflags = cpu->eflags.dword;
	tss->eax = cpu->eax;
	tss->ecx = cpu->ecx;
	tss->edx = cpu->edx;
	tss->ebx = cpu->ebx;
	tss->esp = cpu->esp;
	tss->ebp = cpu->ebp;
	tss->esi = cpu->esi;
	tss->edi = cpu->edi;
	tss->es = cpu->es.selector;
	tss->cs = cpu->cs.selector;
	tss->ss = cpu->ss.selector;
	tss->ds = cpu->ds.selector;
	tss->fs = cpu->fs.selector;
	tss->gs = cpu->gs.selector;
	tss->ldt = cpu->ldtr.selector;
	return 1;
}
static int i80386_task_read_tss(I80386* cpu, const I80386_DESCRIPTOR_CACHE* cache, I80386_TASK_STATE_SEGMENT* tss) {
	if (!cache->ar.present) {
		i80386_exception_code(cpu, EXCEPTION_NP, 0);
		return 0;
	}

	if (cache->limit < 0x67) {
		i80386_exception_code(cpu, EXCEPTION_TS, 0);
		return 0;
	}

	for (int i = 0; i < sizeof(I80386_TASK_STATE_SEGMENT) / 4; ++i) {
		read_dword_logical(cpu, cache->base, i * 4, &tss->values[i]);
	}

	return 1;
}
static int i80386_task_write_tss(I80386* cpu, const I80386_DESCRIPTOR_CACHE* cache, const I80386_TASK_STATE_SEGMENT* tss) {
	if (!cache->ar.present) {
		i80386_exception_code(cpu, EXCEPTION_NP, 0);
		return 0;
	}

	if (cache->limit < 0x67) {
		i80386_exception_code(cpu, EXCEPTION_TS, 0);
		return 0;
	}

	for (int i = 0; i < sizeof(I80386_TASK_STATE_SEGMENT) / 4; ++i) {
		write_dword_logical(cpu, cache->base, i * 4, tss->values[i]);
	}

	return 1;
}
static void i80386_task_set_busy(I80386_DESCRIPTOR_TABLE_ENTRY* desc) {
	if (desc->ar.type == I80386_GATE_TYPE_AVAL_286) {
		desc->ar.type = I80386_GATE_TYPE_BUSY_286;
	}
	else if (desc->ar.type == I80386_GATE_TYPE_AVAL_386) {
		desc->ar.type = I80386_GATE_TYPE_BUSY_386;
	}
}
static void i80386_task_clear_busy(I80386_DESCRIPTOR_TABLE_ENTRY* desc) {
	if (desc->ar.type == I80386_GATE_TYPE_BUSY_286) {
		desc->ar.type = I80386_GATE_TYPE_AVAL_286;
	}
	else if (desc->ar.type == I80386_GATE_TYPE_BUSY_386) {
		desc->ar.type = I80386_GATE_TYPE_AVAL_386;
	}
}
static int i80386_task_switch(I80386* cpu, uint16_t selector, I80386_TASK_SWITCH_REASON reason) {
	I80386_DESCRIPTOR_TABLE_ENTRY entry = { 0 };
	I80386_DESCRIPTOR_TABLE_ENTRY old_entry = { 0 };
	I80386_DESCRIPTOR_CACHE cache = { 0 };
	I80386_TASK_STATE_SEGMENT old_tss = { 0 };
	I80386_TASK_STATE_SEGMENT new_tss = { 0 };

	/* task_validate_descriptor */
	if ((selector & 0xFFF8) == 0) {
		i80386_exception_code(cpu, EXCEPTION_TS, 0);
		return 0;
	}
	if (!i80386_read_descriptor_table_entry(cpu, selector, &entry)) {
		return 0;
	}
	if (entry.ar.s) {
		i80386_exception_code(cpu, EXCEPTION_TS, selector);
		return 0;
	}

	/* task_validate_tr */
	if ((cpu->tr.selector & 0xFFF8) == 0) {
		i80386_exception_code(cpu, EXCEPTION_TS, 0);
		return 0;
	}
	if (!i80386_read_descriptor_table_entry(cpu, cpu->tr.selector, &old_entry)) {
		return 0;
	}
	if (old_entry.ar.s) {
		i80386_exception_code(cpu, EXCEPTION_TS, selector);
		return 0;
	}

	/* compute_cache */
	i80386_update_system_descriptor_cache(&entry, &cache);

	/* task_get_current_task */
	if (!i80386_task_store_tss(cpu, &old_tss)) {
		return 0;
	}

	/* task_save_current_task */
	if (!i80386_task_write_tss(cpu, &cpu->tr.desc, &old_tss)) {
		return 0;
	}

	/* task_update_desc [reason] */
	switch (reason) {
		case TASK_SWITCH_CALL:
		case TASK_SWITCH_INT:
			/* task_set_busy_bits */
			i80386_task_set_busy(&entry);

			/* task_write_back_descriptor */
			write_word_logical(cpu, cache.base, 0, cpu->tr.selector); /* back_link = tr */
			if (!i80386_write_descriptor_table_entry(cpu, selector, &entry)) {
				return 0;
			}
			break;
		case TASK_SWITCH_JMP:
			/* task_set_busy_bits */
			i80386_task_clear_busy(&old_entry);
			i80386_task_set_busy(&entry);

			/* task_write_back_descriptor */
			if (!i80386_write_descriptor_table_entry(cpu, cpu->tr.selector, &old_entry)) {
				return 0;
			}
			if (!i80386_write_descriptor_table_entry(cpu, selector, &entry)) {
				return 0;
			}
			break;
		case TASK_SWITCH_IRET:
			/* task_set_busy_bits */
			i80386_task_clear_busy(&old_entry);

	/* task_write_back_descriptor */
	if (!i80386_write_descriptor_table_entry(cpu, cpu->tr.selector, &old_entry)) {
		return 0;
	}
			break;
	}

	/* task_read_new_tss */
	if (!i80386_task_read_tss(cpu, &cache, &new_tss)) {
		return 0;
	}

	/* task_load_tr */
	cpu->tr.selector = selector;
	i80386_update_system_descriptor_cache(&entry, &cpu->tr.desc);

	/* task_load_new_tss */
	if (!i80386_task_load_tss(cpu, &new_tss)) {
		return 0;
	}

	/* set_ts */
	cpu->msw.ts = 1;

	/* task_update_nt [reason] */
	switch (reason) {
		case TASK_SWITCH_CALL:
		case TASK_SWITCH_INT:
			cpu->psw.nt = 1;
			break;
		case TASK_SWITCH_JMP:
			break;
		case TASK_SWITCH_IRET:
			cpu->psw.nt = 0;
			break;
	}

	return 1;
}

/* Interrupt functions */

void i80386_intr(I80386* cpu, uint8_t vector) {
	if (!cpu->intr) {
		cpu->intr = 1;
		cpu->intr_type = vector;
	}
}
void i80386_nmi(I80386* cpu) {
	cpu->nmi = 1;
}
void i80386_int(I80386* cpu, uint8_t vector, int interrupt_type) {
	if (!cpu->msw.pe) {
		/* Real mode */
		uint16_t offset = 0;
		uint16_t selector = 0;
		uint32_t final_esp = cpu->esp;
		uint32_t vector_offset = vector << 2;

		if (vector_offset + 3U > cpu->idtr.limit) {
			i80386_exception(cpu, EXCEPTION_GP);
			return;
		}
		if (!read_word_logical(cpu, cpu->idtr.base, vector_offset, &offset)) {
			return;
		}
		if (!read_word_logical(cpu, cpu->idtr.base, vector_offset + 2, &selector)) {
			return;
		}

		/* push frame */
		if (!push_word_at(cpu, &final_esp, cpu->psw.word)) {
			return;
		}
		if (!push_word_at(cpu, &final_esp, cpu->cs.selector)) {
			return;
		}
		if (!push_word_at(cpu, &final_esp, cpu->ip)) {
			return;
		}

		/* commit register values */
		if (cpu->address_size) {
			cpu->esp = final_esp;
		}
		else {
			cpu->esp = final_esp & 0xFFFF;
		}
		if (!i80386_load_segment_register(cpu, &cpu->cs, SEG_CS, selector)) {
			return;
		}
		cpu->eip = offset; /* clear upper 16bits */
		cpu->psw.in = 0;
		cpu->psw.tf = 0;
		cpu->halt = 0;
	}
	else if (cpu->eflags.vm) {
		/* Virtual 8086 mode */
		
	}
	else {
		/* Protected mode */
		I80386_DESCRIPTOR_TABLE_ENTRY entry = { 0 };
		I80386_GATE gate = { 0 };

		if (!i80386_read_interrupt_table_entry(cpu, vector, &gate)) {
			return;
		}

		entry.qword = gate.qword;

		switch (gate.type) {
			case I80386_GATE_TYPE_TASK:
				i80386_task_gate(cpu, &entry, TASK_SWITCH_INT);
				break;
			case I80386_GATE_TYPE_INT_286:
			case I80386_GATE_TYPE_INT_386:
			case I80386_GATE_TYPE_TRAP_286:
			case I80386_GATE_TYPE_TRAP_386:
				i80386_int_gate(cpu, &gate, interrupt_type);
				break;
			default:
				i80386_exception_code(cpu, EXCEPTION_GP, 0);
				return;
		}		
	}
	}
static int i80386_classify_exception(uint8_t exception) {
    switch (exception) {
        case EXCEPTION_TS:
        case EXCEPTION_NP:
        case EXCEPTION_SS:
        case EXCEPTION_GP:
            return I80386_EXCEPTION_CLASS_CONTRIBUTORY;
        case EXCEPTION_PF:
            return I80386_EXCEPTION_CLASS_PAGE_FAULT;
        default:
            return I80386_EXCEPTION_CLASS_BENIGN;
    }
}
void i80386_exception(I80386 * cpu, uint8_t exception) {
	switch (cpu->exception.state) {
		case I80386_EXCEPTION_STATE_TRIPLE_FAULT:
			break;
		case I80386_EXCEPTION_STATE_DOUBLE_FAULT:
			cpu->exception.state = I80386_EXCEPTION_STATE_TRIPLE_FAULT;
			cpu->exception.tf_number = exception;
			break;
		case I80386_EXCEPTION_STATE_STD: {
			int first = i80386_classify_exception(cpu->exception.number);
			int second = i80386_classify_exception(exception);
			if ((first == I80386_EXCEPTION_CLASS_CONTRIBUTORY && second == I80386_EXCEPTION_CLASS_CONTRIBUTORY) ||
				(first == I80386_EXCEPTION_CLASS_PAGE_FAULT && second == I80386_EXCEPTION_CLASS_CONTRIBUTORY) ||
				(first == I80386_EXCEPTION_CLASS_PAGE_FAULT && second == I80386_EXCEPTION_CLASS_PAGE_FAULT)) {
				cpu->exception.state = I80386_EXCEPTION_STATE_DOUBLE_FAULT;
				cpu->exception.df_number = exception;
				i80386_int(cpu, EXCEPTION_DF, I80386_INTERRUPT_TYPE_HARDWARE);
				break;
			}
		} 
		/* Not a double fault; fall through to NONE */
		case I80386_EXCEPTION_STATE_NONE:
			cpu->exception.state = I80386_EXCEPTION_STATE_STD;
			cpu->exception.number = exception;
			cpu->exception.code = 0;
			cpu->exception.df_number = 0;
			cpu->exception.tf_number = 0;
			i80386_rollback_instruction(cpu); /* eip at exception points to instruction that caused it. */
			i80386_int(cpu, exception, I80386_INTERRUPT_TYPE_HARDWARE);
			break;
	}
}

void i80386_exception_code(I80386* cpu, uint8_t exception, uint16_t code) {
	i80386_exception(cpu, exception);
	cpu->exception.code = code;
}
void i80386_page_fault(I80386* cpu, uint32_t linear_address, int present, int is_write) {
	cpu->cr2 = linear_address;
	uint8_t code = 0;
	if (!present) {
		code |= 0x1;
	}
	if (is_write) {
		code |= 0x2;
	}
	if (cpu->cpl == 0x3) {
		code |= 0x4;
	}
	i80386_exception_code(cpu, EXCEPTION_PF, code);
}
static void i80386_check_interrupts(I80386* cpu) {
	if (cpu->int_delay == 1) {
		cpu->int_delay = 0;
		return;
	}

	if (cpu->nmi) {
		/* Non-Maskable int */
		cpu->nmi = 0;
		i80386_int(cpu, INT_NMI, I80386_INTERRUPT_TYPE_HARDWARE);
	}
	else if (cpu->intr && cpu->int_latch) {
		/* Hardware int; INTR is masked by IF */
		cpu->intr = 0;
		i80386_int(cpu, cpu->intr_type, I80386_INTERRUPT_TYPE_HARDWARE);
	}

	if (cpu->tf_latch) {
		/* Trap int */
		i80386_int(cpu, INT_TRAP, I80386_INTERRUPT_TYPE_HARDWARE);
	}

	/* latch int flag for next cycle */
	cpu->int_latch = cpu->psw.in;

	/* latch trap flag for next cycle */
	cpu->tf_latch = cpu->psw.tf;
}
static int i80386_check_halt(I80386* cpu) {
	if (cpu->halt) {
		return 0;
	}
	return 1;
}

/* Address Translation functions */

int i80386_segment_translation(I80386* cpu, uint32_t base, uint32_t offset, uint32_t* linear_address) {
	(void)cpu;
	*linear_address = i80386_get_physical_address_bo(base, offset);
	return 1;
}
static int i80386_read_page_directory_entry(I80386* cpu, I80386_PAGE_TABLE_ENTRY* pde, I80386_LINEAR_ADDRESS la, int is_write) {
	uint32_t pde_address = 0;
	uint32_t pde_old = 0;

	pde_address = (cpu->cr3 & 0xFFFFF000) | (la.dir << 2);
	read_dword_physical(cpu, pde_address, &pde->dword);

	if (!pde->present) {
		i80386_page_fault(cpu, la.dword, 1, is_write);
		return 0;
	}
	if (cpu->cpl == 0x3) {
		if (is_write && !pde->rw) {
			i80386_page_fault(cpu, la.dword, 0, is_write);
			return 0;
		}
		if (!pde->us) {
			i80386_page_fault(cpu, la.dword, 0, is_write);
			return 0;
		}
	}
	pde_old = pde->dword;
	pde->accessed = 1;

	if (pde_old != pde->dword) {
		write_dword_physical(cpu, pde_address, pde->dword);
	}
	return 1;
}
static int i80386_read_page_table_entry(I80386* cpu, I80386_PAGE_TABLE_ENTRY* pde, I80386_PAGE_TABLE_ENTRY* pte, I80386_LINEAR_ADDRESS la, int is_write) {
	uint32_t pte_address = 0;
	uint32_t pte_old = 0;
	
	pte_address = (pde->page_frame_address << 12) | (la.page << 2);
	read_dword_physical(cpu, pte_address, &pte->dword);

	if (!pte->present) {
		i80386_page_fault(cpu, la.dword, 1, is_write);
		return 0;
	}
	if (cpu->cpl == 0x3) {
		if (is_write && !pte->rw) {
			i80386_page_fault(cpu, la.dword, 0, is_write);
			return 0;
		}
		if (!pte->us) {
			i80386_page_fault(cpu, la.dword, 0, is_write);
			return 0;
		}
	}
	pte_old = pte->dword;
	pte->accessed = 1;
	if (is_write) {
		pte->dirty = 1;
	}

	if (pte_old != pte->dword) {
		write_dword_physical(cpu, pte_address, pte->dword);
	}
	return 1;
}
int i80386_page_translation(I80386* cpu, uint32_t linear_address, int is_write, uint32_t* physical_address) {
	I80386_LINEAR_ADDRESS la = { .dword = linear_address };
	I80386_PAGE_TABLE_ENTRY pde = { 0 };
	I80386_PAGE_TABLE_ENTRY pte = { 0 };
	uint32_t linear_page = linear_address >> 12;
	I80386_TLB_ENTRY* tlb_entry = NULL;

	if (i80386_search_tlb(cpu, linear_page, &tlb_entry)) {
		if (cpu->cpl == 0x3) {
			if (is_write && !tlb_entry->rw) {
				i80386_page_fault(cpu, la.dword, 0, is_write);
				return 0;
			}
			if (!tlb_entry->us) {
				i80386_page_fault(cpu, la.dword, 0, is_write);
				return 0;
			}
		}
		if (physical_address) {
			*physical_address = (tlb_entry->physical_page << 12) | la.offset;
		}
		return 1; /* TLB Hit */
	}

	/* TLB Miss */

	/* Read PDE */
	if (!i80386_read_page_directory_entry(cpu, &pde, la, is_write)) {
		return 0;
	}

	/* Read PTE */
	if (!i80386_read_page_table_entry(cpu, &pde, &pte, la, is_write)) {
		return 0;
	}

	/* Cache in TLB */
	i80386_insert_tlb(cpu, linear_page, pde, pte);

	if (physical_address) {
		*physical_address = (pte.page_frame_address << 12) | la.offset;
	}
	return 1;
}
int i80386_address_translation(I80386* cpu, uint32_t base, uint32_t offset, int is_write, uint32_t* physical_address) {
	uint32_t linear_address = 0;
	if (!i80386_segment_translation(cpu, base, offset, &linear_address)) {
		return 0;
	}
	if (cpu->cr0.pg) {
		if (!i80386_page_translation(cpu, linear_address, is_write, physical_address)) {
			return 0;
		}
	}
	else {
		*physical_address = linear_address;
	}
	return 1;
}

/* Opcodes */

static void add_rm_imm(I80386* cpu) {
	/* add Eb,Ib / Ev,Iv / Ev,Ib - (80/81/82/83, R/M reg = b000) b100000SW */
	exec_bin_rm_imm(cpu, i80386_alu_add8, i80386_alu_add16, i80386_alu_add32, WB, SE);
}
static void add_rm_reg(I80386* cpu) {
	/* add Eb<->Gb / Ev<->Gv - (00/01/02/03) b000000DW */
	exec_bin_rm_reg(cpu, i80386_alu_add8, i80386_alu_add16, i80386_alu_add32, WB);
}
static void add_accum_imm(I80386* cpu) {
	/* AL,Ib / eAX,Iv - (04/05) b0000010W */
	exec_bin_accum_imm(cpu, i80386_alu_add8, i80386_alu_add16, i80386_alu_add32);
}

static void or_rm_imm(I80386* cpu) {
	/* or Eb,Ib / Ev,Iv / Ev,Ib - (80/81/82/83, R/M reg = b001) b100000SW */
	exec_bin_rm_imm(cpu, i80386_alu_or8, i80386_alu_or16, i80386_alu_or32, WB, SE);
}
static void or_rm_reg(I80386* cpu) {
	/* or Eb<->Gb / Ev<->Gv - (08/0A/09/0B) b000010DW */
	exec_bin_rm_reg(cpu, i80386_alu_or8, i80386_alu_or16, i80386_alu_or32, WB);
}
static void or_accum_imm(I80386* cpu) {
	/* AL,Ib / eAX,Iv - (0C/0D) b0000110W */
	exec_bin_accum_imm(cpu, i80386_alu_or8, i80386_alu_or16, i80386_alu_or32);
}

static void adc_rm_imm(I80386* cpu) {
	/* adc Eb,Ib / Ev,Iv / Ev,Ib - (80/81/82/83, R/M reg = b010) b100000SW */
	exec_bin_rm_imm(cpu, i80386_alu_adc8, i80386_alu_adc16, i80386_alu_adc32, WB, SE);
}
static void adc_rm_reg(I80386* cpu) {
	/* adc Eb<->Gb / Ev<->Gv - (10/12/11/13) b000100DW */
	exec_bin_rm_reg(cpu, i80386_alu_adc8, i80386_alu_adc16, i80386_alu_adc32, WB);
}
static void adc_accum_imm(I80386* cpu) {
	/* AL,Ib / eAX,Iv - (14/15) b0001010W */
	exec_bin_accum_imm(cpu, i80386_alu_adc8, i80386_alu_adc16, i80386_alu_adc32);
}

static void sbb_rm_imm(I80386* cpu) {
	/* sbb Eb,Ib / Ev,Iv / Ev,Ib - (80/81/82/83, R/M reg = b011)  b100000SW */
	exec_bin_rm_imm(cpu, i80386_alu_sbb8, i80386_alu_sbb16, i80386_alu_sbb32, WB, SE);
}
static void sbb_rm_reg(I80386* cpu) {
	/* sbb Eb<->Gb / Ev<->Gv - (18/1A/19/1B) b000110DW */
	exec_bin_rm_reg(cpu, i80386_alu_sbb8, i80386_alu_sbb16, i80386_alu_sbb32, WB);
}
static void sbb_accum_imm(I80386* cpu) {
	/* AL,Ib / eAX,Iv - (1C/1D) b0001110W */
	exec_bin_accum_imm(cpu, i80386_alu_sbb8, i80386_alu_sbb16, i80386_alu_sbb32);
}

static void and_rm_imm(I80386* cpu) {
	/* and Eb,Ib / Ev,Iv / Ev,Ib - (80/81/82/83, R/M reg = b100) b100000SW */
	exec_bin_rm_imm(cpu, i80386_alu_and8, i80386_alu_and16, i80386_alu_and32, WB, SE);
}
static void and_rm_reg(I80386* cpu) {
	/* and Eb<->Gb / Ev<->Gv - (20/22/21/23) b001000DW */
	exec_bin_rm_reg(cpu, i80386_alu_and8, i80386_alu_and16, i80386_alu_and32, WB);
}
static void and_accum_imm(I80386* cpu) {
	/* AL,Ib / eAX,Iv - (24/25) b0010010W */
	exec_bin_accum_imm(cpu, i80386_alu_and8, i80386_alu_and16, i80386_alu_and32);
}

static void sub_rm_imm(I80386* cpu) {
	/* sub Eb,Ib / Ev,Iv / Ev,Ib - (80/81/82/83, R/M reg = b101) b100000SW */
	exec_bin_rm_imm(cpu, i80386_alu_sub8, i80386_alu_sub16, i80386_alu_sub32, WB, SE);
}
static void sub_rm_reg(I80386* cpu) {
	/* sub Eb<->Gb / Ev<->Gv - (28/2A/29/2B) b001010DW */
	exec_bin_rm_reg(cpu, i80386_alu_sub8, i80386_alu_sub16, i80386_alu_sub32, WB);
}
static void sub_accum_imm(I80386* cpu) {
	/* AL,Ib / eAX,Iv - (2C/2D) b0010110W */
	exec_bin_accum_imm(cpu, i80386_alu_sub8, i80386_alu_sub16, i80386_alu_sub32);
}

static void xor_rm_imm(I80386* cpu) {
	/* xor Eb,Ib / Ev,Iv / Ev,Ib - (80/81/82/83, R/M reg = b110) b100000SW */
	exec_bin_rm_imm(cpu, i80386_alu_xor8, i80386_alu_xor16, i80386_alu_xor32, WB, SE);
}
static void xor_rm_reg(I80386* cpu) {
	/* xor Eb<->Gb / Ev<->Gv - (30/32/31/33) b001100DW */
	exec_bin_rm_reg(cpu, i80386_alu_xor8, i80386_alu_xor16, i80386_alu_xor32, WB);
}
static void xor_accum_imm(I80386* cpu) {
	/* AL,Ib / eAX,Iv - (34/35) b0011010W */
	exec_bin_accum_imm(cpu, i80386_alu_xor8, i80386_alu_xor16, i80386_alu_xor32);
}

static void cmp_rm_imm(I80386* cpu) {
	/* cmp Eb,Ib / Ev,Iv / Ev,Ib - (80/81/82/83, R/M reg = b111)  b100000SW */
	exec_bin_rm_imm(cpu, i80386_alu_cmp8, i80386_alu_cmp16, i80386_alu_cmp32, RO, SE);
}
static void cmp_rm_reg(I80386* cpu) {
	/* cmp Eb<->Gb / Ev<->Gv - (38/39/3A/3B) b001110DW */
	exec_bin_rm_reg(cpu, i80386_alu_cmp8, i80386_alu_cmp16, i80386_alu_cmp32, RO);
}
static void cmp_accum_imm(I80386* cpu) {
	/* AL,Ib / eAX,Iv - (3C/3D) b0011110W */
	exec_bin_accum_imm(cpu, i80386_alu_cmp8, i80386_alu_cmp16, i80386_alu_cmp32);
}

static void test_rm_imm(I80386* cpu) {
	/* test Eb,Ib / Ev,Iv - (F6/F7, R/M reg = b000) b1111011W */
	exec_bin_rm_imm(cpu, i80386_alu_test8, i80386_alu_test16, i80386_alu_test32, RO, ZE);
}
static void test_rm_reg(I80386* cpu) {
	/* test Eb<->Gb / Ev<->Gv - (84/85) b1000010W */
	exec_bin_rm_reg(cpu, i80386_alu_test8, i80386_alu_test16, i80386_alu_test32, RO);
}
static void test_accum_imm(I80386* cpu) {
	/* AL,Ib / eAX,Iv - (A8/A9) b1010100W */
	exec_bin_accum_imm(cpu, i80386_alu_test8, i80386_alu_test16, i80386_alu_test32);
}

static void daa(I80386* cpu) {
	/* Decimal Adjust for Addition - daa - (27) b00100111 */
	i80386_alu_daa(cpu, &cpu->al);
}
static void das(I80386* cpu) {
	/* Decimal Adjust for Subtraction - das - (2F) b00101111 */
	i80386_alu_das(cpu, &cpu->al);
}
static void aaa(I80386* cpu) {
	/* ASCII Adjust for Addition - aaa - (37) b00110111 */
	i80386_alu_aaa(cpu, &cpu->ax);
}
static void aas(I80386* cpu) {
	/* ASCII Adjust for Subtraction - aas - (3F) b00111111 */
	i80386_alu_aas(cpu, &cpu->ax);
}
static void aam(I80386* cpu) {
	/* ASCII Adjust for Multiply - aam Ib - (D4) b11010100 */
	uint8_t divisor = 0; /* undocumented operand; should be 0x0A */
	if (!fetch_byte(cpu, &divisor)) {
		return;
	} 
	i80386_alu_aam(cpu, &cpu->ax, divisor);
}
static void aad(I80386* cpu) {
	/* ASCII Adjust for Division - aad Ib - (D5) b11010101 */
	uint8_t divisor = 0; /* undocumented operand; should be 0x0A */
	if (!fetch_byte(cpu, &divisor)) {
		return;
	} 
	i80386_alu_aad(cpu, &cpu->ax, divisor);
}
static void salc(I80386* cpu) {
	/* set carry in AL - salc - (D6) b11010110 (undocumented opcode) */

	/* lock prefix throws #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->psw.cf) {
		cpu->al = 0xFF;
	}
	else {
		cpu->al = 0;
	}
}

static void push_seg(I80386* cpu) {
	/* Push SR - (06/0E/16/1E/A0/A8) bX0ESRXXX */
	uint8_t esr = ESR;
	if (esr < I80386_SEGMENT_COUNT) {
		push_word(cpu, cpu->segment_registers[esr].selector);
	}
}
static void pop_seg(I80386* cpu) {
	/* Pop SR (07/0F/17/1F/A1/A9) bX0ESRXXX */
	uint8_t esr = ESR;
	if (esr < I80386_SEGMENT_COUNT) {
	uint16_t selector = 0;
	if (!pop_word(cpu, &selector)) {
		return;
	}
	if (!i80386_load_segment_register(cpu, &cpu->segment_registers[esr], esr, selector)) {
		return;
	}
	if (esr == SEG_SS) {
		cpu->int_delay = 1;
	}
}
}
static void push_reg(I80386* cpu) {
	/* Push REG (50-57) b01010REG */

	/* The 80386 PUSH SP instruction pushes
	   the value of SP as it existed before the
	   instruction. This differs from the 8086. */

	if (cpu->operand_size) {
		uint32_t tmp = reg32_read(cpu, cpu->opcode);
		if (cpu->address_size) {
			cpu->esp -= 4;
			write_dword_sreg(cpu, SEG_SS, cpu->esp, tmp);
		}
		else {
			cpu->sp -= 4;
			write_dword_sreg(cpu, SEG_SS, cpu->sp, tmp);
		}

	}
	else {
		uint16_t tmp = reg16_read(cpu, cpu->opcode);
		if (cpu->address_size) {
			cpu->esp -= 2;
			write_word_sreg(cpu, SEG_SS, cpu->esp, tmp);
		}
		else {
			cpu->sp -= 2;
			write_word_sreg(cpu, SEG_SS, cpu->sp, tmp);
		}
	}
}
static void pop_reg(I80386* cpu) {
	/* Pop REG (58-5F) b01011REG */
	if (cpu->operand_size) {
		uint32_t tmp = 0;
		if (cpu->address_size) {
			read_dword_sreg(cpu, SEG_SS, cpu->esp, &tmp);
			cpu->esp += 4;
		}
		else {
			read_dword_sreg(cpu, SEG_SS, cpu->sp, &tmp);
			cpu->sp += 4;
		}
		reg32_write(cpu, cpu->opcode, tmp);
	}
	else {
		uint16_t tmp = 0;
		if (cpu->address_size) {
			read_word_sreg(cpu, SEG_SS, cpu->esp, &tmp);
			cpu->esp += 2;
		}
		else {
			read_word_sreg(cpu, SEG_SS, cpu->sp, &tmp);
			cpu->sp += 2;
		}
		reg16_write(cpu, cpu->opcode, tmp);
	}
}
static void push_rm(I80386* cpu) {
	/* Push R/M (FF, R/M reg = b110) b11111111 */
	I80386_OPERAND rm = { 0 };

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}
	
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	if (cpu->operand_size) {
		push_op32(cpu, &rm);
	}
	else {
		push_op16(cpu, &rm);
	}
}
static void pop_rm(I80386* cpu) {
	/* Pop R/M (8F) b10001111 */
	I80386_OPERAND rm = { 0 };

	if (!fetch_modrm(cpu)) {
		return;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	/* reg bits are reserved */
	if (cpu->modrm.reg != 0) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->operand_size) {
		pop_op32(cpu, &rm);
	}
	else {
		pop_op16(cpu, &rm);
	}
}
static void pushf(I80386* cpu) {
	/* push psw (9C) b10011100 */
	cpu->psw.word &= 0xFFD7;
	push_word(cpu, cpu->psw.word);
}
static void popf(I80386* cpu) {
	/* popf (9D) b10011101 */
	I80386_PROGRAM_STATUS_WORD psw = { 0 };
	pop_word(cpu, &psw.word);
	cpu->psw.word = (psw.word | 0x0002) & 0x0FD7;
	
	/* POPF many only alter IF cpl <= iopl */
	if (cpu->cpl <= cpu->psw.iopl) {
		cpu->psw.in = psw.in;
	}
}
static void pusha(I80386* cpu) {
	/* push all (60) b01100000 */
	uint32_t esp = cpu->esp;
	uint32_t len = (0x10 << cpu->operand_size);

	if (cpu->address_size) {
		if ((esp - len) >= cpu->ss.desc.limit) {
			i80386_exception(cpu, EXCEPTION_GP);
			return;
		}
	}
	else {
		if (((esp - len) & 0xFFFF) >= cpu->ss.desc.limit) {
			i80386_exception(cpu, EXCEPTION_GP);
			return;
		}
	}

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->operand_size) {
		push_dword(cpu, cpu->eax);
		push_dword(cpu, cpu->ecx);
		push_dword(cpu, cpu->edx);
		push_dword(cpu, cpu->ebx);
		push_dword(cpu, esp); /* Push OLD esp */
		push_dword(cpu, cpu->ebp);
		push_dword(cpu, cpu->esi);
		push_dword(cpu, cpu->edi);
	}
	else {
		push_word(cpu, cpu->ax);
		push_word(cpu, cpu->cx);
		push_word(cpu, cpu->dx);
		push_word(cpu, cpu->bx);
		push_word(cpu, esp & 0xFFFF); /* Push OLD sp */
		push_word(cpu, cpu->bp);
		push_word(cpu, cpu->si);
		push_word(cpu, cpu->di);
	}
}
static void popa(I80386* cpu) {
	/* pop all - popa - (61) b01100001 */
	uint32_t esp = cpu->esp;
	uint32_t len = (0x10 << cpu->operand_size) - 2;

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->address_size) {
		if ((esp + len) >= cpu->ss.desc.limit) {
			i80386_exception(cpu, EXCEPTION_SS);
			return;
		}
	}
	else {
		if (((esp + len) & 0xFFFF) >= cpu->ss.desc.limit) {
			i80386_exception(cpu, EXCEPTION_SS);
			return;
		}
	}

	if (cpu->operand_size) {		
		pop_dword(cpu, &cpu->edi);
		pop_dword(cpu, &cpu->esi);
		pop_dword(cpu, &cpu->ebp);
		pop_dword(cpu, &esp); /* discard esp */
		pop_dword(cpu, &cpu->ebx);
		pop_dword(cpu, &cpu->edx);
		pop_dword(cpu, &cpu->ecx);
		pop_dword(cpu, &cpu->eax);
	}
	else {
		uint16_t sp = 0;
		pop_word(cpu, &cpu->di);
		pop_word(cpu, &cpu->si);
		pop_word(cpu, &cpu->bp);
		pop_word(cpu, &sp); /* discard sp */
		pop_word(cpu, &cpu->bx);
		pop_word(cpu, &cpu->dx);
		pop_word(cpu, &cpu->cx);
		pop_word(cpu, &cpu->ax);
	}	
}
static void push_imm(I80386* cpu) {
	/* Push immediate value - Push Ib/Iv - (68/6A) b011010S0 */

	if (S) { /* sign extended to 16bit */
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		if (cpu->operand_size) {
			uint32_t se = sign_extend8_32(imm);
			push_dword(cpu, se);
		}
		else {
		uint16_t se = sign_extend8_16(imm);
		push_word(cpu, se);
	}
	}
	else {
		if (cpu->operand_size) {
			uint32_t imm = 0;
			if (!fetch_dword(cpu, &imm)) {
				return;
			}
			push_dword(cpu, imm);
		}
		else {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		push_word(cpu, imm);
	}
}
}

static void enter(I80386* cpu) {
	/* Enter proceedure - enter Iw,Ib - (C8) b11001000 */
	uint16_t op1 = 0;
	uint8_t op2 = 0;

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!fetch_word(cpu, &op1)) {
		return;
	}
	if (!fetch_byte(cpu, &op2)) {
		return;
	}

	cpu->effective_address.stack_address = 1;

	if (cpu->operand_size) {
		uint32_t ebp = cpu->ebp;
		uint16_t sp = cpu->sp;
		uint8_t level = op2 % 32;

		sp -= 4;
		if (!write_dword_sreg(cpu, SEG_SS, sp, ebp)) {
			return;
		}

		uint32_t frame_ptr = sp;
		uint32_t stack_dword = 0;

		if (level > 0) {
			for (int i = 0; i < level - 1; ++i) {
				ebp -= 4;
				if (!read_dword_sreg(cpu, SEG_SS, ebp & 0xFFFF, &stack_dword)) {
					return;
				}

				sp -= 4;
				if (!write_dword_sreg(cpu, SEG_SS, sp, stack_dword)) {
					return;
				}
			}

			sp -= 4;
			if (!write_dword_sreg(cpu, SEG_SS, sp, frame_ptr)) {
				return;
			}
		}

		cpu->ebp = frame_ptr;
		cpu->esp = (sp - op1) & 0xFFFF;
	}
	else {
		uint16_t bp = cpu->bp;
		uint16_t sp = cpu->sp;
		uint8_t level = op2 % 32;
		sp -= 2;
		if (!write_word_sreg(cpu, SEG_SS, sp, bp)) {
			return;
		}

		uint16_t frame_ptr = sp;
		uint16_t stack_word = 0;

		if (level > 0) {
			for (int i = 0; i < level - 1; ++i) {
				bp -= 2;
				if (!read_word_sreg(cpu, SEG_SS, bp, &stack_word)) {
					return;
				}

				sp -= 2;
				if (!write_word_sreg(cpu, SEG_SS, sp, stack_word)) {
					return;
				}
			}

			sp -= 2;
			if (!write_word_sreg(cpu, SEG_SS, sp, frame_ptr)) {
				return;
			}
		}

		cpu->bp = frame_ptr;
		cpu->sp = sp - op1;
	}
}
static void leave(I80386* cpu) {
	/* Leave procedure - leave - (C9) b11001001 */

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->operand_size) {
		if (cpu->bp > cpu->ss.desc.limit - 3U) {
			i80386_exception(cpu, EXCEPTION_SS);
			return;
		}
		cpu->esp = cpu->bp;
		pop_dword(cpu, &cpu->ebp);
	}
	else {
		if (cpu->bp > cpu->ss.desc.limit) {
			i80386_exception(cpu, EXCEPTION_SS);
			return;
		}
		cpu->esp = cpu->bp;
		pop_word(cpu, &cpu->bp);
	}
}

static void nop(I80386* cpu) {
	/* nop (90) b10010000 */
	(void)cpu;
}
static void xchg_accum_reg(I80386* cpu) {
	/* xchg AX/EAX, reg16/reg32 (91-97) b10010REG */
	if (cpu->operand_size) {
		uint32_t tmp = cpu->eax;
		cpu->eax = reg32_read(cpu, cpu->opcode);
		reg32_write(cpu, cpu->opcode, tmp);
	}
	else {
		uint16_t tmp = cpu->ax;
		cpu->ax = reg16_read(cpu, cpu->opcode);
		reg16_write(cpu, cpu->opcode, tmp);
	}
}
static void xchg_rm_reg(I80386* cpu) {
	/* xchg R/M, reg8/16/32 (86/87) b1000011W */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		if (cpu->operand_size) {
			I80386_OPERAND rm = { 0 };
			if (!modrm_get_rm(cpu, &rm)) {
				return;
			}
			uint32_t reg = modrm_read_reg32(cpu);
			uint32_t tmp = 0;
			if (!modrm_read_rm32(cpu, &rm, &tmp)) {
				return;
			}
			if (!modrm_write_rm32(cpu, &rm, reg)) {
				return;
			}
			modrm_write_reg32(cpu, tmp);
		}
		else {
			I80386_OPERAND rm = { 0 };
			if (!modrm_get_rm(cpu, &rm)) {
				return;
			}
			uint16_t reg = modrm_read_reg16(cpu);
			uint16_t tmp = 0;
			if (!modrm_read_rm16(cpu, &rm, &tmp)) {
				return;
			}
			if (!modrm_write_rm16(cpu, &rm, reg)) {
				return;
			}
			modrm_write_reg16(cpu, tmp);
		}
	}
	else {
		I80386_OPERAND rm = { 0 };
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		uint8_t reg = modrm_read_reg8(cpu);
		uint8_t tmp = 0;
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return;
		}
		if (!modrm_write_rm8(cpu, &rm, reg)) {
			return;
		}
		modrm_write_reg8(cpu, tmp);
	}
}

static void cbw(I80386* cpu) {
	/* Convert byte to word (98) b10011000 */
	if (cpu->al & 0x80) {
		cpu->ah = 0xFF;
	}
	else {
		cpu->ah = 0;
	}
}
static void cwd(I80386* cpu) {
	/* Convert word to dword (99) b10011001 */
	if (cpu->ax & 0x8000) {
		cpu->dx = 0xFFFF;
	}
	else {
		cpu->dx = 0;
	}
}

static void wait(I80386* cpu) {
	/* wait (9B) b10011011 */
#if 0
	if (!cpu->test) {
		i80386_rollback_instruction(cpu); /* test pin */
	}
#endif
	(void)cpu;
}

static void sahf(I80386* cpu) {
	/* Store AH into flags (9E) b10011110 */
	cpu->psw.word &= 0xFF02; /* Mask hi byte; Clear bit 2 */
	cpu->psw.word |= cpu->ah & 0xD5;
}
static void lahf(I80386* cpu) {
	/* Load flags into AH (9F) b10011111 */
	cpu->ah = cpu->psw.word & 0xD7;
}

static void hlt(I80386* cpu) {
	/* Halt CPU (F4) b11110100 */
	cpu->halt = 1;
	i80386_rollback_instruction(cpu);
}
static void cmc(I80386* cpu) {
	/* Complement carry flag (F5) b11110101 */
	cpu->psw.cf = !cpu->psw.cf;
}
static void clc(I80386* cpu) {
	/* clear carry flag (F8) b11111000 */
	cpu->psw.cf = 0;
}
static void stc(I80386* cpu) {
	/* set carry flag (F9) b11111001 */
	cpu->psw.cf = 1;
}
static void cli(I80386* cpu) {
	/* clear interrupt flag (FA) b11111010 */
	if (cpu->cpl > cpu->psw.iopl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}
	cpu->psw.in = 0;
}
static void sti(I80386* cpu) {
	/* set interrupt flag (FB) b1111011 */
	if (cpu->cpl > cpu->psw.iopl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}
	cpu->psw.in = 1;
}
static void cld(I80386* cpu) {
	/* clear direction flag (FC) b11111100 */
	cpu->psw.df = 0;
}
static void std(I80386* cpu) {
	/* set direction flag (FD) b11111101 */
	cpu->psw.df = 1;
}

static void inc_reg(I80386* cpu) {
	/* Inc REG (40-47) b01000REG */
	if (cpu->operand_size) {
		uint32_t tmp = reg32_read(cpu, cpu->opcode);
		i80386_alu_inc32(cpu, &tmp);
		reg32_write(cpu, cpu->opcode, tmp);
	}
	else {
	uint16_t tmp = reg16_read(cpu, cpu->opcode);
	i80386_alu_inc16(cpu, &tmp);
	reg16_write(cpu, cpu->opcode, tmp);
}
}
static void inc_rm(I80386* cpu) {
	/* Inc Eb/Ev (FE/FF, R/M reg = b000) b1111111W */
		I80386_OPERAND rm = { 0 };
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
	if (W) {
		if (cpu->operand_size) {
			uint32_t tmp = 0;
			if (!modrm_read_rm32(cpu, &rm, &tmp)) {
				return;
			}
			i80386_alu_inc32(cpu, &tmp);
			modrm_write_rm32(cpu, &rm, tmp);
		}
		else {
			uint16_t tmp = 0;
		if (!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_inc16(cpu, &tmp);
		modrm_write_rm16(cpu, &rm, tmp);
	}
	}
	else {
		uint8_t tmp = 0;
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_inc8(cpu, &tmp);
		modrm_write_rm8(cpu, &rm, tmp);
	}
}
static void dec_reg(I80386* cpu) {
	/* Dec REG (48-4F) b01001REG */
	if (cpu->operand_size) {
		uint32_t tmp = reg32_read(cpu, cpu->opcode);
		i80386_alu_dec32(cpu, &tmp);
		reg32_write(cpu, cpu->opcode, tmp);
	}
	else {
	uint16_t tmp = reg16_read(cpu, cpu->opcode);
	i80386_alu_dec16(cpu, &tmp);
	reg16_write(cpu, cpu->opcode, tmp);
}
}
static void dec_rm(I80386* cpu) {
	/* Dec Eb/Ev (FE/FF, R/M reg = b001) b1111111W */
		I80386_OPERAND rm = { 0 };
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
	if (W) {
		if (cpu->operand_size) {
			uint32_t tmp = 0;
			if (!modrm_read_rm32(cpu, &rm, &tmp)) {
				return;
			}
			i80386_alu_dec32(cpu, &tmp);
			modrm_write_rm32(cpu, &rm, tmp);
		}
		else {
			uint16_t tmp = 0;
		if (!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_dec16(cpu, &tmp);
		modrm_write_rm16(cpu, &rm, tmp);
	}
	}
	else {
		uint8_t tmp = 0;
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_dec8(cpu, &tmp);
		modrm_write_rm8(cpu, &rm, tmp);
	}
}

static void rol_rm_cl(I80386* cpu) {
	/* Rotate left (D0/D1/D2/D3, R/M reg = b000) b110100VW */
	exec_bin_grp2_cl(cpu, i80386_alu_rol8, i80386_alu_rol16, i80386_alu_rol32);
}
static void ror_rm_cl(I80386* cpu) {
	/* Rotate left (D0/D1/D2/D3, R/M reg = b001) b110100VW */
	exec_bin_grp2_cl(cpu, i80386_alu_ror8, i80386_alu_ror16, i80386_alu_ror32);
}
static void rcl_rm_cl(I80386* cpu) {
	/* Rotate through carry left (D0/D1/D2/D3, R/M reg = b010) b110100VW */
	exec_bin_grp2_cl(cpu, i80386_alu_rcl8, i80386_alu_rcl16, i80386_alu_rcl32);
}
static void rcr_rm_cl(I80386* cpu) {
	/* Rotate through carry right (D0/D1/D2/D3, R/M reg = b011) b110100VW */
	exec_bin_grp2_cl(cpu, i80386_alu_rcr8, i80386_alu_rcr16, i80386_alu_rcr32);
}
static void shl_rm_cl(I80386* cpu) {
	/* Shift left (D0/D1/D2/D3, R/M reg = b100) b110100VW */
	exec_bin_grp2_cl(cpu, i80386_alu_shl8, i80386_alu_shl16, i80386_alu_shl32);
}
static void shr_rm_cl(I80386* cpu) {
	/* Shift Logical right (D0/D1/D2/D3, R/M reg = b101) b110100VW */
	exec_bin_grp2_cl(cpu, i80386_alu_shr8, i80386_alu_shr16, i80386_alu_shr32);
}
static void sal_rm_cl(I80386* cpu) {
	/* Shift Arithmetic left (D0/D1/D2/D3, R/M reg = b110) b110100VW */
	exec_bin_grp2_cl(cpu, i80386_alu_sal8, i80386_alu_sal16, i80386_alu_sal32);
}
static void sar_rm_cl(I80386* cpu) {
	/* Shift Arithmetic right (D0/D1/D2/D3, R/M reg = b111) b110100VW */
	exec_bin_grp2_cl(cpu, i80386_alu_sar8, i80386_alu_sar16, i80386_alu_sar32);
}
static void shld_rm_cl(I80386* cpu) {
	/* shld Ev,Gv,CL - (0F A5 /r) */
	I80386_OPERAND rm = { 0 };
	uint8_t tmp3 = cpu->cl & 0x1F;

	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->operand_size) {
		uint32_t tmp = 0;
		uint32_t tmp2 = 0;

		if (tmp3 >= 32) {
			return; /* UNDEFINED */
		}

		if (!modrm_read_rm32(cpu, &rm, &tmp)) {
			return;
		}
		tmp2 = modrm_read_reg32(cpu);

		i80386_alu_shld32(cpu, &tmp, tmp2, tmp3);

		if (!modrm_write_rm32(cpu, &rm, tmp)) {
			return;
		}
	}
	else {
		uint16_t tmp = 0;
		uint16_t tmp2 = 0;

		if (tmp3 >= 16) {
			return; /* UNDEFINED */
		}

		if (!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		tmp2 = modrm_read_reg16(cpu);

		i80386_alu_shld16(cpu, &tmp, tmp2, tmp3);

		if (!modrm_write_rm16(cpu, &rm, tmp)) {
			return;
		}
	}
}
static void shrd_rm_cl(I80386* cpu) {
	/* shrd Ev,Gv,CL - (0F AD /r) */
	I80386_OPERAND rm = { 0 };
	uint8_t tmp3 = cpu->cl & 0x1F;

	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->operand_size) {
		uint32_t tmp = 0;
		uint32_t tmp2 = 0;

		if (tmp3 >= 32) {
			return; /* UNDEFINED */
		}

		if (!modrm_read_rm32(cpu, &rm, &tmp)) {
			return;
		}
		tmp2 = modrm_read_reg32(cpu);

		i80386_alu_shrd32(cpu, &tmp, tmp2, tmp3);

		if (!modrm_write_rm32(cpu, &rm, tmp)) {
			return;
		}
	}
	else {
		uint16_t tmp = 0;
		uint16_t tmp2 = 0;

		if (tmp3 >= 16) {
			return; /* UNDEFINED */
		}

		if (!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		tmp2 = modrm_read_reg16(cpu);

		i80386_alu_shrd16(cpu, &tmp, tmp2, tmp3);

		if (!modrm_write_rm16(cpu, &rm, tmp)) {
			return;
		}
	}
}

static void rol_rm_imm(I80386* cpu) {
	/* Rotate left (C0/C1, R/M reg = b000) b1100000W */
	exec_bin_grp2_imm(cpu, i80386_alu_rol8, i80386_alu_rol16, i80386_alu_rol32);
}
static void ror_rm_imm(I80386* cpu) {
	/* Rotate left (C0/C1, R/M reg = b001) b1100000W */
	exec_bin_grp2_imm(cpu, i80386_alu_ror8, i80386_alu_ror16, i80386_alu_ror32);
}
static void rcl_rm_imm(I80386* cpu) {
	/* Rotate through carry left (C0/C1, R/M reg = b010) b1100000W */
	exec_bin_grp2_imm(cpu, i80386_alu_rcl8, i80386_alu_rcl16, i80386_alu_rcl32);
}
static void rcr_rm_imm(I80386* cpu) {
	/* Rotate through carry right (C0/C1, R/M reg = b011) b1100000W */
	exec_bin_grp2_imm(cpu, i80386_alu_rcr8, i80386_alu_rcr16, i80386_alu_rcr32);
}
static void shl_rm_imm(I80386* cpu) {
	/* Shift left (C0/C1, R/M reg = b100) b1100000W */
	exec_bin_grp2_imm(cpu, i80386_alu_shl8, i80386_alu_shl16, i80386_alu_shl32);
}
static void shr_rm_imm(I80386* cpu) {
	/* Shift Logical right (C0/C1, R/M reg = b101) b1100000W */
	exec_bin_grp2_imm(cpu, i80386_alu_shr8, i80386_alu_shr16, i80386_alu_shr32);
}
static void sal_rm_imm(I80386* cpu) {
	/* Shift Arithmetic left (C0/C1, R/M reg = b110) b1100000W */
	exec_bin_grp2_imm(cpu, i80386_alu_sal8, i80386_alu_sal16, i80386_alu_sal32);
}
static void sar_rm_imm(I80386* cpu) {
	/* Shift Arithmetic right (C0/C1, R/M reg = b111) b1100000W */
	exec_bin_grp2_imm(cpu, i80386_alu_sar8, i80386_alu_sar16, i80386_alu_sar32);
}
static void shld_rm_imm(I80386* cpu) {
	/* shld Ev,Gv,Ib - (0F A4 /r) */
	I80386_OPERAND rm = { 0 };
	uint8_t tmp3 = 0;

	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (!fetch_byte(cpu, &tmp3)) {
		return;
	}
	tmp3 &= 0x1F;

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->operand_size) {
		uint32_t tmp = 0;
		uint32_t tmp2 = 0;

		if (tmp3 >= 32) {
			return; /* UNDEFINED */
		}

		if (!modrm_read_rm32(cpu, &rm, &tmp)) {
			return;
		}
		tmp2 = modrm_read_reg32(cpu);

		i80386_alu_shld32(cpu, &tmp, tmp2, tmp3);

		if (!modrm_write_rm32(cpu, &rm, tmp)) {
			return;
		}
	}
	else {
		uint16_t tmp = 0;
		uint16_t tmp2 = 0;

		if (tmp3 >= 16) {
			return; /* UNDEFINED */
		}

		if (!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		tmp2 = modrm_read_reg16(cpu);

		i80386_alu_shld16(cpu, &tmp, tmp2, tmp3);

		if (!modrm_write_rm16(cpu, &rm, tmp)) {
			return;
		}
	}
}
static void shrd_rm_imm(I80386* cpu) {
	/* shrd Ev,Gv,Ib - (0F AC /r) */
	I80386_OPERAND rm = { 0 };
	uint8_t tmp3 = 0;

	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (!fetch_byte(cpu, &tmp3)) {
		return;
	}
	tmp3 &= 0x1F;

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->operand_size) {
		uint32_t tmp = 0;
		uint32_t tmp2 = 0;

		if (tmp3 >= 32) {
			return; /* UNDEFINED */
		}

		if (!modrm_read_rm32(cpu, &rm, &tmp)) {
			return;
		}
		tmp2 = modrm_read_reg32(cpu);

		i80386_alu_shrd32(cpu, &tmp, tmp2, tmp3);

		if (!modrm_write_rm32(cpu, &rm, tmp)) {
			return;
		}
	}
	else {
		uint16_t tmp = 0;
		uint16_t tmp2 = 0;

		if (tmp3 >= 16) {
			return; /* UNDEFINED */
		}

		if (!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		tmp2 = modrm_read_reg16(cpu);

		i80386_alu_shrd16(cpu, &tmp, tmp2, tmp3);

		if (!modrm_write_rm16(cpu, &rm, tmp)) {
			return;
		}
	}
}

static void loopz(I80386* cpu) {
	/* loop while zero/not zero (E0/E1) b1110000Z */
	uint8_t imm = 0;
	uint32_t offset = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	offset = sign_extend8_32(imm);
	cpu->cx -= 1;
	if (cpu->cx && cpu->psw.zf == Z) {
		if (cpu->address_size) {
			cpu->eip += offset;
		}
		else {
			cpu->ip += (offset & 0xFFFF);
		}
	}
}
static void loop(I80386* cpu) {
	/* loop if CX not zero (E2) b11100010 */
	uint8_t imm = 0;
	uint32_t offset = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	offset = sign_extend8_32(imm);
	cpu->cx -= 1;
	if (cpu->cx) {
		if (cpu->address_size) {
			cpu->eip += offset;
		}
		else {
			cpu->ip += (offset & 0xFFFF);
		}
	}
}

static void jcc_short(I80386* cpu) {
	/* jmp Jb - (70-7F) b0111CCCC */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	if (i80386_check_condition(cpu)) {
		uint32_t offset = sign_extend8_32(imm);
		if (cpu->address_size) {
			cpu->eip += offset;
		}
		else {
			cpu->ip += (offset & 0xFFFF);
		}
	}
}
static void jcc_long(I80386* cpu) {
	/* jmp Jv - (0F 80-8F) b00001111 b1000CCCC */
	uint32_t offset = 0;
	if (cpu->operand_size) {
		if (!fetch_dword(cpu, &offset)) {
			return;
		}
	}
	else {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		offset = sign_extend16_32(imm);
	}

	if (i80386_check_condition(cpu)) {
		if (cpu->address_size) {
			cpu->eip += offset;
		}
		else {
			cpu->ip += (offset & 0xFFFF);
		}
	}
}
static void jcxz(I80386* cpu) {
	/* jcxz Jb - (E3) b11100011 */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	if (cpu->cx == 0) {
		uint32_t se = sign_extend8_32(imm);
		if (cpu->address_size) {
			cpu->eip += se;
		}
		else {
			cpu->ip += (se & 0xFFFF);
		}
	}
}

static void far_jmp_protected_mode(I80386* cpu, uint16_t selector, uint32_t offset) {
	I80386_DESCRIPTOR_TABLE_ENTRY entry = { 0 };

	if (!i80386_read_descriptor_table_entry(cpu, selector, &entry)) {
		return;
	}

	if (entry.ar.s) {
		/* Must be executable */
		if (!entry.ar.e) {
			i80386_exception_code(cpu, EXCEPTION_GP, selector);
			return;
		}

		/* commit register values */
		if (!i80386_load_segment_register(cpu, &cpu->cs, SEG_CS, selector)) {
			return;
		}
		cpu->eip = offset;
	}
	else {
		switch (entry.ar.type) {
			case I80386_GATE_TYPE_AVAL_286:
			case I80386_GATE_TYPE_AVAL_386:
				i80386_task_switch(cpu, selector, TASK_SWITCH_JMP);
				break;
			case I80386_GATE_TYPE_TASK:
				i80386_task_gate(cpu, &entry, TASK_SWITCH_JMP);
				break;
			default:
				i80386_exception_code(cpu, EXCEPTION_GP, selector);
				break;
		}
	}
}
static void far_jmp_real_mode(I80386* cpu, uint16_t selector, uint32_t offset) {
	if (!i80386_load_segment_register(cpu, &cpu->cs, SEG_CS, selector)) {
		return;
	}
	cpu->eip = offset;
}
static void far_call_protected_mode(I80386* cpu, uint16_t selector, uint32_t offset) {
	I80386_DESCRIPTOR_TABLE_ENTRY entry = { 0 };
	uint32_t final_esp = cpu->esp;

	if (!i80386_read_descriptor_table_entry(cpu, selector, &entry)) {
		return;
	}

	if (entry.ar.s) {
		/* Must be executable */
		if (!entry.ar.e) {
			i80386_exception_code(cpu, EXCEPTION_GP, selector);
			return;
		}

		/* push frame */
		if (!push_word_at(cpu, &final_esp, cpu->cs.selector)) {
			return;
		}
		if (cpu->operand_size) {
			if (!push_dword_at(cpu, &final_esp, cpu->eip)) {
				return;
			}
		}
		else {
			if (!push_word_at(cpu, &final_esp, cpu->ip)) {
				return;
			}
		}

		/* commit register values */
		if (!i80386_load_segment_register(cpu, &cpu->cs, SEG_CS, selector)) {
			return;
		}
		cpu->eip = offset;

		if (cpu->address_size) {
			cpu->esp = final_esp;
		}
		else {
			cpu->esp = final_esp & 0xFFFF;
		}
	}
	else {
		switch (entry.ar.type) {
			case I80386_GATE_TYPE_CALL_286:
			case I80386_GATE_TYPE_CALL_386:
				i80386_call_gate(cpu, &entry, selector);				
				break;
			case I80386_GATE_TYPE_TASK:
				i80386_task_gate(cpu, &entry, TASK_SWITCH_CALL);
				break;
			default:
				i80386_exception_code(cpu, EXCEPTION_GP, selector);
				break;
		}
	}
}
static void far_call_real_mode(I80386* cpu, uint16_t selector, uint32_t offset) {
	uint32_t final_esp = cpu->esp;
	if (!push_word_at(cpu, &final_esp, cpu->cs.selector)) {
		return;
	}

	if (cpu->operand_size) {
		if (!push_dword_at(cpu, &final_esp, cpu->eip)) {
			return;
		}
	}
	else {
		if (!push_word_at(cpu, &final_esp, cpu->ip)) {
			return;
		}
	}

	/* commit register values */
	if (!i80386_load_segment_register(cpu, &cpu->cs, SEG_CS, selector)) {
		return;
	}
	cpu->eip = offset;

	if (cpu->address_size) {
		cpu->esp = final_esp;
	}
	else {
		cpu->esp = final_esp & 0xFFFF;
	}
}

static void jmp_intra_direct_short(I80386* cpu) {
	/* jmp Jb - (EB) b11101011 */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	uint32_t se = sign_extend8_32(imm);
	if (cpu->address_size) {
		cpu->eip += se;
	}
	else {
		cpu->ip += (se & 0xFFFF);
	}
}
static void jmp_intra_direct(I80386* cpu) {
	/* jmp Jv - (E9) b11101001 */
	if (cpu->operand_size) {
		/* JMP rel32 */
		uint32_t imm = 0;
		if (!fetch_dword(cpu, &imm)) {
			return;
		}
		if (cpu->address_size) {
			cpu->eip += imm;
		}
		else {
			cpu->ip += (imm & 0xFFFF);
		}
	}
	else {
		/* JMP rel16 */
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		if (cpu->address_size) {
			cpu->eip += imm;
		}
		else {
			cpu->ip += imm;
		}
	}
}
static void jmp_intra_indirect(I80386* cpu) {
	/* jmp Ev - (FF /4) b11111111 */
	if (cpu->operand_size) {
		/* FF /4 JMP r/m32 */
		I80386_OPERAND rm = { 0 };
		uint32_t offset = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm32(cpu, &rm, &offset)) {
			return;
		}
		cpu->eip = offset;
	}
	else {
		/* FF /4 JMP r/m16 */
		I80386_OPERAND rm = { 0 };
		uint16_t offset = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, &rm, &offset)) {
			return;
		}
		cpu->eip = offset;
	}
}
static void jmp_inter_direct(I80386* cpu) {
	/* jmp Ap - (EA) b11101010 */
		uint32_t offset = 0;
		uint16_t selector = 0;

		if (cpu->operand_size) {
			/* EA cp JMP ptr16:32 */
			uint32_t imm = 0;
			if (!fetch_dword(cpu, &imm)) {
				return;
			}
			offset = imm;
		}
		else {
			/* EA cd JMP ptr16:16 */
			uint16_t imm = 0;
			if (!fetch_word(cpu, &imm)) {
				return;
			}
			offset = imm;
		}

		if (!fetch_word(cpu, &selector)) {
			return;
		}

	if (!cpu->msw.pe) {
		/* Real mode */
		far_jmp_real_mode(cpu, selector, offset);
		}
	else if (cpu->eflags.vm) {
		/* Virtual 8086 mode */
	}
	else {
		/* Protected mode */
		far_jmp_protected_mode(cpu, selector, offset);
			}
		}
static void jmp_inter_indirect(I80386* cpu) {
	/* jmp Ep - (FF /5) b11111111 */
	uint16_t selector = 0;
	uint32_t offset = 0;

	/* The mod R/M byte may refer only to memory */
	if (cpu->modrm.mod == 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!modrm_get_effective_address(cpu, NULL)) {
		return;
	}

		if (cpu->operand_size) {
			/* FF /5 JMP m16:32 */
			if (!read_dword_ea(cpu, &cpu->effective_address, &offset)) {
				return;
			}
			cpu->effective_address.logical_address.offset += 4;
		}
		else {
			/* FF /5 JMP m16:16 */
			if (!read_word_ea(cpu, &cpu->effective_address, (uint16_t*)&offset)) {
				return;
			}
			cpu->effective_address.logical_address.offset += 2;
		}

		if (!read_word_ea(cpu, &cpu->effective_address, &selector)) {
			return;
		}

	if (!cpu->msw.pe) {
		/* Real mode */
		far_jmp_real_mode(cpu, selector, offset);
		}
	else if (cpu->eflags.vm) {
		/* Virtual 8086 mode */
	}
	else {
		/* Protected mode */
		far_jmp_protected_mode(cpu, selector, offset);
			}
		}

static void call_intra_direct(I80386* cpu) {
	/* Call Av - (E8) b11101000 */
	if (cpu->operand_size) {
		uint32_t imm = 0;
		if (!fetch_dword(cpu, &imm)) {
			return;
		}
		if (!push_dword(cpu, cpu->eip)) {
			return;
		}
		cpu->eip += imm;
	}
	else {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		if (!push_word(cpu, cpu->ip)) {
			return;
		}
		cpu->eip = (cpu->eip + imm) & 0xFFFF; /* clear upper 16bits */
	}
}
static void call_intra_indirect(I80386* cpu) {
	/* call Ev (FF /2) b11111111 */
	I80386_OPERAND rm = { 0 };
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	if (cpu->operand_size) {
		uint32_t eip = 0;
		if (!modrm_read_rm32(cpu, &rm, &eip)) {
			return;
		}
		if (!push_dword(cpu, cpu->eip)) {
			return;
		}
		cpu->eip = eip;
	}
	else {
		uint16_t ip = 0;
		if (!modrm_read_rm16(cpu, &rm, &ip)) {
			return;
		}
		if (!push_word(cpu, cpu->ip)) {
			return;
		}
		cpu->eip = ip; /* clear upper 16bits */
	}
}
static void call_inter_direct(I80386* cpu) {
	/* call Ap - (9A) b10011010 */
		uint16_t selector = 0;
	uint32_t offset = 0;

		if (cpu->operand_size) {
			if (!fetch_dword(cpu, &offset)) {
				return;
			}
			}
		else {
			uint16_t imm = 0;
			if (!fetch_word(cpu, &imm)) {
				return;
			}
			offset = imm; /* clear upper 16bits */
		}

	if (!fetch_word(cpu, &selector)) {
			return;
		}

	if (!cpu->msw.pe) {
		/* Real mode */
		far_call_real_mode(cpu, selector, offset);
	}
	else if (cpu->eflags.vm) {
		/* Virtual 8086 mode */
	}
	else {
		/* Protected mode */
		far_call_protected_mode(cpu, selector, offset);
			}
		}
static void call_inter_indirect(I80386* cpu) {
	/* call Ep (FF /3) b11111111 */
	uint16_t selector = 0;
	uint32_t offset = 0;

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	/* The mod R/M byte may refer only to memory */
	if (cpu->modrm.mod == 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!modrm_get_effective_address(cpu, NULL)) {
		return;
		}

		if (cpu->operand_size) {
			if (!read_dword_ea(cpu, &cpu->effective_address, &offset)) {
				return;
			}
			cpu->effective_address.logical_address.offset += 4;
		}
		else {
			uint16_t imm = 0;
			if (!read_word_ea(cpu, &cpu->effective_address, &imm)) {
				return;
			}
			offset = imm; /* clear upper 16bits */
			cpu->effective_address.logical_address.offset += 2;
		}

		if (!read_word_ea(cpu, &cpu->effective_address, &selector)) {
			return;
		}

	if (!cpu->msw.pe) {
		/* Real mode */
		far_call_real_mode(cpu, selector, offset);
		}
	else if (cpu->eflags.vm) {
		/* Virtual 8086 mode */
			}
			else {
		/* Protected mode */
		far_call_protected_mode(cpu, selector, offset);
				}
			}

static void ret_intra(I80386* cpu) {
	/* ret/retd <Iw> (C2/C3) b1100001E */
	uint32_t eip = 0;
	uint16_t frame = 0;
	uint32_t final_esp = cpu->esp;

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->operand_size) {
		if (!pop_dword_at(cpu, &final_esp, &eip)) {
			return;
		}
	}
	else {
		uint16_t ip = 0;
		if (!pop_word_at(cpu, &final_esp, &ip)) {
			return;
		}
		eip = ip; /* clear upper 16bits */
	}

	if (eip > cpu->cs.desc.limit) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	/* if bit 1 is set in opcode then fetch frame word */
	if (!(cpu->opcode & 0x1)) {
		if (!fetch_word(cpu, &frame)) {
			return;
		}
	}

	/* commit register values */
	if (cpu->address_size) {
		cpu->esp = (final_esp + frame);
	}
	else {
		cpu->esp = (final_esp + frame) & 0xFFFF;
	}
	cpu->eip = eip;
}
static void ret_inter(I80386* cpu) {
	/* retf/retfd <Iw> (CA/CB) b1100101E */
	uint32_t offset = 0;
	uint32_t final_esp = cpu->esp;
	uint16_t selector = 0;
	uint16_t frame = 0;
	I80386_SEGMENT_REGISTER sreg = { 0 };

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

		if (cpu->operand_size) {
		if (!pop_dword_at(cpu, &final_esp, &offset)) {
				return;
			}
		if (!pop_word_align_at(cpu, &final_esp, &selector)) {
				return;
			}
		}
		else {
			uint16_t ip = 0;
			if (!pop_word_at(cpu, &final_esp, &ip)) {
				return;
			}
		offset = ip;
		if (!pop_word_at(cpu, &final_esp, &selector)) {
				return;
			}
		}

	if (!cpu->msw.pe) {
		/* Real mode */
		
	}
	else if (cpu->eflags.vm) {
		/* Virtual 8086 mode */
		
	}
	else {
		/* Protected mode */
		uint16_t stack_word = 0;
		uint8_t rpl = 0;

		if (cpu->operand_size) {
			/* Third word on stack must be within stack limits */
			stack_peek_word(cpu, 2, &stack_word);
			if (stack_word > cpu->ss.desc.limit) {
				i80386_exception(cpu, EXCEPTION_SS);
				return;
			}
		}
		else {
			/* Second word on stack must be within stack limits */
			stack_peek_word(cpu, 1, &stack_word);
			if (stack_word > cpu->ss.desc.limit) {
				i80386_exception(cpu, EXCEPTION_SS);
				return;
			}
		}

		/* Return selector RPL must be >= CPL */
		if (rpl < cpu->cpl) {
			i80386_exception(cpu, EXCEPTION_GP);
			return;
		}
	}

	if (!i80386_load_segment_register(cpu, &sreg, SEG_CS, selector)) {
		return;
	}

	if (offset > sreg.desc.limit) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	/* fetch frame word count */
	if (!(cpu->opcode & 0x1)) {
		if (!fetch_word(cpu, &frame)) {
			return;
		}
	}

	/* commit final register values */
	i80386_copy_segment_descriptor(&cpu->cs, &sreg);
	cpu->eip = offset;

	if (cpu->address_size) {
		cpu->esp = (final_esp + frame);
	}
	else {
		cpu->esp = (final_esp + frame) & 0xFFFF;
	}
	}

static void mov_rm_imm(I80386* cpu) {
	/* mov Eb,Ib / Ev,Iv - (C6/C7) b1100011W */
	
	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!fetch_modrm(cpu)) {
		return;
	}
	if (cpu->modrm.reg != 0) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (W) {
		/* Ev,Iv */
		if (cpu->operand_size) {
			I80386_OPERAND rm = { 0 };
			uint32_t imm = 0;
			if (!modrm_get_rm(cpu, &rm)) {
				return;
			}
			if (!fetch_dword(cpu, &imm)) {
				return;
			}
			modrm_write_rm32(cpu, &rm, imm);
		}
		else {
			I80386_OPERAND rm = { 0 };
			uint16_t imm = 0;
			if (!modrm_get_rm(cpu, &rm)) {
				return;
			}
			if (!fetch_word(cpu, &imm)) {
				return;
			}
			modrm_write_rm16(cpu, &rm, imm);
		}
	}
	else {
		/* Eb,Ib */
		I80386_OPERAND rm = { 0 };
		uint8_t imm = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		modrm_write_rm8(cpu, &rm, imm);
	}
}
static void mov_reg_imm(I80386* cpu) {
	/* mov Gb,Ib / Gv,Iv - (B0-BF) b1011WREG */
	if (WREG) {
		/* Gv,Iv */
		if (cpu->operand_size) {
			uint32_t imm = 0;
			if (!fetch_dword(cpu, &imm)) {
				return;
			}
			reg32_write(cpu, cpu->opcode, imm);
		}
		else {
			uint16_t imm = 0;
			if (!fetch_word(cpu, &imm)) {
				return;
			}
			reg16_write(cpu, cpu->opcode, imm);
		}
	}
	else {
		/* Gb,Ib */
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		reg8_write(cpu, cpu->opcode, imm);
	}
}
static void mov_rm_reg(I80386* cpu) {
	/* mov Eb<->Gb / Ev<->Gv (88/89/8A/8B) b100010DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		if (cpu->operand_size) {
			I80386_OPERAND rm = { 0 };
			uint32_t tmp = 0;
			if (!modrm_get_rm(cpu, &rm)) {
				return;
			}
			if (D) {
				if (!modrm_read_rm32(cpu, &rm, &tmp)) {
					return;
				}
				modrm_write_reg32(cpu, tmp);
			}
			else {
				tmp = modrm_read_reg32(cpu);
				modrm_write_rm32(cpu, &rm, tmp);
			}
		}
		else {
			I80386_OPERAND rm = { 0 };
			uint16_t tmp = 0;
			if (!modrm_get_rm(cpu, &rm)) {
				return;
			}
			if (D) {
				if (!modrm_read_rm16(cpu, &rm, &tmp)) {
					return;
				}
				modrm_write_reg16(cpu, tmp);
			}
			else {
				tmp = modrm_read_reg16(cpu);
				modrm_write_rm16(cpu, &rm, tmp);
			}
		}
	}
	else {
		I80386_OPERAND rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (D) {
			if (!modrm_read_rm8(cpu, &rm, &tmp)) {
				return;
			}
			modrm_write_reg8(cpu, tmp);
		}
		else {
			tmp = modrm_read_reg8(cpu);
			modrm_write_rm8(cpu, &rm, tmp);
		}
	}
}
static void mov_accum_mem(I80386* cpu) {
	/* MOV AL/AX/EAX, Ob/Ov (A0/A1/A2/A3) b101000DW */
	uint32_t addr = 0;
	if (cpu->address_size) {
		if (!fetch_dword(cpu, &addr)) {
			return;
		}
	}
	else {
		if (!fetch_word(cpu, (uint16_t*)&addr)) {
			return;
		}
	}

	if (W) {
		if (cpu->operand_size) {
			if (D) {
				write_dword_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), addr, cpu->eax);
			}
			else {
				read_dword_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), addr, &cpu->eax);
			}
		}
		else {
			if (D) {
				write_word_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), addr, cpu->ax);
			}
			else {
				read_word_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), addr, &cpu->ax);
			}
		}
	}
	else {
		if (D) {
			write_byte_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), addr, cpu->al);
		}
		else {
			read_byte_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), addr, &cpu->al);
		}
	}
}
static void mov_seg(I80386* cpu) {
	/* mov Ew<->SW (8C/8E) b100011D0 */
	I80386_OPERAND rm = { 0 };
	uint8_t sreg = 0;

	if (!fetch_modrm(cpu)) {
		return;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (!modrm_get_seg_index(cpu, cpu->modrm, &sreg)) {
		return;
	}

	if (D) {
		uint16_t selector = 0;
		if (sreg == SEG_CS) {
			/* illegal - mov cs, r/m */
			i80386_exception(cpu, EXCEPTION_UD);
			return;
		}
		if (sreg == SEG_SS) {
			cpu->int_delay = 1;
		}

		if (!modrm_read_rm16(cpu, &rm, &selector)) {
			return;
		}

		i80386_load_segment_register(cpu, &cpu->segment_registers[sreg], sreg, selector);
	}
	else {
		modrm_write_rm16(cpu, &rm, cpu->segment_registers[sreg].selector);
	}
}
static void mov_cr(I80386* cpu) {
	/* mov Cd,Rd /r - (0F 20/22) 0b00001111 b001000D0 */
	if (!fetch_modrm(cpu)){
		return;
	}

	/* Only 0,2,3 are valid on 386 */
	if (cpu->modrm.reg == 1 || cpu->modrm.reg > 3) { 
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	/* The mod R/M byte may refer only to a register */
	if (cpu->modrm.mod != 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	/* Must be ring 0 */
	if (cpu->cpl != 0) { 
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (D) {
		/* reloading CR3 flushes the TLB */
		if (cpu->modrm.reg == 3) {
			i80386_flush_tlb(cpu);
		}
		/* changing PG bit in CR0 flushes the TLB */
		if (cpu->modrm.reg == 0 && (cpu->control_registers[cpu->modrm.reg] ^ cpu->general_registers[cpu->modrm.rm].r32) & 0x80000000) {
			i80386_flush_tlb(cpu);
		}
		cpu->control_registers[cpu->modrm.reg] = cpu->general_registers[cpu->modrm.rm].r32;
	}
	else {
		cpu->general_registers[cpu->modrm.rm].r32 = cpu->control_registers[cpu->modrm.reg];
	}
}
static void mov_dr(I80386* cpu) {
	/* mov Dd,Rd /r (0F 21/23) 0b00001111 b001000D1 */
	if (!fetch_modrm(cpu)) {
		return;
	}
	
	/* Only 0,1,2,3,6,7 are valid on 386 */
	if (cpu->modrm.reg == 4 || cpu->modrm.reg == 5) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	/* The mod R/M byte may refer only to a register */
	if (cpu->modrm.mod != 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	/* Must be ring 0 */
	if (cpu->cpl != 0) { 
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (D) {
		cpu->debug_registers[cpu->modrm.reg] = cpu->general_registers[cpu->modrm.rm].r32;
	}
	else {
		cpu->general_registers[cpu->modrm.rm].r32 = cpu->debug_registers[cpu->modrm.reg];
	}
}
static void mov_tr(I80386* cpu) {
	/* mov Td,Rd /r (0F 24/26) 0b00001111 b001001D0 */
	if (!fetch_modrm(cpu)) {
		return;
	}
	
	/* Only 6,7 are valid on 386 */
	if (cpu->modrm.reg < 6) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}
	
	/* The mod R/M byte may refer only to a register */
	if (cpu->modrm.mod != 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	/* Must be ring 0 */
	if (cpu->cpl != 0) { 
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (D) {
		cpu->test_registers[cpu->modrm.reg] = cpu->general_registers[cpu->modrm.rm].r32;
	}
	else {
		cpu->general_registers[cpu->modrm.rm].r32 = cpu->test_registers[cpu->modrm.reg];
	}
}
static void movzx(I80386* cpu) {
	/* Move with zero-extend - (0F B6/B7 /r) b */
	
	if (!fetch_modrm(cpu)) {
		return;
	}
	
	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (W) {
		I80386_OPERAND rm = { 0 };
		uint16_t val = 0;

		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, &rm, &val)) {
			return;
		}

		if (cpu->operand_size) {
			/* r32, r/m16 */
			modrm_write_reg32(cpu, val);
		}
		else {
			/* r16, r/m16 */
			modrm_write_reg16(cpu, val);
		}
	}
	else {
		I80386_OPERAND rm = { 0 };
		uint8_t val = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, &rm, &val)) {
			return;
		}

		if (cpu->operand_size) {
			/* r32, r/m8 */
			modrm_write_reg32(cpu, val);
		}
		else {
			/* r16, r/m8 */
			modrm_write_reg16(cpu, val);
		}		
	}
}
static void movsx(I80386* cpu) {
	/* Move with sign-extend - (0F BE/BF /r) b */

	if (!fetch_modrm(cpu)) {
		return;
	}

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (W) {
		I80386_OPERAND rm = { 0 };
		uint16_t val = 0;

		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, &rm, &val)) {
			return;
		}

		if (cpu->operand_size) {
			/* r32, r/m16 */
			uint32_t se = 0;
			se = sign_extend16_32(val);
			modrm_write_reg32(cpu, se);
		}
		else {
			/* r16, r/m16 */
			modrm_write_reg16(cpu, val);
		}
	}
	else {
		I80386_OPERAND rm = { 0 };
		uint8_t val = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, &rm, &val)) {
			return;
		}

		if (cpu->operand_size) {
			/* r32, r/m8 */
			uint32_t se = 0;
			se = sign_extend8_32(val);
			modrm_write_reg32(cpu, se);
		}
		else {
			/* r16, r/m8 */
			uint16_t se = 0;
			se = sign_extend8_16(val);
			modrm_write_reg16(cpu, se);
		}
	}
}

static void lea(I80386* cpu) {
	/* Gv,M (8D) b10001101 */

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!fetch_modrm(cpu)) {
		return;
	}

	/* M - The mod R/M byte may refer only to memory */
	if (cpu->modrm.mod == 0b11) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!modrm_get_effective_address(cpu, NULL)) {
		return;
	}

	if (cpu->operand_size) {
		modrm_write_reg32(cpu, cpu->effective_address.logical_address.offset);
	}
	else {
		modrm_write_reg16(cpu, cpu->effective_address.logical_address.offset & 0xFFFF);
	}
}

static void not(I80386* cpu) {
	/* not reg (F6/F7, R/M reg = b010) b1111011W */
	if (W) {
		I80386_OPERAND rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_not16(cpu, &tmp);
		modrm_write_rm16(cpu, &rm, tmp);
	}
	else {
		I80386_OPERAND rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_not8(cpu, &tmp);
		modrm_write_rm8(cpu, &rm, tmp);
	}
}
static void neg(I80386* cpu) {
	/* neg reg (F6/F7, R/M reg = b011) b1111011W */	
	if (W) {
		I80386_OPERAND rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if(!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_neg16(cpu, &tmp);
		modrm_write_rm16(cpu, &rm, tmp);
	}
	else {
		I80386_OPERAND rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm(cpu, &rm)) {
			return;
		}
		if(!modrm_read_rm8(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_neg8(cpu, &tmp);
		modrm_write_rm8(cpu, &rm, tmp);
	}
}

static void mul_accum_rm(I80386* cpu) {
	/* mul eAX,Ev - (F6/F7, R/M reg = b100) b1111011W */
	I80386_OPERAND rm = { 0 };
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (W) {
		if (cpu->operand_size) {
			uint32_t tmp = 0;
			uint64_t product = 0;
			if (!modrm_read_rm32(cpu, &rm, &tmp)) {
				return;
			}
			i80386_alu_mul32(cpu, cpu->eax, tmp, &product);
			cpu->edx = product >> 32;
			cpu->eax = product & 0xFFFFFFFF;
		}
		else {
			uint16_t tmp = 0;
			uint32_t product = 0;			
			if (!modrm_read_rm16(cpu, &rm, &tmp)) {
				return;
			}
			i80386_alu_mul16(cpu, cpu->ax, tmp, &product);
			cpu->dx = product >> 16;
			cpu->ax = product & 0xFFFF;
		}
	}
	else {
		uint8_t tmp = 0;
		uint16_t product = 0;
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_mul8(cpu, cpu->al, tmp, &product);
		cpu->ax = product;
	}
}
static void imul_accum_rm(I80386* cpu) {
	/* imul eAX,Ev - (F6/F7, R/M reg = b101) b1111011W */
	I80386_OPERAND rm = { 0 };
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (W) {
		if (cpu->operand_size) {
			uint32_t tmp = 0;
			uint64_t product = 0;
			if (!modrm_read_rm32(cpu, &rm, &tmp)) {
				return;
			}
			i80386_alu_imul32(cpu, cpu->eax, tmp, &product);
			cpu->edx = (product >> 32) & 0xFFFFFFFF;
			cpu->eax = product & 0xFFFFFFFF;
		}
		else {
			uint16_t tmp = 0;
			uint32_t product = 0;
			if (!modrm_read_rm16(cpu, &rm, &tmp)) {
				return;
			}
			i80386_alu_imul16(cpu, cpu->ax, tmp, &product);
			cpu->dx = (product >> 16) & 0xFFFF;
			cpu->ax = product & 0xFFFF;
		}
	}
	else {
		uint8_t tmp = 0;
		uint16_t product = 0;
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return;
		}
		i80386_alu_imul8(cpu, cpu->al, tmp, &product);
		cpu->ax = product;
	}
}
static void imul_reg_rm_imm(I80386* cpu) {
	/* imul Gv,Ev,Iv - (69/6B) b011010S1 */
	I80386_OPERAND rm = { 0 };
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->operand_size) {
		uint32_t multiplicand = 0;
		uint32_t multiplier = 0;
		uint64_t product = 0;
		if (S) {
			uint8_t imm = 0;
			if (!fetch_byte(cpu, &imm)) {
				return;
			}
			multiplier = sign_extend8_32(imm);
		}
		else {
			if (!fetch_dword(cpu, &multiplier)) {
				return;
			}
		}
		if (!modrm_read_rm32(cpu, &rm, &multiplicand)) {
			return;
		}
		i80386_alu_imul32(cpu, multiplicand, multiplier, &product);
		modrm_write_reg32(cpu, product & 0xFFFFFFFF);
	}
	else {
		uint16_t multiplicand = 0;
		uint16_t multiplier = 0;
		uint32_t product = 0;
		if (S) {
			uint8_t imm = 0;
			if (!fetch_byte(cpu, &imm)) {
				return;
			}
			multiplier = sign_extend8_16(imm);
		}
		else {
			if (!fetch_word(cpu, &multiplier)) {
				return;
			}
		}
		if (!modrm_read_rm16(cpu, &rm, &multiplicand)) {
			return;
		}
		i80386_alu_imul16(cpu, multiplicand, multiplier, &product);
		modrm_write_reg16(cpu, product & 0xFFFF);
	}
}
static void imul_reg_rm(I80386* cpu) {
	/* imul Gv,Ev - (0F AF) b00001111 b10101111 */
	I80386_OPERAND rm = { 0 };

	if (!fetch_modrm(cpu)) {
		return;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	if (cpu->operand_size) {
		uint32_t multiplicand = 0;
		uint32_t multiplier = 0;
		uint64_t product = 0;
		multiplicand = modrm_read_reg32(cpu);
		if (!modrm_read_rm32(cpu, &rm, &multiplier)) {
			return;
		}
		i80386_alu_imul32(cpu, multiplicand, multiplier, &product);
		modrm_write_reg32(cpu, product & 0xFFFFFFFF);
	}
	else {
		uint16_t multiplicand = 0;
		uint16_t multiplier = 0;
		uint32_t product = 0;
		multiplicand = modrm_read_reg16(cpu);
		if (!modrm_read_rm16(cpu, &rm, &multiplier)) {
			return;
		}
		i80386_alu_imul16(cpu, multiplicand, multiplier, &product);
		modrm_write_reg16(cpu, product & 0xFFFF);
	}
}

static void div_accum_rm(I80386* cpu) {
	/* div eAX,Ev - (F6/F7, R/M reg = b110) b1111011W */
	I80386_OPERAND rm = { 0 };
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (W) {
		uint16_t tmp = 0;
		uint32_t dividend = 0;
		if (!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		dividend = cpu->dx << 16 | cpu->ax;
		i80386_alu_div16(cpu, dividend, tmp, &cpu->ax, &cpu->dx);
	}
	else {
		uint8_t tmp = 0;
		uint16_t dividend = 0;
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return;
		}
		dividend = cpu->ax;
		i80386_alu_div8(cpu, dividend, tmp, &cpu->al, &cpu->ah);
	}
}
static void idiv_accum_rm(I80386* cpu) {
	/* idiv eAX,Ev (F6/F7, R/M reg = b111) b1111011W */
	I80386_OPERAND rm = { 0 };
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (W) {
		uint16_t tmp = 0;
		uint32_t dividend = 0;
		if (!modrm_read_rm16(cpu, &rm, &tmp)) {
			return;
		}
		dividend = cpu->dx << 16 | cpu->ax;
		i80386_alu_idiv16(cpu, dividend, tmp, &cpu->ax, &cpu->dx);
	}
	else {
		uint8_t tmp = 0;
		uint16_t dividend = 0;
		if (!modrm_read_rm8(cpu, &rm, &tmp)) {
			return;
		}
		dividend = cpu->ax;
		i80386_alu_idiv8(cpu, dividend, tmp, &cpu->al, &cpu->ah);
	}
}

static void movs(I80386* cpu) {
	/* movs [ES:eDI], [DS:eSI] (A4/A5) b1010010W */
	uint32_t dest = 0;
	uint32_t src = 0;
	uint32_t counter = 0;
	uint32_t adjustment = 0;

	if (cpu->address_size) {
		dest = cpu->edi;
		src = cpu->esi;
		counter = cpu->ecx;
	}
	else {
		dest = cpu->di;
		src = cpu->si;
		counter = cpu->cx;
	}

	/* calculate adjustment len */
	adjustment = W ? cpu->operand_size ? 4 : 2 : 1;
	if (cpu->psw.df) {
		adjustment = 0U - adjustment;
	}

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	/* Rep prefix check */
	if (F1) {
		if (counter == 0) {
			return;
		}
		counter -= 1;
	}

	/* Do string operation */
	if (W) {
		if (cpu->operand_size) {
			uint32_t val = 0;
			if (!read_dword_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &val)) {
				return;
			}
			if (!write_dword_sreg(cpu, SEG_ES, dest, val)) {
				return;
			}
		}
		else {
			uint16_t val = 0;
			if (!read_word_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &val)) {
				return;
			}
			if (!write_word_sreg(cpu, SEG_ES, dest, val)) {
				return;
			}
		}
	}
	else {
		uint8_t val = 0;
		if (!read_byte_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &val)) {
			return;
		}
		if (!write_byte_sreg(cpu, SEG_ES, dest, val)) {
			return;				
		}
	}

	dest += adjustment;
	src += adjustment;

	if (cpu->address_size) {
		cpu->edi = dest;
		cpu->esi = src;
		cpu->ecx = counter;
	}
	else {
		cpu->di = dest & 0xFFFF;
		cpu->si = src & 0xFFFF;
		cpu->cx = counter & 0xFFFF;
	}

	/* Rep prefix check */
	if (F1) {
		i80386_rollback_instruction(cpu); /* Allow interrupts */
	}
}
static void stos(I80386* cpu) {
	/* stos [ES:eDI], eAX - (AA/AB) b1010101W */
	uint32_t dest = 0;
	uint32_t counter = 0;
	uint32_t adjustment = 0;

	if (cpu->address_size) {
		dest = cpu->edi;
		counter = cpu->ecx;
	}
	else {
		dest = cpu->di;
		counter = cpu->cx;
	}

	/* calculate adjustment len */
	adjustment = W ? cpu->operand_size ? 4 : 2 : 1;
	if (cpu->psw.df) {
		adjustment = 0U - adjustment;
	}

	/* Rep prefix check */
	if (F1) {
		if (counter == 0) {
			return;
		}
		counter -= 1;
	}

	/* Do string operation */
	if (W) {
		if (cpu->operand_size) {
			if (!write_dword_sreg(cpu, SEG_ES, dest, cpu->eax)) {
				return;
			}
		}
		else {
			if (!write_word_sreg(cpu, SEG_ES, dest, cpu->ax)) {
				return;
			}
		}
	}
	else {
		if (!write_byte_sreg(cpu, SEG_ES, dest, cpu->al)) {
			return;
		}
	}
	
	/* Adjust si/di delta */
	dest += adjustment;

	if (cpu->address_size) {
		cpu->edi = dest;
		cpu->ecx = counter;
	}
	else {
		cpu->di = dest & 0xFFFF;
		cpu->cx = counter & 0xFFFF;
	}

	/* Rep prefix check */
	if (F1) {
		i80386_rollback_instruction(cpu); /* Allow interrupts */
	}
}
static void lods(I80386* cpu) {
	/* lods eAX, [DS:eSI] - (AC/AD) b1010110W */
	uint32_t src = 0;
	uint32_t counter = 0;
	uint32_t adjustment = 0;

	if (cpu->address_size) {
		src = cpu->esi;
		counter = cpu->ecx;
	}
	else {
		src = cpu->si;
		counter = cpu->cx;
	}

	/* calculate adjustment len */
	adjustment = W ? cpu->operand_size ? 4 : 2 : 1;
	if (cpu->psw.df) {
		adjustment = 0U - adjustment;
	}

	/* Rep prefix check */
	if (F1) {
		if (counter == 0) {
			return;
		}
		counter -= 1;
	}

	/* Do string operation */
	if (W) {
		if (cpu->operand_size) {
			if (!read_dword_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &cpu->eax)) {
				return;
			}
		}
		else {
			if (!read_word_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &cpu->ax)) {
				return;
			}
		}
	}
	else {
		if (!read_byte_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &cpu->al)) {
			return;
		}
	}

	/* Adjust si/di delta */
	src += adjustment;

	if (cpu->address_size) {
		cpu->esi = src;
		cpu->ecx = counter;
	}
	else {
		cpu->si = src & 0xFFFF;
		cpu->cx = counter & 0xFFFF;
	}

	/* Rep prefix check */
	if (F1) {
		i80386_rollback_instruction(cpu); /* allow interrupts */
	}
}
static void cmps(I80386* cpu) {
	/* cmps [ES:eDI], [DS:eSI] - (A6/A7) b1010011W */
	uint32_t src = 0;
	uint32_t dest = 0;
	uint32_t counter = 0;
	uint32_t adjustment = 0;

	if (cpu->address_size) {
		src = cpu->esi;
		dest = cpu->edi;
		counter = cpu->ecx;
	}
	else {
		src = cpu->si;
		dest = cpu->di;
		counter = cpu->cx;
	}

	/* calculate adjustment len */
	adjustment = W ? cpu->operand_size ? 4 : 2 : 1;
	if (cpu->psw.df) {
		adjustment = 0U - adjustment;
	}

	/* Rep prefix check */
	if (F1) {
		if (counter == 0) {
			return;
		}
		counter -= 1;
	}

	/* Do string operation */
	if (W) {
		if (cpu->operand_size) {
			uint32_t val1 = 0;
			uint32_t val2 = 0;
			if (!read_dword_sreg(cpu, SEG_ES, dest, &val2)) {
				return;
			}
			if (!read_dword_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &val1)) {
				return;
			}
			i80386_alu_cmp32(cpu, &val1, val2);
		}
		else {
			uint16_t val1 = 0;
			uint16_t val2 = 0;
			if (!read_word_sreg(cpu, SEG_ES, dest, &val2)) {
				return;
			}
			if (!read_word_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &val1)) {
				return;
			}
			i80386_alu_cmp16(cpu, &val1, val2);
		}
	}
	else {
		uint8_t val1 = 0;
		uint8_t val2 = 0;
		if (!read_byte_sreg(cpu, SEG_ES, dest, &val2)) {
			return;
		}
		if (!read_byte_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &val1)) {
			return;
		}
		i80386_alu_cmp8(cpu, &val1, val2);
	}

	/* Adjust si/di delta */
	src += adjustment;
	dest += adjustment;

	if (cpu->address_size) {
		cpu->esi = src;
		cpu->edi = dest;
		cpu->ecx = counter;
	}
	else {
		cpu->si = src & 0xFFFF;
		cpu->di = dest & 0xFFFF;
		cpu->cx = counter & 0xFFFF;
	}

	/* Rep prefix check */
	if (F1 && cpu->psw.zf == F1Z) {
		i80386_rollback_instruction(cpu); /* allow interrupts */
	}
}
static void scas(I80386* cpu) {
	/* scas [ES:eDI] (AE/AF) b1010111W */
	uint32_t dest = 0;
	uint32_t counter = 0;
	uint32_t adjustment = 0;

	if (cpu->address_size) {
		dest = cpu->edi;
		counter = cpu->ecx;
	}
	else {
		dest = cpu->di;
		counter = cpu->cx;
	}

	/* calculate adjustment len */
	adjustment = W ? cpu->operand_size ? 4 : 2 : 1;
	if (cpu->psw.df) {
		adjustment = 0U - adjustment;
	}

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	/* Rep prefix check */
	if (F1) {
		if (counter == 0) {
			return;
		}
		counter -= 1;
	}

	/* Do string operation */
	if (W) {
		if (cpu->operand_size) {
			uint32_t val = 0;
			if (!read_dword_sreg(cpu, SEG_ES, dest, &val)) {
				return;
			}
			i80386_alu_cmp32(cpu, &cpu->eax, val);			
		}
		else {
			uint16_t val = 0;
			if (!read_word_sreg(cpu, SEG_ES, dest, &val)) {
				return;
			}
			i80386_alu_cmp16(cpu, &cpu->ax, val);
		}
	}
	else {
		uint8_t val = 0;
		if (!read_byte_sreg(cpu, SEG_ES, dest, &val)) {
			return;
		}
		i80386_alu_cmp8(cpu, &cpu->al, val);
	}

	/* Rep prefix check */
	if (F1 && cpu->psw.zf == F1Z) {
		i80386_rollback_instruction(cpu); /* allow interrupts */
	}

	/* Adjust si/di delta */
	dest += adjustment;

	if (cpu->address_size) {
		cpu->edi = dest;
		cpu->ecx = counter;
	}
	else {
		cpu->di = dest & 0xFFFF;
		cpu->cx = counter & 0xFFFF;
	}
}
static void ins(I80386* cpu) {
	/* ins [ES:eDI], DX (6C/6D) b0110110W */
	uint32_t dest = 0;
	uint32_t counter = 0;
	uint32_t adjustment = 0;

	if (cpu->cpl > cpu->psw.iopl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (cpu->address_size) {
		dest = cpu->edi;
		counter = cpu->ecx;
	}
	else {
		dest = cpu->di;
		counter = cpu->cx;
	}

	/* calculate adjustment len */
	adjustment = W ? cpu->operand_size ? 4 : 2 : 1;
	if (cpu->psw.df) {
		adjustment = 0U - adjustment;
	}

	/* Rep prefix check */
	if (F1) {
		if (counter == 0) {
			return;
		}
		counter -= 1;
	}

	/* Do string operation */
	uint32_t val = cpu->funcs.read_io_byte(cpu->funcs.user_param, cpu->dx);
	if (W) {
		val |= cpu->funcs.read_io_byte(cpu->funcs.user_param, cpu->dx + 1) << 8;
		if (cpu->operand_size) {
			val |= cpu->funcs.read_io_byte(cpu->funcs.user_param, cpu->dx + 2) << 16;
			val |= cpu->funcs.read_io_byte(cpu->funcs.user_param, cpu->dx + 3) << 24;
			if (!write_dword_sreg(cpu, SEG_ES, dest, val)) {
				return;
			}
		}
		else {
			if (!write_word_sreg(cpu, SEG_ES, dest, val & 0xFFFF)) {
				return;
			}
		}
	}
	else {
		if (!write_byte_sreg(cpu, SEG_ES, dest, val & 0xFF)) {
			return;
		}
	}

	/* Adjust si/di delta */
	dest += adjustment;

	if (cpu->address_size) {
		cpu->edi = dest;
		cpu->ecx = counter;
	}
	else {
		cpu->di = dest & 0xFFFF;
		cpu->cx = counter & 0xFFFF;
	}
	
	/* Rep prefix check */
	if (F1) {
		i80386_rollback_instruction(cpu); /* allow interrupts */
	}
}
static void outs(I80386* cpu) {
	/* outs DX, [DS:eSI] - (6E/6F) b0110111W */
	uint32_t src = 0;
	uint32_t counter = 0;
	uint32_t adjustment = 0;

	if (cpu->cpl > cpu->psw.iopl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (cpu->address_size) {
		src = cpu->esi;
		counter = cpu->ecx;
	}
	else {
		src = cpu->si;
		counter = cpu->cx;
	}

	/* calculate adjustment len */
	adjustment = W ? cpu->operand_size ? 4 : 2 : 1;
	if (cpu->psw.df) {
		adjustment = 0U - adjustment;
	}
	
	/* Rep prefix check */
	if (F1) {
		if (counter == 0) {
			return;
		}
		counter -= 1;
	}

	/* Do string operation */
	if (W) {
		if (cpu->operand_size) {
			uint32_t val = 0;
			if (!read_dword_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &val)) {
				return;
			}
			cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx, val & 0xFF);
			cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx + 1, (val >> 8) & 0xFF);
			cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx + 2, (val >> 16) & 0xFF);
			cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx + 3, (val >> 24) & 0xFF);
		}
		else {
			uint16_t val = 0;
			if (!read_word_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &val)) {
				return;
			}
			cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx, val & 0xFF);
			cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx + 1, (val >> 8) & 0xFF);
		}
	}
	else {
		uint8_t val = 0;
		if (!read_byte_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), src, &val)) {
			return;
		}
		cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx, val);
	}

	/* Rep prefix check */
	if (F1) {
		i80386_rollback_instruction(cpu); /* allow interrupts */
	}

	/* Adjust si/di delta */
	src += adjustment;

	if (cpu->address_size) {
		cpu->esi = src;
		cpu->ecx = counter;
	}
	else {
		cpu->si = src & 0xFFFF;
		cpu->cx = counter & 0xFFFF;
	}
}

static void les(I80386* cpu) {
	/* les (Mp) (C4) b11000100 */
	exec_bin_load_segment(cpu, SEG_ES);
}
static void lds(I80386* cpu) {
	/* lds (Mp) (C5) b11000101 */
	exec_bin_load_segment(cpu, SEG_DS);
}
static void lss(I80386* cpu) {
	/* lss (Mp) (0F B2) b10110010 */
	exec_bin_load_segment(cpu, SEG_SS);
}
static void lfs(I80386* cpu) {
	/* lfs (Mp) (0F B4) b10110100 */
	exec_bin_load_segment(cpu, SEG_FS);
}
static void lgs(I80386* cpu) {
	/* lgs (Mp) (0F B5) b10110101 */
	exec_bin_load_segment(cpu, SEG_GS);
}

static void xlat(I80386* cpu) {
	/* Get data pointed by (E)BX + AL (D7) b11010111 */
	uint32_t offset = 0;
	uint8_t entry = 0;

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->address_size) {
		offset = cpu->ebx + cpu->al;
	}
	else {
		offset = cpu->bx + cpu->al;
	}
	if (!read_byte_sreg(cpu, GET_SEG_OVERRIDE(SEG_DS), offset, &entry)) {
		return;
	}
	
	cpu->al = entry;
}

static void esc(I80386* cpu) {
	/* esc (D8-DF R/M reg = XXX) b11010REG */
	uint8_t esc_opcode = 0;
	uint16_t reg = 0;
	I80386_OPERAND rm = { 0 };
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (rm.type == OPERAND_TYPE_MEMORY && rm.mem.ea.logical_address.offset == 0xFFFF) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}
	esc_opcode = ((cpu->opcode & 7) << 3) | cpu->modrm.reg;
	reg = reg16_read(cpu, cpu->opcode);
}

static void in_accum_imm(I80386* cpu) {
	/* in AL/AX/EAX, imm - (E4/E5) b0000000W  */
	uint8_t imm = 0;

	if (cpu->cpl > cpu->psw.iopl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	cpu->al = cpu->funcs.read_io_byte(cpu->funcs.user_param, imm);
	if (W) {
		cpu->ah = cpu->funcs.read_io_byte(cpu->funcs.user_param, imm + 1);
		if (cpu->operand_size) {
			cpu->eax = cpu->eax & 0x0000FFFF;
			cpu->eax |= cpu->funcs.read_io_byte(cpu->funcs.user_param, imm + 2) << 16;
			cpu->eax |= cpu->funcs.read_io_byte(cpu->funcs.user_param, imm + 3) << 24;
		}
	}	
}
static void out_accum_imm(I80386* cpu) {
	/* out imm, AL/AX/EAX - (E6/E7) b0000000W */
	uint8_t imm = 0;

	if (cpu->cpl > cpu->psw.iopl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	cpu->funcs.write_io_byte(cpu->funcs.user_param, imm, cpu->al);
	if (W) {
		cpu->funcs.write_io_byte(cpu->funcs.user_param, imm + 1, cpu->ah);
		if (cpu->operand_size) {
			cpu->funcs.write_io_byte(cpu->funcs.user_param, imm + 2, (cpu->eax >> 16) & 0xFF);
			cpu->funcs.write_io_byte(cpu->funcs.user_param, imm + 3, (cpu->eax >> 24) & 0xFF);
		}
	}
}
static void in_accum_dx(I80386* cpu) {
	/* in AL/AX/EAX, DX - (EC/ED) b0000000W */
	if (cpu->cpl > cpu->psw.iopl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	cpu->al = cpu->funcs.read_io_byte(cpu->funcs.user_param, cpu->dx);
	if (W) {
		cpu->ah = cpu->funcs.read_io_byte(cpu->funcs.user_param, cpu->dx + 1);
		if (cpu->operand_size) {
			cpu->eax = cpu->eax & 0x0000FFFF;
			cpu->eax |= cpu->funcs.read_io_byte(cpu->funcs.user_param, cpu->dx + 2) << 16;
			cpu->eax |= cpu->funcs.read_io_byte(cpu->funcs.user_param, cpu->dx + 3) << 24;
		}
	}
}
static void out_accum_dx(I80386* cpu) {
	/* out DX, AL/AX/EAX - (EE/EF) b0000000W  */
	if (cpu->cpl > cpu->psw.iopl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx, cpu->al);
	if (W) {
		cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx + 1, cpu->ah);
		if (cpu->operand_size) {
			cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx + 2, (cpu->eax >> 16) & 0xFF);
			cpu->funcs.write_io_byte(cpu->funcs.user_param, cpu->dx + 3, (cpu->eax >> 24) & 0xFF);
		}
	}
}

static void int_(I80386* cpu) {
	/* Ib - interrupt CD b11001101 */
	uint8_t type = 0;
	if (!fetch_byte(cpu, &type)) {
		return;
	}
	i80386_int(cpu, type, I80386_INTERRUPT_TYPE_SOFTWARE);
}
static void int3(I80386* cpu) {
	/* interrupt CC b11001100 */
	i80386_int(cpu, INT_3, I80386_INTERRUPT_TYPE_SOFTWARE);
}
static void into(I80386* cpu) {
	/* interrupt on overflow (CE) b11001110 */
	if (cpu->psw.of) {
		i80386_int(cpu, INT_OVERFLOW, I80386_INTERRUPT_TYPE_SOFTWARE);
	}
}
static void iret(I80386* cpu) {
	/* return from interrupt (CF) b11001111 */
	uint16_t ip = 0;
	uint16_t cs = 0;
	uint16_t psw = 0;
	
	if (cpu->psw.nt) {
		/* Return from nested task */
		uint16_t back_link = 0;
		if (!read_word_logical(cpu, cpu->tr.desc.base, 0, &back_link)) {
			return;
		}
		i80386_task_switch(cpu, back_link, TASK_SWITCH_IRET);
		return;
	}

	if (!pop_word(cpu, &ip)) {
		return;
	}
	if (!pop_word(cpu, &cs)) {
		return;
	}
	if (!pop_word(cpu, &psw)) {
		return;
	}
	if (!i80386_load_segment_register(cpu, &cpu->cs, SEG_CS, cs)) {
		return;
	}
	cpu->eip = ip;
	cpu->psw.word = (psw | 0x0002) & 0x0FD7;
}

static void bound(I80386* cpu) {
	/* Gv,Ma - bound (62) b01100010 */
	uint16_t reg = 0;
	I80386_OPERAND rm = { 0 };
	uint16_t low = 0;
	uint16_t high = 0;

	if (!fetch_modrm(cpu)) {
		return;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (rm.type == OPERAND_TYPE_GENERAL_REGISTER) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}
	else if (rm.mem.ea.logical_address.offset == 0xFFFF || rm.mem.ea.logical_address.offset == 0xFFFD) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (!modrm_read_rm16(cpu, &rm, &low)) {
		return;
	}
	rm.mem.ea.logical_address.offset += 2;
	if (!modrm_read_rm16(cpu, &rm, &high)) {
		return;
	}
	reg = modrm_read_reg16(cpu);

	if ((int16_t)reg < (int16_t)low || (int16_t)reg > (int16_t)high) {
		i80386_exception(cpu, INT_BOUNDS);
		return;
	}
}

static void sldt(I80386* cpu) {
	/* Ew - Store LDT (0F 00 /0) */

	/* real mode or virtual 8086 raises #UD */
	if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}
}
static void str(I80386* cpu) {
	/* Ew - Store task register (0F 00 /1) */

	/* real mode or virtual 8086 raises #UD */
	if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}
}
static void lldt(I80386* cpu) {
	/* Ew - Load LDT (0F 00 /2) */

	/* real mode or virtual 8086 raises #UD */
	if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}
}
static void ltr(I80386* cpu) {
	/* Ew - Load task register (0F 00 /3) */
	I80386_OPERAND rm = { 0 };
	uint16_t selector = 0;
	I80386_DESCRIPTOR_TABLE_ENTRY entry = { 0 };

	/* real mode or virtual 8086 raises #UD */
	if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	/* Must be ring 0 */
	if (cpu->cpl != 0) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	if (!modrm_read_rm16(cpu, &rm, &selector)) {
		return;
	}

	/* TR cannot have a NULL selector */
	if ((selector & 0xFFF8) == 0) {
		i80386_exception_code(cpu, EXCEPTION_GP, 0);
		return;
	}

	/* TI must be 0 (GDT) */
	if (selector & 0x4) {
		i80386_exception_code(cpu, EXCEPTION_GP, selector);
		return;
	}

	if (!i80386_read_descriptor_table_entry(cpu, selector, &entry)) {
		return;
	}

	if (!entry.ar.present) {
		i80386_exception_code(cpu, EXCEPTION_NP, selector);
		return;
	}

	switch (entry.ar.type) {
		case I80386_GATE_TYPE_AVAL_286:
		case I80386_GATE_TYPE_AVAL_386:
			break;

		default:
			i80386_exception_code(cpu, EXCEPTION_GP, selector);
			return;
	}

	/* Mark descriptor busy */
	if (entry.ar.type == I80386_GATE_TYPE_AVAL_286) {
		entry.ar.type = I80386_GATE_TYPE_BUSY_286;
}
	else {
		entry.ar.type = I80386_GATE_TYPE_BUSY_386;
	}

	if (!i80386_write_descriptor_table_entry(cpu, selector, &entry)) {
		return;
	}

	i80386_load_segment_register(cpu, &cpu->tr, SEG_TR, selector);
}
static void verr(I80386* cpu) {
	/* Ew - (0F 00 /4) */

	/* real mode or virtual 8086 raises #UD */
	if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}
}
static void verw(I80386* cpu) {
	/* Ew - (0F 00 /5) */

	/* real mode or virtual 8086 raises #UD */
	if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}
}

static void sgdt(I80386* cpu) {
	/* Store GDT Ms - (0F 01 /0) */
	exec_bin_store_descriptor_table_register(cpu, &cpu->gdtr);
}
static void sidt(I80386* cpu) {
	/* Store IDT Ms - (0F 01 /1) */
	exec_bin_store_descriptor_table_register(cpu, &cpu->idtr);
}
static void lgdt(I80386* cpu) {
	/* Load GDT Ms - (0F 01 /2) */
	exec_bin_load_descriptor_table_register(cpu, &cpu->gdtr);
}
static void lidt(I80386* cpu) {
	/* Load IDT Ms - (0F 01 /3) */
	exec_bin_load_descriptor_table_register(cpu, &cpu->idtr);
}
static void smsw(I80386* cpu) {
	/* Store MSW Ew - (0F 01 /4) */
	I80386_OPERAND rm = { 0 };
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	modrm_write_rm16(cpu, &rm, cpu->msw.word);
}
static void lmsw(I80386* cpu) {
	/* Load MSW Ew - (0F 01 /6) */
	I80386_OPERAND rm = { 0 };
	uint16_t msw = 0;
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (!modrm_read_rm16(cpu, &rm, &msw)) {
		return;
	}

	/* PE can be reset only by loading CR0, it cannot be reset by the LMSW instruction. */
	cpu->msw.word = (cpu->msw.word & 0x1) | msw;
}
static void lar(I80386* cpu) {
	/* Load access rights Gw,Ew - (0F 02 /r) */
	I80386_OPERAND rm = { 0 };
	uint16_t selector = 0;
	I80386_DESCRIPTOR_TABLE_ENTRY descriptor = { 0 };

	cpu->psw.zf = 0; /* ZF=0 indicates failure */

	if (!fetch_modrm(cpu)) {
		return;
	}

	/* real mode or virtual 8086 raises #UD */
	if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (!modrm_read_rm16(cpu, &rm, &selector)) {
		return;
	}
	if ((selector & 0xFFF8) == 0) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}
	if (!i80386_read_descriptor_table_entry(cpu, selector, &descriptor)) {
		return;
	}
	if (descriptor.ar.dpl < cpu->cpl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	modrm_write_reg16(cpu, descriptor.ar.word);
	cpu->psw.zf = 1; /* ZF=1 indicates success */
}
static void lsl(I80386* cpu) {
	/* Load segment limit Gv,Ew - (0F 03 /r) */
	I80386_OPERAND rm = { 0 };
	uint16_t selector = 0;
	I80386_DESCRIPTOR_TABLE_ENTRY descriptor = { 0 };

	cpu->psw.zf = 0; /* ZF=0 indicates failure */

	/* real mode or virtual 8086 raises #UD */
	if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!fetch_modrm(cpu)) {
		return;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	if (!modrm_read_rm16(cpu, &rm, &selector)){
		return;
	}
	if ((selector & 0xFFF8) == 0) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}
	if (!i80386_read_descriptor_table_entry(cpu, selector, &descriptor)) {
		return;
	}
	if (descriptor.ar.dpl < cpu->cpl) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	if (cpu->operand_size) {
		modrm_write_reg32(cpu, descriptor.limit_lo | ((uint32_t)descriptor.ar.limit_hi << 16));
	}
	else {
	modrm_write_reg16(cpu, descriptor.limit_lo);
	}
	cpu->psw.zf = 1; /* ZF=1 indicates success */
}
static void clts(I80386* cpu) {
	/* clear TS (0F 06) */

	if (cpu->cpl != 0) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	cpu->msw.ts = 0;
}
static void arpl(I80386* cpu) {
	/* ARPL Ew,Gw - (63 /r) */

	I80386_OPERAND rm = { 0 };
	uint16_t reg = 0;
	uint16_t dst = 0;

	/* real mode or virtual 8086 raises #UD */
	if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!fetch_modrm(cpu)) {
		return;
	}

	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}
	reg = modrm_read_reg16(cpu);

	if (!modrm_read_rm16(cpu, &rm, &dst)) {
		return;
	}

	uint16_t dst_rpl = dst & 3;
	uint16_t src_rpl = reg & 3;

	if (dst_rpl < src_rpl) {
		dst = (dst & ~3) | src_rpl;
		if (!modrm_write_rm16(cpu, &rm, dst)) {
			cpu->psw.zf = 0;
			return;
		}
		cpu->psw.zf = 1;
	}
	else {
		cpu->psw.zf = 0;
	}
}

static void loadall_dtr(I80386* cpu, uint32_t base, uint32_t offset, I80386_DESCRIPTOR_TABLE_REGISTER* dtr) {
	read_dword_logical(cpu, base, offset + 4, &dtr->base);
	read_dword_logical(cpu, base, offset + 8, &dtr->limit);
}
static void loadall_sreg(I80386* cpu, uint32_t base, uint32_t offset, I80386_SEGMENT_REGISTER* sreg) {
	read_word_logical(cpu, base, offset + 0, &sreg->selector);
}
static void loadall_desc(I80386* cpu, uint32_t base, uint32_t offset, I80386_DESCRIPTOR_CACHE* descriptor) {
	read_word_logical(cpu, base, offset + 0, &descriptor->ar.word);
	read_dword_logical(cpu, base, offset + 4, &descriptor->base);
	read_dword_logical(cpu, base, offset + 8, &descriptor->limit);
}
static void loadall(I80386* cpu) {
	/* 0F 07 - load all */

	/* Must be ring 0 */
	if (cpu->cpl != 0) {
		i80386_exception(cpu, EXCEPTION_GP);
		return;
	}

	uint32_t base = cpu->es.desc.base;
	uint32_t offset = cpu->edi;
	I80386_LOADALL buffer = { 0 };

	for (uint32_t i = 0; i < sizeof(I80386_LOADALL); i += sizeof(uint32_t)) {
		read_dword_logical(cpu, base, offset + i, &((uint32_t*)&buffer)[i]);
		}

	cpu->cr0.dword = buffer.cr0;
	cpu->eflags.dword = buffer.eflags;

	cpu->eip = buffer.eip;
	cpu->edi = buffer.edi;
	cpu->esi = buffer.esi;
	cpu->ebp = buffer.ebp;
	cpu->esp = buffer.esp;
	cpu->ebx = buffer.ebx;
	cpu->edx = buffer.edx;
	cpu->ecx = buffer.ecx;
	cpu->eax = buffer.eax;

	cpu->dr6.dword = buffer.dr6;
	cpu->dr7.dword = buffer.dr7;

	cpu->tr.selector = buffer.tr & 0xFFFF;

	//cpu->tss

	cpu->idtr.base = buffer.idt.base;
	cpu->idtr.limit = buffer.idt.limit;

	cpu->gdtr.base = buffer.gdt.base;
	cpu->gdtr.limit = buffer.gdt.limit;

	cpu->ldtr.selector = buffer.ldt_selector & 0xFFFF;
	cpu->ldtr.desc.base = buffer.ldt.base;
	cpu->ldtr.desc.limit = buffer.ldt.limit;
	cpu->ldtr.desc.ar.word = buffer.ldt.ar & 0xFFFF;

	cpu->gs.selector = buffer.gs_selector & 0xFFFF;
	cpu->gs.desc.base = buffer.gs.base;
	cpu->gs.desc.limit = buffer.gs.limit;
	cpu->gs.desc.ar.word = buffer.gs.ar & 0xFFFF;

	cpu->fs.selector = buffer.fs_selector & 0xFFFF;
	cpu->fs.desc.base = buffer.fs.base;
	cpu->fs.desc.limit = buffer.fs.limit;
	cpu->fs.desc.ar.word = buffer.fs.ar & 0xFFFF;

	cpu->ds.selector = buffer.ds_selector & 0xFFFF;
	cpu->ds.desc.base = buffer.ds.base;
	cpu->ds.desc.limit = buffer.ds.limit;
	cpu->ds.desc.ar.word = buffer.ds.ar & 0xFFFF;

	cpu->ss.selector = buffer.ss_selector & 0xFFFF;
	cpu->ss.desc.base = buffer.ss.base;
	cpu->ss.desc.limit = buffer.ss.limit;
	cpu->ss.desc.ar.word = buffer.ss.ar & 0xFFFF;

	cpu->cs.selector = buffer.cs_selector & 0xFFFF;
	cpu->cs.desc.base = buffer.cs.base;
	cpu->cs.desc.limit = buffer.cs.limit;
	cpu->cs.desc.ar.word = buffer.cs.ar & 0xFFFF;

	cpu->es.selector = buffer.es_selector & 0xFFFF;
	cpu->es.desc.base = buffer.es.base;
	cpu->es.desc.limit = buffer.es.limit;
	cpu->es.desc.ar.word = buffer.es.ar & 0xFFFF;
}

static void setcc(I80386* cpu) {
	/* setcc Eb - (0F 90-9F) b1001CCCC */
	I80386_OPERAND rm = { 0 };
	int b = 0;

	/* lock raises #UD */
	if (cpu->lock_prefix) {
		i80386_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (!fetch_modrm(cpu)) {
		return;
	}
	if (!modrm_get_rm(cpu, &rm)) {
		return;
	}

	b = i80386_check_condition(cpu);
	modrm_write_rm8(cpu, &rm, b & 0x1);
}
static void bt(I80386* cpu) {
	/* bit test - bt Ev,Gv - (0F A3) b10100011 */
	exec_bin_bit_test(cpu, i80386_alu_bt16, i80386_alu_bt32, RO);
}
static void bts(I80386* cpu) {
	/* bit test set - bts Ev,Gv - (0F AB) b10101011 */
	exec_bin_bit_test(cpu, i80386_alu_bts16, i80386_alu_bts32, WB);
}
static void btr(I80386* cpu) {
	/* bit test reset - btr Ev,Gv - (0F B3) b10110011 */
	exec_bin_bit_test(cpu, i80386_alu_btr16, i80386_alu_btr32, WB);
}
static void btc(I80386* cpu) {
	/* bit test complement - btc Ev,Gv - (0F BB) b10111011 */
	exec_bin_bit_test(cpu, i80386_alu_btc16, i80386_alu_btc32, WB);
}
static void bsf(I80386* cpu) {
	/* bit search forward - bsf Ev,Gv - (0F BC) b10111100 */
	exec_bin_bit_search(cpu, 0);
}
static void bsr(I80386* cpu) {
	/* bit search reverse - bsr Ev,Gv - (0F BD) b10111101 */
	exec_bin_bit_search(cpu, 1);
	}

/* prefix byte */
static int rep(I80386* cpu) {
	/* rep/repz/repnz (F2/F3) b1111001Z */
	cpu->internal_flags |= INTERNAL_FLAG_F1;    /* Set F1 */
	cpu->internal_flags &= ~INTERNAL_FLAG_F1Z;  /* Clr F1Z */
	cpu->internal_flags |= (cpu->opcode & Z);   /* Set F1Z */
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80386_DECODE_OK;
	}
	return I80386_DECODE_REQ_CYCLE;
}
static int segment_override(I80386* cpu) {
	/* (26/2E/36/3E) b001SR110 */
	cpu->segment_override = SR;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80386_DECODE_OK;
	}
	return I80386_DECODE_REQ_CYCLE;
}
static int segment_override_extended(I80386* cpu) {
	/* (64/65) b01100SRE */
	cpu->segment_override = SRE;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80386_DECODE_OK;
	}
	return I80386_DECODE_REQ_CYCLE;
}
static int lock(I80386* cpu) {
	/* lock the bus (F0/F1) b11110000 */
	cpu->lock_prefix = 1;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80386_DECODE_OK;
	}
	return I80386_DECODE_REQ_CYCLE;
}
static int operand_size(I80386* cpu) {
	cpu->operand_size ^= 0x1;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80386_DECODE_OK;
	}
	return I80386_DECODE_REQ_CYCLE;
}
static int address_size(I80386* cpu) {
	cpu->address_size ^= 0x1;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80386_DECODE_OK;
	}
	return I80386_DECODE_REQ_CYCLE;
}

#if 0
/* operand get/set proto */
typedef enum {
	AM_NONE = 0,
	AM_A    = (1 << 8),  /* Direct address; operand is encoded in the instruction */
	AM_C    = (2 << 8),  /* The reg field of ModR/M byte selects a control register */
	AM_D    = (3 << 8),  /* The reg field of ModR/M byte selects a debug register */
	AM_E    = (4 << 8),  /* A ModR/M byte follows the opcode and specifies the operand */
	AM_F    = (5 << 8),  /* Eflags */
	AM_G    = (6 << 8),  /* The reg field of ModR/M byte selects a general register */
	AM_I    = (7 << 8),  /* Immediate data; operand is encoded in the instruction */
	AM_J    = (8 << 8),  /* The instruction contains a relative offset */
	AM_M    = (9 << 8),  /* The ModR/M byte may ONLY refer to memory */
	AM_O    = (10 << 8), /* The instruction has no ModR/M byte. The offset of the operand is encoded asa word/dword in the instruction */
	AM_R    = (11 << 8), /* The reg field of ModR/M byte may ONLY select a general register */
	AM_S    = (12 << 8), /* The reg field of ModR/M byte selects a segment register */
	AM_T    = (13 << 8), /* The reg field of ModR/M byte selects a test register */
	AM_X    = (14 << 8), /* Memory addressed by DS:SI */
	AM_Y    = (15 << 8), /* Memory addressed by ES:DI */
} ADDRESSING_METHOD;

typedef enum {
	OT_NONE = 0,
	OT_a    = 1, /* Two word operands or two dword operands in memory (depending on operand size) */
	OT_b    = 2, /* byte (regardless of operand size) */
	OT_c    = 3, /* byte or word (depending on operand size) */
	OT_d    = 4, /* dword (regardless of operand size) */
	OT_p    = 5, /* 48bit or 64bit pointer (depending on operand size) */
	OT_s    = 6, /* 6 byte pseudo descriptor */
	OT_v    = 7, /* word or dword (depending on operand size) */
	OT_w    = 8, /* word (regardless of operand size) */
} OPERAND_ACCESS_TYPE;

typedef enum {
	OC_Eb = AM_E | OT_b, /* ModR/M 8bit */
	OC_Ew = AM_E | OT_w, /* ModR/M 16bit */
	OC_Ed = AM_E | OT_d, /* ModR/M 32bit */
	OC_Ev = AM_E | OT_v, /* ModR/M 16/32bit (depending on operand size) */
	OC_Gb = AM_G | OT_b, /* GPR 8bit */
	OC_Gw = AM_G | OT_w, /* GPR 16bit */
	OC_Gd = AM_G | OT_d, /* GPR 32bit */
	OC_Gv = AM_G | OT_v, /* GPR 16/32bit (depending on operand size) */
	OC_Ib = AM_I | OT_b, /* Immediate 8bit */
	OC_Iw = AM_I | OT_w, /* Immediate 16bit */
	OC_Id = AM_I | OT_d, /* Immediate 32bit */
	OC_Iv = AM_I | OT_v, /* Immediate 16/32bit (depending on operand size) */
	OC_Mp = AM_M | OT_p,
	OC_Av = AM_A | OT_v,
	OC_Ap = AM_A | OT_p,
} OPERAND_CODE;
static int i80386_decode_operand_type(I80386* cpu, I80386_OPERAND* op, OPERAND_ACCESS_TYPE ot);
static int i80386_decode_addressing_method(I80386* cpu, I80386_OPERAND* op, ADDRESSING_METHOD am);
static int i80386_decode_operand_type(I80386* cpu, I80386_OPERAND* op, OPERAND_ACCESS_TYPE ot) {
	switch (ot) {
		case OT_b:
			op->size = 1;
			break;
		case OT_w:
			op->size = 2;
			break;
		case OT_d:
			op->size = 4;
			break;
		case OT_s:
			op->size = 6;
			break;

		case OT_c:
			op->size = cpu->operand_size ? 2 : 1;
			break;
		case OT_v:
			op->size = cpu->operand_size ? 4 : 2;
			break;
		case OT_a:
			op->size = cpu->operand_size ? 8 : 4;
			break;
		case OT_p:
			op->size = cpu->operand_size ? 8 : 6;
			break;
		default:
			op->size = 0;
			break;
	}
	return 1;
}
static int i80386_decode_addressing_method(I80386* cpu, I80386_OPERAND* op, ADDRESSING_METHOD am) {
	switch (am) {
		case AM_A:
			op->type = OPERAND_TYPE_FAR_POINTER;
			break;
		case AM_C:
			op->type = OPERAND_TYPE_CONTROL_REGISTER;
			op->reg.index = cpu->modrm.reg;
			break;
		case AM_D:
			op->type = OPERAND_TYPE_DEBUG_REGISTER;
			op->reg.index = cpu->modrm.reg;
			break;
		case AM_E:
			if (cpu->modrm.mod == 0b11) {
				op->type = OPERAND_TYPE_GENERAL_REGISTER;
				op->reg.index = cpu->modrm.rm;
				return 1;
			}
			else {
				op->type = OPERAND_TYPE_MEMORY;
				return modrm_get_effective_address(cpu, &op->mem.ea);
			}
			break;
		case AM_F:
			op->type = OPERAND_TYPE_FLAGS;
			break;
		case AM_G:
			op->type = OPERAND_TYPE_GENERAL_REGISTER;
			op->reg.index = cpu->modrm.reg;
			break;
		case AM_I:
			op->type = OPERAND_TYPE_IMMEDIATE;
			break;
		case AM_J:
			op->type = OPERAND_TYPE_REL;
			break;
		case AM_M:
			/* The mod R/M byte may refer only to memory */
			if (cpu->modrm.mod == 0b11) {
				i80386_exception(cpu, EXCEPTION_UD);
				return 0;
			}
			op->type = OPERAND_TYPE_MEMORY;
			return modrm_get_effective_address(cpu, &op->mem.ea);
		case AM_O:
			op->type = OPERAND_TYPE_IMMEDIATE;
			break;
		case AM_R:
			/* The mod R/M byte may refer only to a register */
			if (cpu->modrm.mod != 0b11) {
				i80386_exception(cpu, EXCEPTION_UD);
				return 0;
			}
			op->type = OPERAND_TYPE_GENERAL_REGISTER;
			op->reg.index = cpu->modrm.rm;
			break;
		case AM_S:
			op->type = OPERAND_TYPE_SEGMENT_REGISTER;
			op->reg.index = cpu->modrm.rm;
			break;
		case AM_T:
			op->type = OPERAND_TYPE_TEST_REGISTER;
			op->reg.index = cpu->modrm.rm;
			break;
		case AM_X:
			op->type = OPERAND_TYPE_MEMORY;
			op->mem.ea.segment_index = GET_SEG_OVERRIDE(SEG_DS);
			op->mem.ea.logical_address.base = cpu->segment_registers[op->mem.ea.segment_index].desc.base;
			op->mem.ea.logical_address.offset = cpu->si;
			break;
		case AM_Y:
			op->type = OPERAND_TYPE_MEMORY;
			op->mem.ea.segment_index = SEG_ES;
			op->mem.ea.logical_address.base = cpu->es.desc.base;
			op->mem.ea.logical_address.offset = cpu->di;
			break;
	}
	return 1;
}

static int i80386_get_operand(I80386* cpu, I80386_OPERAND* op, OPERAND_CODE oc) {
	if (!i80386_decode_operand_type(cpu, op, oc & 0xFF)) {
		return 0;
	}
	if (!i80386_decode_addressing_method(cpu, op, oc & 0xFF00)) {
		return 0;
	}
	return 1;
}
static int i80386_read_operand(I80386* cpu, const I80386_OPERAND* op, void* value) {
	switch (op->type) {
		case OPERAND_TYPE_MEMORY:
			switch (op->size) {
				case 1:
					return read_byte_ea(cpu, &op->mem.ea, value);
				case 2:
					return read_word_ea(cpu, &op->mem.ea, value);
				case 4:
					return read_dword_ea(cpu, &op->mem.ea, value);
				case 8:
					return read_qword_ea(cpu, &op->mem.ea, value);
				default:
					return 0;
			}
			break;
		case OPERAND_TYPE_IMMEDIATE:
			switch (op->size) {
				case 1:
					return fetch_byte(cpu, value);
				case 2:
					return fetch_word(cpu, value);
				case 4:
					return fetch_dword(cpu, value);
				case 8:
					return fetch_qword(cpu, value);
				default:
					return 0;
			}
		case OPERAND_TYPE_GENERAL_REGISTER:
			switch (op->size) {
				case 1:
					*(uint8_t*)value = reg8_read(cpu, op->reg.index);
					return 1;
				case 2:
					*(uint16_t*)value = reg16_read(cpu, op->reg.index);
					return 1;
				case 4:
					*(uint32_t*)value = reg32_read(cpu, op->reg.index);
					return 1;
				case 8:
					*(uint64_t*)value = reg32_read(cpu, op->reg.index);
					return 1;
				default:
					return 0;
			}
		case OPERAND_TYPE_SEGMENT_REGISTER:
			*(uint16_t*)value = cpu->segment_registers[op->reg.index].selector;
			return 1;
		case OPERAND_TYPE_CONTROL_REGISTER:
			*(uint32_t*)value = cpu->control_registers[op->reg.index];
			return 1;
		case OPERAND_TYPE_DEBUG_REGISTER:
			*(uint32_t*)value = cpu->debug_registers[op->reg.index];
			return 1;
		case OPERAND_TYPE_TEST_REGISTER:
			*(uint32_t*)value = cpu->test_registers[op->reg.index];
			return 1;
		default:
			return 0;
	}
}
static int i80386_write_operand(I80386* cpu, const I80386_OPERAND* op, void* value) {
	switch (op->type) {
		case OPERAND_TYPE_MEMORY:
			switch (op->size) {
				case 1:
					return write_byte_ea(cpu, &op->mem.ea, *(uint8_t*)value);
				case 2:
					return write_word_ea(cpu, &op->mem.ea, *(uint16_t*)value);
				case 4:
					return write_dword_ea(cpu, &op->mem.ea, *(uint32_t*)value);
				case 8:
					return write_qword_ea(cpu, &op->mem.ea, *(uint64_t*)value);
				default:
					return 0;
			}
			break;
		case OPERAND_TYPE_IMMEDIATE:
			return 0;
		case OPERAND_TYPE_GENERAL_REGISTER:
			switch (op->size) {
				case 1:
					reg8_write(cpu, op->reg.index, *(uint8_t*)value);
					return 1;
				case 2:
					reg16_write(cpu, op->reg.index, *(uint16_t*)value);
					return 1;
				case 4:
					reg32_write(cpu, op->reg.index, *(uint32_t*)value);
					return 1;
				case 8:
					reg32_write(cpu, op->reg.index, *(uint64_t*)value);
					return 1;
				default:
					return 0;
			}
			break;
		case OPERAND_TYPE_SEGMENT_REGISTER:
			cpu->segment_registers[op->reg.index].selector = *(uint16_t*)value;
			return 1;
		case OPERAND_TYPE_CONTROL_REGISTER:
			cpu->control_registers[op->reg.index] = *(uint32_t*)value;
			return 1;
		case OPERAND_TYPE_DEBUG_REGISTER:
			cpu->debug_registers[op->reg.index] = *(uint32_t*)value;
			return 1;
		case OPERAND_TYPE_TEST_REGISTER:
			cpu->test_registers[op->reg.index] = *(uint32_t*)value;
			return 1;
		default:
			return 0;
	}
}
#endif

/* Fetch next opcode */
static int i80386_fetch(I80386* cpu) {
	cpu->internal_flags = 0;
	cpu->modrm.byte = 0;
	cpu->sib.byte = 0;
	cpu->segment_override = 0xFF;
	cpu->lock_prefix = 0;
	cpu->instruction_len = 0;
	cpu->operand_size = cpu->cs.desc.ar.default_size;
	cpu->address_size = cpu->cs.desc.ar.default_size;
	cpu->opcode = 0;

	cpu->effective_address.stack_address = 0;
	cpu->effective_address.valid = 0;
	cpu->effective_address.segment_index = 0;
	cpu->effective_address.base = 0;
	cpu->effective_address.scale = 0;
	cpu->effective_address.index = 0;
	cpu->effective_address.logical_address.offset = 0;
	cpu->effective_address.logical_address.base = 0;
	
	cpu->exception.number = 0;
	cpu->exception.df_number = 0;
	cpu->exception.tf_number = 0;
	cpu->exception.code = 0;
	cpu->exception.state = I80386_EXCEPTION_STATE_NONE;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return 0;
	}
	return 1;
}

/* decode opcode */
static int i80386_decode_opcode_80(I80386* cpu) {
	/* Immediate Group 1 - (80/81/82/83) b100000SW */
	if (!fetch_modrm(cpu)) {
		return I80386_DECODE_OK;
	}
	switch (cpu->modrm.reg) {
		case 0b000:
			add_rm_imm(cpu);
			break;
		case 0b001:
			or_rm_imm(cpu);
			break;
		case 0b010:
			adc_rm_imm(cpu);
			break;
		case 0b011:
			sbb_rm_imm(cpu);
			break;
		case 0b100:
			and_rm_imm(cpu);
			break;
		case 0b101:
			sub_rm_imm(cpu);
			break;
		case 0b110:
			xor_rm_imm(cpu);
			break;
		case 0b111:
			cmp_rm_imm(cpu);
			break;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_opcode_c0(I80386* cpu) {
	/* Shift group 2 E,I - (C0/C1 /r) b1100000W */
	if (!fetch_modrm(cpu)) {
		return I80386_DECODE_OK;
	}
	switch (cpu->modrm.reg) {
		case 0b000:
			rol_rm_imm(cpu);
			break;
		case 0b001:
			ror_rm_imm(cpu);
			break;
		case 0b010:
			rcl_rm_imm(cpu);
			break;
		case 0b011:
			rcr_rm_imm(cpu);
			break;
		case 0b100:
			shl_rm_imm(cpu);
			break;
		case 0b101:
			shr_rm_imm(cpu);
			break;
		case 0b110:
			sal_rm_imm(cpu);
			break;
		case 0b111:
			sar_rm_imm(cpu);
			break;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_opcode_d0(I80386* cpu) {
	/* Shift group 2 E,1/CL - (D0/D1/D2/D3 /r) b110100VW */
	if (!fetch_modrm(cpu)) {
		return I80386_DECODE_OK;
	}
	switch (cpu->modrm.reg) {
		case 0b000:
			rol_rm_cl(cpu);
			break;
		case 0b001:
			ror_rm_cl(cpu);
			break;
		case 0b010:
			rcl_rm_cl(cpu);
			break;
		case 0b011:
			rcr_rm_cl(cpu);
			break;
		case 0b100:
			shl_rm_cl(cpu);
			break;
		case 0b101:
			shr_rm_cl(cpu);
			break;
		case 0b110:
			sal_rm_cl(cpu);
			break;
		case 0b111:
			sar_rm_cl(cpu);
			break;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_opcode_f6(I80386* cpu) {
	/* Unary group 3 - (F6/F7) b1111011W */
	if (!fetch_modrm(cpu)) {
		return I80386_DECODE_OK;
	}
	switch (cpu->modrm.reg) {
		case 0b000:
			test_rm_imm(cpu);
			break;
		case 0b001: /* todo: is this valid on 80386? */
			test_rm_imm(cpu);
			break;
		case 0b010:
			not(cpu);
			break;
		case 0b011:
			neg(cpu);
			break;
		case 0b100:
			mul_accum_rm(cpu);
			break;
		case 0b101:
			imul_accum_rm(cpu);
			break;
		case 0b110:
			div_accum_rm(cpu);
			break;
		case 0b111:
			idiv_accum_rm(cpu);
			break;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_opcode_fe(I80386* cpu) {
	/* inc/dec Group 4 - (FE) b11111110 */
	if (!fetch_modrm(cpu)) {
		return I80386_DECODE_OK;
	}
	switch (cpu->modrm.reg) {
		case 0b000:
			inc_rm(cpu);
			break;
		case 0b001:
			dec_rm(cpu);
			break;

		case 0b010:
		case 0b011:
		case 0b100:
		case 0b101:
		case 0b110:
		case 0b111:
			i80386_exception(cpu, EXCEPTION_UD);
			return I80386_DECODE_UNDEFINED;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_opcode_ff(I80386* cpu) {
	/* Indirect Group 5 - (FF) b11111111 */
	if (!fetch_modrm(cpu)) {
		return I80386_DECODE_OK;
	}
	switch (cpu->modrm.reg) {
		case 0b000:
			inc_rm(cpu);
			break;
		case 0b001:
			dec_rm(cpu);
			break;
		case 0b010:
			call_intra_indirect(cpu);
			break;
		case 0b011:
			call_inter_indirect(cpu);
			break;
		case 0b100:
			jmp_intra_indirect(cpu);
			break;
		case 0b101:
			jmp_inter_indirect(cpu);
			break;
		case 0b110:
			push_rm(cpu);
			break;
		case 0b111: /* todo: is this valid on 80386? */
			push_rm(cpu);
			break;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_opcode_0f00(I80386* cpu) {
	/* Group 6 - (0F 00) b00000000 */
	if (!fetch_modrm(cpu)) {
		return I80386_DECODE_OK;
	}
	switch (cpu->modrm.reg) {
		case 0b000:
			sldt(cpu);
			break;
		case 0b001:
			str(cpu);
			break;
		case 0b010:
			lldt(cpu);
			break;
		case 0b011:
			ltr(cpu);
			break;
		case 0b100:
			verr(cpu);
			break;
		case 0b101:
			verw(cpu);
			break;
		case 0b110:
		case 0b111:
			i80386_exception(cpu, EXCEPTION_UD);
			return I80386_DECODE_UNDEFINED;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_opcode_0f01(I80386* cpu) {
	/* Group 7 - (0F 01) b00000001 */
	if (!fetch_modrm(cpu)) {
		return I80386_DECODE_OK;
	}
	switch (cpu->modrm.reg) {
		case 0b000:
			sgdt(cpu);
			break;
		case 0b001:
			sidt(cpu);
			break;
		case 0b010:
			lgdt(cpu);
			break;
		case 0b011:
			lidt(cpu);
			break;
		case 0b100:
			smsw(cpu);
			break;
		case 0b101:
			i80386_exception(cpu, EXCEPTION_UD);
			return I80386_DECODE_UNDEFINED;
		case 0b110:
			lmsw(cpu);
			break;
		case 0b111:
			i80386_exception(cpu, EXCEPTION_UD);
			return I80386_DECODE_UNDEFINED;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_opcode_0f(I80386* cpu) {
	/* 2-byte opcode map - (0F XX) b00001111 */
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80386_DECODE_OK;
	}

	switch (cpu->opcode) {
		case 0x00: /* Group 6 */
			return i80386_decode_opcode_0f00(cpu);
		case 0x01: /* Group 7 */
			return i80386_decode_opcode_0f01(cpu);
		case 0x02:
			lar(cpu);
			break;
		case 0x03:
			lsl(cpu);
			break;
		case 0x06:
			clts(cpu);
			break;
		case 0x07:
			loadall(cpu);
			break;

		case 0x20:
			mov_cr(cpu);
			break;
		case 0x21:
			mov_dr(cpu);
			break;
		case 0x22:
			mov_cr(cpu);
			break;
		case 0x23:
			mov_dr(cpu);
			break;
		case 0x24:
			mov_tr(cpu);
			break;
		case 0x26:
			mov_tr(cpu);
			break;

		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
		case 0x88:
		case 0x89:
		case 0x8A:
		case 0x8B:
		case 0x8C:
		case 0x8D:
		case 0x8E:
		case 0x8F:
			jcc_long(cpu);
			break;

		case 0x90:
		case 0x91:
		case 0x92:
		case 0x93:
		case 0x94:
		case 0x95:
		case 0x96:
		case 0x97:
		case 0x98:
		case 0x99:
		case 0x9A:
		case 0x9B:
		case 0x9C:
		case 0x9D:
		case 0x9E:
		case 0x9F:
			setcc(cpu);
			break;

		case 0xA0: /* Push FS */
			push_seg(cpu);
			break;
		case 0xA1: /* Pop FS */
			pop_seg(cpu);
			break;
		case 0xA3:
			bt(cpu);
			break;
		case 0xA4:
			shld_rm_imm(cpu);
			break;
		case 0xA5:
			shld_rm_cl(cpu);
			break;

		case 0xA8: /* Push GS */
			push_seg(cpu);
			break;
		case 0xA9: /* Pop GS */
			pop_seg(cpu);
			break;

		case 0xAB:
			bts(cpu);
			break;
		case 0xAC:
			shrd_rm_imm(cpu);
			break;
		case 0xAD:
			shrd_rm_cl(cpu);
			break;
		case 0xAF:
			imul_reg_rm(cpu);
			break;

		case 0xB2:
			lss(cpu);
			break;
		case 0xB3:
			btr(cpu);
			break;
		case 0xB4:
			lfs(cpu);
			break;
		case 0xB5:
			lgs(cpu);
			break;
		case 0xB6:
		case 0xB7:
			movzx(cpu);
			break;
		case 0xBB:
			btc(cpu);
			break;
		case 0xBC:
			bsf(cpu);
			break;
		case 0xBD:
			bsr(cpu);
			break;
		case 0xBE:
		case 0xBF:
			movsx(cpu);
			break;

		default:
			i80386_exception(cpu, EXCEPTION_UD);
			return I80386_DECODE_UNDEFINED;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_opcode(I80386* cpu) {
	switch (cpu->opcode) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
			add_rm_reg(cpu);
			break;
		case 0x04:
		case 0x05:
			add_accum_imm(cpu);
			break;
		case 0x06:
			push_seg(cpu);
			break;
		case 0x07:
			pop_seg(cpu);
			break;
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
			or_rm_reg(cpu);
			break;
		case 0x0C:
		case 0x0D:
			or_accum_imm(cpu);
			break;
		case 0x0E:
			push_seg(cpu);
			break;
		case 0x0F:
			return i80386_decode_opcode_0f(cpu);
		
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			adc_rm_reg(cpu);
			break;
		case 0x14:
		case 0x15:
			adc_accum_imm(cpu);
			break;
		case 0x16:
			push_seg(cpu);
			break;
		case 0x17:
			pop_seg(cpu);
			break;
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
			sbb_rm_reg(cpu);
			break;
		case 0x1C:
		case 0x1D:
			sbb_accum_imm(cpu);
			break;
		case 0x1E:
			push_seg(cpu);
			break;
		case 0x1F:
			pop_seg(cpu);
			break;
		
		case 0x20:
		case 0x21:
		case 0x22:
		case 0x23:
			and_rm_reg(cpu);
			break;
		case 0x24:
		case 0x25:
			and_accum_imm(cpu);
			break;
		case 0x26:
			return segment_override(cpu);
		case 0x27:
			daa(cpu);
			break;
		case 0x28:
		case 0x29:
		case 0x2A:
		case 0x2B:
			sub_rm_reg(cpu);
			break;
		case 0x2C:
		case 0x2D:
			sub_accum_imm(cpu);
			break;
		case 0x2E:
			return segment_override(cpu);
		case 0x2F:
			das(cpu);
			break;
		
		case 0x30:
		case 0x31:
		case 0x32:
		case 0x33:
			xor_rm_reg(cpu);
			break;
		case 0x34:
		case 0x35:
			xor_accum_imm(cpu);
			break;
		case 0x36:
			return segment_override(cpu);
		case 0x37:
			aaa(cpu);
			break;
		case 0x38:
		case 0x39:
		case 0x3A:
		case 0x3B:
			cmp_rm_reg(cpu);
			break;
		case 0x3C:
		case 0x3D:
			cmp_accum_imm(cpu);
			break;
		case 0x3E:
			return segment_override(cpu);
		case 0x3F:
			aas(cpu);
			break;

		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:
		case 0x46:
		case 0x47:
			inc_reg(cpu);
			break;

		case 0x48:
		case 0x49:
		case 0x4A:
		case 0x4B:
		case 0x4C:
		case 0x4D:
		case 0x4E:
		case 0x4F:
			dec_reg(cpu);
			break;

		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:
		case 0x54:
		case 0x55:
		case 0x56:
		case 0x57:
			push_reg(cpu);
			break;

		case 0x58:
		case 0x59:
		case 0x5A:
		case 0x5B:
		case 0x5C:
		case 0x5D:
		case 0x5E:
		case 0x5F:
			pop_reg(cpu);
			break;

		case 0x60:
			pusha(cpu);
			break;
		case 0x61:
			popa(cpu);
			break;
		case 0x62:
			bound(cpu);
			break;
		case 0x63:
			arpl(cpu);
			break;
		case 0x64:
			return segment_override_extended(cpu);
		case 0x65:
			return segment_override_extended(cpu);
		case 0x66:
			return operand_size(cpu);
		case 0x67:
			return address_size(cpu);
		case 0x68:
			push_imm(cpu);
			break;
		case 0x69:
			imul_reg_rm_imm(cpu);
			break;
		case 0x6A:
			push_imm(cpu);
			break;
		case 0x6B:
			imul_reg_rm_imm(cpu);
			break;
		case 0x6C:
		case 0x6D:
			ins(cpu);
			break;
		case 0x6E:
		case 0x6F:
			outs(cpu);
			break;

		case 0x70:
		case 0x71:
		case 0x72:
		case 0x73:
		case 0x74:
		case 0x75:
		case 0x76:
		case 0x77:
		case 0x78:
		case 0x79:
		case 0x7A:
		case 0x7B:
		case 0x7C:
		case 0x7D:
		case 0x7E:
		case 0x7F:
			jcc_short(cpu);
			break;

		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
			return i80386_decode_opcode_80(cpu);
		case 0x84:
		case 0x85:
			test_rm_reg(cpu);
			break;
		case 0x86:
		case 0x87:
			xchg_rm_reg(cpu);
			break;
		case 0x88:
		case 0x89:
		case 0x8A:
		case 0x8B:
			mov_rm_reg(cpu);
			break;
		case 0x8C:
			mov_seg(cpu);
			break;
		case 0x8D:
			lea(cpu);
			break;
		case 0x8E:
			mov_seg(cpu);
			break;
		case 0x8F:
			pop_rm(cpu);
			break;

		case 0x90:
			nop(cpu);
			break;
		case 0x91:
		case 0x92:
		case 0x93:
		case 0x94:
		case 0x95:
		case 0x96:
		case 0x97:
			xchg_accum_reg(cpu);
			break;
		case 0x98:
			cbw(cpu);
			break;
		case 0x99:
			cwd(cpu);
			break; 
		case 0x9A:
			call_inter_direct(cpu);
			break;
		case 0x9B:
			wait(cpu);
			break;
		case 0x9C:
			pushf(cpu);
			break;
		case 0x9D:
			popf(cpu);
			break;
		case 0x9E:
			sahf(cpu);
			break;
		case 0x9F:
			lahf(cpu);
			break;

		case 0xA0:
		case 0xA1:
		case 0xA2:
		case 0xA3:
			mov_accum_mem(cpu);
			break;
		case 0xA4:
		case 0xA5:
			movs(cpu);
			break;
		case 0xA6:
		case 0xA7:
			cmps(cpu);
			break;
		case 0xA8:
		case 0xA9:
			test_accum_imm(cpu);
			break;
		case 0xAA:
		case 0xAB:
			stos(cpu);
			break;
		case 0xAC:
		case 0xAD:
			lods(cpu);
			break;
		case 0xAE:
		case 0xAF:
			scas(cpu);
			break;

		case 0xB0:
		case 0xB1:
		case 0xB2:
		case 0xB3:
		case 0xB4:
		case 0xB5:
		case 0xB6:
		case 0xB7:
		case 0xB8:
		case 0xB9:
		case 0xBA:
		case 0xBB:
		case 0xBC:
		case 0xBD:
		case 0xBE:
		case 0xBF:
			mov_reg_imm(cpu);
			break;

		case 0xC0:
		case 0xC1:
			return i80386_decode_opcode_c0(cpu);
		case 0xC2:
		case 0xC3:
			ret_intra(cpu);
			break;
		case 0xC4:
			les(cpu);
			break;
		case 0xC5:
			lds(cpu);
			break;
		case 0xC6:
		case 0xC7:
			mov_rm_imm(cpu);
			break;
		case 0xC8:
			enter(cpu);
			break;
		case 0xC9:
			leave(cpu);
			break;
		case 0xCA:
		case 0xCB:
			ret_inter(cpu);
			break;
		case 0xCC:
			int3(cpu);
			break;
		case 0xCD:
			int_(cpu);
			break;
		case 0xCE:
			into(cpu);
			break;
		case 0xCF:
			iret(cpu);
			break;
			
		case 0xD0:
		case 0xD1:
		case 0xD2:
		case 0xD3:
			return i80386_decode_opcode_d0(cpu);
		case 0xD4:
			aam(cpu);
			break;
		case 0xD5:
			aad(cpu);
			break;
		case 0xD6: /* 8086+ undocumented; Set AL to Carry */
			salc(cpu);
			break;
		case 0xD7:
			xlat(cpu);
			break;
		case 0xD8:
		case 0xD9:
		case 0xDA:
		case 0xDB:
		case 0xDC:
		case 0xDD:
		case 0xDE:
		case 0xDF:
			esc(cpu);
			break;

		case 0xE0:
		case 0xE1:
			loopz(cpu);
			break;
		case 0xE2:
			loop(cpu);
			break;
		case 0xE3:
			jcxz(cpu);
			break;
		case 0xE4:
		case 0xE5:
			in_accum_imm(cpu);
			break;
		case 0xE6:
		case 0xE7:
			out_accum_imm(cpu);
			break;
		case 0xE8:
			call_intra_direct(cpu);
			break;
		case 0xE9:
			jmp_intra_direct(cpu);
			break;
		case 0xEA:
			jmp_inter_direct(cpu);
			break;
		case 0xEB:
			jmp_intra_direct_short(cpu);
			break;
		case 0xEC:
		case 0xED:
			in_accum_dx(cpu);
			break;
		case 0xEE:
		case 0xEF:
			out_accum_dx(cpu);
			break;

		case 0xF0:
		case 0xF1: /* 8086 undocumented; Decodes identically to 0xF0. todo: is this valid on 386? */
			return lock(cpu);
		case 0xF2:
		case 0xF3:
			return rep(cpu);
		case 0xF4:
			hlt(cpu);
			break;
		case 0xF5:
			cmc(cpu);
			break;
		case 0xF6:
		case 0xF7:
			return i80386_decode_opcode_f6(cpu);
		case 0xF8:
			clc(cpu);
			break;
		case 0xF9:
			stc(cpu);
			break;
		case 0xFA:
			cli(cpu);
			break;
		case 0xFB:
			sti(cpu);
			break;
		case 0xFC:
			cld(cpu);
			break;
		case 0xFD:
			std(cpu);
			break;
		case 0xFE:
			return i80386_decode_opcode_fe(cpu);
		case 0xFF:
			return i80386_decode_opcode_ff(cpu);
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_instruction(I80386* cpu) {
	int r = 0;
	if (!i80386_fetch(cpu)) {
		return 0;
	}
	do {
		r = i80386_decode_opcode(cpu);
	} while (r == I80386_DECODE_REQ_CYCLE);
	return r;
}

void i80386_init(I80386* cpu) {
	cpu->funcs.exe_mem_byte = NULL;
	cpu->funcs.read_mem_byte = NULL;
	cpu->funcs.write_mem_byte = NULL;
	cpu->funcs.read_io_byte = NULL;
	cpu->funcs.write_io_byte = NULL;
}
void i80386_reset(I80386* cpu) {
	for (int i = 0; i < I80386_REGISTER_COUNT; ++i) {
		cpu->general_registers[i].r32 = 0;
	}
	for (int i = 0; i < I80386_SEGMENT_COUNT; ++i) {
		cpu->segment_registers[i].selector = 0x0000;
		cpu->segment_registers[i].desc.limit = 0x0000FFFF;
		cpu->segment_registers[i].desc.base = 0x00000000;
		cpu->segment_registers[i].desc.ar.word = 0;
		cpu->segment_registers[i].desc.ar.rw = 1;
		cpu->segment_registers[i].desc.ar.present = 1;
	}
	for (int i = 0; i < I80386_CONTROL_REGISTER_COUNT; ++i) {
		cpu->control_registers[i] = 0x00000000;
	}
	for (int i = 0; i < I80386_DEBUG_REGISTER_COUNT; ++i) {
		cpu->debug_registers[i] = 0x00000000;
	}
	for (int i = 0; i < I80386_TEST_REGISTER_COUNT; ++i) {
		cpu->test_registers[i] = 0x00000000;
	}

	cpu->idtr.base = 0x00000000;
	cpu->idtr.limit = 0x000003FF;

	cpu->gdtr.base = 0x00000000;
	cpu->gdtr.limit = 0x00000000;

	cpu->ldtr.selector = 0x00000000;
	cpu->ldtr.desc.base = 0x00000000;
	cpu->ldtr.desc.limit = 0x00000000;
	cpu->ldtr.desc.ar.word = 0x0000;

	cpu->eip = 0x0000FFF0;
	cpu->cs.selector = 0xF000;
	cpu->cs.desc.base = 0xFFFF0000;
	cpu->cs.desc.ar.e = 1;

	cpu->psw.word = 0x2;
	cpu->msw.word = 0xFFF0;

	cpu->opcode = 0;
	cpu->modrm.byte = 0;
	cpu->sib.byte = 0;
	cpu->operand_size = 0;
	cpu->address_size = 0;
	cpu->segment_override = 0xFF;

	cpu->cycles = 0;
	cpu->internal_flags = 0;
	cpu->instruction_len = 0;

	cpu->intr = 0;
	cpu->nmi = 0;

	cpu->tf_latch = 0;
	cpu->int_latch = 0;
	cpu->int_delay = 0;
	cpu->intr_type = 0;
	cpu->halt = 0;

	cpu->exception.number = 0;
	cpu->exception.df_number = 0;
	cpu->exception.tf_number = 0;
	cpu->exception.state = 0;
	cpu->exception.code = 0;

	cpu->effective_address.valid = 0;
	cpu->effective_address.stack_address = 0;
	cpu->effective_address.segment_index = 0;
	cpu->effective_address.logical_address.offset = 0;
	cpu->effective_address.logical_address.base = 0;

	i80386_flush_tlb(cpu);
}

int i80386_execute(I80386* cpu) {
	if (cpu->exception.state == I80386_EXCEPTION_STATE_TRIPLE_FAULT) {
		return I80386_DECODE_TRIPLE_FAULT;
	}
	i80386_check_interrupts(cpu);
	if (!i80386_check_halt(cpu)) {
		return I80386_DECODE_OK;
	}
	return i80386_decode_instruction(cpu);
}

uint32_t i80386_get_physical_address_bo(uint32_t base, uint32_t offset) {
	return (base + offset) & 0xFFFFFFFF;
}
uint32_t i80386_get_physical_address_so(uint16_t selector, uint32_t offset) {
	return (((uint32_t)selector << 4) + offset) & 0xFFFFFFFF;
}
int i80386_read_descriptor_table_entry(const I80386* cpu, uint16_t selector, I80386_DESCRIPTOR_TABLE_ENTRY* entry) {
	uint8_t rpl = selector & 3U;        /* requestor's privilege level */
	uint8_t ti = (selector >> 2U) & 1U; /* type */
int i80386_read_descriptor_table_entry(I80386* cpu, uint16_t selector, I80386_DESCRIPTOR_TABLE_ENTRY* entry) {
	uint8_t ti = selector & 0x04;       /* type */
	uint16_t index = selector & 0xFFF8; /* entry index */

	uint32_t limit = 0;
	uint32_t base = 0;

	if (ti) {
		/* LDT */
		if ((cpu->ldtr.selector & 0xFFF8) == 0) {
			i80386_exception_code(cpu, EXCEPTION_TS, selector);
			return 0; /* #TS(selector) */
		}
		limit = cpu->ldtr.desc.limit;
		base = cpu->ldtr.desc.base;
	}
	else {
		/* GDT */
		limit = cpu->gdtr.limit;
		base = cpu->gdtr.base;
	}

	if (index + 7U > limit) {
		i80386_exception_code(cpu, EXCEPTION_GP, selector);
		return 0;
	}

	read_qword_logical(cpu, base, index, &entry->qword);
	
	return 1; /* success */
}
int i80386_write_descriptor_table_entry(I80386* cpu, uint16_t selector, const I80386_DESCRIPTOR_TABLE_ENTRY* entry) {
	uint8_t ti = selector & 0x04;       /* type */
	uint16_t index = selector & 0xFFF8; /* entry index */	

	uint32_t limit = 0;
	uint32_t base = 0;

	if (ti) {
		/* LDT */
		if ((cpu->ldtr.selector & 0xFFF8) == 0) {
			i80386_exception_code(cpu, EXCEPTION_TS, selector);
			return 0; /* #TS(selector) */
		}
		limit = cpu->ldtr.desc.limit;
		base = cpu->ldtr.desc.base;
	}
	else {
		/* GDT */
		limit = cpu->gdtr.limit;
		base = cpu->gdtr.base;
	}

	if (index + 7U > limit) {
		i80386_exception_code(cpu, EXCEPTION_GP, selector);
		return 0;
	}

	write_qword_logical(cpu, base, index, entry->qword);
	
	return 1; /* success */
}
void i80386_update_system_descriptor_cache(const I80386_DESCRIPTOR_TABLE_ENTRY* entry, I80386_DESCRIPTOR_CACHE* cache) {
	cache->base = (uint32_t)entry->base_lo | (entry->base_mi << 16) | (entry->base_hi << 24);
	cache->limit = (uint32_t)entry->limit_lo | (entry->ar.limit_hi << 16);
	cache->ar.word = entry->ar.word;
}
void i80386_update_segment_descriptor_cache(const I80386_DESCRIPTOR_TABLE_ENTRY* entry, I80386_DESCRIPTOR_CACHE* cache) {
	i80386_update_system_descriptor_cache(entry, cache);
	if (cache->ar.granularity) {
		cache->limit = (cache->limit << 12) | 0xFFF;
	}
}
int i80386_load_segment_register(I80386* cpu, I80386_SEGMENT_REGISTER* sreg, int sreg_index, uint16_t selector) {
	if (!cpu->msw.pe) {
		/* Real mode */
		sreg->selector = selector;
		sreg->desc.base = (uint32_t)selector << 4;
		sreg->desc.limit = 0x0000FFFF;
		sreg->desc.ar.word = 0;
		return 1;
	}
	else if (cpu->eflags.vm) {
		/* Virtual 8086 mode */
		sreg->selector = selector;
		sreg->desc.base = (uint32_t)selector << 4;
		sreg->desc.limit = 0x0000FFFF;
		sreg->desc.ar.word = 0;
		return 1;
	}
	else {
		/* Protected mode */
		int sr_type = 0;
		I80386_DESCRIPTOR_TABLE_ENTRY entry = { 0 };
		uint8_t	rpl = selector & 0x3;
		uint16_t index = selector & 0xFFF8;

		if (sreg_index < 0 || sreg_index >= I80386_SEGMENT_COUNT) {
			return 0;
		}

		if (sreg_index == SEG_CS) {
			sr_type = SR_TYPE_CODE;
		}
		else if (sreg_index == SEG_SS) {
			sr_type = SR_TYPE_STACK;
		}
		else if (sreg_index == SEG_LDT) {
			sr_type = SR_TYPE_LDT;
		}
		else if (sreg_index == SEG_TR) {
			sr_type = SR_TYPE_TR;
		}
		else {
			sr_type = SR_TYPE_DATA;
		}

		/* Read descriptor */
		if (index != 0) {
		if (!i80386_read_descriptor_table_entry(cpu, selector, &entry)) {
				return 0;
			}
		}

		switch (sr_type) {
			case SR_TYPE_DATA:
				if (index == 0) {
					/* ES/DS/FS/GS are allowed to be loaded with a null selector. */
					break;
				}

				/* Must be present */
				if (!entry.ar.present) {
					i80386_exception_code(cpu, EXCEPTION_NP, selector);
					return 0;
				}

				/* Cannot be system */
				if (!entry.ar.s) {
			i80386_exception_code(cpu, EXCEPTION_GP, selector);
			return 0;
		}

				/* Must be readable */
				if (entry.ar.e && !entry.ar.rw) {
					i80386_exception_code(cpu, EXCEPTION_GP, selector);
					return 0;
				}
				break;
			case SR_TYPE_CODE:
				/* CS cannot have a NULL selector */
				if (index == 0) {
					i80386_exception_code(cpu, EXCEPTION_GP, selector);
					return 0;
				}

				/*  Must be present */
				if (!entry.ar.present) {
				i80386_exception_code(cpu, EXCEPTION_NP, selector);
				return 0;
			}

				/* Cannot be system */
				if (!entry.ar.s) {
					i80386_exception_code(cpu, EXCEPTION_GP, selector);
					return 0;
				}

				/* Must be executable */
				if (!entry.ar.e) {
					i80386_exception_code(cpu, EXCEPTION_GP, selector);
					return 0;
				}

				if (entry.ar.conforming) { 
					/* Conforming */

					/* CPL must be >= DPL */
					if (cpu->cpl < entry.ar.dpl) {
						i80386_exception_code(cpu, EXCEPTION_GP, selector);
						return 0;
					}
				}
				else {
					/* Non-conforming */

					/* CPL must be >= DPL */
					if (cpu->cpl < entry.ar.dpl) {
				i80386_exception_code(cpu, EXCEPTION_GP, selector);
				return 0;
			}

					/* RPL must be >= CPL */
					if (rpl < cpu->cpl) {
						i80386_exception_code(cpu, EXCEPTION_GP, selector);
				return 0;
			}
		}
				break;
			case SR_TYPE_STACK:
			/* SS cannot have a NULL selector */
				if (index == 0) {
					i80386_exception_code(cpu, EXCEPTION_SS, selector);
					return 0;
				}

				/* Must be present */
				if (!entry.ar.present) {
					i80386_exception_code(cpu, EXCEPTION_SS, selector);
				return 0;
			}

				/* Cannot be system */
				if (!entry.ar.s) {
				i80386_exception_code(cpu, EXCEPTION_SS, selector);
				return 0;
			}

				/* Cannot be executable */
				if (entry.ar.e) {
					i80386_exception_code(cpu, EXCEPTION_GP, selector);
					return 0;
				}

				/* Must be writable */
				if (!entry.ar.rw) {
				i80386_exception_code(cpu, EXCEPTION_GP, selector);
				return 0;
			}

				/* CPL must == DPL */
				if (cpu->cpl != entry.ar.dpl) {
					i80386_exception_code(cpu, EXCEPTION_GP, selector);
				return 0;
			}
				break;
			case SR_TYPE_LDT:
				/* LDT is allowed to be loaded with a null selector. */
				if (index == 0) {
					break;
		}

				/* TI must be 0 (GDT) */
				if (selector & 0x4) {
					i80386_exception_code(cpu, EXCEPTION_GP, selector);
				return 0;
			}

				/* Must be present */
				if (!entry.ar.present) {
				i80386_exception_code(cpu, EXCEPTION_NP, selector);
				return 0;
			}

				/* Must be system */
				if (entry.ar.s) {
				i80386_exception_code(cpu, EXCEPTION_GP, selector);
				return 0;
			}

				/* Must be ldt */
				if (entry.ar.type != I80386_GATE_TYPE_LDT) {
					i80386_exception_code(cpu, EXCEPTION_GP, selector);
				return 0;
			}
				break;
			case SR_TYPE_TR:
				/* todo: is TR allowed to be loaded with a null selector? */
				if (index == 0) {
					break;
		}

				/* Must be present */
				if (!entry.ar.present) {
				i80386_exception_code(cpu, EXCEPTION_NP, selector);
				return 0;
			}

				/* Must be system */
				if (entry.ar.s) {
				i80386_exception_code(cpu, EXCEPTION_GP, selector);
				return 0;
			}
				break;

			default:
			case SR_TYPE_SYS:
				return 0;
		}
		
		i80386_update_segment_descriptor_cache(&entry, &sreg->desc);
		sreg->selector = selector;
		return 1;
	}
}
void i80386_copy_segment_descriptor(I80386_SEGMENT_REGISTER* dest, const I80386_SEGMENT_REGISTER* src) {
	dest->selector = src->selector;
	dest->desc.base = src->desc.base;
	dest->desc.limit = src->desc.limit;
	dest->desc.ar.word = src->desc.ar.word;
}
int i80386_resolve_segment_selector(const I80386* cpu, uint16_t selector, uint32_t* base) {
	if (cpu->msw.pe && !cpu->eflags.vm) {
		/* Protected mode */
		I80386_DESCRIPTOR_TABLE_ENTRY entry = { 0 };
		I80386_DESCRIPTOR_CACHE cache = { 0 };
		if (!i80386_read_descriptor_table_entry(cpu, selector, &entry)) {
			return 0;
		}
		i80386_update_segment_descriptor_cache(&entry, &cache);
		*base = cache.base;
		return 1;
	}
	else if (!cpu->msw.pe || (cpu->msw.pe && cpu->eflags.vm)) {
		/* Real mode / Virtual 8086 mode */
		*base = (uint32_t)selector << 4;
		return 1;
	}
	return 0;
}

int i80386_modrm_get_segment(const I80386* cpu, uint8_t address_size, I80386_MOD_RM modrm, I80386_SIB sib, I80386_EFFECTIVE_ADDRESS* effective_address, uint8_t segment_prefix) {
	if (segment_prefix != 0xFF) {
		modrm_set_effective_address_descriptor(cpu, segment_prefix, effective_address);
		return 1;
	}
	else {
		if (address_size) {
			uint8_t base_reg = 0;
			if (modrm.rm == 0b100) {
				/* SIB */
				base_reg = sib.base;

				/* mod=00 special case for base=101 -> [disp32] uses DS */
				if (modrm.mod == 0b00 && base_reg == 0b101) {
					modrm_set_effective_address_descriptor(cpu, SEG_DS, effective_address);
					return 1;
				}
			}
			else {
				base_reg = cpu->modrm.rm;
			}

			/* mod=00 special case for r/m=101 -> [disp32] uses DS */
			if (modrm.mod == 0b00 && modrm.rm == 0b101) {
				modrm_set_effective_address_descriptor(cpu, SEG_DS, effective_address);
				return 1;
			}
			/* SS if base is EBP or ESP */
			else if (base_reg == REG_EBP || base_reg == REG_ESP) {
				modrm_set_effective_address_descriptor(cpu, SEG_SS, effective_address);
				return 1;
			}
			else {
				modrm_set_effective_address_descriptor(cpu, SEG_DS, effective_address);
				return 1;
			}
		}
		else {
			/* mod=00 special case for r/m=110 -> [disp16] uses DS */
			if (modrm.mod == 0b00 && modrm.rm == 0b110) {
				modrm_set_effective_address_descriptor(cpu, SEG_DS, effective_address);
				return 1;
			}
			else {
				switch (modrm.rm) {
				case 0b010: /* [BP+SI] */
				case 0b011: /* [BP+DI] */
				case 0b110: /* [BP] (mod != 00) */
					modrm_set_effective_address_descriptor(cpu, SEG_SS, effective_address);
					return 1;
				default:
					modrm_set_effective_address_descriptor(cpu, SEG_DS, effective_address);
					return 1;
				}
			}
		}
	}
}
int i80386_modrm_get_offset(const I80386* cpu, uint8_t address_size, I80386_MOD_RM modrm, I80386_SIB sib, I80386_EFFECTIVE_ADDRESS* ea,
	I80386_FETCH_BYTE fetch_byte, I80386_FETCH_WORD fetch_word, I80386_FETCH_DWORD fetch_dword, void* user_param) {
	ea->logical_address.offset = 0;

	switch (modrm.mod) {
		case 0b00:
			if (address_size) {
				if (modrm.rm == 0b100) {
					/* [SIB] */
					if (sib.index == 0b100) {
						if (sib.base == 0b101) {
							/* [disp32] */
							uint32_t disp = 0;
							if (!fetch_dword(user_param, &disp)) {
								return 0;
							}
							ea->logical_address.offset = disp;
							return 1;
						}
						else {
	#ifdef _386_SIB_UNDEFINED_
							/* [base * scale] - scale!=b00 && index==0b100 is undefined on 386 */
							uint32_t base = reg32_read(cpu, sib.base);
							ea->logical_address.offset = base << sib.scale;
	#else
							/* [base] */
							uint32_t base = reg32_read(cpu, sib.base);
							ea->logical_address.offset = base;
	#endif
							return 1;
						}
					}
					else {
						if (sib.base == 0b101) {
							/* [index * scale + disp32] */
							uint32_t disp = 0;
							if (!fetch_dword(user_param, &disp)) {
								return 0;
							}
							uint32_t index = reg32_read(cpu, sib.index);
							ea->logical_address.offset = (index << sib.scale) + (int32_t)disp;
							return 1;
						}
						else {
							/* [base + index * scale] */
							uint32_t base = reg32_read(cpu, sib.base);
							uint32_t index = reg32_read(cpu, sib.index);
							ea->logical_address.offset = base + (index << sib.scale);
							return 1;
						}
					}
				}
				else if (modrm.rm == 0b101) {
					/* [disp32] */
					uint32_t disp = 0;
					if (!fetch_dword(user_param, &disp)) {
						return 0;
					}
					ea->logical_address.offset = disp;
					return 1;
				}
				else {
					/* [reg32] */
					ea->logical_address.offset = reg32_read(cpu, modrm.rm);
					return 1;
				}
			}
			else {
				if (modrm.rm == 0b110) {
					/* [disp16] */
					uint16_t disp = 0;
					if (!fetch_word(user_param, &disp)) {
						return 0;
					}
					ea->logical_address.offset = disp;
					return 1;
				}
				else {
					/* [base16] */
					uint16_t base = modrm_get_base_offset(cpu, modrm);
					ea->logical_address.offset = base;
					return 1;
				}
			}

		case 0b01: {
			uint8_t disp = 0;
			if (!fetch_byte(user_param, &disp)) {
				return 0;
			}

			if (address_size) {
				if (modrm.rm == 0b100) {
					/* [SIB + disp8] */
					if (sib.index == 0b100) {
	#ifdef _386_SIB_UNDEFINED_
						/* [base * scale + disp8] - scale!=b00 && index==0b100 is undefined on i80386 */
						uint32_t base = reg32_read(cpu, sib.base);
						ea->logical_address.offset = (base << sib.scale) + (int8_t)disp;
	#else
						/* [base + disp8] */
						uint32_t base = reg32_read(cpu, sib.base);
						ea->logical_address.offset = base + (int8_t)disp;
	#endif
						return 1;
					}
					else {
						/* [base + index * scale + disp8] */
						uint32_t base = reg32_read(cpu, sib.base);
						uint32_t index = reg32_read(cpu, sib.index);
						ea->logical_address.offset = base + (index << sib.scale) + (int8_t)disp;
						return 1;
					}
				}
				else {
					/* [reg32 + disp8] */
					uint32_t base = reg32_read(cpu, modrm.rm);
					ea->logical_address.offset = (base + (int8_t)disp) & 0xFFFFFFFF;
					return 1;
				}
			}
			else {
				/* [base16 + disp8] */
				uint32_t base = modrm_get_base_offset(cpu, modrm);
				ea->logical_address.offset = (base + (int8_t)disp) & 0xFFFF;
				return 1;
			}
		}

		case 0b10: {
			if (address_size) {
				uint32_t disp = 0;
				if (!fetch_dword(user_param, &disp)) {
					return 0;
				}

				if (modrm.rm == 0b100) {
					/* [SIB + disp32] */
					if (sib.index == 0b100) {
	#ifdef _386_SIB_UNDEFINED_
						/* [base * scale + disp32] - scale!=b00 && index==0b100 is undefined on 386 */
						uint32_t base = reg32_read(cpu, sib.base);
						ea->logical_address.offset = (base << sib.scale) + (int32_t)disp;
	#else
						/* [base + disp32] */
						uint32_t base = reg32_read(cpu, sib.base);
						ea->logical_address.offset = base + (int32_t)disp;
	#endif
						return 1;
					}
					else {
						/* [base + index * scale + disp32] */
						uint32_t base = reg32_read(cpu, sib.base);
						uint32_t index = reg32_read(cpu, sib.index);
						ea->logical_address.offset = base + (index << sib.scale) + (int32_t)disp;
						return 1;
					}
				}
				else {
					/* [reg32 + disp32] */
					uint32_t base = reg32_read(cpu, modrm.rm);
					ea->logical_address.offset = (base + (int32_t)disp) & 0xFFFFFFFF;
					return 1;
				}
			}
			else {
				/* [base16 + disp16] */
				uint16_t disp = 0;
				if (!fetch_word(user_param, &disp)) {
					return 0;
				}
				uint32_t base = modrm_get_base_offset(cpu, modrm);
				ea->logical_address.offset = (base + (int16_t)disp) & 0xFFFF;
				return 1;
			}
		}

		/* case 0b11: register mode SHOULD never calls this */
		case 0b11:
		default:
			return 0;
	}
}

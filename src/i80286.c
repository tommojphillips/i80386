/* i80286.c
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 CPU
 */

#include <stdint.h>

#include "i80286.h"
#include "i80286_alu.h"
#include "sign_extend.h"

#define PSW cpu->psw.u.word
#define MSW cpu->msw.u.word
#define NMI cpu->nmi
#define INTR cpu->intr

#define SF cpu->psw.u.bits.sf
#define CF cpu->psw.u.bits.cf
#define ZF cpu->psw.u.bits.zf
#define PF cpu->psw.u.bits.pf
#define OF cpu->psw.u.bits.of
#define AF cpu->psw.u.bits.af
#define DF cpu->psw.u.bits.df
#define TF cpu->psw.u.bits.tf
#define IF cpu->psw.u.bits.in

#define PE cpu->msw.u.bits.pe
#define MP cpu->msw.u.bits.mp
#define EM cpu->msw.u.bits.em
#define TS cpu->msw.u.bits.ts

#define IP cpu->ip

#define AL cpu->registers[REG_AL].u.r8.l   /* accum low byte 8bit register */
#define AH cpu->registers[REG_AL].u.r8.h   /* accum high byte 8bit register */
#define AX cpu->registers[REG_AX].u.r16    /* accum 16bit register */

#define CL cpu->registers[REG_CL].u.r8.l   /* count low byte 8bit register */
#define CH cpu->registers[REG_CL].u.r8.h   /* count high byte 8bit register */
#define CX cpu->registers[REG_CX].u.r16    /* count 16bit register */

#define DL cpu->registers[REG_DL].u.r8.l   /* data low byte 8bit register */
#define DH cpu->registers[REG_DL].u.r8.h   /* data high byte 8bit register */
#define DX cpu->registers[REG_DX].u.r16    /* data 16bit register */

#define BL cpu->registers[REG_BL].u.r8.l   /* base low byte 8bit register */
#define BH cpu->registers[REG_BL].u.r8.h   /* base high byte 8bit register */
#define BX cpu->registers[REG_BX].u.r16    /* base 16bit register */

#define SP cpu->registers[REG_SP].u.r16    /* stack pointer 16bit register */
#define BP cpu->registers[REG_BP].u.r16    /* base pointer 16bit register */
#define SI cpu->registers[REG_SI].u.r16    /* src index 16bit register */
#define DI cpu->registers[REG_DI].u.r16    /* dest index 16bit register */

#define ES cpu->segments[SEG_ES].selector  /* extra segment register */
#define CS cpu->segments[SEG_CS].selector  /* code segment register */
#define SS cpu->segments[SEG_SS].selector  /* stack segment register */
#define DS cpu->segments[SEG_DS].selector  /* data segment register */

#define ES_CACHE cpu->segments[SEG_ES].descriptor  /* extra segment register */
#define CS_CACHE cpu->segments[SEG_CS].descriptor  /* code segment register */
#define SS_CACHE cpu->segments[SEG_SS].descriptor  /* stack segment register */
#define DS_CACHE cpu->segments[SEG_DS].descriptor  /* data segment register */

#define LDT       cpu->ldt.selector
#define LDT_CACHE cpu->ldt.descriptor

#define IDTR      cpu->idtr
#define GDTR      cpu->gdtr

#define TR        cpu->tr
#define TSS       cpu->tss

 /* byte/word operation. 0 = byte; 1 = word */
#define W (cpu->opcode & 0x1)

/* byte/word operation. 0 = byte; 1 = word */
#define WREG (cpu->opcode & 0x8) 

/* sign extend. 0 = word; 1 = byte sign extended to word */
#define S (cpu->opcode & 0x2)

/* segment register. es=b00; cs=b01; ss=b10; ds=b11 */
#define SR ((cpu->opcode >> 0x3) & 0x3)

/* 0 = (count = 1); 1 = (count = CL) */
#define VW (cpu->opcode & 0x2)

/* register direction (reg <- r/m) or (r/m <- reg) */
#define D (cpu->opcode & 0x2) 

 /* Jump condition */
#define CCCC (cpu->opcode & 0x0F)
#define JCC_JO  0b0000
#define JCC_JNO 0b0001
#define JCC_JC  0b0010
#define JCC_JNC 0b0011
#define JCC_JZ  0b0100
#define JCC_JNZ 0b0101
#define JCC_JBE 0b0110
#define JCC_JA  0b0111
#define JCC_JS  0b1000
#define JCC_JNS 0b1001
#define JCC_JPE 0b1010
#define JCC_JPO 0b1011
#define JCC_JL  0b1100
#define JCC_JGE 0b1101
#define JCC_JLE 0b1110
#define JCC_JG  0b1111

/* Get default or override segment index */
#define GET_SEG_OVERRIDE(seg) ((cpu->segment_prefix != 0xFF) ? cpu->segment_prefix : seg)

/* Get default or override segment register */
#define SEG_DEFAULT_OR_OVERRIDE(seg) (cpu->segments[GET_SEG_OVERRIDE(seg)].selector)

/* Read byte from IO port */
#define READ_IO_BYTE(port) cpu->funcs.read_io_byte(port)

/* Write byte to IO port */
#define WRITE_IO_BYTE(port,value) cpu->funcs.write_io_byte(port, value)

#define INT_DBZ      0 /* ITC 0 */
#define INT_TRAP     1 /* ITC 1 */
#define INT_NMI      2 /* ITC 2 */
#define INT_3        3 /* ITC 3 */
#define INT_OVERFLOW 4 /* ITC 4 */
#define INT_BOUNDS   5 /* ITC 5 */

#define EXCEPTION_UD  6 /* #UD */
#define EXCEPTION_GP 13 /* #GP */

/* Internal flag F1. Signals that a rep prefix is in use for this decode cycle */
#define F1  (cpu->internal_flags & INTERNAL_FLAG_F1)

/* Internal flag F1Z. Signals which rep (repz/repnz) is in use for this decode cycle */
#define F1Z (cpu->internal_flags & INTERNAL_FLAG_F1Z)

/* 8bit r/m operand */
typedef struct {
	uint8_t is_reg;
	union {
		uint8_t reg_index;
		struct {
			uint16_t segment;
			uint16_t offset;
		} mem;
	} u;
} OPERAND8;

/* 16bit r/m operand */
typedef struct {
	uint8_t is_reg;
	union {
		uint8_t reg_index;
		struct {
			uint16_t segment;
			uint16_t offset;
		} mem;
	} u;
} OPERAND16;

void i80286_exception(I80286* cpu, uint8_t exception);

static int read_byte_linear(I80286* cpu, uint24_t physical_address, uint8_t* value) {
	*value = cpu->funcs.read_mem_byte(physical_address);
	return 1;
}
static int write_byte_linear(I80286* cpu, uint24_t physical_address, uint8_t value) {
	cpu->funcs.write_mem_byte(physical_address, value);
	return 1;
}

static int read_byte(I80286* cpu, uint16_t segment, uint16_t offset, uint8_t* value) {
	if (offset > 0xFFFF) {
		i80286_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	*value = cpu->funcs.read_mem_byte(i80286_get_physical_address(segment, offset));
	return 1;
}
static int write_byte(I80286* cpu, uint16_t segment, uint16_t offset, uint8_t value) {
	if (offset > 0xFFFF) {
		i80286_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	cpu->funcs.write_mem_byte(i80286_get_physical_address(segment, offset), value);
	return 1;
}
static int fetch_byte(I80286* cpu, uint8_t* value) {
	read_byte(cpu, CS, IP, value);
	IP += 1;
	cpu->instruction_len += 1;
	if (cpu->instruction_len > 10) {
		i80286_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	return 1;
}

static int read_word_linear(I80286* cpu, uint24_t physical_address, uint16_t* value) {
	*value = (((uint16_t)cpu->funcs.read_mem_byte(physical_address + 1) << 8) | cpu->funcs.read_mem_byte(physical_address));
	return 1;
}
static int write_word_linear(I80286* cpu, uint24_t physical_address, uint16_t value) {
	cpu->funcs.write_mem_byte(physical_address, value & 0xFF);
	cpu->funcs.write_mem_byte(physical_address + 1, (value >> 8) & 0xFF);
	return 1;
}

static int read_word(I80286* cpu, uint16_t segment, uint16_t offset, uint16_t* value) {
	if (offset >= 0xFFFF) {
		i80286_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	*value = (((uint16_t)cpu->funcs.read_mem_byte(i80286_get_physical_address(segment, offset + 1)) << 8) | cpu->funcs.read_mem_byte(i80286_get_physical_address(segment, offset)));
	return 1;
}
static int write_word(I80286* cpu, uint16_t segment, uint16_t offset, uint16_t value) {
	if (offset >= 0xFFFF) {
		i80286_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	cpu->funcs.write_mem_byte(i80286_get_physical_address(segment, offset), value & 0xFF);
	cpu->funcs.write_mem_byte(i80286_get_physical_address(segment, offset + 1), (value >> 8) & 0xFF);
	return 1;
}
static int fetch_word(I80286* cpu, uint16_t* value) {
	read_word(cpu, CS, IP, value);
	IP += 2;
	cpu->instruction_len += 2;
	if (cpu->instruction_len > 10) {
		i80286_exception(cpu, EXCEPTION_GP);
		return 0;
	}
	return 1;
}

static uint8_t reg8_read(I80286* cpu, uint8_t reg) {
	if (reg & 0x4) {
		return cpu->registers[reg & 0x3].u.r8.h;
	}
	else {
		return cpu->registers[reg & 0x3].u.r8.l;
	}
}
static void reg8_write(I80286* cpu, uint8_t reg, uint8_t v) {
	if (reg & 0x4) {
		cpu->registers[reg & 0x3].u.r8.h = v;
	}
	else {
		cpu->registers[reg & 0x3].u.r8.l = v;
	}
}

static uint16_t reg16_read(I80286* cpu, uint8_t reg) {
	return cpu->registers[reg & 0x7].u.r16;
}
static void reg16_write(I80286* cpu, uint8_t reg, uint16_t v) {
	cpu->registers[reg & 0x7].u.r16 = v;
}

/* Mod R/M */
static uint16_t modrm_get_base_offset(I80286* cpu) {
	switch (cpu->modrm.u.bits.rm) {
		case 0b000: /* base rel indexed - BX + SI */
			return (BX + SI);
		case 0b001: /* base rel indexed - BX + DI */
			return (BX + DI);
		case 0b010: /* base rel indexed stack - BP + SI */
			return (BP + SI);
		case 0b011: /* base rel indexed stack - BP + DI */
			return (BP + DI);
		case 0b100: /* implied SI */
			return SI;
		case 0b101: /* implied DI */
			return DI;
		case 0b110: /* implied BP */
			return BP;
		case 0b111: /* implied BX */
			return BX;
	}
	return 0;
}
static uint16_t modrm_get_segment(I80286* cpu) {
	if (cpu->segment_prefix != 0xFF) {
		return cpu->segments[cpu->segment_prefix & 0x3].selector; /* CS/DS/ES/SS override */
	}
	else {
		/* mod = 00 special case for r/m=110 -> [disp16] uses DS */
		if (cpu->modrm.u.bits.mod == 0b00 && cpu->modrm.u.bits.rm == 0b110) {
			return cpu->segments[SEG_DS].selector;
		}
		else {
			switch (cpu->modrm.u.bits.rm) {
				case 0b010: /* [BP+SI] */
				case 0b011: /* [BP+DI] */
				case 0b110: /* [BP] (mod != 00) */
					return cpu->segments[SEG_SS].selector; /* defaults to SS */
					break;
				default:
					return cpu->segments[SEG_DS].selector; /* everything else defaults to DS */
					break;
			}
		}
	}
}
static int modrm_get_offset(I80286* cpu, uint16_t* offset) {
	switch (cpu->modrm.u.bits.mod) {
		case 0b00:
			if (cpu->modrm.u.bits.rm == 0b110) {
				return fetch_word(cpu, offset);
			}
			else {
				*offset = modrm_get_base_offset(cpu);
				return 1;
			}

		case 0b01: {
			uint8_t disp8 = 0;
			if (!fetch_byte(cpu, &disp8)) {
				return 0;
			}
			*offset = (modrm_get_base_offset(cpu) + (int8_t)disp8) & 0xFFFF;
			return 1;
		}

		case 0b10: {
			uint16_t disp16 = 0;
			if (!fetch_word(cpu, &disp16)) {
				return 0;
			}
			*offset = (modrm_get_base_offset(cpu) + (int16_t)disp16) & 0xFFFF;
			return 1;
		}

		/* case 0b11: register mode never calls this */
		case 0b11:
		default:
			*offset = 0;
			return 1;
	}
}

static int modrm_get_rm8(I80286* cpu, OPERAND8* op8) {
	if (cpu->modrm.u.bits.mod == 0b11) {
		op8->is_reg = 1;
		op8->u.reg_index = cpu->modrm.u.bits.rm;
		return 1;
	}
	else {
		op8->is_reg = 0;
		op8->u.mem.segment = modrm_get_segment(cpu);
		return modrm_get_offset(cpu, &op8->u.mem.offset);
	}
}

static int  modrm_read_rm8(I80286* cpu, OPERAND8 op8, uint8_t* value) {
	if (op8.is_reg) {
		*value = reg8_read(cpu, op8.u.reg_index);
		return 1;
	}
	else {
		return read_byte(cpu, op8.u.mem.segment, op8.u.mem.offset, value);
	}
}
static int  modrm_write_rm8(I80286* cpu, OPERAND8 op8, uint8_t value) {
	if (op8.is_reg) {
		reg8_write(cpu, op8.u.reg_index, value);
		return 1;
	}
	else {
		return write_byte(cpu, op8.u.mem.segment, op8.u.mem.offset, value);
	}
}

static uint8_t modrm_read_reg8(I80286* cpu) {
	return reg8_read(cpu, cpu->modrm.u.bits.reg);
}
static void modrm_write_reg8(I80286* cpu, uint8_t value) {
	reg8_write(cpu, cpu->modrm.u.bits.reg, value);
}

static int modrm_get_rm16(I80286* cpu, OPERAND16* op16) {
	if (cpu->modrm.u.bits.mod == 0b11) {
		op16->is_reg = 1;
		op16->u.reg_index = cpu->modrm.u.bits.rm;
		return 1;
	}
	else {
		op16->is_reg = 0;
		op16->u.mem.segment = modrm_get_segment(cpu);
		return modrm_get_offset(cpu, &op16->u.mem.offset);
	}
}

static int modrm_read_rm16(I80286* cpu, OPERAND16 op16, uint16_t* value) {
	if (op16.is_reg) {
		*value = reg16_read(cpu, op16.u.reg_index);
		return 1;
	}
	else {
		return read_word(cpu, op16.u.mem.segment, op16.u.mem.offset, value);
	}
}
static int modrm_write_rm16(I80286* cpu, OPERAND16 op16, uint16_t value) {
	if (op16.is_reg) {
		reg16_write(cpu, op16.u.reg_index, value);
		return 1;
	}
	else {
		return write_word(cpu, op16.u.mem.segment, op16.u.mem.offset, value);
	}
}

static uint16_t modrm_read_reg16(I80286* cpu) {
	return reg16_read(cpu, cpu->modrm.u.bits.reg);
}
static void modrm_write_reg16(I80286* cpu, uint16_t value) {
	reg16_write(cpu, cpu->modrm.u.bits.reg, value);
}

static int fetch_modrm(I80286* cpu) {
	cpu->modrm.u.byte = 0;
	if (!fetch_byte(cpu, &cpu->modrm.u.byte)) {
		return 0;
	}
	return 1;
}

static int exec_bin_op8(I80286* cpu, void (*op)(I80286*, uint8_t*, uint8_t)) {
	OPERAND8 rm = { 0 };
	uint8_t reg = 0;
	uint8_t tmp = 0;

	if (!modrm_get_rm8(cpu, &rm)) {
		return 0;
	}

	reg = modrm_read_reg8(cpu);
	
	if (!modrm_read_rm8(cpu, rm, &tmp)) {
		return 0;
	}
	
	if (D) {
		op(cpu, &reg, tmp);
		modrm_write_reg8(cpu, reg);
	}
	else {
		op(cpu, &tmp, reg);
		modrm_write_rm8(cpu, rm, tmp);
	}
	return 1;
}
static int exec_bin_op8_ro(I80286* cpu, void (*op)(I80286*, uint8_t, uint8_t)) {
	OPERAND8 rm = { 0 };
	uint8_t reg = 0;
	uint8_t tmp = 0;

	if (!modrm_get_rm8(cpu, &rm)) {
		return 0;
	}

	reg = modrm_read_reg8(cpu);
	
	if (!modrm_read_rm8(cpu, rm, &tmp)) {
		return 0;
	}
	
	if (D) {
		op(cpu, reg, tmp);
	}
	else {
		op(cpu, tmp, reg);
	}
	return 1;
}

static int exec_bin_rm_imm_op8(I80286* cpu, void (*op)(I80286*, uint8_t*, uint8_t)) {
	OPERAND8 rm = { 0 };
	uint8_t imm = 0;
	uint8_t tmp = 0;

	if (!modrm_get_rm8(cpu, &rm)) {
		return 0;
	}

	if (!fetch_byte(cpu, &imm)) {
		return 0;
	}

	if (!modrm_read_rm8(cpu, rm, &tmp)) {
		return 0;
	}

	op(cpu, &tmp, imm);
	modrm_write_rm8(cpu, rm, tmp);
	return 1;
}
static int exec_bin_rm_imm_op8_ro(I80286* cpu, void (*op)(I80286*, uint8_t, uint8_t)) {
	OPERAND8 rm = { 0 };
	uint8_t imm = 0;
	uint8_t tmp = 0;
	
	if (!modrm_get_rm8(cpu, &rm)) {
		return 0;
	}

	if (!fetch_byte(cpu, &imm)) {
		return 0;
	}

	if (!modrm_read_rm8(cpu, rm, &tmp)) {
		return 0;
	}

	op(cpu, tmp, imm);
	return 1;
}

static int exec_bin_op16(I80286* cpu, void (*op)(I80286*, uint16_t*, uint16_t)) {
	OPERAND16 rm = { 0 };
	uint16_t reg = 0;
	uint16_t tmp = 0;

	if (!modrm_get_rm16(cpu, &rm)) {
		return 0;
	}

	reg = modrm_read_reg16(cpu);

	if (!modrm_read_rm16(cpu, rm, &tmp)) {
		return 0;
	}

	if (D) {
		op(cpu, &reg, tmp);
		modrm_write_reg16(cpu, reg);
	}
	else {
		op(cpu, &tmp, reg);
		modrm_write_rm16(cpu, rm, tmp);
	}
	return 1;
}
static int exec_bin_op16_ro(I80286* cpu, void (*op)(I80286*, uint16_t, uint16_t)) {
	OPERAND16 rm = { 0 };
	uint16_t reg = 0;
	uint16_t tmp = 0;

	if (!modrm_get_rm16(cpu, &rm)) {
		return 0;
	}

	reg = modrm_read_reg16(cpu);

	if (!modrm_read_rm16(cpu, rm, &tmp)) {
		return 0;
	}
	
	if (D) {
		op(cpu, reg, tmp);
	}
	else {
		op(cpu, tmp, reg);
	}
	return 1;
}

static int exec_bin_rm_imm_op16(I80286* cpu, void (*op)(I80286*, uint16_t*, uint16_t)) {
	OPERAND16 rm = { 0 };
	uint16_t imm = 0;
	uint16_t tmp = 0;
		
	if (!modrm_get_rm16(cpu, &rm)) {
		return 0;
	}

	if (!fetch_word(cpu, &imm)) {
		return 0;
	}

	if (!modrm_read_rm16(cpu, rm, &tmp)) {
		return 0;
	}

	op(cpu, &tmp, imm);
	modrm_write_rm16(cpu, rm, tmp);
	return 1;
}
static int exec_bin_rm_imm_op16_ro(I80286* cpu, void (*op)(I80286*, uint16_t, uint16_t)) {
	OPERAND16 rm = { 0 };
	uint16_t imm = 0;
	uint16_t tmp = 0;

	if (!modrm_get_rm16(cpu, &rm)) {
		return 0;
	}

	if (!fetch_word(cpu, &imm)) {
		return 0;
	}

	if (!modrm_read_rm16(cpu, rm, &tmp)) {
		return 0;
	}

	op(cpu, tmp, imm);
	return 1;
}

static int exec_bin_rm_imm8_op16(I80286* cpu, void (*op)(I80286*, uint16_t*, uint16_t)) {
	OPERAND16 rm = { 0 };
	uint8_t imm = 0;
	uint16_t tmp = 0;
	uint16_t se = 0;

	if (!modrm_get_rm16(cpu, &rm)) {
		return 0;
	}

	if (!fetch_byte(cpu, &imm)) {
		return 0;
	}

	if (!modrm_read_rm16(cpu, rm, &tmp)) {
		return 0;
	}

	se = sign_extend8_16(imm);
	op(cpu, &tmp, se);
	modrm_write_rm16(cpu, rm, tmp);
	return 1;
}
static int exec_bin_rm_imm8_op16_ro(I80286* cpu, void (*op)(I80286*, uint16_t, uint16_t)) {
	OPERAND16 rm = { 0 };
	uint8_t imm = 0;
	uint16_t tmp = 0;
	uint16_t se = 0;

	if (!modrm_get_rm16(cpu, &rm)) {
		return 0;
	}

	if (!fetch_byte(cpu, &imm)) {
		return 0;
	}

	if (!modrm_read_rm16(cpu, rm, &tmp)) {
		return 0;
	}

	se = sign_extend8_16(imm);
	op(cpu, tmp, se);
	return 1;
}

static int push_op16(I80286* cpu, OPERAND16 op16) {
	uint16_t tmp = 0;
	if(!modrm_read_rm16(cpu, op16, &tmp)) {
		return 0;
	}
	SP -= 2;
	return write_word(cpu, SS, SP, tmp);
}
static int push_word(I80286* cpu, uint16_t value) {
	SP -= 2;
	return write_word(cpu, SS, SP, value);
}

static int pop_op16(I80286* cpu, OPERAND16 op16) {
	uint16_t tmp = 0;
	if (!read_word(cpu, SS, SP, &tmp)) {
		return 0;
	}
	SP += 2;
	return modrm_write_rm16(cpu, op16, tmp);
}
static int pop_word(I80286* cpu, uint16_t* value) {
	if (!read_word(cpu, SS, SP, value)) {
		return 0;		
	}
	SP += 2;
	return 1;
}

void i80286_intr(I80286* cpu, uint8_t type) {
	if (!INTR) {
		INTR = 1;
		cpu->intr_type = type;
	}
}
void i80286_nmi(I80286* cpu) {
	NMI = 1;
}
void i80286_int(I80286* cpu, uint8_t type) {
	push_word(cpu, PSW);
	push_word(cpu, CS);
	push_word(cpu, IP);
	uint16_t offset = type * 4;

	read_word(cpu, 0x0000, offset, &IP);
	read_word(cpu, 0x0000, offset + 2, &CS);
	IF = 0;
	TF = 0;
}
void i80286_exception(I80286* cpu, uint8_t exception) {
	IP -= cpu->instruction_len;
	i80286_int(cpu, exception);
}
static void i80286_check_interrupts(I80286* cpu) {
	if (cpu->int_delay == 1) {
		cpu->int_delay = 0;
		return;
	}

	if (NMI) {
		/* Non-Maskable int */
		NMI = 0;
		i80286_int(cpu, INT_NMI);
		cpu->halt = 0; /* stop halting */
	}
	else if (INTR && cpu->int_latch) {
		/* Hardware int; INTR is masked by IF */
		INTR = 0;
		i80286_int(cpu, cpu->intr_type);
		cpu->halt = 0; /* stop halting */
	}

	if (cpu->tf_latch) {
		/* Trap int */
		i80286_int(cpu, INT_TRAP); 
		cpu->halt = 0; /* stop halting */
	}

	/* latch int flag for next cycle */
	cpu->int_latch = IF;

	/* latch trap flag for next cycle */
	cpu->tf_latch = TF;
}

static void i80286_check_halt(I80286* cpu) {
	if (cpu->halt) {
		IP -= cpu->instruction_len; /* continue halting */
	}
}

static int modrm_get_seg_index(I80286* cpu, uint8_t* sr) {
	if (cpu->modrm.u.bits.reg > 3) {
		i80286_exception(cpu, EXCEPTION_UD);
		return 0; /* error - 286 doesnt wrap segment registers like the 8086 */
	}
	*sr = cpu->modrm.u.bits.reg;
	return 1;
}

/* Opcodes */

static void add_rm_imm(I80286* cpu) {
	/* add r/m, imm (80/81/82/83, R/M reg = b000) b100000SW */
	if (W) {
		if (S) {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			exec_bin_rm_imm8_op16(cpu, i80286_alu_add16);
		}
		else {
			/* reg16, disp16 - 0x81 */
			exec_bin_rm_imm_op16(cpu, i80286_alu_add16);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		exec_bin_rm_imm_op8(cpu, i80286_alu_add8);
	}
}
static void add_rm_reg(I80286* cpu) {
	/* add r/m, reg (00/01/02/03) b000000DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		exec_bin_op16(cpu, i80286_alu_add16);
	}
	else {
		exec_bin_op8(cpu, i80286_alu_add8);
	}
}
static void add_accum_imm(I80286* cpu) {
	/* add AL/AX, imm (04/05) b0000010W */
	if (W) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		i80286_alu_add16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		i80286_alu_add8(cpu, &AL, imm);
	}
}

static void or_rm_imm(I80286* cpu) {
	/* or r/m, imm (80/81/82/83, R/M reg = b001) b100000SW */
	if (W) {
		if (S) {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			exec_bin_rm_imm8_op16(cpu, i80286_alu_or16);
		}
		else {
			/* reg16, disp16 - 0x81 */
			exec_bin_rm_imm_op16(cpu, i80286_alu_or16);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		exec_bin_rm_imm_op8(cpu, i80286_alu_or8);
	}
}
static void or_rm_reg(I80286* cpu) {
	/* or r/m, reg (08/0A/09/0B) b000010DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		exec_bin_op16(cpu, i80286_alu_or16);
	}
	else {
		exec_bin_op8(cpu, i80286_alu_or8);
	}
}
static void or_accum_imm(I80286* cpu) {
	/* or AL/AX, imm (0C/0D) b0000110W */
	if (W) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		i80286_alu_or16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		i80286_alu_or8(cpu, &AL, imm);
	}
}

static void adc_rm_imm(I80286* cpu) {
	/* adc r/m, imm (80/81/82/83, R/M reg = b010) b100000SW */
	if (W) {
		if (S) {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			exec_bin_rm_imm8_op16(cpu, i80286_alu_adc16);
		}
		else {
			/* reg16, disp16 - 0x81 */
			exec_bin_rm_imm_op16(cpu, i80286_alu_adc16);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		exec_bin_rm_imm_op8(cpu, i80286_alu_adc8);
	}
}
static void adc_rm_reg(I80286* cpu) {
	/* adc r/m, reg (10/12/11/13) b000100DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		exec_bin_op16(cpu, i80286_alu_adc16);
	}
	else {
		exec_bin_op8(cpu, i80286_alu_adc8);
	}
}
static void adc_accum_imm(I80286* cpu) {
	/* adc AL/AX, imm (14/15) b0001010W */
	if (W) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		i80286_alu_adc16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		i80286_alu_adc8(cpu, &AL, imm);
	}
}

static void sbb_rm_imm(I80286* cpu) {
	/* sbb r/m, imm (80/81/82/83, R/M reg = b011)  b100000SW */
	if (W) {
		if (S) {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			exec_bin_rm_imm8_op16(cpu, i80286_alu_sbb16);
		} 
		else {
			/* reg16, disp16 - 0x81 */
			exec_bin_rm_imm_op16(cpu, i80286_alu_sbb16);
		} 
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		exec_bin_rm_imm_op8(cpu, i80286_alu_sbb8);
	}
}
static void sbb_rm_reg(I80286* cpu) {
	/* sbb r/m, reg (18/1A/19/1B) b000110DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		exec_bin_op16(cpu, i80286_alu_sbb16);
	}
	else {
		exec_bin_op8(cpu, i80286_alu_sbb8);
	}
}
static void sbb_accum_imm(I80286* cpu) {
	/* sbb AL/AX, imm (1C/1D) b0001110W */
	if (W) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		i80286_alu_sbb16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		i80286_alu_sbb8(cpu, &AL, imm);
	}
}

static void and_rm_imm(I80286* cpu) {
	/* and r/m, imm (80/81/82/83, R/M reg = b100) b100000SW */
	if (W) {
		if (S) {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			exec_bin_rm_imm8_op16(cpu, i80286_alu_and16);
		}
		else {
			/* reg16, disp16 - 0x81 */
			exec_bin_rm_imm_op16(cpu, i80286_alu_and16);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		exec_bin_rm_imm_op8(cpu, i80286_alu_and8);
	}
}
static void and_rm_reg(I80286* cpu) {
	/* and r/m, reg (20/22/21/23) b001000DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		exec_bin_op16(cpu, i80286_alu_and16);
	}
	else {
		exec_bin_op8(cpu, i80286_alu_and8);
	}
}
static void and_accum_imm(I80286* cpu) {
	/* and AL/AX, imm (24/25) b0010010W */
	if (W) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		i80286_alu_and16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		i80286_alu_and8(cpu, &AL, imm);
	}
}

static void sub_rm_imm(I80286* cpu) {
	/* sub r/m, imm (80/81, R/M reg = b101) b100000SW */
	if (W) {
		if (S) {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			exec_bin_rm_imm8_op16(cpu, i80286_alu_sub16);
		}
		else {
			/* reg16, disp16 - 0x81 */
			exec_bin_rm_imm_op16(cpu, i80286_alu_sub16);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		exec_bin_rm_imm_op8(cpu, i80286_alu_sub8);
	}
}
static void sub_rm_reg(I80286* cpu) {
	/* sub r/m, reg (28/2A/29/2B) b001010DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		exec_bin_op16(cpu, i80286_alu_sub16);
	}
	else {
		exec_bin_op8(cpu, i80286_alu_sub8);
	}
}
static void sub_accum_imm(I80286* cpu) {
	/* sub AL/AX, imm (2C/2D) b0010110W */
	if (W) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		i80286_alu_sub16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		i80286_alu_sub8(cpu, &AL, imm);
	}
}

static void xor_rm_imm(I80286* cpu) {
	/* xor r/m, imm (80/81/82/83, R/M reg = b110) b100000SW */
	if (W) {
		if (S) {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			exec_bin_rm_imm8_op16(cpu, i80286_alu_xor16);
		}
		else {
			/* reg16, disp16 - 0x81 */
			exec_bin_rm_imm_op16(cpu, i80286_alu_xor16);
		}
	} 
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		exec_bin_rm_imm_op8(cpu, i80286_alu_xor8);
	}
}
static void xor_rm_reg(I80286* cpu) {
	/* xor r/m, reg (30/32/31/33) b001100DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		exec_bin_op16(cpu, i80286_alu_xor16);
	}
	else {
		exec_bin_op8(cpu, i80286_alu_xor8);
	}
}
static void xor_accum_imm(I80286* cpu) {
	/* xor AL/AX, imm (34/35) b0011010W */
	if (W) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		i80286_alu_xor16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		i80286_alu_xor8(cpu, &AL, imm);
	}
}

static void cmp_rm_imm(I80286* cpu) {
	/* cmp r/m, imm (80/81/82/83, R/M reg = b111)  b100000SW */
	if (W) {
		if (S) {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			exec_bin_rm_imm8_op16_ro(cpu, i80286_alu_cmp16);
		}
		else {
			/* reg16, disp16 - 0x81 */
			exec_bin_rm_imm_op16_ro(cpu, i80286_alu_cmp16);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		exec_bin_rm_imm_op8_ro(cpu, i80286_alu_cmp8);
	}
}
static void cmp_rm_reg(I80286* cpu) {
	/* cmp r/m, reg (38/39/3A/3B) b001110DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		exec_bin_op16_ro(cpu, i80286_alu_cmp16);
	}
	else {
		exec_bin_op8_ro(cpu, i80286_alu_cmp8);
	}
}
static void cmp_accum_imm(I80286* cpu) {
	/* cmp AL/AX, imm (3C/3D) b0011110W */
	if (W) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		i80286_alu_cmp16(cpu, AX, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		i80286_alu_cmp8(cpu, AL, imm);
	}
}

static void test_rm_imm(I80286* cpu) {
	/* test r/m, imm (F6/F7, R/M reg = b000) b1111011W */
	if (W) {
		exec_bin_rm_imm_op16_ro(cpu, i80286_alu_test16);
	}
	else {
		exec_bin_rm_imm_op8_ro(cpu, i80286_alu_test8);
	}
}
static void test_rm_reg(I80286* cpu) {
	/* test r/m, reg (84/85) b1000010W */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		exec_bin_op16_ro(cpu, i80286_alu_test16);
	}
	else {
		exec_bin_op8_ro(cpu, i80286_alu_test8);
	}
}
static void test_accum_imm(I80286* cpu) {
	/* test AL/AX, imm (A8/A9) b1010100W */
	if (W) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		i80286_alu_test16(cpu, AX, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		i80286_alu_test8(cpu, AL, imm);
	}
}

static void daa(I80286* cpu) {
	/* Decimal Adjust for Addition (27) b00100111 */
	i80286_alu_daa(cpu, &AL);
}
static void das(I80286* cpu) {
	/* Decimal Adjust for Subtraction (2F) b00101111 */
	i80286_alu_das(cpu, &AL);
}
static void aaa(I80286* cpu) {
	/* ASCII Adjust for Addition (37) b00110111 */
	i80286_alu_aaa(cpu, &AL, &AH);
}
static void aas(I80286* cpu) {
	/* ASCII Adjust for Subtraction (3F) b00111111 */
	i80286_alu_aas(cpu, &AL, &AH);
}
static void aam(I80286* cpu) {
	/* ASCII Adjust for Multiply (D4 0A) b11010100 00001010 */
	uint8_t divisor = 0;
	if (!fetch_byte(cpu, &divisor)) {
		return;
	} /* undocumented operand; normally 0x0A */
	i80286_alu_aam(cpu, &AL, &AH, divisor);
}
static void aad(I80286* cpu) {
	/* ASCII Adjust for Division (D5 0A) b11010101 00001010 */
	uint8_t divisor = 0;
	if (!fetch_byte(cpu, &divisor)) {
		return;
	} /* undocumented operand; normally 0x0A */
	i80286_alu_aad(cpu, &AL, &AH, divisor);
}
static void salc(I80286* cpu) {
	/* set carry in AL (D6) b11010110 undocumented opcode */
	if (CF) {
		AL = 0xFF;
	}
	else {
		AL = 0;
	}
}

static void push_seg(I80286* cpu) {
	/* Push seg16 (06/0E/16/1E) b000SR110 */
	push_word(cpu, cpu->segments[SR].selector);
}
static void pop_seg(I80286* cpu) {
	/* Pop seg16 (07/0F/17/1F) b000SR111 */
	pop_word(cpu, &cpu->segments[SR].selector);

	/* Interrupts Following 'POP SS' May Corrupt Memory. On early Intel 8088 processors
		(marked "INTEL '78" or "(C) 1978"), if an interrupt occurs immediately after a
		'POP SS' instruction, data may be pushed using an incorrect stack address,
		resulting in memory corruption. */
	cpu->int_delay = 1;
}
static void push_reg(I80286* cpu) {
	/* Push reg16 (50-57) b01010REG */

	/* The 80286 PUSH SP instruction pushes
	   the value of SP as it existed before the
	   instruction. This differs from the 8086. */

	uint16_t tmp = reg16_read(cpu, cpu->opcode);
	SP -= 2;
	write_word(cpu, SS, SP, tmp);
}
static void pop_reg(I80286* cpu) {
	/* Pop reg16 (58-5F) b01011REG */
	uint16_t tmp = 0;
	read_word(cpu, SS, SP, &tmp);
	SP += 2;
	reg16_write(cpu, cpu->opcode, tmp);
}
static void push_rm(I80286* cpu) {
	/* Push R/M (FF, R/M reg = 110) b11111111 */
	OPERAND16 rm = { 0 };
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}
	push_op16(cpu, rm);
}
static void pop_rm(I80286* cpu) {
	/* Pop R/M (8F) b10001111 */
	if (!fetch_modrm(cpu)) {
		return;
	}

	if (cpu->modrm.u.bits.reg != 0) {
		i80286_exception(cpu, EXCEPTION_UD); /* reg bits are reserved */
		return;
	}

	OPERAND16 rm = { 0 };
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}
	pop_op16(cpu, rm);
}
static void pushf(I80286* cpu) {
	/* push psw (9C) b10011100 */
	PSW &= 0xFFD7;
	push_word(cpu, PSW);
}
static void popf(I80286* cpu) {
	/* pop psw (9D) b10011101 */
	uint16_t psw = 0;
	pop_word(cpu, &psw);
	PSW = (psw | 0x0002) & 0x0FD7;
}
static void pusha(I80286* cpu) {
	/* push all (60) b01100000 */
	uint16_t sp = SP;

	if (((sp - 0x10) & 0xFFFF) == 0xFFFF) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	push_word(cpu, AX);
	push_word(cpu, CX);
	push_word(cpu, DX);
	push_word(cpu, BX);
	push_word(cpu, sp); /* Push OLD sp */
	push_word(cpu, BP);
	push_word(cpu, SI);
	push_word(cpu, DI);
}
static void popa(I80286* cpu) {
	/* pop all (61) b01100001 */
	uint16_t sp = SP;

	if (sp == 0xFFFF || sp + 0xE == 0xFFFF) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	pop_word(cpu, &DI);
	pop_word(cpu, &SI);
	pop_word(cpu, &BP);
	pop_word(cpu, &sp); /* discard sp */
	pop_word(cpu, &BX);
	pop_word(cpu, &DX);
	pop_word(cpu, &CX);
	pop_word(cpu, &AX);
}
static void push_imm(I80286* cpu) {
	/* Push IMM (68/6A) b011010S0 */

	if (S) { /* sign extended to 16bit */
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		uint16_t se = sign_extend8_16(imm);
		push_word(cpu, se);
	}
	else {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		push_word(cpu, imm);
	}
}

static void enter(I80286* cpu) {
	/* enter procedure (C8) b11001000 */

	uint16_t op1 = 0;
	uint8_t op2 = 0;

	if (!fetch_word(cpu, &op1)) {
		return;
	}

	if (!fetch_byte(cpu, &op2)) {
		return;
	}

	uint16_t bp = BP;
	uint16_t sp = SP;

	uint8_t level = op2 % 32;

	sp -= 2;
	if (!write_word(cpu, SS, sp, bp)) {
		return;
	}

	uint16_t frame_ptr = sp;
	uint16_t stack_word = 0;

	if (level > 0) {
		for (int i = 0; i < level - 1; ++i) {
			bp -= 2;
			if (!read_word(cpu, SS, bp, &stack_word)) {
				return;
			}

			sp -= 2;
			if (!write_word(cpu, SS, sp, stack_word)) {
				return;
			}
		}

		sp -= 2;
		if (!write_word(cpu, SS, sp, frame_ptr)) {
			return;
		}
	}

	BP = frame_ptr;
	SP = sp - op1;
}
static void leave(I80286* cpu) {
	/* leave procedure (C9) b11001001 */

	if (BP == 0xFFFF) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	SP = BP;
	pop_word(cpu, &BP);
}

static void nop(I80286* cpu) {
	/* nop (90) b10010000 */
	(void)cpu;
}
static void xchg_accum_reg(I80286* cpu) {
	/* xchg AX, reg16 (91 - 97) b10010REG */	
	uint16_t tmp = AX;
	AX = reg16_read(cpu, cpu->opcode);
	reg16_write(cpu, cpu->opcode, tmp);
}
static void xchg_rm_reg(I80286* cpu) {
	/* xchg R/M, reg8/16 (86/87) b1000011W */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		OPERAND16 rm = { 0 };
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		uint16_t reg = modrm_read_reg16(cpu);
		uint16_t tmp = 0;
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		if (!modrm_write_rm16(cpu, rm, reg)) {
			return;
		}
		modrm_write_reg16(cpu, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		uint8_t reg = modrm_read_reg8(cpu);
		uint8_t tmp = 0;
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		if (!modrm_write_rm8(cpu, rm, reg)) {
			return;
		}
		modrm_write_reg8(cpu, tmp);
	}
}

static void cbw(I80286* cpu) {
	/* Convert byte to word (98) b10011000 */
	if (AL & 0x80) {
		AH = 0xFF;
	}
	else {
		AH = 0;
	}
}
static void cwd(I80286* cpu) {
	/* Convert word to dword (99) b10011001 */
	if (AX & 0x8000) {
		DX = 0xFFFF;
	}
	else {
		DX = 0;
	}
}

static void wait(I80286* cpu) {
	/* wait (9B) b10011011 */
	/*if (!cpu->test) {
		IP -= cpu->instruction_len;
	}*/
	(void)cpu;
}

static void sahf(I80286* cpu) {
	/* Store AH into flags (9E) b10011110 */
	PSW &= 0xFF02; /* Mask hi byte; Clear bit 2 */
	PSW |= AH & 0xD5;
}
static void lahf(I80286* cpu) {
	/* Load flags into AH (9F) b10011111 */
	AH = PSW & 0xD7;
}

static void hlt(I80286* cpu) {
	/* Halt CPU (F4) b11110100 */
	cpu->halt = 1;
	//IP -= cpu->instruction_len;
}
static void cmc(I80286* cpu) {
	/* Complement carry flag (F5) b11110101 */
	CF = !CF;
}
static void clc(I80286* cpu) {
	/* clear carry flag (F8) b11111000 */
	CF = 0;
}
static void stc(I80286* cpu) {
	/* set carry flag (F9) b11111001 */
	CF = 1;
}
static void cli(I80286* cpu) {
	/* clear interrupt flag (FA) b11111010 */
	IF = 0;
}
static void sti(I80286* cpu) {
	/* set interrupt flag (FB) b1111011 */
	IF = 1;
}
static void cld(I80286* cpu) {
	/* clear direction flag (FC) b11111100 */
	DF = 0;
}
static void std(I80286* cpu) {
	/* set direction flag (FD) b11111101 */
	DF = 1;
}

static void inc_reg(I80286* cpu) {
	/* Inc reg16 (40-47) b01000REG */
	uint16_t tmp = reg16_read(cpu, cpu->opcode);
	i80286_alu_inc16(cpu, &tmp);
	reg16_write(cpu, cpu->opcode, tmp);
}
static void inc_rm(I80286* cpu) {
	/* Inc R/M (FE/FF, R/M reg = 000) b1111111W */
	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_inc16(cpu, &tmp);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_inc8(cpu, &tmp);
		modrm_write_rm8(cpu, rm, tmp);
	}
}

static void dec_reg(I80286* cpu) {
	/* Dec reg16 (48-4F) b01001REG */
	uint16_t tmp = reg16_read(cpu, cpu->opcode);
	i80286_alu_dec16(cpu, &tmp);
	reg16_write(cpu, cpu->opcode, tmp);
}
static void dec_rm(I80286* cpu) {
	/* Dec R/M (FE/FF, R/M reg = 001) b1111111W */
	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_dec16(cpu, &tmp);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_dec8(cpu, &tmp);
		modrm_write_rm8(cpu, rm, tmp);
	}
}

static void rol(I80286* cpu) {
	/* Rotate left (D0/D1/D2/D3, R/M reg = 000) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rol16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rol8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void ror(I80286* cpu) {
	/* Rotate left (D0/D1/D2/D3, R/M reg = 001) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_ror16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_ror8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void rcl(I80286* cpu) {
	/* Rotate through carry left (D0/D1/D2/D3, R/M reg = 010) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rcl16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rcl8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void rcr(I80286* cpu) {
	/* Rotate through carry right (D0/D1/D2/D3, R/M reg = 011) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rcr16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rcr8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void shl(I80286* cpu) {
	/* Shift left (D0/D1/D2/D3, R/M reg = 100) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_shl16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_shl8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void shr(I80286* cpu) {
	/* Shift Logical right (D0/D1/D2/D3, R/M reg = 101) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_shr16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_shr8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void sal(I80286* cpu) {
	/* Shift Arithmetic left (D0/D1/D2/D3, R/M reg = 110) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_sal16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_sal8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void sar(I80286* cpu) {
	/* Shift Arithmetic right (D0/D1/D2/D3, R/M reg = 111) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_sar16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_sar8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}

static void rol_rm_imm(I80286* cpu) {
	/* Rotate left (C0/C1, R/M reg = 000) b1100000W */
	uint8_t count = 0;

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rol16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rol8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void ror_rm_imm(I80286* cpu) {
	/* Rotate left (C0/C1, R/M reg = 001) b1100000W */
	uint8_t count = 0;

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_ror16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_ror8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void rcl_rm_imm(I80286* cpu) {
	/* Rotate through carry left (C0/C1, R/M reg = 010) b1100000W */
	uint8_t count = 0;

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rcl16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rcl8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void rcr_rm_imm(I80286* cpu) {
	/* Rotate through carry right (C0/C1, R/M reg = 011) b1100000W */
	uint8_t count = 0;

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rcr16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_rcr8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void shl_rm_imm(I80286* cpu) {
	/* Shift left (C0/C1, R/M reg = 100) b1100000W */
	uint8_t count = 0;

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_shl16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_shl8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void shr_rm_imm(I80286* cpu) {
	/* Shift Logical right (C0/C1, R/M reg = 101) b1100000W */
	uint8_t count = 0;

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_shr16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_shr8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void sal_rm_imm(I80286* cpu) {
	/* Shift Arithmetic left (C0/C1, R/M reg = 110) b1100000W */
	uint8_t count = 0;

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_sal16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_sal8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void sar_rm_imm(I80286* cpu) {
	/* Shift Arithmetic right (C0/C1, R/M reg = 111) b1100000W */
	uint8_t count = 0;

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_sar16(cpu, &tmp, count);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!fetch_byte(cpu, &count)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_sar8(cpu, &tmp, count);
		modrm_write_rm8(cpu, rm, tmp);
	}
}

static int jump_condition(I80286* cpu) {
	switch (CCCC) {
		case JCC_JO:
			if (OF) return 1;
			break;
		case JCC_JNO:
			if (!OF) return 1;
			break;
		case JCC_JC:
			if (CF) return 1;
			break;
		case JCC_JNC:
			if (!CF) return 1;
			break;
		case JCC_JZ:
			if (ZF) return 1;
			break;
		case JCC_JNZ:
			if (!ZF) return 1;
			break;
		case JCC_JBE:
			if (CF || ZF) return 1;
			break;
		case JCC_JA:
			if (!CF && !ZF) return 1;
			break;
		case JCC_JS:
			if (SF) return 1;
			break;
		case JCC_JNS:
			if (!SF) return 1;
			break;
		case JCC_JPE:
			if (PF) return 1;
			break;
		case JCC_JPO:
			if (!PF) return 1;
			break;
		case JCC_JL:
			if (SF != OF) return 1;
			break;
		case JCC_JGE:
			if (SF == OF) return 1;
			break;
		case JCC_JLE:
			if (ZF || SF != OF) return 1;
			break;
		case JCC_JG:
			if (!ZF && SF == OF) return 1;
			break;
	}
	return 0;
}
static void jcc(I80286* cpu) {
	/* conditional jump(70-7F) b011XCCCC
	   8086 cpu decode 60-6F the same as 70-7F */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	if (jump_condition(cpu)) {
		uint16_t offset = sign_extend8_16(imm);
		IP += offset;
	}
}
static void jcxz(I80286* cpu) {
	/* jump if CX zero (E3) b11100011 */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	if (CX == 0) {
		uint16_t offset = sign_extend8_16(imm);
		IP += offset;
	}
}

static void jmp_intra_direct_short(I80286* cpu) {
	/* Jump short imm8 (EB) b11101011 */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	uint16_t se = sign_extend8_16(imm);
	IP += se;
}
static void jmp_intra_direct(I80286* cpu) {
	/* Jump near  imm16 (E9) b11101001 */
	uint16_t imm = 0;
	if (!fetch_word(cpu, &imm)) {
		return;
	}
	IP += imm;
}
static void jmp_inter_direct(I80286* cpu) {
	/* Jump far addr:seg (EA) b11101010 */
	uint16_t imm = 0;
	uint16_t imm2 = 0;
	if (!fetch_word(cpu, &imm)) {
		return;
	}
	if (!fetch_word(cpu, &imm2)) {
		return;
	}
	IP = imm;
	CS = imm2;
}

static void jmp_intra_indirect(I80286* cpu) {
	/* Jump near indirect (FF, R/M reg = 100) b11111111 */

	OPERAND16 rm = { 0 };
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}
	modrm_read_rm16(cpu, rm, &IP);
}
static void jmp_inter_indirect(I80286* cpu) {
	/* Jump far indirect (FF, R/M reg = 101) b11111111 */
	uint16_t segment = 0;
	uint16_t offset = 0;

	if (cpu->modrm.u.bits.mod == 0b11) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	segment = modrm_get_segment(cpu);
	if (!modrm_get_offset(cpu, &offset)) {
		return;
	}
	if (!read_word(cpu, segment, offset, &IP)) {
		return;
	}
	if (!read_word(cpu, segment, offset + 2, &CS)) {
		return;
	}
}

static void call_intra_direct(I80286* cpu) {
	/* Call disp (E8) b11101000 */
	uint16_t imm = 0;
	if (!fetch_word(cpu, &imm)) {
		return;
	}
	push_word(cpu, IP);
	IP += imm;
}
static void call_inter_direct(I80286* cpu) {
	/* Call addr:seg (9A) b10011010 */
	uint16_t imm = 0;
	uint16_t imm2 = 0;
	if (!fetch_word(cpu, &imm)) {
		return;
	}
	if (!fetch_word(cpu, &imm2)) {
		return;
	}
	push_word(cpu, CS);
	push_word(cpu, IP);
	IP = imm;
	CS = imm2;
}

static void call_intra_indirect(I80286* cpu) {
	/* Call near R/M (FF, R/M reg = 010) b11111111 */
	uint16_t ip = 0;
	OPERAND16 rm = { 0 };
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}
	if (!modrm_read_rm16(cpu, rm, &ip)) {
		return;
	}
	push_word(cpu, IP);

	IP = ip;
}
static void call_inter_indirect(I80286* cpu) {
	/* Call far R/M (FF, R/M reg = 011) b11111111 */
		
	uint16_t ip = 0;
	uint16_t cs = 0;
	uint16_t segment = 0;
	uint16_t offset = 0;

	if (cpu->modrm.u.bits.mod == 0b11) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	segment = modrm_get_segment(cpu);
	if (!modrm_get_offset(cpu, &offset)) {
		return;
	}
	if (!read_word(cpu, segment, offset, &ip)) {
		return;
	}
	if (!read_word(cpu, segment, offset + 2, &cs)) {
		return;
	}

	push_word(cpu, CS);
	push_word(cpu, IP);
	
	IP = ip;
	CS = cs;
}

static void ret_intra_add_imm(I80286* cpu) {
	/* retn imm16 (C2) b11000010 */
	uint16_t imm = 0;
	if (!fetch_word(cpu, &imm)) {
		return;
	}
	if (!pop_word(cpu, &IP)) {
		return;
	}
	SP += imm;
}
static void ret_intra(I80286* cpu) {
	/* retn (C3) b11000011 */
	if (!pop_word(cpu, &IP)) {
		return;
	}
}
static void ret_inter_add_imm(I80286* cpu) {
	/* retf imm16 (CA) b11001010 */

	uint16_t imm = 0;
	if (!fetch_word(cpu, &imm)) {
		return;
	}
	if (!pop_word(cpu, &IP)) {
		return;
	}
	if (!pop_word(cpu, &CS)) {
		return;
	}
	SP += imm;
}
static void ret_inter(I80286* cpu) {
	/* retf (CB) b11001011 */
	if (!pop_word(cpu, &IP)) {
		return;
	}
	if (!pop_word(cpu, &CS)) {
		return;
	}
}

static void mov_rm_imm(I80286* cpu) {
	/* mov r/m, imm (C6/C7) b1100011W */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (cpu->modrm.u.bits.reg != 0) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}
	if (W) {
		OPERAND16 rm = { 0 };
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		modrm_write_rm16(cpu, rm, imm);
	}
	else {
		OPERAND8 rm = { 0 };
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		modrm_write_rm8(cpu, rm, imm);
	}
}
static void mov_reg_imm(I80286* cpu) {
	/* mov r/m, reg (B0-BF) b1011WREG */
	if (WREG) {
		uint16_t imm = 0;
		if (!fetch_word(cpu, &imm)) {
			return;
		}
		reg16_write(cpu, cpu->opcode, imm);
	}
	else {
		uint8_t imm = 0;
		if (!fetch_byte(cpu, &imm)) {
			return;
		}
		reg8_write(cpu, cpu->opcode, imm);
	}
}
static void mov_rm_reg(I80286* cpu) {
	/* mov r/m, reg (88/89/8A/8B) b100010DW */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		OPERAND16 rm = { 0 };
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (D) {
			uint16_t tmp = 0;
			if (!modrm_read_rm16(cpu, rm, &tmp)) {
				return;
			}
			modrm_write_reg16(cpu, tmp);
		}
		else {
			uint16_t tmp = modrm_read_reg16(cpu);
			modrm_write_rm16(cpu, rm, tmp);
		}
	}
	else {
		OPERAND8 rm = { 0 };
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (D) {
			uint8_t tmp = 0;
			if (!modrm_read_rm8(cpu, rm, &tmp)) {
				return;
			}
			modrm_write_reg8(cpu, tmp);
		}
		else {
			uint8_t tmp = modrm_read_reg8(cpu);
			modrm_write_rm8(cpu, rm, tmp);
		}
	}
}
static void mov_accum_mem(I80286* cpu) {
	/* mov AL/AX, [mem] (A0/A1/A2/A3) b101000DW */
	uint16_t addr = 0;
	if (!fetch_word(cpu, &addr)) {
		return;
	}
	if (W) {
		if (D) {
			write_word(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), addr, AX);
		}
		else {
			read_word(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), addr, &AX);
		}
	}
	else {
		if (D) {
			write_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), addr, AL);
		}
		else {
			read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), addr, &AL);
		}
	}
}
static void mov_seg(I80286* cpu) {
	/* mov r/m, seg (8C/8E) b100011D0 */
	if (!fetch_modrm(cpu)) {
		return;
	}
	OPERAND16 rm = { 0 };
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}

	uint8_t sreg = 0;
	if (!modrm_get_seg_index(cpu, &sreg)) {
		return;
	}

	if (D) {
		if (sreg == SEG_CS) {
			/* illegal - mov cs, r/m */
			i80286_exception(cpu, EXCEPTION_UD); 
			return;
		}
		modrm_read_rm16(cpu, rm, &cpu->segments[sreg].selector);
	}
	else {
		modrm_write_rm16(cpu, rm, cpu->segments[sreg].selector);
	}
	
	if (D) {
		/* Interrupts Following 'MOV SS, XXX' May Corrupt Memory. On early Intel 8088 processors
			(marked "INTEL '78" or "(C) 1978"), if an interrupt occurs immediately after a 
			'MOV SS, XXX' instruction, data may be pushed using an incorrect stack address,
			resulting in memory corruption. */
		cpu->int_delay = 1;
	}
}

static void lea(I80286* cpu) {
	/* lea reg16, [r/m] (8D) b10001101 */
	if (!fetch_modrm(cpu)) {
		return;
	}

	if (cpu->modrm.u.bits.mod == 0b11) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	uint16_t offset = 0;
	if (!modrm_get_offset(cpu, &offset)) {
		return;
	}
	modrm_write_reg16(cpu, offset);
}

static void not(I80286* cpu) {
	/* not reg (F6/F7, R/M reg = b010) b1111011W */
	if (W) {
		OPERAND16 rm = { 0 };
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		uint16_t tmp = 0;
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_not16(cpu, &tmp);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		uint8_t tmp = 0;
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_not8(cpu, &tmp);
		modrm_write_rm8(cpu, rm, tmp);
	}
}
static void neg(I80286* cpu) {
	/* neg reg (F6/F7, R/M reg = b011) b1111011W */
	
	/* If the operand is zero, its sign is not changed.
	 Attempting to negate a byte containing -128 or 
	 a word containing -32,768 causes no change to 
	 the operand and sets OF. NEG updates AF, CF, OF,
	 PF, SF and ZF. CF is always set except when the
	 operand is zero, in which case it is cleared */

	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if(!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_neg16(cpu, &tmp);
		modrm_write_rm16(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if(!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_neg8(cpu, &tmp);
		modrm_write_rm8(cpu, rm, tmp);
	}
}

static void mul_rm(I80286* cpu) {
	/* mul r/m (F6/F7, R/M reg = b100) b1111011W */
	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		uint32_t product = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_mul16(cpu, AX, tmp, &product);
		DX = product >> 16;
		AX = product & 0xFFFF;
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		uint16_t product = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_mul8(cpu, AL, tmp, &product);
		AX = product;
	}
}
static void imul_rm(I80286* cpu) {
	/* imul r/m (F6/F7, R/M reg = b101) b1111011W */
	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		uint32_t product = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_imul16(cpu, AX, tmp, &product);
		DX = product >> 16;
		AX = product & 0xFFFF;
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		uint16_t product = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		i80286_alu_imul8(cpu, AL, tmp, &product);
		AX = product;
	}
}
static void imul_rm_imm(I80286* cpu) {
	/* imul reg,r/m,imm (69/6B, R/M reg = b101) b011010S1 */

	OPERAND16 rm = { 0 };
	uint16_t multiplicand = 0;
	uint32_t product = 0;
	uint16_t imm = 0;

	if (!fetch_modrm(cpu)) {
		return;
	}
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}
	if (S) {
		uint8_t imm2 = 0;
		if (!fetch_byte(cpu, &imm2)) {
			return;
		}

		imm = sign_extend8_16(imm2);
	}
	else {
		if (!fetch_word(cpu, &imm)) {
			return;
		}
	}

	if (!modrm_read_rm16(cpu, rm, &multiplicand)) {
		return;
	}
	i80286_alu_imul16(cpu, multiplicand, imm, &product);
	modrm_write_reg16(cpu, product & 0xFFFF);
}
static void div_rm(I80286* cpu) {
	/* div r/m (F6/F7, R/M reg = b110) b1111011W */
	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		uint32_t dividend = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		dividend = DX << 16 | AX;
		i80286_alu_div16(cpu, dividend, tmp, &AX, &DX);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		uint16_t dividend = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		dividend = AX;
		i80286_alu_div8(cpu, dividend, tmp, &AL, &AH);
	}
}
static void idiv_rm(I80286* cpu) {
	/* idiv r/m (F6/F7, R/M reg = b111) b1111011W */
	if (W) {
		OPERAND16 rm = { 0 };
		uint16_t tmp = 0;
		uint32_t dividend = 0;
		if (!modrm_get_rm16(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm16(cpu, rm, &tmp)) {
			return;
		}
		dividend = DX << 16 | AX;
		i80286_alu_idiv16(cpu, dividend, tmp, &AX, &DX);
	}
	else {
		OPERAND8 rm = { 0 };
		uint8_t tmp = 0;
		uint16_t dividend = 0;
		if (!modrm_get_rm8(cpu, &rm)) {
			return;
		}
		if (!modrm_read_rm8(cpu, rm, &tmp)) {
			return;
		}
		dividend = AX;
		i80286_alu_idiv8(cpu, AX, tmp, &AL, &AH);
	}
}

static void movs(I80286* cpu) {
	/* movs (A4/A5) b1010010W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return;
		}
		CX -= 1;
	}

	uint16_t adjustment = 0;
	if (DF) {
		adjustment = 0xFFFF;
	}
	else {
		adjustment = 1;
	}

	/* Do string operation */
	if (W) {
		uint16_t src = 0;
		if (read_word(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI, &src)) {
			if (write_word(cpu, ES, DI, src)) {
				/* Rep prefix check */
				if (F1) {
					IP -= cpu->instruction_len; /* Allow interrupts */
				}
			}
			else {
				if (F1) CX -= 1; /* Yes, the 80286 DECREMENTS CX another time if DI caused an exception. Why the fuck? */
			}
			DI += adjustment;
			DI += adjustment;
		}
		SI += adjustment;
		SI += adjustment;
	}
	else {
		uint8_t src = 0;
		if (read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI, &src)) {
			if (write_byte(cpu, ES, DI, src)) {
				/* Rep prefix check */
				if (F1) {
					IP -= cpu->instruction_len; /* Allow interrupts */
				}
			}
			else {
				if (F1) CX -= 1; /* Yes, the 80286 DECREMENTS CX another time if DI caused an exception. Why the fuck? */
			}
			DI += adjustment;
		}
		SI += adjustment;
	}
}
static void stos(I80286* cpu) {
	/* stos (AA/AB) b1010101W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return;
		}
		CX -= 1;
	}

	/* Do string operation */
	if (W) {
		if (write_word(cpu, ES, DI, AX)) {
			/* Rep prefix check */
			if (F1) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
		}
		else {
			if (F1) CX -= 1; /* Yes, the 80286 DECREMENTS CX another time if DI caused an exception. Why the fuck? */
		}
	}
	else {
		if (write_byte(cpu, ES, DI, AL)) {
			/* Rep prefix check */
			if (F1) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
		}
		else {
			if (F1) CX -= 1; /* Yes, the 80286 DECREMENTS CX another time if DI caused an exception. Why the fuck? */
		}
	}

	/* Adjust si/di delta */
	if (DF) {
		DI -= (1 << W);
	}
	else {
		DI += (1 << W);
	}
}
static void lods(I80286* cpu) {
	/* lods (AC/AD) b1010110W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return;
		}
		CX -= 1;
	}

	/* Do string operation */
	if (W) {
		if (read_word(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI, &AX)){
			/* Rep prefix check */
			if (F1) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
		}
	}
	else {
		if (read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI, &AL)){
			/* Rep prefix check */
			if (F1) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
		}
	}

	/* Adjust si/di delta */
	if (DF) {
		SI -= (1 << W);
	}
	else {
		SI += (1 << W);
	}
}
static void cmps(I80286* cpu) {
	/* cmps (A6/A7) b1010011W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return;
		}
		CX -= 1;
	}

	uint16_t adjustment = 0;
	if (DF) {
		adjustment = 0xFFFF;
	}
	else {
		adjustment = 1;
	}

	/* Do string operation */
	if (W) {
		uint16_t src = 0;
		uint16_t dest = 0;
		if (read_word(cpu, ES, DI, &dest)) {
			if (read_word(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI, &src)) {
				i80286_alu_cmp16(cpu, src, dest);
				/* Rep prefix check */
				if (F1 && ZF == F1Z) {
					IP -= cpu->instruction_len; /* Allow interrupts */
				}
			}
			SI += adjustment;
			SI += adjustment;
		}
		else {
			if (F1) CX += 1; /* Yes, CX doesnt change if DI caused an exception. This makes sense. */
		}
		DI += adjustment;
		DI += adjustment;
	}
	else {
		uint8_t src = 0;
		uint8_t dest = 0;
		if (read_byte(cpu, ES, DI, &dest)) {
			if (read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI, &src)) {
				i80286_alu_cmp8(cpu, src, dest);
				/* Rep prefix check */
				if (F1 && ZF == F1Z) {
					IP -= cpu->instruction_len; /* Allow interrupts */
				}
			}
			SI += adjustment;
		}
		else {
			if (F1) CX += 1; /* Yes, CX doesnt change if DI caused an exception. This makes sense. */
		}
		DI += adjustment;
	}
}
static void scas(I80286* cpu) {
	/* scas (AE/AF) b1010111W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return;
		}
		CX -= 1;
	}

	/* Do string operation */
	if (W) {
		uint16_t dest = 0;
		if (read_word(cpu, ES, DI, &dest)) {
			i80286_alu_cmp16(cpu, AX, dest);
			/* Rep prefix check */
			if (F1 && ZF == F1Z) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
		}
	}
	else {
		uint8_t dest = 0;
		if (read_byte(cpu, ES, DI, &dest)) {
			i80286_alu_cmp8(cpu, AL, dest);
			/* Rep prefix check */
			if (F1 && ZF == F1Z) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
		}
	}

	/* Adjust si/di delta */
	if (DF) {
		DI -= (1 << W);
	}
	else {
		DI += (1 << W);
	}
}
static void ins(I80286* cpu) {
	/* ins (6C/6D) b0110110W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return;
		}
		CX -= 1;
	}

	/* Do string operation */
	if (W) {
		uint16_t dest = (READ_IO_BYTE(DX) | (READ_IO_BYTE(DX + 1) << 8));
		if (write_word(cpu, ES, DI, dest)) {
			/* Rep prefix check */
			if (F1) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
		}
		else {
			if (F1) CX -= 1; /* Yes, the 80286 DECREMENTS CX another time if DI caused an exception. Why the fuck? */
		}
	}
	else {
		uint8_t dest = READ_IO_BYTE(DX);
		if (write_byte(cpu, ES, DI, dest)) {
			/* Rep prefix check */
			if (F1) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
		}
		else {
			if (F1) CX -= 1; /* Yes, the 80286 DECREMENTS CX another time if DI caused an exception. Why the fuck? */
		}
	}

	/* Adjust si/di delta */
	if (DF) {
		DI -= (1 << W);
	}
	else {
		DI += (1 << W);
	}
}
static void outs(I80286* cpu) {
	/* ins (6E/6F) b0110111W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return;
		}
		CX -= 1;
	}

	/* Do string operation */
	if (W) {
		uint16_t src = 0;
		if (read_word(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI, &src)) {
			/* Rep prefix check */
			if (F1) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
			WRITE_IO_BYTE(DX, src & 0xFF);
			WRITE_IO_BYTE(DX+1, (src >> 8) & 0xFF);
		}
	}
	else {
		uint8_t src = 0;
		if (read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI, &src)) {
			/* Rep prefix check */
			if (F1) {
				IP -= cpu->instruction_len; /* Allow interrupts */
			}
			WRITE_IO_BYTE(DX, src);
		}
	}

	/* Adjust si/di delta */
	if (DF) {
		SI -= (1 << W);
	}
	else {
		SI += (1 << W);
	}
}

static void les(I80286* cpu) {
	/* les (C4) b11000100 */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (cpu->modrm.u.bits.mod == 0b11) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	uint16_t segment = modrm_get_segment(cpu);
	uint16_t offset = 0;
	if (!modrm_get_offset(cpu, &offset)) {
		return;
	}
	uint16_t tmp = 0;
	if (!read_word(cpu, segment, offset, &tmp)) {
		return;
	}
	modrm_write_reg16(cpu, tmp);
	read_word(cpu, segment, offset + 2, &ES);
}
static void lds(I80286* cpu) {
	/* lds (C5) b11000101 */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (cpu->modrm.u.bits.mod == 0b11) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	uint16_t segment = modrm_get_segment(cpu);
	uint16_t offset = 0;
	if (!modrm_get_offset(cpu, &offset)) {
		return;
	}
	uint16_t tmp = 0;
	if (!read_word(cpu, segment, offset, &tmp)) {
		return;
	}
	modrm_write_reg16(cpu, tmp);
	read_word(cpu, segment, offset + 2, &DS);
}

static void xlat(I80286* cpu) {
	/* Get data pointed by BX + AL (D7) b11010111 */
	uint8_t mem = 0;
	read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), BX + AL, &mem);
	AL = mem;
}

static void esc(I80286* cpu) {
	/* esc (D8-DF R/M reg = XXX) b11010REG */
	if (!fetch_modrm(cpu)) {
		return;
	}
	uint8_t esc_opcode = ((cpu->opcode & 7) << 3) | cpu->modrm.u.bits.reg;
	uint16_t reg = reg16_read(cpu, cpu->opcode);
	OPERAND16 rm = { 0 };
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}
	if (rm.is_reg == 0 && rm.u.mem.offset == 0xFFFF) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}
	(void)esc_opcode;
	(void)reg;
	(void)rm;
}

static void loopnz(I80286* cpu) {
	/* loop while not zero (E0) b1110000Z */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	uint16_t se = sign_extend8_16(imm);
	CX -= 1;
	if (CX && !ZF) {
		IP += se;
	}
}
static void loopz(I80286* cpu) {
	/* loop while zero (E1) b1110000Z */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	uint16_t se = sign_extend8_16(imm);
	CX -= 1;
	if (CX && ZF) {
		IP += se;
	}
}
static void loop(I80286* cpu) {
	/* loop if CX not zero (E2) b11100010 */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	uint16_t se = sign_extend8_16(imm);
	CX -= 1;
	if (CX) {
		IP += se;
	}
}

static void in_accum_imm(I80286* cpu) {
	/* in AL/AX, imm */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	if (W) {
		AX = (READ_IO_BYTE(imm) | (READ_IO_BYTE(imm + 1) << 8));
	}
	else {
		AL = READ_IO_BYTE(imm);
	}
}
static void out_accum_imm(I80286* cpu) {
	/* out imm, AL/AX */
	uint8_t imm = 0;
	if (!fetch_byte(cpu, &imm)) {
		return;
	}
	if (W) {
		WRITE_IO_BYTE(imm, AL);
		WRITE_IO_BYTE(imm + 1, AH);
	}
	else {
		WRITE_IO_BYTE(imm, AL);
	}
}
static void in_accum_dx(I80286* cpu) {
	/* in AL/AX, DX */
	if (W) {
		AX = (READ_IO_BYTE(DX) | (READ_IO_BYTE(DX + 1) << 8));
	}
	else {
		AL = READ_IO_BYTE(DX);
	}
}
static void out_accum_dx(I80286* cpu) {
	/* out DX, AL/AX */
	if (W) {
		WRITE_IO_BYTE(DX, AL);
		WRITE_IO_BYTE(DX + 1, AH);
	}
	else {
		WRITE_IO_BYTE(DX, AL);
	}
}

static void int_(I80286* cpu) {
	/* interrupt CD b11001101 */	
	uint8_t type = 0;
	if (!fetch_byte(cpu, &type)) {
		return;
	}
	i80286_int(cpu, type);
}
static void int3(I80286* cpu) {
	/* interrupt CC b11001100 */
	i80286_int(cpu, INT_3);
}
static void into(I80286* cpu) {
	/* interrupt on overflow (CE) b11001110 */
	if (OF) {
		i80286_int(cpu, INT_OVERFLOW);
	}
}
static void iret(I80286* cpu) {
	/* return from interrupt (CF) b11001111 */
	pop_word(cpu, &IP);
	pop_word(cpu, &CS);
	uint16_t psw = 0;
	pop_word(cpu, &psw);
	PSW = (psw | 0x0002) & 0x0FD7;
}

static void bound(I80286* cpu) {
	/* bound (62) b01100010 */

	if (!fetch_modrm(cpu)) {
		return;
	}
	uint16_t reg = modrm_read_reg16(cpu);
	OPERAND16 rm = { 0 };
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}

	if (rm.is_reg) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}
	else if (rm.u.mem.offset == 0xFFFF || rm.u.mem.offset == 0xFFFD) { /* 286: Only 0xFFFD and 0xFFFF cause #GP */
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	uint16_t low = 0;
	uint16_t high = 0;
	modrm_read_rm16(cpu, rm, &low);
	rm.u.mem.offset += 2;
	modrm_read_rm16(cpu, rm, &high);

	if ((int16_t)reg < (int16_t)low || (int16_t)reg >(int16_t)high) {
		i80286_exception(cpu, INT_BOUNDS);
	}
}

static int get_descriptor_table(I80286* cpu, uint16_t selector, I80286_DESCRIPTOR* descriptor) {
	uint16_t index = selector >> 0x03;
	int ti = selector & 0x04;
	uint24_t base = 0;

	if (ti) {
		/* LDT */
		if (index * 8 >= cpu->ldt.descriptor.limit) {
			i80286_exception(cpu, EXCEPTION_GP);
			return 0;
		}
		base = cpu->ldt.descriptor.base;
	}
	else {
		/* GDT */
		if (index * 8 >= cpu->gdtr.limit) {
			i80286_exception(cpu, EXCEPTION_GP);
			return 0;
		}
		base = cpu->gdtr.base;
	}

	uint24_t address = base + (index * 8);

	uint16_t limit = 0;
	uint16_t base_lo = 0;
	uint8_t  base_hi = 0;
	read_word_linear(cpu, address, &limit);
	read_word_linear(cpu, address + 2, &base_lo);
	read_byte_linear(cpu, address + 4, &base_hi);

	descriptor->base = (uint24_t)base_lo | (base_hi << 16);
	descriptor->limit = limit;
	return 1;
}

static void sldt(I80286* cpu) {
	/* 0F 00 /0 */
}

static void str(I80286* cpu) {
	/* 0F 00 /1 */
}

static void lldt(I80286* cpu) {
	/* 0F 00 /2 */
}

static void ltr(I80286* cpu) {
	/* 0F 00 /3 - Load task register */

	if (!cpu->msw.u.bits.pe) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}
}

static void verr(I80286* cpu) {
	/* 0F 00 /4 */
}

static void verw(I80286* cpu) {
	/* 0F 00 /5 */
}


static void sgdt(I80286* cpu) {
	/* 0F 01 /0 */

	if (cpu->modrm.u.bits.mod == 0b11) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	uint16_t offset = 0;
	uint16_t segment = 0;
	modrm_get_offset(cpu, &offset);
	segment = modrm_get_segment(cpu);

	uint16_t limit = cpu->gdtr.limit;
	uint16_t base_lo = (uint16_t)(cpu->gdtr.base & 0xFFFF);
	uint8_t  base_hi = (uint8_t)((cpu->gdtr.base >> 16) & 0xFF);

	if (!write_word(cpu, segment, offset + 0, limit)) {
		return;
	}
	if (!write_word(cpu, segment, offset + 2, base_lo)) {
		return;
	}
	if (!write_byte(cpu, segment, offset + 4, base_hi)) {
		return;
	}
}

static void sidt(I80286* cpu) {
	/* 0F 01 /1 */	
	
	if (cpu->modrm.u.bits.mod == 0b11) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	uint16_t offset = 0;
	uint16_t segment = 0;
	modrm_get_offset(cpu, &offset);
	segment = modrm_get_segment(cpu);

	uint16_t limit = cpu->idtr.limit;
	uint16_t base_lo = (uint16_t)(cpu->idtr.base & 0xFFFF);
	uint8_t  base_hi = (uint8_t)((cpu->idtr.base >> 16) & 0xFF);

	if (!write_word(cpu, segment, offset + 0, limit)) {
		return;
	}
	if (!write_word(cpu, segment, offset + 2, base_lo)) {
		return;
	}
	if (!write_byte(cpu, segment, offset + 4, base_hi)) {
		return;
	}
}

static void lgdt(I80286* cpu) {
	/* 0F 01 /2 */

	if (cpu->modrm.u.bits.mod == 0b11) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->msw.u.bits.pe && cpu->cpl > 0) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	uint16_t limit = 0;
	uint16_t base_lo = 0;
	uint8_t base_hi = 0;

	uint16_t offset = 0;
	uint16_t segment = 0;
	modrm_get_offset(cpu, &offset);
	segment = modrm_get_segment(cpu);

	if (!read_word(cpu, segment, offset, &limit)) {
		return;
	}
	if (!read_word(cpu, segment, offset + 2, &base_lo)) {
		return;
	}
	if (!read_byte(cpu, segment, offset + 4, &base_hi)) {
		return;
	}

	cpu->gdtr.base = (uint24_t)base_lo | (base_hi << 16);
	cpu->gdtr.limit = limit;
}

static void lidt(I80286* cpu) {
	/* 0F 01 /3 */

	if (cpu->modrm.u.bits.mod == 0b11) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	if (cpu->msw.u.bits.pe && cpu->cpl > 0) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	uint16_t limit = 0;
	uint16_t base_lo = 0;
	uint8_t base_hi = 0;

	uint16_t offset = 0;
	uint16_t segment = 0;
	modrm_get_offset(cpu, &offset);
	segment = modrm_get_segment(cpu);

	if (!read_word(cpu, segment, offset, &limit)) {
		return;
	}
	if (!read_word(cpu, segment, offset + 2, &base_lo)) {
		return;
	}
	if (!read_byte(cpu, segment, offset + 4, &base_hi)) {
		return;
	}

	cpu->idtr.base = (uint24_t)base_lo | (base_hi << 16);
	cpu->idtr.limit = limit;
}

static void smsw(I80286* cpu) {
	/* 0F 01 /4 */

	OPERAND16 rm = { 0 };
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}

	modrm_write_rm16(cpu, rm, cpu->msw.u.word);
}

static void lmsw(I80286* cpu) {
	/* 0F 01 /6 */

	OPERAND16 rm = { 0 };
	uint16_t msw = 0;
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}

	if (!modrm_read_rm16(cpu, rm, &msw)) {
		return;
	}

	cpu->msw.u.word = (cpu->msw.u.word & 0x1) | msw;
}

static void lar(I80286* cpu) {
	/* 0F 02 /r - Load access rights */

	ZF = 0;
	if (!fetch_modrm(cpu)) {
		return;
	}

	if (!cpu->msw.u.bits.pe) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	OPERAND16 rm = { 0 };
	uint16_t selector = 0;
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}

	if (!modrm_read_rm16(cpu, rm, &selector)) {
		return;
	}

	if (selector == 0) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	I80286_DESCRIPTOR descriptor = { 0 };
	if (!get_descriptor_table(cpu, selector, &descriptor)) {
		return;
	}

	uint8_t dpl = (descriptor.access >> 5) & 3;
	if (dpl < cpu->cpl) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	modrm_write_reg16(cpu, descriptor.access);
	ZF = 1;
}

static void lsl(I80286* cpu) {
	/* 0F 03 /r - Load segment limit */

	ZF = 0;
	if (!fetch_modrm(cpu)) {
		return;
	}

	if (!cpu->msw.u.bits.pe) {
		i80286_exception(cpu, EXCEPTION_UD);
		return;
	}

	OPERAND16 rm = { 0 };
	uint16_t selector = 0;
	if (!modrm_get_rm16(cpu, &rm)) {
		return;
	}

	if (!modrm_read_rm16(cpu, rm, &selector)){
		return;
	}

	if (selector == 0) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	I80286_DESCRIPTOR descriptor = { 0 };
	if (!get_descriptor_table(cpu, selector, &descriptor)) {
		return;
	}
	
	uint8_t dpl = (descriptor.access >> 5) & 3;
	if (dpl < cpu->cpl) {
		i80286_exception(cpu, EXCEPTION_GP);
		return;
	}

	modrm_write_reg16(cpu, descriptor.limit);
	ZF = 1;
}

static void loadall_cache(I80286* cpu, uint16_t segment, uint16_t address, I80286_DESCRIPTOR* descriptor) {
	uint8_t v1 = 0;
	uint8_t v2 = 0;
	uint8_t v3 = 0;
	read_byte(cpu, segment, address, &v1);
	read_byte(cpu, segment, address+1, &v2);
	read_byte(cpu, segment, address+2, &v3);
	descriptor->base = (uint24_t)v1 | (v2 << 8) | (v3 << 16);

	read_byte(cpu, segment, address+3, &v1);
	descriptor->access = v1;

	read_byte(cpu, segment, address + 4, &v1);
	read_byte(cpu, segment, address + 5, &v2);
	descriptor->limit = (uint16_t)v1 | (v2 << 8);
}
static void loadall(I80286* cpu) {
	/* 0F 05 - load all */

	/* LOADALL reads a 102 byte area of physical memory starting at physical memory location 000800H.
	The entire execution state of the 80286 is defined upon completion of this instruction. 
	The descriptor cache registers for the ES, DS, SS, CS, TR, and LDT are directly loaded from this area. */
	
	read_word(cpu, 0x0000, 0x0806, &MSW);
	read_word(cpu, 0x0000, 0x0816, &TR);
	read_word(cpu, 0x0000, 0x0818, &PSW);
	read_word(cpu, 0x0000, 0x081A, &IP);
	read_word(cpu, 0x0000, 0x081C, &LDT);
	read_word(cpu, 0x0000, 0x081E, &DS);
	read_word(cpu, 0x0000, 0x0820, &SS);
	read_word(cpu, 0x0000, 0x0822, &CS);
	read_word(cpu, 0x0000, 0x0824, &ES);
	read_word(cpu, 0x0000, 0x0826, &DI);
	read_word(cpu, 0x0000, 0x0828, &SI);
	read_word(cpu, 0x0000, 0x082A, &BP);
	read_word(cpu, 0x0000, 0x082C, &SP);
	read_word(cpu, 0x0000, 0x082E, &BX);
	read_word(cpu, 0x0000, 0x0830, &DX);
	read_word(cpu, 0x0000, 0x0832, &CX);
	read_word(cpu, 0x0000, 0x0834, &AX);	
	loadall_cache(cpu, 0x0000, 0x0836, &ES_CACHE);
	loadall_cache(cpu, 0x0000, 0x083C, &CS_CACHE);
	loadall_cache(cpu, 0x0000, 0x0842, &SS_CACHE);
	loadall_cache(cpu, 0x0000, 0x0848, &DS_CACHE);
	loadall_cache(cpu, 0x0000, 0x084E, &GDTR);
	loadall_cache(cpu, 0x0000, 0x0854, &LDT_CACHE);
	loadall_cache(cpu, 0x0000, 0x085A, &IDTR);
	loadall_cache(cpu, 0x0000, 0x0860, &TSS);
}

static void clts(I80286* cpu) {
	/* 0F 06 */
	cpu->msw.u.bits.ts = 0;
}

static void arpl(I80286* cpu) {
	/* 63 /r */
}

/* prefix byte */
static int rep(I80286* cpu) {
	/* rep/repz/repnz (F2/F3) b1111001Z */
	cpu->internal_flags |= INTERNAL_FLAG_F1;    /* Set F1 */
	cpu->internal_flags &= ~INTERNAL_FLAG_F1Z;  /* Clr F1Z */
	cpu->internal_flags |= (cpu->opcode & 0x1); /* Set F1Z */

	cpu->opcode = 0;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80286_DECODE_OK;
	}
	return I80286_DECODE_REQ_CYCLE;
}
static int segment_override(I80286* cpu) {
	/* (26/2E/36/3E) b001SR110 */
	cpu->segment_prefix = SR;
	cpu->opcode = 0;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80286_DECODE_OK;
	}
	return I80286_DECODE_REQ_CYCLE;
}
static int lock(I80286* cpu) {
	/* lock the bus (F0/F1) b11110000 */
	cpu->lock_prefix = 1;
	cpu->opcode = 0;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return I80286_DECODE_OK;
	}
	return I80286_DECODE_REQ_CYCLE;
}

/* Fetch next opcode */
static void i80286_fetch(I80286* cpu) {
	cpu->internal_flags = 0;
	cpu->modrm.u.byte = 0;
	cpu->segment_prefix = 0xFF;
	cpu->lock_prefix = 0;
	cpu->instruction_len = 0;
	cpu->opcode = 0;
	if (!fetch_byte(cpu, &cpu->opcode)) {
		return;
	}
}

/* decode opcode */
static void i80286_decode_opcode_0f(I80286* cpu) {
	/* 0x0F XX */
	if (!fetch_byte(cpu, &cpu->opcode)){
		return;
	}

	switch (cpu->opcode) {
		case 0x00:
			if (!fetch_modrm(cpu)) {
				return;
			}

			switch (cpu->modrm.u.bits.reg) {
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
			}
			break;

		case 0x01:
			if (!fetch_modrm(cpu)) {
				return;
			}

			switch (cpu->modrm.u.bits.reg) {
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
				case 0b110:
					lmsw(cpu);
					break;
			}
			break;

		case 0x02:
			lar(cpu);
			break;
		case 0x03:
			lsl(cpu);
			break;
		case 0x05:
			loadall(cpu);
			break;
		case 0x06:
			clts(cpu);
			break;
	}
}
static void i80286_decode_opcode_80(I80286* cpu) {
	/* 0x80 - 0x83 b100000SW (Immed) */
	if (!fetch_modrm(cpu)) {
		return;
	}
	switch (cpu->modrm.u.bits.reg) {
		case 0b000: /* ADD */
			add_rm_imm(cpu);
			break;
		case 0b001: /* OR */
			or_rm_imm(cpu);
			break;
		case 0b010: /* ADC */
			adc_rm_imm(cpu);
			break;
		case 0b011: /* SBB */
			sbb_rm_imm(cpu);
			break;
		case 0b100: /* AND */
			and_rm_imm(cpu);
			break;
		case 0b101: /* SUB */
			sub_rm_imm(cpu);
			break;
		case 0b110: /* XOR */
			xor_rm_imm(cpu);
			break;
		case 0b111: /* CMP */
			cmp_rm_imm(cpu);
			break;
	}
}
static void i80286_decode_opcode_c0(I80286* cpu) {
	/* 0xC0 - 0xC1 b1100000W (Shift Imm) */
	if (!fetch_modrm(cpu)) {
		return;
	}
	switch (cpu->modrm.u.bits.reg) {
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
}
static void i80286_decode_opcode_d0(I80286* cpu) {
	/* 0xD0 - 0xD3 b110100VW (Shift) */
	if (!fetch_modrm(cpu)) {
		return;
	}
	switch (cpu->modrm.u.bits.reg) {
		case 0b000:
			rol(cpu);
			break;
		case 0b001:
			ror(cpu);
			break;
		case 0b010:
			rcl(cpu);
			break;
		case 0b011:
			rcr(cpu);
			break;
		case 0b100:
			shl(cpu);
			break;
		case 0b101:
			shr(cpu);
			break;
		case 0b110:
			sal(cpu);
			break;
		case 0b111:
			sar(cpu);
			break;
	}
}
static void i80286_decode_opcode_f6(I80286* cpu) {
	/* F6/F7 b1111011W (Group 1) */
	if (!fetch_modrm(cpu)) {
		return;
	}
	switch (cpu->modrm.u.bits.reg) {
		case 0b000:
			test_rm_imm(cpu);
			break;
		case 0b001: /* 8086 undocumented; Decodes identically to b000 */
			test_rm_imm(cpu);
			break;
		case 0b010:
			not(cpu);
			break;
		case 0b011:
			neg(cpu);
			break;
		case 0b100:
			mul_rm(cpu);
			break;
		case 0b101:
			imul_rm(cpu);
			break;
		case 0b110:
			div_rm(cpu);
			break;
		case 0b111:
			idiv_rm(cpu);
			break;
	}
}
static void i80286_decode_opcode_fe(I80286* cpu) {
	/* FE/FF b1111111W (Group 2) */
	if (!fetch_modrm(cpu)) {
		return;
	}
	if (W) {
		switch (cpu->modrm.u.bits.reg) {
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
			case 0b111: /* 8086 undocumented; Decodes identically to b110 */
				push_rm(cpu);
				break;
		}
	}
	else {
		switch (cpu->modrm.u.bits.reg) {
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
				i80286_exception(cpu, EXCEPTION_UD);
				break;
		}
	}
	return;
}

static int i80286_decode_opcode(I80286* cpu) {
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
			i80286_decode_opcode_0f(cpu);
			break;
		
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
			break;
		case 0x65:
			break;
		case 0x66:
			break;
		case 0x67:
			break;
		case 0x68:
			push_imm(cpu);
			break;
		case 0x69:
			imul_rm_imm(cpu);
			break;
		case 0x6A:
			push_imm(cpu);
			break;
		case 0x6B:
			imul_rm_imm(cpu);
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
			jcc(cpu);
			break;

		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
			i80286_decode_opcode_80(cpu);
			break;
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
			i80286_decode_opcode_c0(cpu);
			break;
		case 0xC2:
			ret_intra_add_imm(cpu);
			break;
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
			ret_inter_add_imm(cpu);
			break;
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
			i80286_decode_opcode_d0(cpu);
			break;
		case 0xD4:
			aam(cpu);
			break;
		case 0xD5:
			aad(cpu);
			break;
		case 0xD6: /* 80286 undocumented; Set AL to Carry */
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
			loopnz(cpu);
			break;
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
		case 0xF1: /* 8086 undocumented; Decodes identically to 0xF0 */
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
			i80286_decode_opcode_f6(cpu);
			break;
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
		case 0xFF:
			i80286_decode_opcode_fe(cpu);
			break;
	}
	return I80286_DECODE_OK;
}

static int i80286_decode_instruction(I80286* cpu) {
	int r = 0;
	do {
		r = i80286_decode_opcode(cpu);
	} while (r == I80286_DECODE_REQ_CYCLE);
	return r;
}

void i80286_init(I80286* cpu) {
	cpu->funcs.read_mem_byte = NULL;
	cpu->funcs.write_mem_byte = NULL;
	cpu->funcs.read_io_byte = NULL;
	cpu->funcs.write_io_byte = NULL;
}
void i80286_reset(I80286* cpu) {
	for (int i = 0; i < I80286_REGISTER_COUNT; ++i) {
		cpu->registers[i].u.r16 = 0;
	}

	for (int i = 0; i < I80286_SEGMENT_COUNT; ++i) {
		cpu->segments[i].selector = 0;
		cpu->segments[i].descriptor.limit = 0xFFFF;
		cpu->segments[i].descriptor.base = 0x000000;
	}

	cpu->idtr.base = 0x000000;
	cpu->idtr.limit = 0x03FF;

	IP = 0xFFF0;
	CS = 0xF000;
	CS_CACHE.base = 0xFF0000;
	PSW = 0x2;
	MSW = 0xFFF0;

	cpu->opcode = 0;
	cpu->modrm.u.byte = 0;
	cpu->cycles = 0;
	cpu->internal_flags = 0;
	cpu->segment_prefix = 0xFF;
	cpu->instruction_len = 0;

	cpu->intr = 0;
	cpu->nmi = 0;
		
	cpu->tf_latch = 0;
	cpu->int_latch = 0;
	cpu->int_delay = 0;
	cpu->intr_type = 0;
	cpu->halt = 0;
}

int i80286_execute(I80286* cpu) {
	i80286_check_interrupts(cpu);
	i80286_check_halt(cpu);
	i80286_fetch(cpu);
	return i80286_decode_instruction(cpu);
}

uint24_t i80286_get_physical_address(uint16_t segment, uint16_t address) {
	return (((uint24_t)segment << 4) + address) & 0xFFFFFF;
}

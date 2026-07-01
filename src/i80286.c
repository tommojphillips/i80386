/* i8086.c
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 CPU
 */

#include <stdint.h>

#include "i8086.h"
#include "i8086_alu.h"
#include "sign_extend.h"

#define PSW cpu->status.word

#define DBZ cpu->dbz
#define NMI cpu->nmi
#define INTR cpu->intr

#define SF cpu->status.sf
#define CF cpu->status.cf
#define ZF cpu->status.zf
#define PF cpu->status.pf
#define OF cpu->status.of
#define AF cpu->status.af
#define DF cpu->status.df
#define TF cpu->status.tf
#define IF cpu->status.in

#define IP cpu->ip

#define AL cpu->registers[REG_AL].l   // accum low byte 8bit register
#define AH cpu->registers[REG_AL].h   // accum high byte 8bit register
#define AX cpu->registers[REG_AX].r16 // accum 16bit register

#define CL cpu->registers[REG_CL].l   // count low byte 8bit register
#define CH cpu->registers[REG_CL].h   // count high byte 8bit register
#define CX cpu->registers[REG_CX].r16 // count 16bit register

#define DL cpu->registers[REG_DL].l   // data low byte 8bit register
#define DH cpu->registers[REG_DL].h   // data high byte 8bit register
#define DX cpu->registers[REG_DX].r16 // data 16bit register

#define BL cpu->registers[REG_BL].l   // base low byte 8bit register
#define BH cpu->registers[REG_BL].h   // base high byte 8bit register
#define BX cpu->registers[REG_BX].r16 // base 16bit register

#define SP cpu->registers[REG_SP].r16 // stack pointer 16bit register
#define BP cpu->registers[REG_BP].r16 // base pointer 16bit register
#define SI cpu->registers[REG_SI].r16 // src index 16bit register
#define DI cpu->registers[REG_DI].r16 // dest index 16bit register

#define ES cpu->segments[SEG_ES] // extra segment register
#define CS cpu->segments[SEG_CS] // code segment register
#define SS cpu->segments[SEG_SS] // stack segment register
#define DS cpu->segments[SEG_DS] // data segment register

 // byte/word operation. 0 = byte; 1 = word
#define W (cpu->opcode & 0x1)

// byte/word operation. 0 = byte; 1 = word
#define WREG (cpu->opcode & 0x8) 

// byte/word operation. b00 = byte; b01 = word; b11 = byte sign extended to word
#define SW (cpu->opcode & 0x3)

// segment register. es=b00; cs=b01; ss=b10; ds=b11
#define SR ((cpu->opcode >> 0x3) & 0x3)

// 0 = (count = 1); 1 = (count = CL)
#define VW (cpu->opcode & 0x2)

// register direction (reg <- r/m) or (r/m <- reg)
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
#define SEG_DEFAULT_OR_OVERRIDE(seg) (cpu->segments[GET_SEG_OVERRIDE(seg)])

/* Read byte from IO port */
#define READ_IO_BYTE(port) cpu->funcs.read_io_byte(port)

/* Write byte to IO port */
#define WRITE_IO_BYTE(port,value) cpu->funcs.write_io_byte(port, value)

/* Get ptr to 16bit segment */
#define GET_SEG(seg) (&cpu->segments[seg & 3])

#define TRANSFERS(x) cpu->cycles += (x * 4)
#define TRANSFERS_RM(reg_transfer, mem_transfer) cpu->cycles += ((cpu->modrm.mod == 0b11 ? reg_transfer : mem_transfer) * 4)
#define TRANSFERS_RM_D(reg_transfer, mr_transfer, rm_transfer) cpu->cycles += ((cpu->modrm.mod == 0b11 ? reg_transfer : !D ? mr_transfer : rm_transfer) * 4)

#define CYCLES(x) cpu->cycles += x
#define CYCLES_RM(reg_cyc, mem_cyc) cpu->cycles += (cpu->modrm.mod == 0b11 ? reg_cyc : mem_cyc)
#define CYCLES_RM_D(reg_cyc, mr_cyc, rm_cyc) cpu->cycles += (cpu->modrm.mod == 0b11 ? reg_cyc : !D ? mr_cyc : rm_cyc)

#define INT_DBZ      0 // ITC 0
#define INT_TRAP     1 // ITC 1
#define INT_NMI      2 // ITC 2
#define INT_3        3 // ITC 3
#define INT_OVERFLOW 4 // ITC 4

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

static uint8_t op8_read(I8086* cpu, OPERAND8 op8);
static void op8_write(I8086* cpu, OPERAND8 op8, uint8_t v);

static uint16_t op16_read(I8086* cpu, OPERAND16 op16);
static void op16_write(I8086* cpu, OPERAND16 op16, uint16_t v);

static uint8_t read_byte(I8086* cpu, uint16_t segment, uint16_t offset) {
	return cpu->funcs.read_mem_byte(i8086_get_physical_address(segment, offset));
}
static void write_byte(I8086* cpu, uint16_t segment, uint16_t offset, uint8_t value) {
	cpu->funcs.write_mem_byte(i8086_get_physical_address(segment, offset), value);
}
static uint8_t fetch_byte(I8086* cpu) {
	uint8_t v = read_byte(cpu, CS, IP);
	IP += 1;
	cpu->instruction_len += 1;
	return v;
}

static uint16_t read_word(I8086* cpu, uint16_t segment, uint16_t offset) {
	return (((uint16_t)cpu->funcs.read_mem_byte(i8086_get_physical_address(segment, offset + 1)) << 8) | cpu->funcs.read_mem_byte(i8086_get_physical_address(segment, offset)));
}
static void write_word(I8086* cpu, uint16_t segment, uint16_t offset, uint16_t value) {
	cpu->funcs.write_mem_byte(i8086_get_physical_address(segment, offset), value & 0xFF);
	cpu->funcs.write_mem_byte(i8086_get_physical_address(segment, offset + 1), (value >> 8) & 0xFF);
}
static uint16_t fetch_word(I8086* cpu) {
	uint16_t v = read_word(cpu, CS, IP);
	IP += 2;
	cpu->instruction_len += 2;
	return v;
}

static void push_op8(I8086* cpu, OPERAND8 op8) {
	SP -= 2;
	uint8_t tmp = op8_read(cpu, op8);
	write_byte(cpu, SS, SP, tmp);
}
static void push_byte(I8086* cpu, uint8_t value) {
	SP -= 2;
	write_byte(cpu, SS, SP, value);
}

static void push_op16(I8086* cpu, OPERAND16 op16) {
	SP -= 2;
	uint16_t tmp = op16_read(cpu, op16);
	write_word(cpu, SS, SP, tmp);
}
static void push_word(I8086* cpu, uint16_t value) {
	SP -= 2;
	write_word(cpu, SS, SP, value);
}

static void pop_op16(I8086* cpu, OPERAND16 op16) {
	uint16_t tmp = read_word(cpu, SS, SP);
	SP += 2;
	op16_write(cpu, op16, tmp);
}
static void pop_word(I8086* cpu, uint16_t* value) {
	*value = read_word(cpu, SS, SP);
	SP += 2;
}

void i8086_intr(I8086* cpu, uint8_t type) {
	if (!INTR) {
		INTR = 1;
		cpu->intr_type = type;
	}
}
void i8086_nmi(I8086* cpu) {
	NMI = 1;
}
void i8086_int(I8086* cpu, uint8_t type) {

#ifdef I8086_ENABLE_INTERRUPT_HOOKS
	I8086_INT_CB hook = i8086_find_interrupt_cb(cpu, type);
	if (hook != NULL) {
		if (hook(cpu)){
			return; // Interrupt was handled by the hook
		}
	}
#endif

	push_word(cpu, PSW);
	push_word(cpu, CS);
	push_word(cpu, IP);
	uint16_t offset = type * 4;
	IP = read_word(cpu, 0x0000, offset);
	CS = read_word(cpu, 0x0000, offset + 2);
	IF = 0;
	TF = 0;
}
static void i8086_check_interrupts(I8086* cpu) {
		
	if (cpu->int_delay == 1) {
		cpu->int_delay = 0;
		return;
	}

	if (NMI) {
		/* Non-Maskable int */
		NMI = 0;
		i8086_int(cpu, INT_NMI);
		TRANSFERS(5);
		CYCLES(50);
	}
	else if (INTR && cpu->int_latch) {
		/* Hardware int; INTR is masked by IF */
		INTR = 0;
		i8086_int(cpu, cpu->intr_type);
		TRANSFERS(7);
		CYCLES(61);
	}

	if (cpu->tf_latch) {
		/* Trap int */
		i8086_int(cpu, INT_TRAP);
		TRANSFERS(5);
		CYCLES(50);
	}

	/* latch int flag for next cycle */
	cpu->int_latch = IF;

	/* latch trap flag for next cycle */
	cpu->tf_latch = TF;
}

static uint8_t reg8_read(I8086* cpu, uint8_t reg) {
	if (reg & 0x4) {
		return cpu->registers[reg & 0x3].h;
	}
	else {
		return cpu->registers[reg & 0x3].l;
	}
}
static void reg8_write(I8086* cpu, uint8_t reg, uint8_t v) {
	if (reg & 0x4) {
		cpu->registers[reg & 0x3].h = v;
	}
	else {
		cpu->registers[reg & 0x3].l = v;
	}
}

static uint16_t reg16_read(I8086* cpu, uint8_t reg) {
	return cpu->registers[reg & 0x7].r16;
}
static void reg16_write(I8086* cpu, uint8_t reg, uint16_t v) {
	cpu->registers[reg & 0x7].r16 = v;
}

/* Mod R/M */

/* Use the mod r/m byte to calculate a 16bit indirect address eg (BX+SI) */
static uint16_t modrm_get_base_offset(I8086* cpu) {
	switch (cpu->modrm.rm) {
		case 0b000: // base rel indexed - BX + SI
			return (BX + SI);
		case 0b001: // base rel indexed - BX + DI
			return (BX + DI);
		case 0b010: // base rel indexed stack - BP + SI
			return (BP + SI);
		case 0b011: // base rel indexed stack - BP + DI
			return (BP + DI);
		case 0b100: // implied SI
			return SI;
		case 0b101: // implied DI
			return DI;
		case 0b110: // implied BP
			return BP;
		case 0b111: // implied BX
			return BX;
	}
	return 0;
}

/* Use the mod r/m byte to calculate the selected segment */
static uint16_t modrm_get_segment(I8086* cpu) {
	if (cpu->segment_prefix != 0xFF) {
		cpu->ea_segment = cpu->segments[cpu->segment_prefix & 0x3]; // CS/DS/ES/SS override
	}
	else {
		// mod = 00 special case for r/m=110 -> [disp16] uses DS
		if (cpu->modrm.mod == 0b00 && cpu->modrm.rm == 0b110) {
			cpu->ea_segment = cpu->segments[SEG_DS];
		}
		else {
			switch (cpu->modrm.rm) {
				case 0b010: // [BP+SI]
				case 0b011: // [BP+DI]
				case 0b110: // [BP] (mod != 00)
					cpu->ea_segment = cpu->segments[SEG_SS]; // defaults to SS
					break;
				default:
					cpu->ea_segment = cpu->segments[SEG_DS]; // everything else defaults to DS
					break;
			}
		}
	}
	return cpu->ea_segment;
}

/* Use the mod r/m byte to calculate a 16-bit address (offset) */
static uint16_t modrm_get_offset(I8086* cpu) {
	switch (cpu->modrm.mod) {
		case 0b00:
			if (cpu->modrm.rm == 0b110) {
				cpu->ea_offset = fetch_word(cpu);
				CYCLES(6);
			}
			else {
				cpu->ea_offset = modrm_get_base_offset(cpu);
			}
			break;

		case 0b01: {
			int8_t disp8 = (int8_t)fetch_byte(cpu);
			cpu->ea_offset = (modrm_get_base_offset(cpu) + disp8) & 0xFFFF;
			CYCLES(4);
		} break;

		case 0b10: {
			int16_t disp16 = (int16_t)fetch_word(cpu);
			cpu->ea_offset = (modrm_get_base_offset(cpu) + disp16) & 0xFFFF;
			CYCLES(4);
		} break;

		// case 0b11: register mode never calls this
	}
	return cpu->ea_offset;
}

static OPERAND8 modrm_get_op8(I8086* cpu) {
	OPERAND8 op8 = { 0 };
	if (cpu->modrm.mod == 0b11) {
		op8.is_reg = 1;
		op8.u.reg_index = cpu->modrm.rm;
	}
	else {
		op8.is_reg = 0;
		op8.u.mem.segment = modrm_get_segment(cpu);
		op8.u.mem.offset = modrm_get_offset(cpu);
	}
	return op8;
}
static OPERAND8 mem_get_op8(uint16_t segment, uint16_t offset) {
	OPERAND8 op8 = { 0 };
	op8.is_reg = 0;
	op8.u.mem.segment = segment;
	op8.u.mem.offset = offset;
	return op8;
}
static uint8_t op8_read(I8086* cpu, OPERAND8 op8) {
	if (op8.is_reg) {
		return reg8_read(cpu, op8.u.reg_index);
	}
	else {
		return read_byte(cpu, op8.u.mem.segment, op8.u.mem.offset);
	}
}
static void op8_write(I8086* cpu, OPERAND8 op8, uint8_t v) {
	if (op8.is_reg) {
		reg8_write(cpu, op8.u.reg_index, v);
	}
	else {
		write_byte(cpu, op8.u.mem.segment, op8.u.mem.offset, v);
	}
}

// Resolve bit8 ModR/M + reg with D bit, then execute a binary op. with writeback
// The op must be of the form: void op(I8086*, uint8_t*, uint8_t);
static void exec_bin_op8(I8086* cpu, void (*op)(I8086*, uint8_t*, uint8_t)) {
	OPERAND8 rm = modrm_get_op8(cpu);
	uint8_t reg = reg8_read(cpu, cpu->modrm.reg);
	uint8_t tmp = op8_read(cpu, rm);
	if (D) {
		op(cpu, &reg, tmp);
		reg8_write(cpu, cpu->modrm.reg, reg);
	}
	else {
		op(cpu, &tmp, reg);
		op8_write(cpu, rm, tmp);
	}
}
// Resolve bit8 ModR/M + reg with D bit, then execute a binary op. no writeback
// The op must be of the form: void op(I8086*, uint8_t, uint8_t);
static void exec_bin_op8_ro(I8086* cpu, void (*op)(I8086*, uint8_t, uint8_t)) {
	OPERAND8 rm = modrm_get_op8(cpu);
	uint8_t reg = reg8_read(cpu, cpu->modrm.reg);
	uint8_t tmp = op8_read(cpu, rm);
	if (D) {
		op(cpu, reg, tmp);
	}
	else {
		op(cpu, tmp, reg);
	}
}

static OPERAND16 modrm_get_op16(I8086* cpu) {
	OPERAND16 op16 = { 0 };
	if (cpu->modrm.mod == 0b11) {
		op16.is_reg = 1;
		op16.u.reg_index = cpu->modrm.rm;
	}
	else {
		op16.is_reg = 0;
		op16.u.mem.segment = modrm_get_segment(cpu);
		op16.u.mem.offset = modrm_get_offset(cpu);
	}
	return op16;
}
static OPERAND16 mem_get_op16(uint16_t segment, uint16_t offset) {
	OPERAND16 op16 = { 0 };
	op16.is_reg = 0;
	op16.u.mem.segment = segment;
	op16.u.mem.offset = offset;
	return op16;
}
static uint16_t op16_read(I8086* cpu, OPERAND16 op16) {
	if (op16.is_reg) {
		return reg16_read(cpu, op16.u.reg_index);
	}
	else {
		return read_word(cpu, op16.u.mem.segment, op16.u.mem.offset);
	}
}
static void op16_write(I8086* cpu, OPERAND16 op16, uint16_t v) {
	if (op16.is_reg) {
		reg16_write(cpu, op16.u.reg_index, v);
	}
	else {
		write_word(cpu, op16.u.mem.segment, op16.u.mem.offset, v);
	}
}

// Resolve bit16 ModR/M + reg with D bit, then execute a binary op. with writeback
// The op must be of the form: void op(I8086*, uint16_t*, uint16_t);
static void exec_bin_op16(I8086* cpu, void (*op)(I8086*, uint16_t*, uint16_t)) {
	OPERAND16 rm = modrm_get_op16(cpu);
	uint16_t reg = reg16_read(cpu, cpu->modrm.reg);
	uint16_t tmp = op16_read(cpu, rm);
	if (D) {
		op(cpu, &reg, tmp);
		reg16_write(cpu, cpu->modrm.reg, reg);
	}
	else {
		op(cpu, &tmp, reg);
		op16_write(cpu, rm, tmp);
	}
}
// Resolve bit16 ModR/M + reg with D bit, then execute a binary op. no writeback
// The op must be of the form: void op(I8086*, uint16_t, uint16_t);
static void exec_bin_op16_ro(I8086* cpu, void (*op)(I8086*, uint16_t, uint16_t)) {
	OPERAND16 rm = modrm_get_op16(cpu);
	uint16_t reg = reg16_read(cpu, cpu->modrm.reg);
	uint16_t tmp = op16_read(cpu, rm);
	if (D) {
		op(cpu, reg, tmp);
	}
	else {
		op(cpu, tmp, reg);
	}
}

static void fetch_modrm(I8086* cpu) {
	cpu->modrm.byte = fetch_byte(cpu);
}

/* Opcodes */

static void add_rm_imm(I8086* cpu) {
	/* add r/m, imm (80/81/82/83, R/M reg = b000) b100000SW */
	if (W) {
		if (SW == 0b01) {
			/* reg16, disp16 - 0x81 */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint16_t imm = fetch_word(cpu);
			alu_add16(cpu, &tmp, imm);
			op16_write(cpu, rm, tmp);
		}
		else {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint8_t imm = fetch_byte(cpu);
			uint16_t se = sign_extend8_16(imm);
			alu_add16(cpu, &tmp, se);
			op16_write(cpu, rm, tmp);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		uint8_t imm = fetch_byte(cpu);
		alu_add8(cpu, &tmp, imm);
		op8_write(cpu, rm, tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(4, 17);
}
static void add_rm_reg(I8086* cpu) {
	/* add r/m, reg (00/01/02/03) b000000DW */
	fetch_modrm(cpu);
	if (W) {
		exec_bin_op16(cpu, alu_add16);
	}
	else {
		exec_bin_op8(cpu, alu_add8);
	}
	TRANSFERS_RM_D(0, 2, 1);
	CYCLES_RM_D(3, 16, 9);
}
static void add_accum_imm(I8086* cpu) {
	/* add AL/AX, imm (04/05) b0000010W */
	if (W) {
		uint16_t imm = fetch_word(cpu);
		alu_add16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		alu_add8(cpu, &AL, imm);
	}
	CYCLES(4);
}

static void or_rm_imm(I8086* cpu) {
	/* or r/m, imm (80/81/82/83, R/M reg = b001) b100000SW */
	if (W) {
		if (SW == 0b01) {
			/* reg16, disp16 - 0x81 */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint16_t imm = fetch_word(cpu);
			alu_or16(cpu, &tmp, imm);
			op16_write(cpu, rm, tmp);
		}
		else {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint8_t imm = fetch_byte(cpu);
			uint16_t se = sign_extend8_16(imm);
			alu_or16(cpu, &tmp, se);
			op16_write(cpu, rm, tmp);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		uint8_t imm = fetch_byte(cpu);
		alu_or8(cpu, &tmp, imm);
		op8_write(cpu, rm, tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(4, 17);
}
static void or_rm_reg(I8086* cpu) {
	/* or r/m, reg (08/0A/09/0B) b000010DW */
	fetch_modrm(cpu);
	if (W) {
		exec_bin_op16(cpu, alu_or16);
	}
	else {
		exec_bin_op8(cpu, alu_or8);
	}
	TRANSFERS_RM_D(0, 2, 1);
	CYCLES_RM_D(3, 16, 9);
}
static void or_accum_imm(I8086* cpu) {
	/* or AL/AX, imm (0C/0D) b0000110W */
	if (W) {
		uint16_t imm = fetch_word(cpu);
		alu_or16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		alu_or8(cpu, &AL, imm);
	}
	CYCLES(4);
}

static void adc_rm_imm(I8086* cpu) {
	/* adc r/m, imm (80/81/82/83, R/M reg = b010) b100000SW */
	if (W) {
		if (SW == 0b01) {
			/* reg16, disp16 - 0x81 */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint16_t imm = fetch_word(cpu);
			alu_adc16(cpu, &tmp, imm);
			op16_write(cpu, rm, tmp);
		}
		else {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint8_t imm = fetch_byte(cpu);
			uint16_t se = sign_extend8_16(imm);
			alu_adc16(cpu, &tmp, se);
			op16_write(cpu, rm, tmp);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		uint8_t imm = fetch_byte(cpu);
		alu_adc8(cpu, &tmp, imm);
		op8_write(cpu, rm, tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(4, 17);
}
static void adc_rm_reg(I8086* cpu) {
	/* adc r/m, reg (10/12/11/13) b000100DW */
	fetch_modrm(cpu);
	if (W) {
		exec_bin_op16(cpu, alu_adc16);
	}
	else {
		exec_bin_op8(cpu, alu_adc8);
	}
	TRANSFERS_RM_D(0, 2, 1);
	CYCLES_RM_D(3, 16, 9);
}
static void adc_accum_imm(I8086* cpu) {
	/* adc AL/AX, imm (14/15) b0001010W */
	if (W) {
		uint16_t imm = fetch_word(cpu);
		alu_adc16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		alu_adc8(cpu, &AL, imm);
	}
	CYCLES(4);
}

static void sbb_rm_imm(I8086* cpu) {
	/* sbb r/m, imm (80/81/82/83, R/M reg = b011)  b100000SW */
	if (W) {
		if (SW == 0b01) {
			/* reg16, disp16 - 0x81 */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint16_t imm = fetch_word(cpu);
			alu_sbb16(cpu, &tmp, imm);
			op16_write(cpu, rm, tmp);
		}
		else {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint8_t imm = fetch_byte(cpu);
			uint16_t se = sign_extend8_16(imm);
			alu_sbb16(cpu, &tmp, se);
			op16_write(cpu, rm, tmp);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		uint8_t imm = fetch_byte(cpu);
		alu_sbb8(cpu, &tmp, imm);
		op8_write(cpu, rm, tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(4, 17);
}
static void sbb_rm_reg(I8086* cpu) {
	/* sbb r/m, reg (18/1A/19/1B) b000110DW */
	fetch_modrm(cpu);
	if (W) {
		exec_bin_op16(cpu, alu_sbb16);
	}
	else {
		exec_bin_op8(cpu, alu_sbb8);
	}
	TRANSFERS_RM_D(0, 2, 1);
	CYCLES_RM_D(3, 16, 9);
}
static void sbb_accum_imm(I8086* cpu) {
	/* sbb AL/AX, imm (1C/1D) b0001110W */
	if (W) {
		uint16_t imm = fetch_word(cpu);
		alu_sbb16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		alu_sbb8(cpu, &AL, imm);
	}
	CYCLES(4);
}

static void and_rm_imm(I8086* cpu) {
	/* and r/m, imm (80/81/82/83, R/M reg = b100) b100000SW */
	if (W) {
		if (SW == 0b01) {
			/* reg16, disp16 - 0x81 */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint16_t imm = fetch_word(cpu);
			alu_and16(cpu, &tmp, imm);
			op16_write(cpu, rm, tmp);
		}
		else {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint8_t imm = fetch_byte(cpu);
			uint16_t se = sign_extend8_16(imm);
			alu_and16(cpu, &tmp, se);
			op16_write(cpu, rm, tmp);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		uint8_t imm = fetch_byte(cpu);
		alu_and8(cpu, &tmp, imm);
		op8_write(cpu, rm, tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(4, 17);
}
static void and_rm_reg(I8086* cpu) {
	/* and r/m, reg (20/22/21/23) b001000DW */
	fetch_modrm(cpu);
	if (W) {
		exec_bin_op16(cpu, alu_and16);
	}
	else {
		exec_bin_op8(cpu, alu_and8);
	}
	TRANSFERS_RM_D(0, 2, 1);
	CYCLES_RM_D(3, 16, 9);
}
static void and_accum_imm(I8086* cpu) {
	/* and AL/AX, imm (24/25) b0010010W */
	if (W) {
		uint16_t imm = fetch_word(cpu);
		alu_and16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		alu_and8(cpu, &AL, imm);
	}
	CYCLES(4);
}

static void sub_rm_imm(I8086* cpu) {
	/* sub r/m, imm (80/81, R/M reg = b101) b100000SW */
	if (W) {
		if (SW == 0b01) {
			/* reg16, disp16 - 0x81 */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint16_t imm = fetch_word(cpu);
			alu_sub16(cpu, &tmp, imm);
			op16_write(cpu, rm, tmp);
		}
		else {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint8_t imm = fetch_byte(cpu);
			uint16_t se = sign_extend8_16(imm);
			alu_sub16(cpu, &tmp, se);
			op16_write(cpu, rm, tmp);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		uint8_t imm = fetch_byte(cpu);
		alu_sub8(cpu, &tmp, imm);
		op8_write(cpu, rm, tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(4, 17);
}
static void sub_rm_reg(I8086* cpu) {
	/* sub r/m, reg (28/2A/29/2B) b001010DW */
	fetch_modrm(cpu);
	if (W) {
		exec_bin_op16(cpu, alu_sub16);
	}
	else {
		exec_bin_op8(cpu, alu_sub8);
	}
	TRANSFERS_RM_D(0, 2, 1);
	CYCLES_RM_D(3, 16, 9);
}
static void sub_accum_imm(I8086* cpu) {
	/* sub AL/AX, imm (2C/2D) b0010110W */
	if (W) {
		uint16_t imm = fetch_word(cpu);
		alu_sub16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		alu_sub8(cpu, &AL, imm);
	}
	CYCLES(4);
}

static void xor_rm_imm(I8086* cpu) {
	/* xor r/m, imm (80/81/82/83, R/M reg = b110) b100000SW */
	if (W) {
		if (SW == 0b01) {
			/* reg16, disp16 - 0x81 */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint16_t imm = fetch_word(cpu);
			alu_xor16(cpu, &tmp, imm);
			op16_write(cpu, rm, tmp);
		}
		else {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint8_t imm = fetch_byte(cpu);
			uint16_t se = sign_extend8_16(imm);
			alu_xor16(cpu, &tmp, se);
			op16_write(cpu, rm, tmp);
		}
	} 
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		uint8_t imm = fetch_byte(cpu);
		alu_xor8(cpu, &tmp, imm);
		op8_write(cpu, rm, tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(4, 17);
}
static void xor_rm_reg(I8086* cpu) {
	/* xor r/m, reg (30/32/31/33) b001100DW */
	fetch_modrm(cpu);
	if (W) {
		exec_bin_op16(cpu, alu_xor16);
	}
	else {
		exec_bin_op8(cpu, alu_xor8);
	}
	TRANSFERS_RM_D(0, 2, 1);
	CYCLES_RM_D(3, 16, 9);
}
static void xor_accum_imm(I8086* cpu) {
	/* xor AL/AX, imm (34/35) b0011010W */
	if (W) {
		uint16_t imm = fetch_word(cpu);
		alu_xor16(cpu, &AX, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		alu_xor8(cpu, &AL, imm);
	}
	CYCLES(4);
}

static void cmp_rm_imm(I8086* cpu) {
	/* cmp r/m, imm (80/81/82/83, R/M reg = b111)  b100000SW */
	if (W) {
		if (SW == 0b01) {
			/* reg16, disp16 - 0x81 */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint16_t imm = fetch_word(cpu);
			alu_cmp16(cpu, tmp, imm);
		}
		else {
			/* reg16, disp8 - 0x83 is 8bit sign extended to 16bit */
			OPERAND16 rm = modrm_get_op16(cpu);
			uint16_t tmp = op16_read(cpu, rm);
			uint8_t imm = fetch_byte(cpu);
			uint16_t se = sign_extend8_16(imm);
			alu_cmp16(cpu, tmp, se);
		}
	}
	else {
		/* reg8, disp8 - 0x80, 0x82 */
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		uint8_t imm = fetch_byte(cpu);
		alu_cmp8(cpu, tmp, imm);
	}
	TRANSFERS_RM(0, 1);
	CYCLES_RM(4, 10);
}
static void cmp_rm_reg(I8086* cpu) {
	/* cmp r/m, reg (38/39/3A/3B) b001110DW */
	fetch_modrm(cpu);
	if (W) {
		exec_bin_op16_ro(cpu, alu_cmp16);
	}
	else {
		exec_bin_op8_ro(cpu, alu_cmp8);
	}
	TRANSFERS_RM(0, 1);
	CYCLES_RM(3, 9);
}
static void cmp_accum_imm(I8086* cpu) {
	/* cmp AL/AX, imm (3C/3D) b0011110W */
	if (W) {
		uint16_t imm = fetch_word(cpu);
		alu_cmp16(cpu, AX, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		alu_cmp8(cpu, AL, imm);
	}
	CYCLES(4);
}

static void test_rm_imm(I8086* cpu) {
	/* test r/m, imm (F6/F7, R/M reg = b000) b1111011W */
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		uint16_t imm = fetch_word(cpu);
		alu_test16(cpu, tmp, imm);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		uint8_t imm = fetch_byte(cpu);
		alu_test8(cpu, tmp, imm);
	}
	CYCLES_RM(5, 11);
}
static void test_rm_reg(I8086* cpu) {
	/* test r/m, reg (84/85) b1000010W */
	fetch_modrm(cpu);
	if (W) {
		exec_bin_op16_ro(cpu, alu_test16);
	}
	else {
		exec_bin_op8_ro(cpu, alu_test8);
	}
	TRANSFERS_RM(0, 1);
	CYCLES_RM(3, 9);
}
static void test_accum_imm(I8086* cpu) {
	/* test AL/AX, imm (A8/A9) b1010100W */
	if (W) {
		uint16_t imm = fetch_word(cpu);
		alu_test16(cpu, AX, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		alu_test8(cpu, AL, imm);
	}
	CYCLES(4);
}

static void daa(I8086* cpu) {
	/* Decimal Adjust for Addition (27) b00100111 */
	alu_daa(cpu, &AL);
	CYCLES(4);
}
static void das(I8086* cpu) {
	/* Decimal Adjust for Subtraction (2F) b00101111 */
	alu_das(cpu, &AL);
	CYCLES(4);
}
static void aaa(I8086* cpu) {
	/* ASCII Adjust for Addition (37) b00110111 */
	alu_aaa(cpu, &AL, &AH);
	CYCLES(4);
}
static void aas(I8086* cpu) {
	/* ASCII Adjust for Subtraction (3F) b00111111 */
	alu_aas(cpu, &AL, &AH);
	CYCLES(4);
}
static void aam(I8086* cpu) {
	/* ASCII Adjust for Multiply (D4 0A) b11010100 00001010 */
	uint8_t divisor = fetch_byte(cpu); // undocumented operand; normally 0x0A
	alu_aam(cpu, &AL, &AH, divisor);
	CYCLES(83);
}
static void aad(I8086* cpu) {
	/* ASCII Adjust for Division (D5 0A) b11010101 00001010 */
	uint8_t divisor = fetch_byte(cpu); // undocumented operand; normally 0x0A
	alu_aad(cpu, &AL, &AH, divisor);
	CYCLES(60);
}
static void salc(I8086* cpu) {
	/* set carry in AL (D6) b11010110 undocumented opcode */
	if (CF) {
		AL = 0xFF;
	}
	else {
		AL = 0;
	}
	CYCLES(4); // ????
}

static void push_seg(I8086* cpu) {
	/* Push seg16 (06/0E/16/1E) b000SR110 */
	push_word(cpu, cpu->segments[SR]);
	TRANSFERS(1);
	CYCLES(10);
}
static void pop_seg(I8086* cpu) {
	/* Pop seg16 (07/0F/17/1F) b000SR111 */
	pop_word(cpu, &cpu->segments[SR]);
	TRANSFERS(1);
	CYCLES(8);

	/* Interrupts Following 'POP SS' May Corrupt Memory. On early Intel 8088 processors
		(marked "INTEL '78" or "(C) 1978"), if an interrupt occurs immediately after a
		'POP SS' instruction, data may be pushed using an incorrect stack address,
		resulting in memory corruption. */
	cpu->int_delay = 1;
}
static void push_reg(I8086* cpu) {
	/* Push reg16 (50-57) b01010REG */

	/* NOTE: SP needs to be decemented prior to reading the register.
		This is so when pushing SP the NEW SP is pushed. */

	SP -= 2;
	write_word(cpu, SS, SP, reg16_read(cpu, cpu->opcode));
	TRANSFERS(1);
	CYCLES(11);
}
static void pop_reg(I8086* cpu) {
	/* Pop reg16 (58-5F) b01011REG */

	/* NOTE: SP needs to be incemented after reading the value from memory.
		This is so when popping SP the OLD SP is popped. */

	uint16_t tmp = read_word(cpu, SS, SP);
	SP += 2;
	reg16_write(cpu, cpu->opcode, tmp);
	TRANSFERS(1);
	CYCLES(8);
}
static void push_rm(I8086* cpu) {
	/* Push R/M (FE/FF, R/M reg = 110) b1111111W */

	/* NOTE: SP needs to be decemented prior to reading the register.
		This is so when pushing SP the NEW SP is pushed. */

	if (W) {
		OPERAND16 op16 = modrm_get_op16(cpu);
		push_op16(cpu, op16);
	}
	else {
		/* Undocumented instruction; PUSH byte R/M. */
		OPERAND8 op8 = modrm_get_op8(cpu);
		push_op8(cpu, op8);
	}
	TRANSFERS(2);
	CYCLES(16);
}
static void pop_rm(I8086* cpu) {
	/* Pop R/M (8F) b10001111 */

	/* NOTE: SP needs to be incemented after reading the value from memory.
		This is so when popping SP the OLD SP is popped. */

	fetch_modrm(cpu);
	OPERAND16 op16 = modrm_get_op16(cpu);
	pop_op16(cpu, op16);

	TRANSFERS(2);
	CYCLES(17);
}
static void pushf(I8086* cpu) {
	/* push psw (9C) b10011100 */
	PSW &= 0xFFD7;
	push_word(cpu, PSW);
	TRANSFERS(1);
	CYCLES(10);
}
static void popf(I8086* cpu) {
	/* pop psw (9D) b10011101 */
	uint16_t psw = 0;
	pop_word(cpu, &psw);
	PSW = (psw | 0xF002) & 0xFFD7;
	TRANSFERS(1);
	CYCLES(8);
}

static void nop(I8086* cpu) {
	/* nop (90) b10010000 */
	(void)cpu;
	CYCLES(3);
}
static void xchg_accum_reg(I8086* cpu) {
	/* xchg AX, reg16 (91 - 97) b10010REG */	
	uint16_t tmp = AX;
	AX = reg16_read(cpu, cpu->opcode);
	reg16_write(cpu, cpu->opcode, tmp);
	CYCLES(3);
}
static void xchg_rm_reg(I8086* cpu) {
	/* xchg R/M, reg16 (86/87) b1000011W */
	fetch_modrm(cpu);
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t reg = reg16_read(cpu, cpu->modrm.reg);
		uint16_t tmp = op16_read(cpu, rm);
		op16_write(cpu, rm, reg);
		reg16_write(cpu, cpu->modrm.reg, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t reg = reg8_read(cpu, cpu->modrm.reg);
		uint8_t tmp = op8_read(cpu, rm);
		op8_write(cpu, rm, reg);
		reg8_write(cpu, cpu->modrm.reg, tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(4, 17);
}

static void cbw(I8086* cpu) {
	/* Convert byte to word (98) b10011000 */
	if (AL & 0x80) {
		AH = 0xFF;
	}
	else {
		AH = 0;
	}
	CYCLES(2);
}
static void cwd(I8086* cpu) {
	/* Convert word to dword (99) b10011001 */
	if (AX & 0x8000) {
		DX = 0xFFFF;
	}
	else {
		DX = 0;
	}
	CYCLES(5);
}

static void wait(I8086* cpu) {
	/* wait (9B) b10011011 */
	//if (!cpu->test) {
		//IP -= cpu->instruction_len;
	//}
	CYCLES(4);
}

static void sahf(I8086* cpu) {
	/* Store AH into flags (9E) b10011110 */
	PSW &= 0xFF02; /* Mask hi byte; Clear bit 2 */
	PSW |= AH & 0xD5;
	CYCLES(4);
}
static void lahf(I8086* cpu) {
	/* Load flags into AH (9F) b10011111 */
	AH = PSW & 0xD7;
	CYCLES(4);
}

static void hlt(I8086* cpu) {
	/* Halt CPU (F4) b11110100 */
	IP -= cpu->instruction_len;
	CYCLES(2);
}
static void cmc(I8086* cpu) {
	// Complement carry flag (F5) b11110101
	CF = !CF;
	CYCLES(2);
}
static void clc(I8086* cpu) {
	// clear carry flag (F8) b11111000
	CF = 0;
	CYCLES(2);
}
static void stc(I8086* cpu) {
	// set carry flag (F9) b11111001
	CF = 1;
	CYCLES(2);
}
static void cli(I8086* cpu) {
	// clear interrupt flag (FA) b11111010
	IF = 0;
	CYCLES(2);
}
static void sti(I8086* cpu) {
	// set interrupt flag (FB) b1111011
	IF = 1;
	CYCLES(2);
}
static void cld(I8086* cpu) {
	// clear direction flag (FC) b11111100
	DF = 0;
	CYCLES(2);
}
static void std(I8086* cpu) {
	// set direction flag (FD) b11111101
	DF = 1;
	CYCLES(2);
}

static void inc_reg(I8086* cpu) {
	/* Inc reg16 (40-47) b01000REG */
	uint16_t tmp = reg16_read(cpu, cpu->opcode);
	alu_inc16(cpu, &tmp);
	reg16_write(cpu, cpu->opcode, tmp);
	CYCLES(2);
}
static void inc_rm(I8086* cpu) {
	/* Inc R/M (FE/FF, R/M reg = 000) b1111111W */
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_inc16(cpu, &tmp);
		op16_write(cpu, rm, tmp);
		CYCLES_RM(2, 15);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_inc8(cpu, &tmp);
		op8_write(cpu, rm, tmp);
		CYCLES_RM(3, 15);
	}
	TRANSFERS_RM(0, 2);
}

static void dec_reg(I8086* cpu) {
	/* Dec reg16 (48-4F) b01001REG */
	uint16_t tmp = reg16_read(cpu, cpu->opcode);
	alu_dec16(cpu, &tmp);
	reg16_write(cpu, cpu->opcode, tmp);
	CYCLES(2);
}
static void dec_rm(I8086* cpu) {
	/* Dec R/M (FE/FF, R/M reg = 001) b1111111W */
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_dec16(cpu, &tmp);
		op16_write(cpu, rm, tmp);
		CYCLES_RM(2, 15);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_dec8(cpu, &tmp);
		op8_write(cpu, rm, tmp);
		CYCLES_RM(3, 15);
	}
	TRANSFERS_RM(0, 2);
}

static void rol(I8086* cpu) {
	/* Rotate left (D0/D1/D2/D3, R/M reg = 000) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_rol16(cpu, &tmp, count);
		op16_write(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_rol8(cpu, &tmp, count);
		op8_write(cpu, rm, tmp);
	}

	if (VW) {
		CYCLES((4 * count));
		CYCLES_RM(8, 20);
	}
	else {
		CYCLES_RM(2, 15);
	}
	TRANSFERS_RM(0, 2);
}
static void ror(I8086* cpu) {
	/* Rotate left (D0/D1/D2/D3, R/M reg = 001) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_ror16(cpu, &tmp, count);
		op16_write(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_ror8(cpu, &tmp, count);
		op8_write(cpu, rm, tmp);
	}

	if (VW) {
		CYCLES((4 * count));
		CYCLES_RM(8, 20);
	}
	else {
		CYCLES_RM(2, 15);
	}
	TRANSFERS_RM(0, 2);
}
static void rcl(I8086* cpu) {
	/* Rotate through carry left (D0/D1/D2/D3, R/M reg = 010) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_rcl16(cpu, &tmp, count);
		op16_write(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_rcl8(cpu, &tmp, count);
		op8_write(cpu, rm, tmp);
	}

	if (VW) {
		CYCLES((4 * count));
		CYCLES_RM(8, 20);
	}
	else {
		CYCLES_RM(2, 15);
	}
	TRANSFERS_RM(0, 2);
}
static void rcr(I8086* cpu) {
	/* Rotate through carry right (D0/D1/D2/D3, R/M reg = 011) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_rcr16(cpu, &tmp, count);
		op16_write(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_rcr8(cpu, &tmp, count);
		op8_write(cpu, rm, tmp);
	}

	if (VW) {
		CYCLES((4 * count));
		CYCLES_RM(8, 20);
	}
	else {
		CYCLES_RM(2, 15);
	}
	TRANSFERS_RM(0, 2);
}
static void shl(I8086* cpu) {
	/* Shift left (D0/D1/D2/D3, R/M reg = 100) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_shl16(cpu, &tmp, count);
		op16_write(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_shl8(cpu, &tmp, count);
		op8_write(cpu, rm, tmp);
	}

	if (VW) {
		CYCLES((4 * count));
		CYCLES_RM(8, 20);
	}
	else {
		CYCLES_RM(2, 15);
	}
	TRANSFERS_RM(0, 2);
}
static void shr(I8086* cpu) {
	/* Shift Logical right (D0/D1/D2/D3, R/M reg = 101) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_shr16(cpu, &tmp, count);
		op16_write(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_shr8(cpu, &tmp, count);
		op8_write(cpu, rm, tmp);
	}

	if (VW) {
		CYCLES((4 * count));
		CYCLES_RM(8, 20);
	}
	else {
		CYCLES_RM(2, 15);
	}
	TRANSFERS_RM(0, 2);
}
static void sar(I8086* cpu) {
	/* Shift Arithmetic right (D0/D1/D2/D3, R/M reg = 111) b110100VW */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_sar16(cpu, &tmp, count);
		op16_write(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_sar8(cpu, &tmp, count);
		op8_write(cpu, rm, tmp);
	}

	if (VW) {
		CYCLES((4 * count));
		CYCLES_RM(8, 20);
	}
	else {
		CYCLES_RM(2, 15);
	}
	TRANSFERS_RM(0, 2);
}

static void setmo(I8086* cpu) {
	/* Set Minus One (D0/D1/D2/D3, R/M reg = 110) b110100VW (undocumented) */
	uint8_t count = 1;
	if (VW) {
		count = CL;
	}

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_setmo16(cpu, &tmp, count);
		op16_write(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_setmo8(cpu, &tmp, count);
		op8_write(cpu, rm, tmp);
	}
}

static int jump_condition(I8086* cpu) {
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
static void jcc(I8086* cpu) {
	/* conditional jump(70-7F) b011XCCCC
	   8086 cpu decode 60-6F the same as 70-7F */
	uint8_t imm = fetch_byte(cpu);
	if (jump_condition(cpu)) {
		uint16_t offset = sign_extend8_16(imm);
		IP += offset;
		CYCLES(16);
	}
	else {
		CYCLES(4);
	}
}
static void jcxz(I8086* cpu) {
	/* jump if CX zero (E3) b11100011 */
	uint8_t imm = fetch_byte(cpu);
	if (CX == 0) {
		uint16_t offset = sign_extend8_16(imm);
		IP += offset;
		CYCLES(18);
	}
	else {
		CYCLES(6);
	}
}

static void jmp_intra_direct_short(I8086* cpu) {
	/* Jump short imm8 (EB) b11101011 */
	uint8_t imm = fetch_byte(cpu);
	uint16_t se = sign_extend8_16(imm);
	IP += se;
	CYCLES(15);
}
static void jmp_intra_direct(I8086* cpu) {
	/* Jump near  imm16 (E9) b11101001 */
	uint16_t imm = fetch_word(cpu);
	IP += imm;
	CYCLES(15);
}
static void jmp_inter_direct(I8086* cpu) {
	/* Jump far addr:seg (EA) b11101010 */
	uint16_t imm = fetch_word(cpu);
	uint16_t imm2 = fetch_word(cpu);
	IP = imm;
	CS = imm2;
	CYCLES(15);
}

static void jmp_intra_indirect(I8086* cpu) {
	/* Jump near indirect (FE/FF, R/M reg = 100) b1111111W */

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		IP = op16_read(cpu, rm);
	}
	else {
		/* Undocumented instruction; JMP near byte R/M.
			This is based on the JSON 8086 v2 undefined opcode tests.
			02.09.2025 - tommojphillips */

		OPERAND8 rm = modrm_get_op8(cpu);
		if (cpu->modrm.mod == 0b11) {
			/* For a 8bit register, the pair is actually reversed;
				Toggle bit3 in the rm field of the modrm byte to
				access the other 8bit register in the pair. */
			cpu->modrm.rm ^= 0x4;
			OPERAND8 rm2 = modrm_get_op8(cpu);
			IP = op8_read(cpu, rm);
			IP |= ((uint16_t)op8_read(cpu, rm2) << 8);
		}
		else {
			/* For a memory location; Set the hi byte to FF. */
			IP = 0xFF00 | op8_read(cpu, rm);
		}
	}

	TRANSFERS_RM(0, 1);
	CYCLES_RM(11, 18);
}
static int jmp_inter_indirect(I8086* cpu) {
	/* Jump far indirect (FE/FF, R/M reg = 101) b1111111W */

	if (W) {
		if (cpu->modrm.mod == 0b11) {
			/* Undocumented instruction; JMP far word R/M.
			This instruction cannot be traditionally emulated when
			register operands are specified. Due to	assumptions
			made by the microcode, this routine	uses the internal
			register tmpb which is stale/uninitialized. */
			return I8086_DECODE_UNDEFINED;
		}
		else {
			uint16_t segment = modrm_get_segment(cpu);
			uint16_t offset = modrm_get_offset(cpu);
			IP = read_word(cpu, segment, offset);
			CS = read_word(cpu, segment, offset + 2);
		}
	}
	else {
		if (cpu->modrm.mod == 0b11) {
			/* Undocumented instruction; JMP far byte R/M.
			This instruction cannot be traditionally emulated when
			register operands are specified. Due to	assumptions
			made by the microcode, this routine	uses the internal
			register tmpb which is stale/uninitialized. */
			return I8086_DECODE_UNDEFINED;
		}
		else {
			/* Undocumented instruction; JMP far byte R/M. */

			/* Get Mod R/M offset; fetch displacement (if applicable); incrementing IP. */
			uint16_t offset = modrm_get_offset(cpu);

			/* IP is read respecting segment override. Set the hi byte to FF. */
			uint16_t segment = modrm_get_segment(cpu);
			IP = 0xFF00 | read_byte(cpu, segment, offset);

			/* CS is read disregarding segment override. Set the hi byte to FF. */
			cpu->segment_prefix = 0xFF;
			segment = modrm_get_segment(cpu);
			CS = 0xFF00 | read_byte(cpu, segment, offset);
		}
	}
	TRANSFERS(2);
	CYCLES(24);
	return I8086_DECODE_OK;
}

static void call_intra_direct(I8086* cpu) {
	/* Call disp (E8) b11101000 */

	/* NOTE: We need to read ip prior to pushing it.
		This is so if SP = [R/M],
		it doesn't write over it when pushing ip.
		This is based on the JSON 8088 tests. */

	uint16_t imm = fetch_word(cpu);
	push_word(cpu, IP);
	IP += imm;
	TRANSFERS(1);
	CYCLES(19);
}
static void call_inter_direct(I8086* cpu) {
	/* Call addr:seg (9A) b10011010 */

	/* NOTE: We need to read ip,cs prior to pushing them.
		This is so if SP = [R/M],
		it doesn't write over it when pushing ip,cs.
		This is based on the JSON 8088 tests. */

	uint16_t ip = fetch_word(cpu);
	uint16_t cs = fetch_word(cpu);
	push_word(cpu, CS);
	push_word(cpu, IP);
	IP = ip;
	CS = cs;
	TRANSFERS(2);
	CYCLES(28);
}

static void call_intra_indirect(I8086* cpu) {
	/* Call near R/M (FE/FF, R/M reg = 010) b1111111W */

	uint16_t ip = 0;

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		ip = op16_read(cpu, rm);
		push_word(cpu, IP);
	}
	else {
		/* Undocumented instruction; CALL near byte R/M. */

		OPERAND8 rm = modrm_get_op8(cpu);
		if (cpu->modrm.mod == 0b11) {
			/* For a 8bit register, the pair is actually reversed;
				Toggle bit3 in the rm field of the modrm byte to
				access the other 8bit register in the pair. */
			cpu->modrm.rm ^= 0x4;
			OPERAND8 rm2 = modrm_get_op8(cpu);
			ip = op8_read(cpu, rm);
			ip |= ((uint16_t)op8_read(cpu, rm2) << 8);
		}
		else {
			/* For a memory location; Set the hi byte to FF. */
			ip = 0xFF00 | op8_read(cpu, rm);
		}

		/* Only the lo byte of IP is pushed to the stack. */
		push_byte(cpu, IP & 0xFF);
	}
	IP = ip;

	TRANSFERS_RM(1, 2);
	CYCLES_RM(16, 21);
}
static int call_inter_indirect(I8086* cpu) {
	/* Call far R/M (FE/FF, R/M reg = 011) b1111111W */
		
	uint16_t ip = 0;
	uint16_t cs = 0;

	if (W) {
		if (cpu->modrm.mod == 0b11) {
			/* Undocumented instruction; CALL far word R/M.
			This instruction cannot be traditionally emulated when
			register operands are specified. Due to	assumptions
			made by the microcode, this routine	uses the internal
			register tmpb which is stale/uninitialized. */
			return I8086_DECODE_UNDEFINED;
		}
		else {
			uint16_t segment = modrm_get_segment(cpu);
			uint16_t offset = modrm_get_offset(cpu);
			ip = read_word(cpu, segment, offset);
			cs = read_word(cpu, segment, offset + 2);
		}

		push_word(cpu, CS);
		push_word(cpu, IP);
	}
	else {		
		if (cpu->modrm.mod == 0b11) {
			/* Undocumented instruction; CALL far byte R/M.
			This instruction cannot be traditionally emulated when
			register operands are specified. Due to	assumptions
			made by the microcode, this routine	uses the internal
			register tmpb which is stale/uninitialized. */
			return I8086_DECODE_UNDEFINED;
		}
		else {
			/* Undocumented instruction; CALL far byte R/M. */

			/* Get Mod R/M offset; fetch displacement (if applicable); incrementing IP. */
			uint16_t offset = modrm_get_offset(cpu);

			/* IP is read respecting segment override. Set the hi byte to FF. */
			uint16_t segment = modrm_get_segment(cpu);
			ip = 0xFF00 | read_byte(cpu, segment, offset);

			/* CS is read disregarding segment override. Set the hi byte to FF. */
			cpu->segment_prefix = 0xFF;
			segment = modrm_get_segment(cpu);
			cs = 0xFF00 | read_byte(cpu, segment, offset);
		}

		/* Only the lo byte of CS and IP are pushed to the stack. */
		push_byte(cpu, CS & 0xFF);
		push_byte(cpu, IP & 0xFF);
	}

	IP = ip;
	CS = cs;
	TRANSFERS(4);
	CYCLES(37);
	return I8086_DECODE_OK;
}

static void ret_intra_add_imm(I8086* cpu) {
	/* Ret imm16 (C2) b110000X0 - undocumented* on 8086 C0 decodes identically to C2 */
	uint16_t imm = fetch_word(cpu);
	pop_word(cpu, &IP);
	SP += imm; 
	TRANSFERS(1);
	CYCLES(12);
}
static void ret_intra(I8086* cpu) {
	/* Ret (C3) b110000X1 - undocumented* on 8086 C1 decodes identically to C3 */
	pop_word(cpu, &IP);
	TRANSFERS(1);
	CYCLES(8);
}
static void ret_inter_add_imm(I8086* cpu) {
	/* Ret imm16 (CA) b110010X0 - undocumented* on 8086 C8 decodes identically to CA */
	uint16_t imm = fetch_word(cpu);
	pop_word(cpu, &IP);
	pop_word(cpu, &CS);
	SP += imm;
	TRANSFERS(2);
	CYCLES(17);
}
static void ret_inter(I8086* cpu) {
	/* Ret (CB) b110010X1 - undocumented* on 8086 C9 decodes identically to CB */
	pop_word(cpu, &IP);
	pop_word(cpu, &CS);
	TRANSFERS(2);
	CYCLES(18);
}

static void mov_rm_imm(I8086* cpu) {
	/* mov r/m, imm (C6/C7) b1100011W */
	fetch_modrm(cpu);
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t imm = fetch_word(cpu);
		op16_write(cpu, rm, imm);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t imm = fetch_byte(cpu);
		op8_write(cpu, rm, imm);
	}
	TRANSFERS_RM(0, 1);
	CYCLES_RM(4, 10);
}
static void mov_reg_imm(I8086* cpu) {
	/* mov r/m, reg (B0-BF) b1011WREG */
	if (WREG) {
		uint16_t imm = fetch_word(cpu);
		reg16_write(cpu, cpu->opcode, imm);
	}
	else {
		uint8_t imm = fetch_byte(cpu);
		reg8_write(cpu, cpu->opcode, imm);
	}
	CYCLES(4);
}
static void mov_rm_reg(I8086* cpu) {
	/* mov r/m, reg (88/89/8A/8B) b100010DW */
	fetch_modrm(cpu);
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		if (D) {
			uint16_t tmp = op16_read(cpu, rm);
			reg16_write(cpu, cpu->modrm.reg, tmp);
		}
		else {
			uint16_t tmp = reg16_read(cpu, cpu->modrm.reg);
			op16_write(cpu, rm, tmp);
		}
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		if (D) {
			uint8_t tmp = op8_read(cpu, rm);
			reg8_write(cpu, cpu->modrm.reg, tmp);
		}
		else {
			uint8_t tmp = reg8_read(cpu, cpu->modrm.reg);
			op8_write(cpu, rm, tmp);
		}
	}
	TRANSFERS_RM(0, 1);
	CYCLES_RM_D(2, 9, 8);
}
static void mov_accum_mem(I8086* cpu) {
	/* mov AL/AX, [mem] (A0/A1/A2/A3) b101000DW */
	uint16_t addr = fetch_word(cpu);
	if (W) {
		OPERAND16 mem = mem_get_op16(SEG_DEFAULT_OR_OVERRIDE(SEG_DS), addr);
		if (D) {
			uint16_t tmp = reg16_read(cpu, REG_AX);
			op16_write(cpu, mem, tmp);
		}
		else {
			uint16_t tmp = op16_read(cpu, mem);
			reg16_write(cpu, REG_AX, tmp);
		}
	}
	else {
		OPERAND8 mem = mem_get_op8(SEG_DEFAULT_OR_OVERRIDE(SEG_DS), addr);
		if (D) {
			uint8_t tmp = reg8_read(cpu, REG_AL);
			op8_write(cpu, mem, tmp);
		}
		else {
			uint8_t tmp = op8_read(cpu, mem);
			reg8_write(cpu, REG_AL, tmp);
		}
	}
	TRANSFERS(1);
	CYCLES(10);
}
static void mov_seg(I8086* cpu) {
	/* mov r/m, seg (8C/8E) b100011D0 */
	fetch_modrm(cpu);		
	OPERAND16 rm = modrm_get_op16(cpu);
	uint16_t* seg = GET_SEG(cpu->modrm.reg);

	if (D) {
		*seg = op16_read(cpu, rm);
	}
	else {
		op16_write(cpu, rm, *seg);
	}
	
	TRANSFERS_RM(0, 1);
	CYCLES_RM_D(2, 9, 8);

	if (D) {
		/* Interrupts Following 'MOV SS, XXX' May Corrupt Memory. On early Intel 8088 processors
			(marked "INTEL '78" or "(C) 1978"), if an interrupt occurs immediately after a 
			'MOV SS, XXX' instruction, data may be pushed using an incorrect stack address,
			resulting in memory corruption. */
		cpu->int_delay = 1;
	}
}

static void lea(I8086* cpu) {
	/* lea reg16, [r/m] (8D) b10001101 */
	fetch_modrm(cpu);
	uint16_t addr = modrm_get_offset(cpu);
	reg16_write(cpu, cpu->modrm.reg, addr);
	CYCLES(2);
}

static void not(I8086* cpu) {
	/* not reg (F6/F7, R/M reg = b010) b1111011W */
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		op16_write(cpu, rm, ~tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		op8_write(cpu, rm, ~tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(3, 16);
}
static void neg(I8086* cpu) {
	/* neg reg (F6/F7, R/M reg = b011) b1111011W */
	
	/* If the operand is zero, its sign is not changed.
	 Attempting to negate a byte containing -128 or 
	 a word containing -32,768 causes no change to 
	 the operand and sets OF. NEG updates AF, CF, OF,
	 PF, SF and ZF. CF is always set except when the
	 operand is zero, in which case it is cleared */

	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_neg16(cpu, &tmp);
		op16_write(cpu, rm, tmp);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_neg8(cpu, &tmp);
		op8_write(cpu, rm, tmp);
	}
	TRANSFERS_RM(0, 2);
	CYCLES_RM(3, 16);
}
static void mul_rm(I8086* cpu) {
	/* mul r/m (F6/F7, R/M reg = b100) b1111011W */
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_mul16(cpu, AX, tmp, &AX, &DX);
		TRANSFERS_RM(0, 1);
		CYCLES_RM(118, 224);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_mul8(cpu, AL, tmp, &AL, &AH);
		TRANSFERS_RM(0, 1);
		CYCLES_RM(70, 76);
	}
}
static void imul_rm(I8086* cpu) {
	/* imul r/m (F6/F7, R/M reg = b101) b1111011W */
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_imul16(cpu, AX, tmp, &AX, &DX);
		TRANSFERS_RM(0, 1);
		CYCLES_RM(128, 134);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_imul8(cpu, AL, tmp, &AL, &AH);
		TRANSFERS_RM(0, 1);
		CYCLES_RM(80, 86);
	}

}
static void div_rm(I8086* cpu) {
	/* div r/m (F6/F7, R/M reg = b110) b1111011W */
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_div16(cpu, AX, DX, tmp, &AX, &DX);
		TRANSFERS_RM(0, 1);
		CYCLES_RM(144, 150);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_div8(cpu, AL, AH, tmp, &AL, &AH);
		TRANSFERS_RM(0, 1);
		CYCLES_RM(80, 86);
	}
}
static void idiv_rm(I8086* cpu) {
	/* idiv r/m (F6/F7, R/M reg = b111) b1111011W */
	if (W) {
		OPERAND16 rm = modrm_get_op16(cpu);
		uint16_t tmp = op16_read(cpu, rm);
		alu_idiv16(cpu, AX, DX, tmp, &AX, &DX);
		TRANSFERS_RM(0, 1);
		CYCLES_RM(165, 171);
	}
	else {
		OPERAND8 rm = modrm_get_op8(cpu);
		uint8_t tmp = op8_read(cpu, rm);
		alu_idiv8(cpu, AL, AH, tmp, &AL, &AH);
		TRANSFERS_RM(0, 1);
		CYCLES_RM(101, 107);
	}

}

static int movs(I8086* cpu) {
	/* movs (A4/A5) b1010010W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return I8086_DECODE_OK;
		}
		CX -= 1;
	}

	/* Do string operation */
	if (W) {
		uint16_t src = read_word(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI);
		write_word(cpu, ES, DI, src);
	}
	else {
		uint8_t src = read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI);
		write_byte(cpu, ES, DI, src);
	}
	TRANSFERS(2);
	CYCLES(18);

	/* Adjust si/di delta */
	if (DF) {
		SI -= (1 << W);
		DI -= (1 << W);
	}
	else {
		SI += (1 << W);
		DI += (1 << W);
	}

	/* Rep prefix check */
	if (F1) {
		IP -= cpu->instruction_len; /* Allow interrupts */
	}
	return I8086_DECODE_OK;
}
static int stos(I8086* cpu) {
	/* stos (AA/AB) b1010101W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return I8086_DECODE_OK;
		}
		CX -= 1;
	}

	/* Do string operation */
	if (W) {
		write_word(cpu, ES, DI, AX);
	}
	else {
		write_byte(cpu, ES, DI, AL);
	}
	TRANSFERS(1);
	CYCLES(11);

	/* Adjust si/di delta */
	if (DF) {
		DI -= (1 << W);
	}
	else {
		DI += (1 << W);
	}

	/* Rep prefix check */
	if (F1) {
		IP -= cpu->instruction_len; /* Allow interrupts */
	}
	return I8086_DECODE_OK;
}
static int lods(I8086* cpu) {
	/* lods (AC/AD) b1010110W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return I8086_DECODE_OK;
		}
		CX -= 1;
		CYCLES(1); // +1 per REP
	}

	/* Do string operation */
	if (W) {
		AX = read_word(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI);
	}
	else {
		AL = read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI);
	}
	TRANSFERS(1);
	CYCLES(12);

	/* Adjust si/di delta */
	if (DF) {
		SI -= (1 << W);
	}
	else {
		SI += (1 << W);
	}

	/* Rep prefix check */
	if (F1) {
		IP -= cpu->instruction_len; /* Allow interrupts */
	}
	return I8086_DECODE_OK;
}
static int cmps(I8086* cpu) {
	/* cmps (A6/A7) b1010011W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return I8086_DECODE_OK;
		}
		CX -= 1;
	}

	/* Do string operation */
	if (W) {
		uint16_t src = read_word(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI);
		uint16_t dest = read_word(cpu, ES, DI);
		alu_cmp16(cpu, src, dest);
	}
	else {
		uint8_t src = read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), SI);
		uint8_t dest = read_byte(cpu, ES, DI);
		alu_cmp8(cpu, src, dest);
	}
	TRANSFERS(2);
	CYCLES(22);

	/* Adjust si/di delta */
	if (DF) {
		SI -= (1 << W);
		DI -= (1 << W);
	}
	else {
		SI += (1 << W);
		DI += (1 << W);
	}

	/* Rep prefix check */
	if (F1 && ZF == F1Z) {
		IP -= cpu->instruction_len; /* Allow interrupts */
	}
	return I8086_DECODE_OK;
}
static int scas(I8086* cpu) {
	/* scas (AE/AF) b1010111W */

	/* Rep prefix check */
	if (F1) {
		if (CX == 0) {
			return I8086_DECODE_OK;
		}
		CX -= 1;
	}

	/* Do string operation */
	if (W) {
		uint16_t dest = read_word(cpu, ES, DI);
		alu_cmp16(cpu, AX, dest);
	}
	else {
		uint8_t dest = read_byte(cpu, ES, DI);
		alu_cmp8(cpu, AL, dest);
	}
	TRANSFERS(1);
	CYCLES(15);

	/* Adjust si/di delta */
	if (DF) {
		DI -= (1 << W);
	}
	else {
		DI += (1 << W);
	}

	/* Rep prefix check */
	if (F1 && ZF == F1Z) {
		IP -= cpu->instruction_len; /* Allow interrupts */
	}
	return I8086_DECODE_OK;
}

static void les(I8086* cpu) {
	/* les (C4) b11000100 */
	fetch_modrm(cpu);
	uint16_t segment = modrm_get_segment(cpu);
	uint16_t offset = modrm_get_offset(cpu);
	uint16_t tmp = read_word(cpu, segment, offset);
	reg16_write(cpu, cpu->modrm.reg, tmp);
	ES = read_word(cpu, segment, offset + 2);
	TRANSFERS(2);
	CYCLES(16);
}
static void lds(I8086* cpu) {
	/* lds (C5) b11000101 */
	fetch_modrm(cpu);
	uint16_t segment = modrm_get_segment(cpu);
	uint16_t offset = modrm_get_offset(cpu);
	uint16_t tmp = read_word(cpu, segment, offset);
	reg16_write(cpu, cpu->modrm.reg, tmp);
	DS = read_word(cpu, segment, offset + 2);
	TRANSFERS(2);
	CYCLES(16);
}

static void xlat(I8086* cpu) {
	/* Get data pointed by BX + AL (D7) b11010111 */
	uint8_t mem = read_byte(cpu, SEG_DEFAULT_OR_OVERRIDE(SEG_DS), BX + AL);
	AL = mem;
	TRANSFERS(1);
	CYCLES(11);
}

static void esc(I8086* cpu) {
	/* esc (D8-DF R/M reg = XXX) b11010REG */
	fetch_modrm(cpu);
	if (cpu->modrm.mod != 0b11) {
		uint8_t esc_opcode = ((cpu->opcode & 7) << 3) | cpu->modrm.reg;
		uint16_t reg = reg16_read(cpu, cpu->opcode);
		OPERAND16 rm = modrm_get_op16(cpu);
		(void)esc_opcode;
		(void)reg;
		(void)rm;
	}
}

static void loopnz(I8086* cpu) {
	/* loop while not zero (E0) b1110000Z */
	uint8_t imm = fetch_byte(cpu);
	uint16_t se = sign_extend8_16(imm);
	CX -= 1;
	if (CX && !ZF) {
		IP += se;
		CYCLES(19);
	}
	else {
		CYCLES(5);
	}
}
static void loopz(I8086* cpu) {
	/* loop while zero (E1) b1110000Z */
	uint8_t imm = fetch_byte(cpu);
	uint16_t se = sign_extend8_16(imm);
	CX -= 1;
	if (CX && ZF) {
		IP += se;
		CYCLES(18);
	}
	else {
		CYCLES(6);
	}
}
static void loop(I8086* cpu) {
	/* loop if CX not zero (E2) b11100010 */
	uint8_t imm = fetch_byte(cpu);
	uint16_t se = sign_extend8_16(imm);
	CX -= 1;
	if (CX) {
		IP += se;
		CYCLES(17);
	}
	else {
		CYCLES(5);
	}
}

static void in_accum_imm(I8086* cpu) {
	/* in AL/AX, imm */
	uint8_t imm = fetch_byte(cpu);
	if (W) {
		AX = (READ_IO_BYTE(imm) | (READ_IO_BYTE(imm + 1) << 8));
	}
	else {
		AL = READ_IO_BYTE(imm);
	}
	TRANSFERS(1);
	CYCLES(10);
}
static void out_accum_imm(I8086* cpu) {
	/* out imm, AL/AX */
	uint8_t imm = fetch_byte(cpu);
	if (W) {
		WRITE_IO_BYTE(imm, AL);
		WRITE_IO_BYTE(imm + 1, AH);
	}
	else {
		WRITE_IO_BYTE(imm, AL);
	}
	TRANSFERS(1);
	CYCLES(10);
}
static void in_accum_dx(I8086* cpu) {
	/* in AL/AX, DX */
	if (W) {
		AX = (READ_IO_BYTE(DX) | (READ_IO_BYTE(DX + 1) << 8));
	}
	else {
		AL = READ_IO_BYTE(DX);
	}
	TRANSFERS(1);
	CYCLES(8);
}
static void out_accum_dx(I8086* cpu) {
	/* out DX, AL/AX */
	if (W) {
		WRITE_IO_BYTE(DX, AL);
		WRITE_IO_BYTE(DX + 1, AH);
	}
	else {
		WRITE_IO_BYTE(DX, AL);
	}
	TRANSFERS(1);
	CYCLES(8);
}

static void int_(I8086* cpu) {
	/* interrupt CD b11001101 */	
	uint8_t type = fetch_byte(cpu);
	i8086_int(cpu, type);
	TRANSFERS(5);
	CYCLES(51);	
}
static void int3(I8086* cpu) {
	/* interrupt CC b11001100 */
	i8086_int(cpu, INT_3);
	TRANSFERS(5);
	CYCLES(52);
}
static void into(I8086* cpu) {
	/* interrupt on overflow (CE) b11001110 */
	if (OF) {
		i8086_int(cpu, INT_OVERFLOW);
		TRANSFERS(5);
		CYCLES(53);
	}
	else {
		CYCLES(4);
	}
}
static void iret(I8086* cpu) {
	/* return from interrupt (CF) b11001111 */
	pop_word(cpu, &IP);
	pop_word(cpu, &CS);
	uint16_t psw = 0;
	pop_word(cpu, &psw);
	PSW = (psw | 0xF002) & 0xFFD7;
	TRANSFERS(3);
	CYCLES(24);
}

/* prefix byte */
static int rep(I8086* cpu) {
	/* rep/repz/repnz (F2/F3) b1111001Z */
	cpu->internal_flags |= INTERNAL_FLAG_F1;    /* Set F1 */
	cpu->internal_flags &= ~INTERNAL_FLAG_F1Z;  /* Clr F1Z */
	cpu->internal_flags |= (cpu->opcode & 0x1); /* Set F1Z */
	
	cpu->opcode = fetch_byte(cpu);
	CYCLES(9);
	return I8086_DECODE_REQ_CYCLE;
}
static int segment_override(I8086* cpu) {
	/* (26/2E/36/3E) b001SR110 */
	cpu->segment_prefix = SR;
	cpu->opcode = fetch_byte(cpu);
	CYCLES(2);
	return I8086_DECODE_REQ_CYCLE;
}
static int lock(I8086* cpu) {
	/* lock the bus (F0/F1) b11110000 */
	cpu->opcode = fetch_byte(cpu);
	CYCLES(2);
	return I8086_DECODE_REQ_CYCLE;
}

/* Fetch next opcode */
static void i8086_fetch(I8086* cpu) {
	cpu->internal_flags = 0;
	cpu->modrm.byte = 0;
	cpu->segment_prefix = 0xFF;
	cpu->instruction_len = 0;
	cpu->opcode = fetch_byte(cpu);
}

/* decode opcode */
static void i8086_decode_opcode_80(I8086* cpu) {
	/* 0x80 - 0x83 b100000SW (Immed) */
	fetch_modrm(cpu);
	switch (cpu->modrm.reg) {
		case 0b000: // ADD
			add_rm_imm(cpu);
			break;
		case 0b001: // OR
			or_rm_imm(cpu);
			break;
		case 0b010: // ADC
			adc_rm_imm(cpu);
			break;
		case 0b011: // SBB
			sbb_rm_imm(cpu);
			break;
		case 0b100: // AND
			and_rm_imm(cpu);
			break;
		case 0b101: // SUB
			sub_rm_imm(cpu);
			break;
		case 0b110: // XOR
			xor_rm_imm(cpu);
			break;
		case 0b111: // CMP
			cmp_rm_imm(cpu);
			break;
	}
}
static void i8086_decode_opcode_d0(I8086* cpu) {
	/* 0xD0 - 0xD3 b110100VW (Shift) */
	fetch_modrm(cpu);
	switch (cpu->modrm.reg) {
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
		case 0b110: /* 8086 undocumented; Set Minus One (-1) */
			setmo(cpu);
			break;
		case 0b111:
			sar(cpu);
			break;
	}
}
static void i8086_decode_opcode_f6(I8086* cpu) {
	/* F6/F7 b1111011W (Group 1) */
	fetch_modrm(cpu);
	switch (cpu->modrm.reg) {
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
static int i8086_decode_opcode_fe(I8086* cpu) {
	/* FE/FF b1111111W (Group 2) */
	fetch_modrm(cpu);
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
			return call_inter_indirect(cpu);
		case 0b100:
			jmp_intra_indirect(cpu);
			break;
		case 0b101:
			return jmp_inter_indirect(cpu);
		case 0b110:
			push_rm(cpu);
			break;
		case 0b111: /* 8086 undocumented; Decodes identically to b110 */
			push_rm(cpu);
			break;
	}
	return I8086_DECODE_OK;
}

static int i8086_decode_opcode(I8086* cpu) {
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
		case 0x0F: /* pop cs; 8086 undocumented */
			pop_seg(cpu);
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

		/* 8086 undocumented; 0x60-0x6F decodes identically to 0x70-0x7F on 8086 (b111X CCCC) */
		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
		case 0x64:
		case 0x65:
		case 0x66:
		case 0x67:
		case 0x68:
		case 0x69:
		case 0x6A:
		case 0x6B:
		case 0x6C:
		case 0x6D:
		case 0x6E:
		case 0x6F:
			jcc(cpu);
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
			i8086_decode_opcode_80(cpu);
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
			return movs(cpu);
		case 0xA6:
		case 0xA7:
			return cmps(cpu);
		case 0xA8:
		case 0xA9:
			test_accum_imm(cpu);
			break;
		case 0xAA:
		case 0xAB:
			return stos(cpu);
		case 0xAC:
		case 0xAD:
			return lods(cpu);
		case 0xAE:
		case 0xAF:
			return scas(cpu);

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

		case 0xC0: /* 8086 undocumented; on 8086 0xC0 decodes identically to 0xC2 (b1100 00X0) */
		case 0xC2:
			ret_intra_add_imm(cpu);
			break;
		case 0xC1: /* 8086 undocumented; on 8086 0xC1 decodes identically to 0xC3 (b1100 00X1) */
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
		case 0xC8: /* 8086 undocumented; on 8086 0xC8 decodes identically to 0xCA (b1100 10X0) */
		case 0xCA:
			ret_inter_add_imm(cpu);
			break;
		case 0xC9: /* 8086 undocumented; on 8086 0xC9 decodes identically to 0xCB (b1100 10X1) */
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
			i8086_decode_opcode_d0(cpu);
			break;
		case 0xD4:
			aam(cpu);
			break;
		case 0xD5:
			aad(cpu);
			break;
		case 0xD6: /* 8086 undocumented; Set AL to Carry */
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
			i8086_decode_opcode_f6(cpu);
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
			return i8086_decode_opcode_fe(cpu);
	}
	return I8086_DECODE_OK;
}

static int i8086_decode_instruction(I8086* cpu) {
	int r = 0;
	do {
		r = i8086_decode_opcode(cpu);
	} while (r == I8086_DECODE_REQ_CYCLE);
	return r;
}

void i8086_init(I8086* cpu) {
	cpu->funcs.read_mem_byte = NULL;
	cpu->funcs.write_mem_byte = NULL;
	cpu->funcs.read_io_byte = NULL;
	cpu->funcs.write_io_byte = NULL;

#ifdef I8086_ENABLE_INTERRUPT_HOOKS
	cpu->int_cb_count = 0;
	for (int i = 0; i < I8086_MAX_CB; ++i) {
		cpu->int_cb[i].type = 0;
		cpu->int_cb[i].cb = NULL;
	}
#endif
}
void i8086_reset(I8086* cpu) {
	for (int i = 0; i < I8086_REGISTER_COUNT; ++i) {
		cpu->registers[i].r16 = 0;
	}

	for (int i = 0; i < I8086_SEGMENT_COUNT; ++i) {
		cpu->segments[i] = 0;
	}

	IP = 0;
	CS = 0xFFFF;

	PSW = 0;
	cpu->opcode = 0;
	cpu->modrm.byte = 0;
	cpu->cycles = 0;
	cpu->internal_flags = 0;
	cpu->segment_prefix = 0xFF;
	cpu->instruction_len = 0;

	cpu->intr = 0;
	cpu->nmi = 0;

	cpu->ea_offset = 0;
	cpu->ea_segment = 0;
	
	cpu->tf_latch = 0;
	cpu->int_latch = 0;
	cpu->int_delay = 0;
	cpu->intr_type = 0;
}

int i8086_execute(I8086* cpu) {
	i8086_check_interrupts(cpu);
	i8086_fetch(cpu);	 
	return i8086_decode_instruction(cpu);
}

#ifdef I8086_ENABLE_INTERRUPT_HOOKS
void i8086_set_interrupt_cb(I8086* cpu, I8086_INT_CB hook, uint8_t type) {
	// Replace if hook on interrupt type is already present
	for (uint8_t i = 0; i < cpu->int_cb_count; ++i) {
		if (cpu->int_cb[i].type == type) {
			cpu->int_cb[i].cb = hook;
			return;
		}
	}
	// Add new if space
	if (cpu->int_cb_count < I8086_MAX_CB) {
		cpu->int_cb[cpu->int_cb_count].type = type;
		cpu->int_cb[cpu->int_cb_count].cb = hook;
		cpu->int_cb_count++;
	}
	else {
		// hooks are full.
	}
}
void i8086_remove_interrupt_cb(I8086* cpu, uint8_t type) {

	for (uint8_t i = 0; i < cpu->int_cb_count; ++i) {
		if (cpu->int_cb[i].type == type) {
			// Move last entry to this slot
			cpu->int_cb[i] = cpu->int_cb[cpu->int_cb_count - 1];
			cpu->int_cb_count--;
			return;
		}
	}
}
I8086_INT_CB i8086_find_interrupt_cb(I8086* cpu, uint8_t type) {
	for (uint8_t i = 0; i < cpu->int_cb_count; ++i) {
		if (cpu->int_cb[i].type == type) {
			return cpu->int_cb[i].cb;
		}
	}
	return NULL;
}
#endif

uint20_t i8086_get_physical_address(uint16_t segment, uint16_t address) {
	return (((uint20_t)segment << 4) + address) & 0xFFFFF;
}

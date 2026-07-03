/* i80386_mnem.c
 * Thomas J. Armytage 2025-2026 ( https://github.com/tommojphillips/ )
 * Intel 80386 Mnemonics/Disassembler
 */

#define I80386_ENABLE_MNEM
#ifdef I80386_ENABLE_MNEM

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "i80386.h"
#include "i80386_mnem.h"
#include "sign_extend.h"

#define CS mnem->segment /* code segment register */

#define SF mnem->state->psw.sf
#define CF mnem->state->psw.cf
#define ZF mnem->state->psw.zf
#define PF mnem->state->psw.pf
#define OF mnem->state->psw.of
#define AF mnem->state->psw.af
#define DF mnem->state->psw.df
#define TF mnem->state->psw.tf
#define IF mnem->state->psw.in

#define RF mnem->state->eflags.rf
#define VM mnem->state->eflags.vm

#define PE mnem->state->msw.pe
#define MP mnem->state->msw.mp
#define EM mnem->state->msw.em
#define TS mnem->state->msw.ts
#define PG mnem->state->msw.pg

#define IP  (mnem->offset + mnem->counter)
#define EIP  (mnem->offset32 + mnem->counter)

#define SP mnem->state->sp   /* stack pointer 16bit register */
#define BP mnem->state->bp   /* base pointer 16bit register */
#define SI mnem->state->si   /* src index 16bit register */
#define DI mnem->state->di   /* dest index 16bit register */

#define ES mnem->state->es.selector  /* extra segment register */
#define SS mnem->state->ss.selector  /* stack segment register */
#define DS mnem->state->ds.selector  /* data segment register */

/* Get 8bit register mnemonic */
#define GET_REG8(reg)      reg8_mnem[reg & 7]

/* Get 16bit register mnemonic */
#define GET_REG16(reg)     reg16_mnem[reg & 7]

/* Get 32bit register mnemonic */
#define GET_REG32(reg)     reg32_mnem[reg & 7]

#define I80386_OPCODE mnem->opcode
#include "opcode_bits.h"

/* Internal flag F1. Signals that a rep prefix is in use for this decode cycle */
#define F1  (mnem->internal_flags & INTERNAL_FLAG_F1)

/* Internal flag F1Z. Signals which rep (repz/repnz) is in use for this decode cycle */
#define F1Z (mnem->internal_flags & INTERNAL_FLAG_F1Z)

#define MNEM0_F(mem, x) sprintf(mem+strlen(mem), x)
#define MNEM0(x)        MNEM0_F(mnem->str, x)

#define MNEM_F(mem, x, ...) sprintf(mem+strlen(mem), x, __VA_ARGS__)
#define MNEM(x, ...) MNEM_F(mnem->str, x, __VA_ARGS__)

static const char* reg8_mnem[] = {
	"al", "cl", "dl", "bl", "ah", "ch", "dh", "bh"
};
static const char* reg16_mnem[] = {
	"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"
};
static const char* reg32_mnem[] = {
	"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"
};

static const char* seg_mnem[] = {
	"es", "cs", "ss", "ds", "fs", "gs"
};

static int jump_condition(I80386_MNEM* mnem) {
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

static uint8_t read_byte(const I80386_MNEM* mnem, uint32_t segment, uint32_t offset) {
	uint32_t physical_address = 0;
	i80386_mnem_address_translation(mnem, segment, offset, NULL, &physical_address);
	return mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address);
}
static int fetch_byte(I80386_MNEM* mnem, uint8_t* value) {
	uint32_t physical_address = 0;
	i80386_mnem_address_translation(mnem, mnem->sdescriptor.desc.base, EIP, NULL, &physical_address);
	*value = mnem->funcs.exe_mem_byte(mnem->funcs.user_param, physical_address);
	mnem->counter += 1;
	return 1;
}

static uint16_t read_word(const I80386_MNEM* mnem, uint32_t segment, uint32_t offset) {
	uint32_t physical_address = 0;
	i80386_mnem_address_translation(mnem, segment, offset, NULL, &physical_address);
	return (uint16_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address) |
		((uint16_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address + 1) << 8);
}
static int fetch_word(I80386_MNEM* mnem, uint16_t* value) {
	uint32_t physical_address = 0;
	i80386_mnem_address_translation(mnem, mnem->sdescriptor.desc.base, EIP, NULL, &physical_address);
	*value = (uint16_t)mnem->funcs.exe_mem_byte(mnem->funcs.user_param, physical_address) |
		((uint16_t)mnem->funcs.exe_mem_byte(mnem->funcs.user_param, physical_address + 1) << 8);
	mnem->counter += 2;
	return 1;
}

static uint32_t read_dword_physical(const I80386_MNEM* mnem, uint32_t physical_address) {
	return (uint32_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address) |
		((uint32_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address + 1) << 8) |
		((uint32_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address + 2) << 16) |
		((uint32_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address + 3) << 24);
}
static uint32_t read_dword(const I80386_MNEM* mnem, uint32_t segment, uint32_t offset) {
	uint32_t physical_address = 0;
	i80386_mnem_address_translation(mnem, segment, offset, NULL, &physical_address);
	return (uint32_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address) |
		((uint32_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address + 1) << 8) |
		((uint32_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address + 2) << 16) |
		((uint32_t)mnem->funcs.read_mem_byte(mnem->funcs.user_param, physical_address + 3) << 24);
}
static int fetch_dword(I80386_MNEM* mnem, uint32_t* value) {
	uint32_t physical_address = 0;
	i80386_mnem_address_translation(mnem, mnem->sdescriptor.desc.base, EIP, NULL, &physical_address);
	*value = (uint32_t)mnem->funcs.exe_mem_byte(mnem->funcs.user_param, physical_address) |
		((uint32_t)mnem->funcs.exe_mem_byte(mnem->funcs.user_param, physical_address + 1) << 8) |
		((uint32_t)mnem->funcs.exe_mem_byte(mnem->funcs.user_param, physical_address + 2) << 16) |
		((uint32_t)mnem->funcs.exe_mem_byte(mnem->funcs.user_param, physical_address + 3) << 24);
	mnem->counter += 4;
	return 1;
}

static I80386_OPERAND modrm_get_rm(I80386_MNEM* mnem) {
	I80386_OPERAND op = { 0 };
	if (mnem->modrm.mod == 0b11) {
		op.type = OPERAND_TYPE_GENERAL_REGISTER;
		op.reg.index = mnem->modrm.rm;
	}
	else {
		op.type = OPERAND_TYPE_MEMORY;
		i80386_modrm_get_segment(mnem->state, mnem->addressing_size, mnem->modrm, mnem->sib, &op.mem.ea, mnem->segment_prefix);
		i80386_modrm_get_offset(mnem->state, mnem->addressing_size, mnem->modrm, mnem->sib, &op.mem.ea, fetch_byte, fetch_word, fetch_dword, mnem);
	}
	return op;
}

static uint8_t reg8_read(I80386_MNEM* mnem, uint8_t reg) {
	if (reg & 0x4) {
		return mnem->state->general_registers[reg & 0x3].r8.h;
	}
	else {
		return mnem->state->general_registers[reg & 0x3].r8.l;
	}
}
static uint16_t reg16_read(I80386_MNEM* mnem, uint8_t reg) {
	return mnem->state->general_registers[reg & 0x7].r16;
}

static uint16_t modrm_read_rm16(I80386_MNEM* mnem, I80386_OPERAND op) {
	if (op.type == OPERAND_TYPE_GENERAL_REGISTER) {
		return reg16_read(mnem, op.reg.index);
	}
	else {
		return read_word(mnem, op.mem.ea.logical_address.base, op.mem.ea.logical_address.offset);
	}
}

static uint8_t modrm_read_rm8(I80386_MNEM* mnem, I80386_OPERAND op) {
	if (op.type == OPERAND_TYPE_GENERAL_REGISTER) {
		return reg8_read(mnem, op.reg.index);
	}
	else {
		return read_byte(mnem, op.mem.ea.logical_address.base, op.mem.ea.logical_address.offset);
	}
}

/* Swap pointers if 'D' bit is set in opcode bXXXXXXDX */
static void get_direction(I80386_MNEM* mnem, void** ptr1, void** ptr2) {
	if (D) {
		void* tmp = *ptr1;
		*ptr1 = *ptr2;
		*ptr2 = tmp;
	}
}

static void get_base_mnem(I80386_MNEM* mnem, char* t) {
	switch (mnem->modrm.rm) {
		case 0b000: /* base rel indexed - BX + SI */
			MNEM_F(t, "%s+%s", GET_REG16(REG_BX), GET_REG16(REG_SI));
			break;
		case 0b001: /* base rel indexed - BX + DI */
			MNEM_F(t, "%s+%s", GET_REG16(REG_BX), GET_REG16(REG_DI));
			break;
		case 0b010: /* base rel indexed stack - BP + SI */
			MNEM_F(t, "%s+%s", GET_REG16(REG_BP), GET_REG16(REG_SI));
			break;
		case 0b011: /* base rel indexed stack - BP + DI */
			MNEM_F(t, "%s+%s", GET_REG16(REG_BP), GET_REG16(REG_DI));
			break;
		case 0b100: /* implied SI */
			MNEM_F(t, "%s", GET_REG16(REG_SI));
			break;
		case 0b101: /* implied DI */
			MNEM_F(t, "%s", GET_REG16(REG_DI));
			break;
		case 0b110: /* implied BP */
			MNEM_F(t, "%s", GET_REG16(REG_BP));
			break;
		case 0b111: /* implied BX */
			MNEM_F(t, "%s", GET_REG16(REG_BX));
			break;
	}
}
static int get_seg_index(I80386_MNEM* mnem) {
	if (mnem->segment_prefix != 0xFF) {
		return mnem->segment_prefix; /* CS/DS/ES/SS override */
	}

	if (mnem->addressing_size) {
		uint8_t base_reg;

		if (mnem->modrm.rm == 0b100) {
			/* SIB */
			base_reg = mnem->sib.base;

			/* mod=00 special case for base=101 -> [disp32] uses DS */
			if (mnem->modrm.mod == 0b00 && base_reg == 0b101) {
				return SEG_DS;
			}
		}
		else {
			base_reg = mnem->modrm.rm;
		}

		/* mod=00 special case for r/m=101 -> [disp32] uses DS */
		if (mnem->modrm.mod == 0b00 && mnem->modrm.rm == 0b101) {
			return SEG_DS;
		}
		/* SS if base is EBP or ESP */
		if (base_reg == REG_EBP || base_reg == REG_ESP) {
			return SEG_SS;
		}
		else {
			return SEG_DS;
		}
	}
	else {
		/* mod = 00 special case for r/m=110 -> [disp16] uses DS */
		if (mnem->modrm.mod == 0b00 && mnem->modrm.rm == 0b110) {
			return SEG_DS;
		}

		switch (mnem->modrm.rm) {
			case 0b010: /* [BP+SI] */
			case 0b011: /* [BP+DI] */
			case 0b110: /* [BP] (mod != 00) */
				return SEG_SS;  /* defaults to SS */
			default:
				return SEG_DS;  /* everything else defaults to DS */
		}
	}
}
static void get_seg(I80386_MNEM* mnem, char* buf, const char* fmt) {
	MNEM_F(buf, fmt, seg_mnem[get_seg_index(mnem)]);
}

static void get_signed_disp8(char* buf, uint8_t disp) {
	if ((int8_t)disp < 0) {
		sprintf(buf + strlen(buf), "-%Xh", (-(int8_t)disp) & 0xFF);
	}
	else {
		sprintf(buf + strlen(buf), "+%Xh", disp);
	}
}
static void get_signed_disp16(char* buf, uint16_t disp) {
	if ((int16_t)disp < 0) {
		sprintf(buf + strlen(buf), "-%Xh", (-((int16_t)disp)) & 0xFFFF);
	}
	else {
		sprintf(buf + strlen(buf), "+%Xh", disp);
	}
}
static void get_signed_disp32(char* buf, uint32_t disp) {
	if ((int32_t)disp < 0) {
		sprintf(buf + strlen(buf), "-%Xh", -((int32_t)disp));
	}
	else {
		sprintf(buf + strlen(buf), "+%Xh", disp);
	}
}
static void get_unsigned_disp(char* buf, int disp) {
	sprintf(buf + strlen(buf), "%Xh", disp);
}

static int sib_fetch(I80386_MNEM* mnem) {
	return fetch_byte(mnem, &mnem->sib.byte);
}
static int sib_check(I80386_MNEM* mnem) {
	if (mnem->addressing_size && mnem->modrm.mod != 0b11 && mnem->modrm.rm == 0b100) {
		return sib_fetch(mnem);
	}
	return 1;
}

void I80386_MNEM_get_modrm(I80386_MNEM* mnem, char* buf, const char* fmt) {
	/* Get R/M pointer */
	
	i80386_modrm_get_segment(mnem->state, mnem->addressing_size, mnem->modrm, mnem->sib, &mnem->effective_address, mnem->segment_prefix);
	i80386_modrm_get_offset(mnem->state, mnem->addressing_size, mnem->modrm, mnem->sib, &mnem->effective_address, fetch_byte, fetch_word, fetch_dword, mnem);

	get_seg(mnem, mnem->addressing_segment, "%s:");

	switch (mnem->modrm.mod) {
		case 0b00:
			if (mnem->addressing_size) {
				if (mnem->modrm.rm == 0b100) {
					/* [SIB] */
					if (mnem->sib.index == 0b100) {
						if (mnem->sib.base == 0b101) {
							/* [disp32] */
							get_unsigned_disp(mnem->addressing_offset, mnem->effective_address.displacement);
							MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
						}
						else {
							/* [base] */
							MNEM_F(mnem->addressing_offset, "%s", GET_REG32(mnem->sib.base));
							MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
						}
					}
					else {
						if (mnem->sib.base == 0b101) {
							/* [(index * scale) + disp32] */
							MNEM_F(mnem->addressing_offset, "%s*%d", GET_REG32(mnem->sib.index), (1 << mnem->sib.scale));
							get_signed_disp32(mnem->addressing_offset, mnem->effective_address.displacement);
							MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
						}
						else {
							/* [base + (index * scale)] */
							MNEM_F(mnem->addressing_offset, "%s+%s*%d", GET_REG32(mnem->sib.base), GET_REG32(mnem->sib.index), (1 << mnem->sib.scale));
							MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
						}
					}
				}
				else if (mnem->modrm.rm == 0b101) {
					/* [disp32] */
					get_unsigned_disp(mnem->addressing_offset, mnem->effective_address.displacement);
					MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
				}
				else {
					/* [reg32] */
					MNEM_F(mnem->addressing_offset, "%s", GET_REG32(mnem->modrm.rm));
					MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
				}
			}
			else {
				if (mnem->modrm.rm == 0b110) {
					/* displacement mode - [ disp16 ] */
					get_unsigned_disp(mnem->addressing_offset, mnem->effective_address.displacement);
					MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
				}
				else {
					/* register mode - [ base16 ] */
					get_base_mnem(mnem, mnem->addressing_offset);
					MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
				}
			}
			break;

		case 0b01: {
			if (mnem->addressing_size) {
				if (mnem->modrm.rm == 0b100) {
					/* [SIB + disp8] */
					if (mnem->sib.index == 0b100) {
						/* [base + disp8] */
						MNEM_F(mnem->addressing_offset, "%s", GET_REG32(mnem->sib.base));
						get_signed_disp8(mnem->addressing_offset, mnem->effective_address.displacement & 0xFF);
						MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
					}
					else {
						/* [base + (index * scale) + disp8] */
						MNEM_F(mnem->addressing_offset, "%s+%s*%d", GET_REG32(mnem->sib.base), GET_REG32(mnem->sib.index), (1 << mnem->sib.scale));
						get_signed_disp8(mnem->addressing_offset, mnem->effective_address.displacement & 0xFF);
						MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
					}
				}
				else {
					/* [reg32 + disp8] */
					MNEM_F(mnem->addressing_offset, "%s", GET_REG32(mnem->modrm.rm));
					get_signed_disp8(mnem->addressing_offset, mnem->effective_address.displacement & 0xFF);
					MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
				}
			}
			else {
				/* memory mode; 8bit displacement - [ base16 + disp8 ] */
				get_base_mnem(mnem, mnem->addressing_offset);
				get_signed_disp8(mnem->addressing_offset, mnem->effective_address.displacement & 0xFF);
				MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
			}
		} break;

		case 0b10: {
			if (mnem->addressing_size) {
				if (mnem->modrm.rm == 0b100) {
					/* [SIB + disp32] */
					if (mnem->sib.index == 0b100) {
						/* [base + disp32] */
						MNEM_F(mnem->addressing_offset, "%s", GET_REG32(mnem->sib.base));
						get_signed_disp32(mnem->addressing_offset, mnem->effective_address.displacement);
						MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
					}
					else {
						/* [base + (index * scale) + disp32] */
						MNEM_F(mnem->addressing_offset, "%s+%s*%d", GET_REG32(mnem->sib.base), GET_REG32(mnem->sib.index), (1 << mnem->sib.scale));
						get_signed_disp32(mnem->addressing_offset, mnem->effective_address.displacement);
						MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
					}
				}
				else {
					/* [reg32 + disp32] */
					MNEM_F(mnem->addressing_offset, "%s", GET_REG32(mnem->modrm.rm));
					get_signed_disp32(mnem->addressing_offset, mnem->effective_address.displacement);
					MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
				}
			}
			else {
				/* memory mode; 16bit displacement - [ base16 + disp16 ] */
				get_base_mnem(mnem, mnem->addressing_offset);
				get_signed_disp16(mnem->addressing_offset, mnem->effective_address.displacement & 0xFFFF);
				MNEM_F(buf, fmt, mnem->addressing_segment, mnem->addressing_offset);
			}
		} break;
	}
}

static const char* modrm_get_mnem32_nos(I80386_MNEM* mnem) {
	/* Get R/M pointer; no operand size */
	if (mnem->modrm.mod == 0b11) {
		return GET_REG32(mnem->modrm.rm);
	}
	else {
		I80386_MNEM_get_modrm(mnem, mnem->addressing_str, "[%s%s]");
		return mnem->addressing_str;
	}
}
static const char* modrm_get_mnem32(I80386_MNEM* mnem) {
	/* Get R/M pointer */
	if (mnem->modrm.mod == 0b11) {
		return GET_REG32(mnem->modrm.rm);
	}
	else {
		I80386_MNEM_get_modrm(mnem, mnem->addressing_str, "dword [%s%s]");
		return mnem->addressing_str;
	}
}
static const char* modrm_get_mnem16_nos(I80386_MNEM* mnem) {
	/* Get R/M pointer; no operand size */
	if (mnem->modrm.mod == 0b11) {
		return GET_REG16(mnem->modrm.rm);
	}
	else {
		I80386_MNEM_get_modrm(mnem, mnem->addressing_str, "[%s%s]");
		return mnem->addressing_str;
	}
}
static const char* modrm_get_mnem16(I80386_MNEM* mnem) {
	/* Get R/M pointer */
	if (mnem->modrm.mod == 0b11) {
		return GET_REG16(mnem->modrm.rm);
	}
	else {
		I80386_MNEM_get_modrm(mnem, mnem->addressing_str, "word [%s%s]");
		return mnem->addressing_str;
	}
}
static const char* modrm_get_mnem8(I80386_MNEM* mnem) {
	/* Get R/M pointer */
	if (mnem->modrm.mod == 0b11) {
		return GET_REG8(mnem->modrm.rm);
	}
	else {
		I80386_MNEM_get_modrm(mnem, mnem->addressing_str, "byte [%s%s]");
		return mnem->addressing_str;
	}
}
static const char* modrm_get_mnem8_nos(I80386_MNEM* mnem) {
	/* Get R/M pointer */
	if (mnem->modrm.mod == 0b11) {
		return GET_REG8(mnem->modrm.rm);
	}
	else {
		I80386_MNEM_get_modrm(mnem, mnem->addressing_str, "[%s%s]");
		return mnem->addressing_str;
	}
}

static void fetch_modrm(I80386_MNEM* mnem) {
	 fetch_byte(mnem, &mnem->modrm.byte);
	 sib_check(mnem);
}

static void set_step_into_target(I80386_MNEM* mnem, const I80386_SEGMENT_REGISTER* sdescriptor, uint16_t selector, uint32_t offset) {
	mnem->step_into_has_target = 1;
	mnem->step_into_address.offset = offset;
	if (sdescriptor != NULL) {
		mnem->step_into_address.base = sdescriptor->desc.base;
	}
	else {
		i80386_resolve_segment_selector(mnem->state, selector, &mnem->step_into_address.base);
	}
}
static void set_step_over_target(I80386_MNEM* mnem, const I80386_SEGMENT_REGISTER* sdescriptor, uint16_t selector, uint32_t offset) {
	mnem->step_over_has_target = 1;
	mnem->step_over_address.offset = offset;
	if (sdescriptor != NULL) {
		mnem->step_over_address.base = sdescriptor->desc.base;
	}
	else {
		i80386_resolve_segment_selector(mnem->state, selector, &mnem->step_over_address.base);
	}
}

int i80386_mnem_segment_translation(const I80386_MNEM* mnem, uint32_t base, uint32_t offset, uint32_t* linear_address) {
	(void)mnem;
	*linear_address = i80386_get_physical_address_bo(base, offset);
	return 1;
}
int i80386_mnem_page_translation(const I80386_MNEM* mnem, uint32_t linear_address, uint32_t* physical_address) {
	I80386_LINEAR_ADDRESS la = { .dword = linear_address };
	I80386_PAGE_TABLE_ENTRY pde = { 0 };
	I80386_PAGE_TABLE_ENTRY pte = { 0 };
	uint32_t page_directory_address = 0;
	uint32_t page_table_address = 0;

	page_directory_address = (mnem->state->cr3 & 0xFFFFF000) | (la.dir << 2);
	pde.dword = read_dword_physical(mnem, page_directory_address);
	if (!pde.present) {
		return 0;
	}

	page_table_address = (pde.page_frame_address << 12) | (la.page << 2);
	pte.dword = read_dword_physical(mnem, page_table_address);
	if (!pte.present) {
		return 0;
	}

	if (physical_address) {
		*physical_address = (pte.page_frame_address << 12) | la.offset;
	}
	return 1;
}
int i80386_mnem_address_translation(const I80386_MNEM* mnem, uint32_t base, uint32_t offset, uint32_t* linear_address, uint32_t* physical_address) {
	uint32_t translation = 0;
	if (!i80386_mnem_segment_translation(mnem, base, offset, &translation)) {
		return 0;
	}
	if (linear_address) {
		*linear_address = translation;
	}
	if (mnem->state->cr0.pg) {
		if (!i80386_mnem_page_translation(mnem, translation, physical_address)) {
			*physical_address = translation;
			return 0;
		}
	}
	else {
		if (physical_address) {
			*physical_address = translation;
		}
	}
	return 1;
}

/* Opcodes */

static void add_rm_imm(I80386_MNEM* mnem) {
	/* add r/m, imm (80/81/82/83, R/M reg = b000) b100000SW */
	if (W) {
		if (S) {
			if (mnem->operand_size) {
				/* reg32, disp8 */
				const char* rm = modrm_get_mnem32(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint32_t se = sign_extend8_32(imm);
				MNEM("add %s, %Xh", rm, se);
			}
			else {
				/* reg16, disp8 */
				const char* rm = modrm_get_mnem16(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint16_t se = sign_extend8_16(imm);
				MNEM("add %s, %Xh", rm, se);
			}
		}
		else {
			if (mnem->operand_size) {
				/* reg32, disp32 */
				const char* rm = modrm_get_mnem32(mnem);
				uint32_t imm = 0;
				fetch_dword(mnem, &imm);
				MNEM("add %s, %Xh", rm, imm);
			}
			else {
				/* reg16, disp16 */
				const char* rm = modrm_get_mnem16(mnem);
				uint16_t imm = 0;
				fetch_word(mnem, &imm);
				MNEM("add %s, %Xh", rm, imm);
			}
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("add %s, %Xh", rm, imm);
	}
}
static void add_rm_reg(I80386_MNEM* mnem) {
	/* add r/m, reg (00/01/02/03) b000000DW */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32_nos(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("add %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16_nos(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("add %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8_nos(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("add %s, %s", rm, reg);
	}
}
static void add_accum_imm(I80386_MNEM* mnem) {
	/* add AL/AX/EAX, imm (04/05) b0000010W */
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("add eax, %Xh", imm);
		}
		else {
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("add ax, %Xh", imm);
		}
	}
	else {
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("add al, %Xh", imm);
	}
}

static void or_rm_imm(I80386_MNEM* mnem) {
	/* or r/m, imm (80/81/82/83, R/M reg = b001) b100000SW */
	if (W) {
		if (S) {
			if (mnem->operand_size) {
				/* reg32, disp8 */
				const char* rm = modrm_get_mnem32(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint32_t se = sign_extend8_32(imm);
				MNEM("or %s, %Xh", rm, se);
			}
			else {
				/* reg16, disp8 */
				const char* rm = modrm_get_mnem16(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint16_t se = sign_extend8_16(imm);
				MNEM("or %s, %Xh", rm, se);
			}
		}
		else {
			if (mnem->operand_size) {
				/* reg32, disp32 */
				const char* rm = modrm_get_mnem32(mnem);
				uint32_t imm = 0;
				fetch_dword(mnem, &imm);
				MNEM("or %s, %Xh", rm, imm);
			}
			else {
				/* reg16, disp16 */
				const char* rm = modrm_get_mnem16(mnem);
				uint16_t imm = 0;
				fetch_word(mnem, &imm);
				MNEM("or %s, %Xh", rm, imm);
			}
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("or %s, %Xh", rm, imm);
	}
}
static void or_rm_reg(I80386_MNEM* mnem) {
	/* or r/m, reg (08/0A/09/0B) b000010DW */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32_nos(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("or %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16_nos(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("or %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8_nos(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("or %s, %s", rm, reg);
	}
}
static void or_accum_imm(I80386_MNEM* mnem) {
	/* or AL/AX, imm (0C/0D) b0000110W */
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("or eax, %Xh", imm);
		}
		else {
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("or ax, %Xh", imm);
		}
	}
	else {
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("or al, %Xh", imm);
	}
}

static void adc_rm_imm(I80386_MNEM* mnem) {
	/* adc r/m, imm (80/81/82/83, R/M reg = b010) b100000SW */
	if (W) {
		if (S) {
			if (mnem->operand_size) {
				/* reg32, disp8 */
				const char* rm = modrm_get_mnem32(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint32_t se = sign_extend8_32(imm);
				MNEM("adc %s, %Xh", rm, se);
			}
			else {
				/* reg16, disp8 */
				const char* rm = modrm_get_mnem16(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint16_t se = sign_extend8_16(imm);
				MNEM("adc %s, %Xh", rm, se);
			}
		}
		else {
			if (mnem->operand_size) {
				/* reg32, disp32 */
				const char* rm = modrm_get_mnem32(mnem);
				uint32_t imm = 0;
				fetch_dword(mnem, &imm);
				MNEM("adc %s, %Xh", rm, imm);
			}
			else {
				/* reg16, disp16 */
				const char* rm = modrm_get_mnem16(mnem);
				uint16_t imm = 0;
				fetch_word(mnem, &imm);
				MNEM("adc %s, %Xh", rm, imm);
			}
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("adc %s, %Xh", rm, imm);
	}
}
static void adc_rm_reg(I80386_MNEM* mnem) {
	/* adc r/m, reg (10/12/11/13) b000100DW */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32_nos(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("adc %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16_nos(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("adc %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8_nos(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("adc %s, %s", rm, reg);
	}
}
static void adc_accum_imm(I80386_MNEM* mnem) {
	/* adc AL/AX, imm (14/15) b0001010W */
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("adc eax, %Xh", imm);
		}
		else {
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("adc ax, %Xh", imm);
		}
	}
	else {
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("adc al, %Xh", imm);
	}
}

static void sbb_rm_imm(I80386_MNEM* mnem) {
	/* sbb r/m, imm (80/81/82/83, R/M reg = b011)  b100000SW */
	if (W) {
		if (S) {
			if (mnem->operand_size) {
				/* reg32, disp8 */
				const char* rm = modrm_get_mnem32(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint32_t se = sign_extend8_32(imm);
				MNEM("sbb %s, %Xh", rm, se);
			}
			else {
				/* reg16, disp8 */
				const char* rm = modrm_get_mnem16(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint16_t se = sign_extend8_16(imm);
				MNEM("sbb %s, %Xh", rm, se);
			}
		}
		else {
			if (mnem->operand_size) {
				/* reg32, disp32 */
				const char* rm = modrm_get_mnem32(mnem);
				uint32_t imm = 0;
				fetch_dword(mnem, &imm);
				MNEM("sbb %s, %Xh", rm, imm);
			}
			else {
				/* reg16, disp16 */
				const char* rm = modrm_get_mnem16(mnem);
				uint16_t imm = 0;
				fetch_word(mnem, &imm);
				MNEM("sbb %s, %Xh", rm, imm);
			}
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("sbb %s, %Xh", rm, imm);
	}
}
static void sbb_rm_reg(I80386_MNEM* mnem) {
	/* sbb r/m, reg (18/1A/19/1B) b000110DW */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32_nos(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("sbb %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16_nos(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("sbb %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8_nos(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("sbb %s, %s", rm, reg);
	}
}
static void sbb_accum_imm(I80386_MNEM* mnem) {
	/* sbb AL/AX/EAX, imm (1C/1D) b0001110W */
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("sbb eax, %Xh", imm);
		}
		else {
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("sbb ax, %Xh", imm);
		}
	}
	else {
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("sbb al, %Xh", imm);
	}
}

static void and_rm_imm(I80386_MNEM* mnem) {
	/* and r/m, imm (80/81/82/83, R/M reg = b100) b100000SW */
	if (W) {
		if (S) {
			if (mnem->operand_size) {
				/* reg32, disp8 */
				const char* rm = modrm_get_mnem32(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint32_t se = sign_extend8_32(imm);
				MNEM("and %s, %Xh", rm, se);
			}
			else {
				/* reg16, disp8 */
				const char* rm = modrm_get_mnem16(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint16_t se = sign_extend8_16(imm);
				MNEM("and %s, %Xh", rm, se);
			}
		}
		else {
			if (mnem->operand_size) {
				/* reg32, disp32 */
				const char* rm = modrm_get_mnem32(mnem);
				uint32_t imm = 0;
				fetch_dword(mnem, &imm);
				MNEM("and %s, %Xh", rm, imm);
			}
			else {
				/* reg16, disp16 */
				const char* rm = modrm_get_mnem16(mnem);
				uint16_t imm = 0;
				fetch_word(mnem, &imm);
				MNEM("and %s, %Xh", rm, imm);
			}
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("and %s, %Xh", rm, imm);
	}
}
static void and_rm_reg(I80386_MNEM* mnem) {
	/* and r/m, reg (20/22/21/23) b001000DW */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32_nos(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("and %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16_nos(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("and %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8_nos(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("and %s, %s", rm, reg);
	}
}
static void and_accum_imm(I80386_MNEM* mnem) {
	/* and AL/AX/EAX, imm (24/25) b0010010W */
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("and eax, %Xh", imm);
		}
		else {
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("and ax, %Xh", imm);
		}
	}
	else {
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("and al, %Xh", imm);
	}
}

static void sub_rm_imm(I80386_MNEM* mnem) {
	/* sub r/m, imm (80/81, R/M reg = b101) b100000SW */
	if (W) {
		if (S) {
			if (mnem->operand_size) {
				/* reg32, disp8 */
				const char* rm = modrm_get_mnem32(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint32_t se = sign_extend8_32(imm);
				MNEM("sub %s, %Xh", rm, se);
			}
			else {
				/* reg16, disp8 */
				const char* rm = modrm_get_mnem16(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint16_t se = sign_extend8_16(imm);
				MNEM("sub %s, %Xh", rm, se);
			}
		}
		else {
			if (mnem->operand_size) {
				/* reg32, disp32 */
				const char* rm = modrm_get_mnem32(mnem);
				uint32_t imm = 0;
				fetch_dword(mnem, &imm);
				MNEM("sub %s, %Xh", rm, imm);
			}
			else {
				/* reg16, disp16 */
				const char* rm = modrm_get_mnem16(mnem);
				uint16_t imm = 0;
				fetch_word(mnem, &imm);
				MNEM("sub %s, %Xh", rm, imm);
			}
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("sub %s, %Xh", rm, imm);
	}
}
static void sub_rm_reg(I80386_MNEM* mnem) {
	/* sub r/m, reg (28/2A/29/2B) b001010DW */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32_nos(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("sub %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16_nos(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("sub %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8_nos(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("sub %s, %s", rm, reg);
	}
}
static void sub_accum_imm(I80386_MNEM* mnem) {
	/* sub AL/AX/EAX, imm (2C/2D) b0010110W */
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("sub eax, %Xh", imm);
		}
		else {
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("sub ax, %Xh", imm);
		}
	}
	else {
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("sub al, %Xh", imm);
	}
}

static void xor_rm_imm(I80386_MNEM* mnem) {
	/* xor r/m, imm (80/81/82/83, R/M reg = b110) b100000SW */
	if (W) {
		if (S) {
			if (mnem->operand_size) {
				/* reg32, disp8 */
				const char* rm = modrm_get_mnem32(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint32_t se = sign_extend8_32(imm);
				MNEM("xor %s, %Xh", rm, se);
			}
			else {
				/* reg16, disp8 */
				const char* rm = modrm_get_mnem16(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint16_t se = sign_extend8_16(imm);
				MNEM("xor %s, %Xh", rm, se);
			}
		}
		else {
			if (mnem->operand_size) {
				/* reg32, disp32 */
				const char* rm = modrm_get_mnem32(mnem);
				uint32_t imm = 0;
				fetch_dword(mnem, &imm);
				MNEM("xor %s, %Xh", rm, imm);
			}
			else {
				/* reg16, disp16 */
				const char* rm = modrm_get_mnem16(mnem);
				uint16_t imm = 0;
				fetch_word(mnem, &imm);
				MNEM("xor %s, %Xh", rm, imm);
			}
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("xor %s, %Xh", rm, imm);
	}
}
static void xor_rm_reg(I80386_MNEM* mnem) {
	/* xor r/m, reg (30/32/31/33) b001100DW */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32_nos(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("xor %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16_nos(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("xor %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8_nos(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("xor %s, %s", rm, reg);
	}
}
static void xor_accum_imm(I80386_MNEM* mnem) {
	/* xor AL/AX/EAX, imm (34/35) b0011010W */
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("xor eax, %Xh", imm);
		}
		else {
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("xor ax, %Xh", imm);
		}
	}
	else {
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("xor al, %Xh", imm);
	}
}

static void cmp_rm_imm(I80386_MNEM* mnem) {
	/* cmp r/m, imm (80/81/82/83, R/M reg = b111)  b100000SW */
	if (W) {
		if (S) {
			if (mnem->operand_size) {
				/* reg32, disp8 */
				const char* rm = modrm_get_mnem32(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint32_t se = sign_extend8_32(imm);
				MNEM("cmp %s, %Xh", rm, se);
			}
			else {
				/* reg16, disp8 */
				const char* rm = modrm_get_mnem16(mnem);
				uint8_t imm = 0;
				fetch_byte(mnem, &imm);
				uint16_t se = sign_extend8_16(imm);
				MNEM("cmp %s, %Xh", rm, se);
			}
		}
		else {
			if (mnem->operand_size) {
				/* reg32, disp32 */
				const char* rm = modrm_get_mnem32(mnem);
				uint32_t imm = 0;
				fetch_dword(mnem, &imm);
				MNEM("cmp %s, %Xh", rm, imm);
			}
			else {
				/* reg16, disp16 */
				const char* rm = modrm_get_mnem16(mnem);
				uint16_t imm = 0;
				fetch_word(mnem, &imm);
				MNEM("cmp %s, %Xh", rm, imm);
			}
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("cmp %s, %Xh", rm, imm);
	}
}
static void cmp_rm_reg(I80386_MNEM* mnem) {
	/* cmp r/m, reg (38/39/3A/3B) b001110DW */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32_nos(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("cmp %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16_nos(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("cmp %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8_nos(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("cmp %s, %s", rm, reg);
	}
}
static void cmp_accum_imm(I80386_MNEM* mnem) {
	/* cmp AL/AX, imm (3C/3D) b0011110W */
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("cmp eax, %Xh", imm);
		}
		else {
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("cmp ax, %Xh", imm);
		}
	}
	else {
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("cmp al, %Xh", imm);
	}
}

static void test_rm_imm(I80386_MNEM* mnem) {
	/* test r/m, imm (F6/F7, R/M reg = b000) b1111011W */
	if (W) {
		if (mnem->operand_size) {
			/* reg32, disp32 */
			const char* rm = modrm_get_mnem32(mnem);
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("test %s, %Xh", rm, imm);		
		}
		else {
			/* reg16, disp16 */
			const char* rm = modrm_get_mnem16(mnem);
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("test %s, %Xh", rm, imm);		
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("test %s, %Xh", rm, imm);
	}
}
static void test_rm_reg(I80386_MNEM* mnem) {
	/* test r/m, reg (84/85) b1000010W */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32_nos(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("test %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16_nos(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("test %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8_nos(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("test %s, %s", rm, reg);
	}
}
static void test_accum_imm(I80386_MNEM* mnem) {
	/* test AL/AX, imm (A8/A9) b1010100W */
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			fetch_dword(mnem, &imm);
			MNEM("test eax, %Xh", imm);
		}
		else {
			uint16_t imm = 0;
			fetch_word(mnem, &imm);
			MNEM("test ax, %Xh", imm);
		}
	}
	else {
		uint8_t imm = 0;
		fetch_byte(mnem, &imm);
		MNEM("test al, %Xh", imm);
	}
}

static void daa(I80386_MNEM* mnem) {
	/* Decimal Adjust for Addition (27) b00100111 */
	MNEM0("daa");
}
static void das(I80386_MNEM* mnem) {
	/* Decimal Adjust for Subtraction (2F) b00101111 */
	MNEM0("das");
}
static void aaa(I80386_MNEM* mnem) {
	/* ASCII Adjust for Addition (37) b00110111 */
	MNEM0("aaa");
}
static void aas(I80386_MNEM* mnem) {
	/* ASCII Adjust for Subtraction (3F) b00111111 */
	MNEM0("aas");
}
static void aam(I80386_MNEM* mnem) {
	/* ASCII Adjust for Multiply (D4 0A) b11010100 00001010 */
	uint8_t divisor = 0;
	fetch_byte(mnem, &divisor); /* undocumented operand; normally 0x0A */
	MNEM("aam %Xh", divisor);
}
static void aad(I80386_MNEM* mnem) {
	/* ASCII Adjust for Division (D5 0A) b11010101 00001010 */
	uint8_t divisor = 0;
	fetch_byte(mnem, &divisor); /* undocumented operand; normally 0x0A */
	MNEM("aad %Xh", divisor);
}
static void salc(I80386_MNEM* mnem) {
	/* set carry in AL (D6) b11010110 undocumented opcode */
	MNEM0("salc");
}

static void inc_reg(I80386_MNEM* mnem) {
	/* Inc reg16 (40-47) b01000REG */
	MNEM("inc %s", GET_REG16(mnem->opcode));
}
static void dec_reg(I80386_MNEM* mnem) {
	/* Dec reg16 (48-4F) b01001REG */
	MNEM("dec %s", GET_REG16(mnem->opcode));
}

static void push_seg(I80386_MNEM* mnem) {
	/* Push seg16 (06/0E/16/1E/A0/A8) bX0ESRXXX */
	MNEM("push %s", seg_mnem[ESR]);
}
static void pop_seg(I80386_MNEM* mnem) {
	/* Pop seg16 (07/0F/17/1F/A1/A9) bX0ESRXXX */
	MNEM("pop %s", seg_mnem[ESR]);
}
static void push_reg(I80386_MNEM* mnem) {
	/* Push reg16 (50-57) b01010REG */
	if (mnem->operand_size) {
		MNEM("push %s", GET_REG32(mnem->opcode));
	}
	else {
		MNEM("push %s", GET_REG16(mnem->opcode));
	}
}
static void pop_reg(I80386_MNEM* mnem) {
	/* Pop reg16 (58-5F) b01011REG */
	if (mnem->operand_size) {
		MNEM("pop %s", GET_REG32(mnem->opcode));
	}
	else {
		MNEM("pop %s", GET_REG16(mnem->opcode));
	}
}
static void push_rm(I80386_MNEM* mnem) {
	/* Push R/M (FF, R/M reg = 110) b11111111 */
	const char* rm = NULL;
	if (mnem->operand_size) {
		rm = modrm_get_mnem32(mnem);
	}
	else {
		rm = modrm_get_mnem16(mnem);
	}
	MNEM("push %s", rm);
}
static void pop_rm(I80386_MNEM* mnem) {
	/* Pop R/M (8F) b10001111 */
	fetch_modrm(mnem);
	const char* rm = NULL;
	if (mnem->operand_size) {
		rm = modrm_get_mnem32(mnem);
	}
	else {
		rm = modrm_get_mnem16(mnem);
	}
	MNEM("pop %s", rm);
}
static void pushf(I80386_MNEM* mnem) {
	/* push psw (9C) b10011100 */
	MNEM0("pushf");
}
static void popf(I80386_MNEM* mnem) {
	/* pop psw (9D) b10011101 */
	MNEM0("popf");
}
static void pusha(I80386_MNEM* mnem) {
	/* push all (60) b01100000 */
	MNEM0("pusha");
}
static void popa(I80386_MNEM* mnem) {
	/* pop all (61) b01100001 */
	MNEM0("popa");
}
static void push_imm(I80386_MNEM* mnem) {
	/* Push IMM (68/6A) b011010S0 */
	MNEM0("push ");
	if (S) { /* sign extended to 16bit */
		uint8_t imm = 0;
		if (!fetch_byte(mnem, &imm)) {
			return;
		}
		uint16_t se = sign_extend8_16(imm);
		get_unsigned_disp(mnem->addressing_offset, se);
	}
	else {
		uint16_t imm = 0;
		if (!fetch_word(mnem, &imm)) {
			return;
		}
		get_unsigned_disp(mnem->addressing_offset, imm);
	}
}

static void enter(I80386_MNEM* mnem) {
	/* enter procedure (C8) b11001000 */
	uint16_t op1 = 0;
	uint8_t op2 = 0;
	fetch_word(mnem, &op1);
	fetch_byte(mnem, &op2);
	MNEM("enter %Xh, %Xh", op1, op2);
}
static void leave(I80386_MNEM* mnem) {
	/* leave procedure (C9) b11001001 */
	MNEM0("leave");
}

static void nop(I80386_MNEM* mnem) {
	/* nop (90) b10010000 */
	MNEM0("nop");
}
static void xchg_accum_reg(I80386_MNEM* mnem) {
	/* xchg AX, reg16 (91 - 97) b10010REG */	
	const char* reg = GET_REG16(mnem->opcode);
	MNEM("xchg %s, ax", reg);
}
static void xchg_rm_reg(I80386_MNEM* mnem) {
	/* xchg R/M, reg16 (86/87) b1000011W */
	fetch_modrm(mnem);
	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		const char* reg = GET_REG16(mnem->modrm.reg);
		MNEM("xchg %s, %s", reg, rm);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		MNEM("xchg %s, %s", reg, rm);
		
	}
}

static void cbw(I80386_MNEM* mnem) {
	/* Convert byte to word (98) b10011000 */
	MNEM0("cbw");
}
static void cwd(I80386_MNEM* mnem) {
	/* Convert word to dword (99) b10011001 */
	MNEM0("cwd");
}

static void wait(I80386_MNEM* mnem) {
	/* wait (9B) b10011011 */
	MNEM0("wait");
}

static void sahf(I80386_MNEM* mnem) {
	/* Store AH into flags (9E) b10011110 */
	MNEM0("sahf");
}
static void lahf(I80386_MNEM* mnem) {
	/* Load flags into AH (9F) b10011111 */
	MNEM0("lahf");
}

static void hlt(I80386_MNEM* mnem) {
	/* Halt mnem (F4) b11110100 */
	MNEM0("hlt");

	//set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset + mnem->counter - 1);
}
static void cmc(I80386_MNEM* mnem) {
	/* Complement carry flag (F5) b11110101 */
	MNEM0("cmc");
}
static void clc(I80386_MNEM* mnem) {
	/* clear carry flag (F8) b11111000 */
	MNEM0("clc");
}
static void stc(I80386_MNEM* mnem) {
	/* set carry flag (F9) b11111001 */
	MNEM0("stc");
}
static void cli(I80386_MNEM* mnem) {
	/* clear interrupt flag (FA) b11111010 */
	MNEM0("cli");
}
static void sti(I80386_MNEM* mnem) {
	/* set interrupt flag (FB) b1111011 */
	MNEM0("sti");
}
static void cld(I80386_MNEM* mnem) {
	/* clear direction flag (FC) b11111100 */
	MNEM0("cld");
}
static void std(I80386_MNEM* mnem) {
	/* set direction flag (FD) b11111101 */
	MNEM0("std");
}

static void inc_rm(I80386_MNEM* mnem) {
	/* Inc R/M (FE/FF, R/M reg = 000) b1111111W */
	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("inc %s", rm);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("inc %s", rm);
	}
}
static void dec_rm(I80386_MNEM* mnem) {
	/* Dec R/M (FE/FF, R/M reg = 001) b1111111W */
	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("dec %s", rm);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("dec %s", rm);
	}
}

static void rol_rm_cl(I80386_MNEM* mnem) {
	/* Rotate left (D0/D1/D2/D3, R/M reg = 000) b110100VW */
	char count[5] = { 0 };
	if (VW) {
		strcpy(count, ", cl");
	}

	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("rol %s%s", rm, count);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("rol %s%s", rm, count);
	}
}
static void ror_rm_cl(I80386_MNEM* mnem) {
	/* Rotate left (D0/D1/D2/D3, R/M reg = 001) b110100VW */
	char count[5] = { 0 };
	if (VW) {
		strcpy(count, ", cl");
	}

	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("ror %s%s", rm, count);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("ror %s%s", rm, count);
	}
}
static void rcl_rm_cl(I80386_MNEM* mnem) {
	/* Rotate through carry left (D0/D1/D2/D3, R/M reg = 010) b110100VW */
	char count[5] = { 0 };
	if (VW) {
		strcpy(count, ", cl");
	}

	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("rcl %s%s", rm, count);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("rcl %s%s", rm, count);
	}
}
static void rcr_rm_cl(I80386_MNEM* mnem) {
	/* Rotate through carry right (D0/D1/D2/D3, R/M reg = 011) b110100VW */
	char count[5] = { 0 };
	if (VW) {
		strcpy(count, ", cl");
	}

	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("rcr %s%s", rm, count);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("rcr %s%s", rm, count);
	}
}
static void shl_rm_cl(I80386_MNEM* mnem) {
	/* Shift left (D0/D1/D2/D3, R/M reg = 100) b110100VW */
	char count[5] = { 0 };
	if (VW) {
		strcpy(count, ", cl");
	}

	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("shl %s%s", rm, count);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("shl %s%s", rm, count);
	}
}
static void shr_rm_cl(I80386_MNEM* mnem) {
	/* Shift Logical right (D0/D1/D2/D3, R/M reg = 101) b110100VW */
	char count[5] = { 0 };
	if (VW) {
		strcpy(count, ", cl");
	}

	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("shr %s%s", rm, count);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("shr %s%s", rm, count);
	}
}
static void sal_rm_cl(I80386_MNEM* mnem) {
	/* Set Minus One (D0/D1/D2/D3, R/M reg = 110) b110100VW (undocumented) */
	char count[5] = { 0 };
	if (VW) {
		strcpy(count, ", cl");
	}

	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("sal %s%s", rm, count);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("sal %s%s", rm, count);
	}
}
static void sar_rm_cl(I80386_MNEM* mnem) {
	/* Shift Arithmetic right (D0/D1/D2/D3, R/M reg = 111) b110100VW */
	char count[5] = { 0 };
	if (VW) {
		strcpy(count, ", cl");
	}

	if (W) {
		const char* rm = modrm_get_mnem16(mnem);
		MNEM("sar %s%s", rm, count);
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("sar %s%s", rm, count);
	}
}
static void shld_rm_cl(I80386_MNEM* mnem) {
	const char* rm = NULL;
	const char* reg = NULL;
	fetch_modrm(mnem);
	MNEM("shld ");
	if (W) {
		rm = modrm_get_mnem32_nos(mnem);
		reg = reg32_mnem[mnem->modrm.reg];
	}
	else {
		rm = modrm_get_mnem16_nos(mnem);
		reg = reg32_mnem[mnem->modrm.reg];
	}
	MNEM("%s, %s, cl", rm, reg);
}
static void shrd_rm_cl(I80386_MNEM* mnem) {
	const char* rm = NULL;
	const char* reg = NULL;
	fetch_modrm(mnem);
	MNEM("shrd ");
	if (mnem->operand_size) {
		rm = modrm_get_mnem32_nos(mnem);
		reg = reg32_mnem[mnem->modrm.reg];
	}
	else {
		rm = modrm_get_mnem16_nos(mnem);
		reg = reg16_mnem[mnem->modrm.reg];
	}
	MNEM("%s, %s, cl", rm, reg);
}

static void rol_rm_imm(I80386_MNEM* mnem) {
	/* Rotate left (C0/C1, R/M reg = 000) b1100000W */
	const char* rm = NULL;
	if (W) {
		if (mnem->operand_size) {
			rm = modrm_get_mnem32(mnem);
		}
		else {
			rm = modrm_get_mnem16(mnem);
		}
	}
	else {
		rm = modrm_get_mnem8(mnem);
	}
	MNEM("rol %s, ", rm);
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	get_unsigned_disp(mnem->str, imm);
}
static void ror_rm_imm(I80386_MNEM* mnem) {
	/* Rotate left (C0/C1, R/M reg = 001) b1100000W */
	const char* rm = NULL;
	if (W) {
		if (mnem->operand_size) {
			rm = modrm_get_mnem32(mnem);
		}
		else {
			rm = modrm_get_mnem16(mnem);
		}
	}
	else {
		rm = modrm_get_mnem8(mnem);
	}
	MNEM("ror %s, ", rm);
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	get_unsigned_disp(mnem->str, imm);
}
static void rcl_rm_imm(I80386_MNEM* mnem) {
	/* Rotate through carry left (C0/C1, R/M reg = 010) b1100000W */
	const char* rm = NULL;
	if (W) {
		if (mnem->operand_size) {
			rm = modrm_get_mnem32(mnem);
		}
		else {
			rm = modrm_get_mnem16(mnem);
		}
	}
	else {
		rm = modrm_get_mnem8(mnem);
	}
	MNEM("rcl %s, ", rm);
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	get_unsigned_disp(mnem->str, imm);
}
static void rcr_rm_imm(I80386_MNEM* mnem) {
	/* Rotate through carry right (C0/C1, R/M reg = 011) b1100000W */	
	const char* rm = NULL;
	if (W) {
		if (mnem->operand_size) {
			rm = modrm_get_mnem32(mnem);
		}
		else {
			rm = modrm_get_mnem16(mnem);
		}
	}
	else {
		rm = modrm_get_mnem8(mnem);
	}
	MNEM("rcr %s, ", rm);
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	get_unsigned_disp(mnem->str, imm);
}
static void shl_rm_imm(I80386_MNEM* mnem) {
	/* Shift left (C0/C1, R/M reg = 100) b1100000W */
	const char* rm = NULL;
	if (W) {
		if (mnem->operand_size) {
			rm = modrm_get_mnem32(mnem);
		}
		else {
			rm = modrm_get_mnem16(mnem);
		}
	}
	else {
		rm = modrm_get_mnem8(mnem);
	}
	MNEM("shl %s, ", rm);
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	get_unsigned_disp(mnem->str, imm);
}
static void shr_rm_imm(I80386_MNEM* mnem) {
	/* Shift Logical right (C0/C1, R/M reg = 101) b1100000W */
	const char* rm = NULL;
	if (W) {
		if (mnem->operand_size) {
			rm = modrm_get_mnem32(mnem);
		}
		else {
			rm = modrm_get_mnem16(mnem);
		}
	}
	else {
		rm = modrm_get_mnem8(mnem);
	}
	MNEM("shr %s, ", rm);
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	get_unsigned_disp(mnem->str, imm);
}
static void sal_rm_imm(I80386_MNEM* mnem) {
	/* Shift Arithmetic left (C0/C1, R/M reg = 110) b1100000W */
	const char* rm = NULL;
	if (W) {
		if (mnem->operand_size) {
			rm = modrm_get_mnem32(mnem);
		}
		else {
			rm = modrm_get_mnem16(mnem);
		}
	}
	else {
		rm = modrm_get_mnem8(mnem);
	}
	MNEM("sal %s, ", rm);
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	get_unsigned_disp(mnem->str, imm);
}
static void sar_rm_imm(I80386_MNEM* mnem) {
	/* Shift Arithmetic right (C0/C1, R/M reg = 111) b1100000W */
	const char* rm = NULL;
	if (W) {
		if (mnem->operand_size) {
			rm = modrm_get_mnem32(mnem);
		}
		else {
			rm = modrm_get_mnem16(mnem);
		}
	}
	else {
		rm = modrm_get_mnem8(mnem);
	}
	MNEM("sar %s, ", rm);
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	get_unsigned_disp(mnem->str, imm);
}
static void shld_rm_imm(I80386_MNEM* mnem) {
	const char* rm = NULL;
	const char* reg = NULL;
	uint8_t imm = 0;
	fetch_modrm(mnem);
	MNEM("shld ");
	if (mnem->operand_size) {
		rm = modrm_get_mnem32_nos(mnem);
		reg = reg32_mnem[mnem->modrm.reg];
		fetch_byte(mnem, &imm);
	}
	else {
		rm = modrm_get_mnem16_nos(mnem);
		reg = reg16_mnem[mnem->modrm.reg];
		fetch_byte(mnem, &imm);
	}
	MNEM("%s, %s, %xh", rm, reg, imm);
}
static void shrd_rm_imm(I80386_MNEM* mnem) {
	const char* rm = NULL;
	const char* reg = NULL;
	uint8_t imm = 0;
	fetch_modrm(mnem);
	MNEM("shrd ");
	if (W) {
		rm = modrm_get_mnem32_nos(mnem);
		reg = reg32_mnem[mnem->modrm.reg];
		fetch_byte(mnem, &imm);
	}
	else {
		rm = modrm_get_mnem32_nos(mnem);
		reg = reg32_mnem[mnem->modrm.reg];
		fetch_byte(mnem, &imm);
	}
	MNEM("%s, %s, %xh", rm, reg, imm);
}

static void jcc_short(I80386_MNEM* mnem) {
	/* conditional jump (70-7F) b0111CCCC */
	uint8_t disp = 0;
	fetch_byte(mnem, &disp);
	uint16_t imm = sign_extend8_16(disp);
	imm += mnem->counter;

	switch (CCCC) {
		case JCC_JO:
			MNEM("jo %04Xh", imm);
			break;
		case JCC_JNO:
			MNEM("jno %04Xh", imm);
			break;
		case JCC_JC:
			MNEM("jb %04Xh", imm);
			break;
		case JCC_JNC:
			MNEM("jnb %04Xh", imm);
			break;
		case JCC_JZ:
			MNEM("jz %04Xh", imm);
			break;
		case JCC_JNZ:
			MNEM("jnz %04Xh", imm);
			break;
		case JCC_JBE:
			MNEM("jbe %04Xh", imm);
			break;
		case JCC_JA:
			MNEM("jnbe %04Xh", imm);
			break;
		case JCC_JS:
			MNEM("js %04Xh", imm);
			break;
		case JCC_JNS:
			MNEM("jns %04Xh", imm);
			break;
		case JCC_JPE:
			MNEM("jp %04Xh", imm);
			break;
		case JCC_JPO:
			MNEM("jnp %04Xh", imm);
			break;
		case JCC_JL:
			MNEM("jl %04Xh", imm);
			break;
		case JCC_JGE:
			MNEM("jnl %04Xh", imm);
			break;
		case JCC_JLE:
			MNEM("jle %04Xh", imm);
			break;
		case JCC_JG:
			MNEM("jnle %04Xh", imm);
			break;
	}

	if (jump_condition(mnem)) {
		set_step_into_target(mnem, &mnem->sdescriptor, 0, mnem->offset + imm);
	}
}
static void jcc_long(I80386_MNEM* mnem) {
	/* conditional jump (0F 80-8F) b0111CCCC */
	uint32_t offset = 0;
	if (mnem->operand_size) {
		if (!fetch_dword(mnem, &offset)) {
			return;
		}
	}
	else {
		uint16_t imm = 0;
		if (!fetch_word(mnem, &imm)) {
			return;
		}
		offset = sign_extend16_32(imm);
	}
	offset += mnem->counter;

	switch (CCCC) {
		case JCC_JO:
			MNEM("jo %08Xh", offset);
			break;
		case JCC_JNO:
			MNEM("jno %08Xh", offset);
			break;
		case JCC_JC:
			MNEM("jb %08Xh", offset);
			break;
		case JCC_JNC:
			MNEM("jnb %08Xh", offset);
			break;
		case JCC_JZ:
			MNEM("jz %08Xh", offset);
			break;
		case JCC_JNZ:
			MNEM("jnz %08Xh", offset);
			break;
		case JCC_JBE:
			MNEM("jbe %08Xh", offset);
			break;
		case JCC_JA:
			MNEM("jnbe %08Xh", offset);
			break;
		case JCC_JS:
			MNEM("js %08Xh", offset);
			break;
		case JCC_JNS:
			MNEM("jns %08Xh", offset);
			break;
		case JCC_JPE:
			MNEM("jp %08Xh", offset);
			break;
		case JCC_JPO:
			MNEM("jnp %08Xh", offset);
			break;
		case JCC_JL:
			MNEM("jl %08Xh", offset);
			break;
		case JCC_JGE:
			MNEM("jnl %08Xh", offset);
			break;
		case JCC_JLE:
			MNEM("jle %08Xh", offset);
			break;
		case JCC_JG:
			MNEM("jnle %08Xh", offset);
			break;
	}

	if (jump_condition(mnem)) {
		set_step_into_target(mnem, &mnem->sdescriptor, 0, mnem->offset + offset);
	}
}
static void jmp_intra_direct(I80386_MNEM* mnem) {
	/* Jump near (E9) b11101001 */
	uint16_t imm = 0;
	fetch_word(mnem, &imm);
	imm += mnem->counter;
	MNEM("jmp %04Xh", imm);
	set_step_into_target(mnem, &mnem->sdescriptor, 0, mnem->offset + imm);
}
static void jmp_inter_direct(I80386_MNEM* mnem) {
	/* Jump addr:seg (EA) b11101010 */
	if (mnem->operand_size) {
		uint32_t imm = 0;
		uint16_t imm2 = 0;
		fetch_dword(mnem, &imm);
		fetch_word(mnem, &imm2);
		MNEM("jmpf %04Xh:%08Xh", imm2, imm);
		set_step_into_target(mnem, NULL, imm2, imm);
	}
	else {
		uint16_t imm = 0;
		uint16_t imm2 = 0;
		fetch_word(mnem, &imm);
		fetch_word(mnem, &imm2);
		MNEM("jmpf %04Xh:%04Xh", imm2, imm);
		set_step_into_target(mnem, NULL, imm2, imm);
	}
}
static void jmp_intra_direct_short(I80386_MNEM* mnem) {
	/* Jump near short (EB) b11101011 */
	uint8_t imm = 0;
	uint16_t se = 0;
	fetch_byte(mnem, &imm);
	se = sign_extend8_16(imm);
	se += mnem->counter;
	MNEM("jmp %04Xh", se);
	set_step_into_target(mnem, &mnem->sdescriptor, 0, mnem->offset + se);
}
static void jmp_intra_indirect(I80386_MNEM* mnem) {
	/* Jump near indirect (FF, R/M reg = 100) b11111111 */
	const char* rm = modrm_get_mnem16(mnem);
	MNEM("jmp %s", rm);
	I80386_OPERAND op = modrm_get_rm(mnem);
	set_step_into_target(mnem, &mnem->sdescriptor, 0, modrm_read_rm16(mnem, op));
}
static void jmp_inter_indirect(I80386_MNEM* mnem) {
	/* Jump far indirect (FF, R/M reg = 101) b11111111 */
	const char* rm = modrm_get_mnem16(mnem);
	MNEM("jmpf %s", rm);
	I80386_EFFECTIVE_ADDRESS effective_address = { 0 };
	i80386_modrm_get_segment(mnem->state, mnem->addressing_size, mnem->modrm, mnem->sib, &effective_address, mnem->segment_prefix);
	i80386_modrm_get_offset(mnem->state, mnem->addressing_size, mnem->modrm, mnem->sib, &effective_address, fetch_byte, fetch_word, fetch_dword, mnem);
	set_step_into_target(mnem, NULL, read_word(mnem, effective_address.logical_address.base, effective_address.logical_address.offset + 2), read_word(mnem, effective_address.logical_address.base, effective_address.logical_address.offset));
}

static void call_intra_direct(I80386_MNEM* mnem) {
	/* Call disp (E8) b11101000 */
	if (mnem->operand_size) {
		uint32_t imm = 0;
		fetch_dword(mnem, &imm);
		imm += mnem->counter;
		MNEM("call %08Xh", imm);
		set_step_into_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + imm);
		set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + mnem->counter);
	}
	else {
		uint16_t imm = 0;
		fetch_word(mnem, &imm);
		imm += mnem->counter;
		MNEM("call %04Xh", imm);
		set_step_into_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + imm);
		set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + mnem->counter);
	}
}
static void call_inter_direct(I80386_MNEM* mnem) {
	/* Call addr:seg (9A) b10011010 */
	if (mnem->operand_size) {
		uint32_t imm = 0;
		uint16_t imm2 = 0;
		fetch_dword(mnem, &imm);
		fetch_word(mnem, &imm2);
		MNEM("callf %04Xh:%04Xh", imm2, imm);
		set_step_into_target(mnem, NULL, imm2, imm);
		set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + mnem->counter);
	}
	else {
		uint16_t imm = 0;
		uint16_t imm2 = 0;
		fetch_word(mnem, &imm);
		fetch_word(mnem, &imm2);
		MNEM("callf %04Xh:%04Xh", imm2, imm);
		set_step_into_target(mnem, NULL, imm2, imm);
		set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + mnem->counter);
	}
}
static void call_intra_indirect(I80386_MNEM* mnem) {
	/* Call near R/M (FF, R/M reg = 010) b11111111 */
	const char* rm = NULL;
	if (mnem->operand_size) {
		rm = modrm_get_mnem32(mnem);
	}
	else {
		rm = modrm_get_mnem16(mnem);
	}
	MNEM("call %s", rm);
	I80386_OPERAND op = modrm_get_rm(mnem);
	set_step_into_target(mnem, &mnem->sdescriptor, 0, modrm_read_rm16(mnem, op));
	set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + mnem->counter);
}
static void call_inter_indirect(I80386_MNEM* mnem) {
	/* Call far R/M (FF, R/M reg = 011) b11111111 */
	const char* rm = NULL;
	if (mnem->operand_size) {
		rm = modrm_get_mnem32(mnem);
	}
	else {
		rm = modrm_get_mnem16(mnem);
	}
	get_seg(mnem, mnem->addressing_segment, "%s:");
	MNEM("callf %s%s", mnem->addressing_segment, rm);I80386_EFFECTIVE_ADDRESS effective_address = { 0 };
	i80386_modrm_get_segment(mnem->state, mnem->addressing_size, mnem->modrm, mnem->sib, &effective_address, mnem->segment_prefix);
	i80386_modrm_get_offset(mnem->state, mnem->addressing_size, mnem->modrm, mnem->sib, &effective_address, fetch_byte, fetch_word, fetch_dword, mnem);
	set_step_into_target(mnem, NULL, read_word(mnem, effective_address.logical_address.base, effective_address.logical_address.offset + 2), read_word(mnem, effective_address.logical_address.base, effective_address.logical_address.offset));
	set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + mnem->counter);
}

static void ret_intra_add_imm(I80386_MNEM* mnem) {
	/* Ret imm16 (C2) b11000010 */
	uint16_t imm = 0;
	fetch_word(mnem, &imm);
	if (mnem->operand_size) {
		MNEM("retd %Xh", imm);
	}
	else {
		MNEM("ret %Xh", imm);
	}
	set_step_into_target(mnem, &mnem->sdescriptor, 0, read_word(mnem, SS, SP));
}
static void ret_intra(I80386_MNEM* mnem) {
	/* Ret (C3) b11000011 */
	if (mnem->operand_size) {
		MNEM0("retd");
	}
	else {
		MNEM0("ret");
	}
	set_step_into_target(mnem, &mnem->sdescriptor, 0, read_word(mnem, SS, SP));
}
static void ret_inter_add_imm(I80386_MNEM* mnem) {
	/* Ret imm16 (CA) b11001010 */
	uint16_t imm = 0;
	fetch_word(mnem, &imm);
	if (mnem->operand_size) {
		MNEM("retfd %Xh", imm);
		uint32_t offset = read_dword(mnem, SS, SP);
		uint16_t selector = read_word(mnem, SS, SP + 4);
		set_step_into_target(mnem, NULL, selector, offset);
	}
	else {
		MNEM("retf %Xh", imm);
		uint16_t offset = read_word(mnem, SS, SP);
		uint16_t selector = read_word(mnem, SS, SP + 2);
		set_step_into_target(mnem, NULL, selector, offset);
	}
}
static void ret_inter(I80386_MNEM* mnem) {
	/* Ret (CB) b11001011 */
	if (mnem->operand_size) {
		MNEM0("retfd");
		uint32_t offset = read_dword(mnem, SS, SP);
		uint16_t selector = read_word(mnem, SS, SP + 4);
		set_step_into_target(mnem, NULL, selector, offset);
	}
	else {
		MNEM0("retf");
		uint16_t offset = read_word(mnem, SS, SP);
		uint16_t selector = read_word(mnem, SS, SP + 2);
		set_step_into_target(mnem, NULL, selector, offset);
	}
}

static void mov_rm_imm(I80386_MNEM* mnem) {
	/* mov r/m, imm (C6/C7) b1100011W */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			uint32_t imm = 0;
			const char* rm = modrm_get_mnem32(mnem);
			fetch_dword(mnem, &imm);
			MNEM("mov %s, %Xh", rm, imm);
		}
		else {
			uint16_t imm = 0;
			const char* rm = modrm_get_mnem16(mnem);
			fetch_word(mnem, &imm);
			MNEM("mov %s, %Xh", rm, imm);
		}
	}
	else {
		uint8_t imm = 0;
		const char* rm = modrm_get_mnem8(mnem);
		fetch_byte(mnem, &imm);
		MNEM("mov %s, %Xh", rm, imm);
	}
}
static void mov_reg_imm(I80386_MNEM* mnem) {
	/* mov reg8/16/32, imm8/16/32 (B0-BF) b1011WREG */
	if (WREG) {
		if (mnem->operand_size) {
			/* mov reg32, imm32 */
			uint32_t imm = 0;
			const char* reg = GET_REG32(mnem->opcode);
			if (!fetch_dword(mnem, &imm)) {
				return;
			}
			MNEM("mov %s, %Xh", reg, imm);
		}
		else {
			/* mov reg16, imm16 */
			uint16_t imm = 0;
			const char* reg = GET_REG16(mnem->opcode);
			if (!fetch_word(mnem, &imm)) {
				return;
			}
			MNEM("mov %s, %Xh", reg, imm);
		}
	}
	else {
		/* mov reg8, imm8 */
		uint8_t imm = 0;
		const char* reg = GET_REG8(mnem->opcode);
		if (!fetch_byte(mnem, &imm)) {
			return;
		}
		MNEM("mov %s, %02Xh", reg, imm);
	}
}
static void mov_rm_reg(I80386_MNEM* mnem) {
	/* mov r/m, reg (88/89/8A/8B) b100010DW */
	fetch_modrm(mnem);
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32(mnem);
			const char* reg = GET_REG32(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("mov %s, %s", rm, reg);
		}
		else {
			const char* rm = modrm_get_mnem16(mnem);
			const char* reg = GET_REG16(mnem->modrm.reg);
			get_direction(mnem, &reg, &rm);
			MNEM("mov %s, %s", rm, reg);
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		const char* reg = GET_REG8(mnem->modrm.reg);
		get_direction(mnem, &reg, &rm);
		MNEM("mov %s, %s", rm, reg);
	}
}
static void mov_accum_mem(I80386_MNEM* mnem) {
	/* mov AL/AX/EAX <--> [ptr16:16/32] (A0/A1/A2/A3) b101000DW */
	uint32_t offset = 0;
	const char* mem = NULL;
	const char* reg = NULL;

	if (mnem->addressing_size) {
		if (!fetch_dword(mnem, &offset)) {
			return;
		}
	}
	else {
		if (!fetch_word(mnem, (uint16_t*)&offset)) {
			return;
		}
	}

	get_seg(mnem, mnem->addressing_segment, "%s:");
	get_unsigned_disp(mnem->addressing_offset, offset);
	if (W) {
		if (mnem->operand_size) {
			MNEM_F(mnem->addressing_str, "dword [%s%s]", mnem->addressing_segment, mnem->addressing_offset);
			reg = GET_REG32(REG_EAX);			
		}
		else {
			MNEM_F(mnem->addressing_str, "word [%s%s]", mnem->addressing_segment, mnem->addressing_offset);
			reg = GET_REG16(REG_AX);
		}		
	}
	else {
		MNEM_F(mnem->addressing_str, "byte [%s%s]", mnem->addressing_segment, mnem->addressing_offset);
		reg = GET_REG8(REG_AL);
	}
	mem = mnem->addressing_str;
	get_direction(mnem, &reg, &mem);
	MNEM("mov %s, %s", reg, mem);
}
static void mov_seg(I80386_MNEM* mnem) {
	/* mov r/m, seg (8C/8E) b100011D0 */
	fetch_modrm(mnem);
	const char* rm = modrm_get_mnem16(mnem);
	const char* reg = seg_mnem[mnem->modrm.reg & 3];
	get_direction(mnem, &reg, &rm);
	MNEM("mov %s, %s", rm, reg);
}
static void mov_cr(I80386_MNEM* mnem) {
	/* mov Cd,Rd /r (0F 20/22) b001000D0 */
	fetch_modrm(mnem);
	if (D) {
		MNEM("mov cr%d, %s", mnem->modrm.reg, GET_REG32(mnem->modrm.rm));
	}
	else {
		MNEM("mov %s, cr%d", GET_REG32(mnem->modrm.rm), mnem->modrm.reg);
	}
}
static void mov_dr(I80386_MNEM* mnem) {
	/* mov Dd,Rd /r (0F 21/23) b001000D1 */
	fetch_modrm(mnem);
	if (D) {
		MNEM("mov dr%d, %s", mnem->modrm.reg, GET_REG32(mnem->modrm.rm));
	}
	else {
		MNEM("mov %s, dr%d", GET_REG32(mnem->modrm.rm), mnem->modrm.reg);
	}
}
static void mov_tr(I80386_MNEM* mnem) {
	/* mov Td,Rd /r (0F 24/26) b001001D0 */
	fetch_modrm(mnem);
	if (D) {
		MNEM("mov tr%d, %s", mnem->modrm.reg, GET_REG32(mnem->modrm.rm));
	}
	else {
		MNEM("mov %s, tr%d", GET_REG32(mnem->modrm.rm), mnem->modrm.reg);
	}
}
static void movzx(I80386_MNEM* mnem) {
	/* Move with zero-extend - (0F B6/B7 /r) b */
	fetch_modrm(mnem);
	const char* reg = NULL;
	const char* mem = NULL;
	if (W) {
		mem = modrm_get_mnem16(mnem);
	}
	else {
		mem = modrm_get_mnem8(mnem);
	}

	if (mnem->operand_size) {
		/* r32, r/m16 */
		reg = GET_REG32(mnem->modrm.reg);
	}
	else {
		/* r16, r/m16 */
		reg = GET_REG16(mnem->modrm.reg);
	}

	MNEM("movzx %s, %s", reg, mem);
}
static void movsx(I80386_MNEM* mnem) {
	/* Move with sign-extend - (0F BE/BF /r) b */
	fetch_modrm(mnem);
	const char* reg = NULL;
	const char* mem = NULL;
	if (W) {
		mem = modrm_get_mnem16(mnem);
	}
	else {
		mem = modrm_get_mnem8(mnem);
	}

	if (mnem->operand_size) {
		/* r32, r/m16 */
		reg = GET_REG32(mnem->modrm.reg);
	}
	else {
		/* r16, r/m16 */
		reg = GET_REG16(mnem->modrm.reg);
	}

	MNEM("movsx %s, %s", reg, mem);
}

static void lea(I80386_MNEM* mnem) {
	/* lea reg16, mem (8D) b10001101 */
	fetch_modrm(mnem);
	const char* reg = NULL;
	const char* mem = NULL;
	if (mnem->operand_size) {
		reg = GET_REG32(mnem->modrm.reg);
		mem = modrm_get_mnem32_nos(mnem);
	}
	else {
		reg = GET_REG16(mnem->modrm.reg);
		mem = modrm_get_mnem16_nos(mnem);
	}
	MNEM("lea %s, %s", reg, mem);
}

static void not(I80386_MNEM* mnem) {
	/* not r/m (F6/F7, R/M reg = b010) b1111011W */
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32(mnem);
			MNEM("not %s", rm);
		}
		else {
			const char* rm = modrm_get_mnem16(mnem);
			MNEM("not %s", rm);
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("not %s", rm);
	}
}
static void neg(I80386_MNEM* mnem) {
	/* neg r/m (F6/F7, R/M reg = b011) b1111011W */
	if (W) {
		if (mnem->operand_size) {
			const char* rm = modrm_get_mnem32(mnem);
			MNEM("neg %s", rm);
		}
		else {
			const char* rm = modrm_get_mnem16(mnem);
			MNEM("neg %s", rm);
		}
	}
	else {
		const char* rm = modrm_get_mnem8(mnem);
		MNEM("neg %s", rm);
	}
}

static void mul_accum_rm(I80386_MNEM* mnem) {
	/* mul AX/DX/EDX:AL/AX/EAX, r/m (F6/F7, R/M reg = b100) b1111011W */
	if (W) {
		if (mnem->operand_size) {
			const char* mul = modrm_get_mnem32(mnem);
			MNEM("mul %s", mul);
		}
		else {
			const char* mul = modrm_get_mnem16(mnem);
			MNEM("mul %s", mul);
		}
	}
	else {
		const char* mul = modrm_get_mnem8(mnem);
		MNEM("mul %s", mul);
	}
}
static void imul_accum_rm(I80386_MNEM* mnem) {
	/* imul AX/DX/EDX:AL/AX/EAX, r/m (F6/F7, R/M reg = b101) b1111011W */
	if (W) {
		if (mnem->operand_size) {
			const char* mul = modrm_get_mnem32(mnem);
			MNEM("imul %s", mul);
		}
		else {
			const char* mul = modrm_get_mnem16(mnem);
			MNEM("imul %s", mul);
		}
	}
	else {
		const char* mul = modrm_get_mnem8(mnem);
		MNEM("imul %s", mul);
	}
}
static void imul_reg_rm_imm(I80386_MNEM* mnem) {
	/* imul Gv,Ev,Iv (69/6B) b011010S1 */
	uint32_t disp = 0;
	const char* reg = NULL;
	const char* rm = NULL;

	fetch_modrm(mnem);

	if (mnem->operand_size) {
		reg = GET_REG32(mnem->modrm.reg);
		rm = modrm_get_mnem32_nos(mnem);
	}
	else {
		reg = GET_REG16(mnem->modrm.reg);
		rm = modrm_get_mnem16_nos(mnem);
	}

	if (S) {
		uint8_t imm2 = 0;
		if (!fetch_byte(mnem, &imm2)) {
			return;
		}
		if (mnem->operand_size) {
			disp = sign_extend8_32(imm2);
		}
		else {
			disp = sign_extend8_16(imm2);
		}
	}
	else {
		if (mnem->operand_size) {
			if (!fetch_dword(mnem, &disp)) {
				return;
			}
		}
		else {
			uint16_t imm = 0;
			if (!fetch_word(mnem, &imm)) {
				return;
			}
			disp = imm;
		}
	}

	if (reg == rm) {
		MNEM("imul %s, ", rm);
	}
	else {
		MNEM("imul %s, %s, ", reg, rm);
	}
	get_unsigned_disp(mnem->str, disp);
}
static void imul_reg_rm(I80386_MNEM* mnem) {
	/* imul Gv,Ev - (0F AF) b00001111 b10101111 */
	const char* reg = NULL;
	const char* rm = NULL;
	if (mnem->operand_size) {
		reg = GET_REG32(mnem->modrm.reg);
		rm = modrm_get_mnem32_nos(mnem);
	}
	else {
		reg = GET_REG16(mnem->modrm.reg);
		rm = modrm_get_mnem16_nos(mnem);
	}
	MNEM("imul %s, %s, ", reg, rm);
}
static void div_accum_rm(I80386_MNEM* mnem) {
	/* div AX/DX/EDX:AL/AX/EAX, r/m (F6/F7, R/M reg = b110) b1111011W */
	if (W) {
		if (mnem->operand_size) {
			const char* div = modrm_get_mnem32(mnem);
			MNEM("div %s", div);
		}
		else {
			const char* div = modrm_get_mnem16(mnem);
			MNEM("div %s", div);
		}
	}
	else {
		const char* div = modrm_get_mnem8(mnem);
		MNEM("div %s", div);
	}
}
static void idiv_accum_rm(I80386_MNEM* mnem) {
	/* idiv AX/DX/EDX:AL/AX/EAX, r/m (F6/F7, R/M reg = b111) b1111011W */
	if (W) {
		if (mnem->operand_size) {
			const char* div = modrm_get_mnem32(mnem);
			MNEM("idiv %s", div);
		}
		else {
			const char* div = modrm_get_mnem16(mnem);
			MNEM("idiv %s", div);
		}
	}
	else {
		const char* div = modrm_get_mnem8(mnem);
		MNEM("idiv %s", div);
	}
}

static void movs(I80386_MNEM* mnem) {
	/* movs (A4/A5) b1010010W */
	if (mnem->segment_prefix != 0xFF && mnem->segment_prefix != SEG_DS) {
		get_seg(mnem, mnem->str, "%s ");
	}
	if (F1) {
		if (F1Z) {
			MNEM0("rep ");
		}
		else {
			MNEM0("repne ");
		}
	}
	if (W) {
		if (mnem->operand_size) {
			MNEM0("movsd");
		}
		else {
			MNEM0("movsw");
		}
	}
	else {
		MNEM0("movsb");
	}
}
static void stos(I80386_MNEM* mnem) {
	/* stos (AA/AB) b1010101W */
	if (mnem->segment_prefix != 0xFF) {
		get_seg(mnem, mnem->str, "%s ");
	}
	if (F1) {
		MNEM0("rep ");
	}
	if (W) {
		if (mnem->operand_size) {
			MNEM0("stosd");
		}
		else {
			MNEM0("stosw");
		}
	}
	else {
		MNEM0("stosb");
	}
}
static void lods(I80386_MNEM* mnem) {
	/* lods (AC/AD) b1010110W */
	if (mnem->segment_prefix != 0xFF) {
		get_seg(mnem, mnem->str, "%s ");
	}
	if (F1) {
		MNEM0("rep ");
	}
	if (W) {
		if (mnem->operand_size) {
			MNEM0("lodsd");
		}
		else {
			MNEM0("lodsw");
		}
	}
	else {
		MNEM0("lodsb");
	}
}
static void cmps(I80386_MNEM* mnem) {
	/* cmps (A6/A7) b1010011W */
	if (mnem->segment_prefix != 0xFF) {
		get_seg(mnem, mnem->str, "%s ");
	}
	if (F1) {
		if (F1Z) {
			MNEM0("repe ");
		}
		else {
			MNEM0("repne ");
		}
	}
	if (W) {
		if (mnem->operand_size) {
			MNEM0("cmpsd");
		}
		else {
			MNEM0("cmpsw");
		}
	}
	else {
		MNEM0("cmpsb");
	}
}
static void scas(I80386_MNEM* mnem) {
	/* scas (AE/AF) b1010111W */
	if (F1) {
		if (F1Z) {
			MNEM0("repe ");
		}
		else {
			MNEM0("repne ");
		}
	}
	if (W) {
		if (mnem->operand_size) {
			MNEM0("scasd");
		}
		else {
			MNEM0("scasw");
		}
	}
	else {
		MNEM0("scasb");
	}
}
static void ins(I80386_MNEM* mnem) {
	/* ins (6C/6D) b1010111W */
	if (mnem->segment_prefix != 0xFF) {
		get_seg(mnem, mnem->str, "%s ");
	}
	if (F1) {
		if (F1Z) {
			MNEM0("repe ");
		}
		else {
			MNEM0("repne ");
		}
	}
	if (W) {
		if (mnem->operand_size) {
			MNEM0("insd");
		}
		else {
			MNEM0("insw");
		}
	}
	else {
		MNEM0("insb");
	}
}
static void outs(I80386_MNEM* mnem) {
	/* scas (6E/6F) b1010111W */
	if (mnem->segment_prefix != 0xFF) {
		get_seg(mnem, mnem->str, "%s ");
	}
	if (F1) {
		if (F1Z) {
			MNEM0("repe ");
		}
		else {
			MNEM0("repne ");
		}
	}
	if (W) {
		if (mnem->operand_size) {
			MNEM0("outsd");
		}
		else {
			MNEM0("outsw");
		}
	}
	else {
		MNEM0("outsb");
	}
}

static void les(I80386_MNEM* mnem) {
	/* les (C4) b11000100 */
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* reg = GET_REG32(mnem->modrm.reg);
		const char* rm = modrm_get_mnem32_nos(mnem);
		MNEM("les %s, %s", reg, rm);
	}
	else {
		const char* reg = GET_REG16(mnem->modrm.reg);
		const char* rm = modrm_get_mnem16_nos(mnem);
		MNEM("les %s, %s", reg, rm);
	}
}
static void lds(I80386_MNEM* mnem) {
	/* lds (C5) b11000101 */
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* reg = GET_REG32(mnem->modrm.reg);
		const char* rm = modrm_get_mnem32_nos(mnem);
		MNEM("lds %s, %s", reg, rm);
	}
	else {
		const char* reg = GET_REG16(mnem->modrm.reg);
		const char* rm = modrm_get_mnem16_nos(mnem);
		MNEM("lds %s, %s", reg, rm);
	}
}
static void lss(I80386_MNEM* mnem) {
	/* lss (0F B2) b10110010 */
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* reg = GET_REG32(mnem->modrm.reg);
		const char* rm = modrm_get_mnem32_nos(mnem);
		MNEM("lss %s, %s", reg, rm);
	}
	else {
		const char* reg = GET_REG16(mnem->modrm.reg);
		const char* rm = modrm_get_mnem16_nos(mnem);
		MNEM("lss %s, %s", reg, rm);
	}
}
static void lfs(I80386_MNEM* mnem) {
	/* lfs (0F B4) b10110100 */
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* reg = GET_REG32(mnem->modrm.reg);
		const char* rm = modrm_get_mnem32_nos(mnem);
		MNEM("lfs %s, %s", reg, rm);
	}
	else {
		const char* reg = GET_REG16(mnem->modrm.reg);
		const char* rm = modrm_get_mnem16_nos(mnem);
		MNEM("lfs %s, %s", reg, rm);
	}
}
static void lgs(I80386_MNEM* mnem) {
	/* lgs (0F B5) b10110101 */
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* reg = GET_REG32(mnem->modrm.reg);
		const char* rm = modrm_get_mnem32_nos(mnem);
		MNEM("lgs %s, %s", reg, rm);
	}
	else {
		const char* reg = GET_REG16(mnem->modrm.reg);
		const char* rm = modrm_get_mnem16_nos(mnem);
		MNEM("lgs %s, %s", reg, rm);
	}
}

static void xlat(I80386_MNEM* mnem) {
	/* Get data pointed by BX + AL (D7) b11010111 */
	if (mnem->segment_prefix != 0xFF && mnem->segment_prefix != SEG_DS) {
		get_seg(mnem, mnem->str, "%s ");
	}
	MNEM0("xlatb");
}

static void esc(I80386_MNEM* mnem) {
	/* esc (D8-DF R/M reg = XXX) b11010REG */
	fetch_modrm(mnem);
	const char* rm = modrm_get_mnem16(mnem);
	MNEM("esc %s", rm);
}

static void loopnz(I80386_MNEM* mnem) {
	/* loop while not zero (E0) b1110000Z */
	uint8_t disp = 0;
	uint16_t se = 0;
	fetch_byte(mnem, &disp);
	se = sign_extend8_16(disp) + mnem->counter;
	MNEM("loopne %04Xh", se);
}
static void loopz(I80386_MNEM* mnem) {
	/* loop while zero (E1) b1110000Z */
	uint8_t disp = 0;
	uint16_t se = 0;
	fetch_byte(mnem, &disp);
	se = sign_extend8_16(disp) + mnem->counter;
	MNEM("loope %04Xh", se);
}
static void loop(I80386_MNEM* mnem) {
	/* loop if CX not zero (E2) b11100010 */
	uint8_t disp = 0;
	uint16_t se = 0;
	fetch_byte(mnem, &disp);
	se = sign_extend8_16(disp) + mnem->counter;
	MNEM("loop %04Xh", se);
}
static void jcxz(I80386_MNEM* mnem) {
	/* jump if CX zero (E3) b11100011 */
	uint8_t disp = 0;
	uint16_t se = 0;
	fetch_byte(mnem, &disp);
	se = sign_extend8_16(disp) + mnem->counter;
	MNEM("jcxz %04Xh", se);

	if (mnem->state->cx == 0) {
		set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset + se);
	}
}

static void in_accum_imm(I80386_MNEM* mnem) {
	/* in AL/AX/EAX, imm - (E4/E5) b0000000W */
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	if (W) {
		if (mnem->operand_size) {
			MNEM("in eax, %Xh", imm);
		}
		else {
			MNEM("in ax, %Xh", imm);
		}
	}
	else {
		MNEM("in al, %Xh", imm);
	}
}
static void out_accum_imm(I80386_MNEM* mnem) {
	/* out imm, AL/AX/EAX - (E6/E7) b0000000W  */
	uint8_t imm = 0;
	fetch_byte(mnem, &imm);
	if (W) {
		if (mnem->operand_size) {
			MNEM("out %Xh, eax", imm);
		}
		else {
			MNEM("out %Xh, ax", imm);
		}
	}
	else {
		MNEM("out %Xh, al", imm);
	}
}
static void in_accum_dx(I80386_MNEM* mnem) {
	/* in AL/AX/EAX, DX - (EC/ED) b0000000W */
	if (W) {
		if (mnem->operand_size) {
			MNEM0("in eax, dx");
		}
		else {
			MNEM0("in ax, dx");
		}
	}
	else {
		MNEM0("in al, dx");
	}
}
static void out_accum_dx(I80386_MNEM* mnem) {
	/* out DX, AL/AX/EAX - (EE/EF) b0000000W */
	if (W) {
		if (mnem->operand_size) {
			MNEM0("out dx, eax");
		}
		else {
			MNEM0("out dx, ax");
		}
	}
	else {
		MNEM0("out dx, al");
	}
}

static void int_(I80386_MNEM* mnem) {
	/* interrupt CD b11001101 */
	uint8_t type = 0;
 	if (mnem->opcode & 0x1) {
		fetch_byte(mnem, &type);
	}
	MNEM("int %Xh", type);

	uint16_t offset = read_word(mnem, mnem->state->idtr.base, type << 2);
	uint16_t selector = read_word(mnem, mnem->state->idtr.base, (type << 2) + 2);

	set_step_into_target(mnem, NULL, selector, offset);
	set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + mnem->counter);
}
static void int3(I80386_MNEM* mnem) {
	/* interrupt CC b11001100 */
	MNEM0("int3");

	uint16_t offset = read_word(mnem, mnem->state->idtr.base, 3 << 2);
	uint16_t selector = read_word(mnem, mnem->state->idtr.base, (3 << 2) + 2);

	set_step_into_target(mnem, NULL, selector, offset);
	set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + mnem->counter);
}
static void into(I80386_MNEM* mnem) {
	/* interrupt on overflow (CE) b11001110 */
	MNEM0("into");

	uint16_t offset = read_word(mnem, mnem->state->idtr.base, 4 << 2);
	uint16_t selector = read_word(mnem, mnem->state->idtr.base, (4 << 2) + 2);

	set_step_into_target(mnem, NULL, selector, offset);
	set_step_over_target(mnem, &mnem->sdescriptor, 0, mnem->offset32 + mnem->counter);
}
static void iret(I80386_MNEM* mnem) {
	/* interrupt on return (CF) b11001111 */
	MNEM0("iret");

	set_step_into_target(mnem, NULL, read_word(mnem, SS, SP + 2), read_word(mnem, SS, SP));
}

static void bound(I80386_MNEM* mnem) {
	/* bound (62) b01100010 */
	MNEM("bound");
}

static void sldt(I80386_MNEM* mnem) {
	/* Store LDT (0F 00 /0) */
	MNEM0("sldt");
}
static void str(I80386_MNEM* mnem) {
	/* Store task register (0F 00 /1) */
	MNEM0("str");
}
static void lldt(I80386_MNEM* mnem) {
	/* Load LDT (0F 00 /2) */
	MNEM0("lldt");
}
static void ltr(I80386_MNEM* mnem) {
	/* Load task register (0F 00 /3) */
	MNEM0("ltr");
}
static void verr(I80386_MNEM* mnem) {
	/* (0F 00 /4) */
	MNEM0("verr");
}
static void verw(I80386_MNEM* mnem) {
	/* (0F 00 /5) */
	MNEM0("verw");
}
static void sgdt(I80386_MNEM* mnem) {
	/* Store GDT (0F 01 /0) */
	MNEM0("sgdt");
}
static void sidt(I80386_MNEM* mnem) {
	/* Store IDT (0F 01 /1) */
	MNEM0("sidt");
}
static void lgdt(I80386_MNEM* mnem) {
	/* Load GDT (0F 01 /2) */
	I80386_MNEM_get_modrm(mnem, mnem->addressing_str, "[%s%s]");
	MNEM("lgdt %s", mnem->addressing_str);
}
static void lidt(I80386_MNEM* mnem) {
	/* Load IDT (0F 01 /3) */
	MNEM0("lidt");
}
static void smsw(I80386_MNEM* mnem) {
	/* Store MSW (0F 01 /4) */
	MNEM0("smsw");
}
static void lmsw(I80386_MNEM* mnem) {
	/* Load MSW (0F 01 /6) */
	MNEM0("lmsw");
}
static void lar(I80386_MNEM* mnem) {
	/* Load access rights (0F 02 /r) */
	MNEM0("lar");
}
static void lsl(I80386_MNEM* mnem) {
	/* Load segment limit (0F 03 /r) */
	MNEM0("lsl");
}
static void clts(I80386_MNEM* mnem) {
	/* clear TS (0F 06) */
	MNEM0("clts");
}
static void arpl(I80386_MNEM* mnem) {
	/* (63 /r) */
	MNEM0("arpl");
}
static void loadall(I80386_MNEM* mnem) {
	/* loadall (0F 07) b00000111 */
	MNEM("loadall");
}

static void setcc(I80386_MNEM* mnem) {
	/* set byte on condition (Eb) (0F 90-9F) b1001CCCC */
	MNEM0("setcc");
}
static void bt(I80386_MNEM* mnem) {
	/* bit test (Ev) (0F A3) b10100011 */
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* rm = modrm_get_mnem32_nos(mnem);
		const char* reg = GET_REG32(mnem->modrm.reg);
		MNEM("bt %s, %s", rm, reg);
	}
	else {
		const char* rm = modrm_get_mnem16_nos(mnem);
		const char* reg = GET_REG16(mnem->modrm.reg);
		MNEM("bt %s, %s", rm, reg);
	}
}
static void bts(I80386_MNEM* mnem) {
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* rm = modrm_get_mnem32_nos(mnem);
		const char* reg = GET_REG32(mnem->modrm.reg);
		MNEM("bts %s, %s", rm, reg);
	}
	else {
		const char* rm = modrm_get_mnem16_nos(mnem);
		const char* reg = GET_REG16(mnem->modrm.reg);
		MNEM("bts %s, %s", rm, reg);
	}
}
static void btr(I80386_MNEM* mnem) {
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* rm = modrm_get_mnem32_nos(mnem);
		const char* reg = GET_REG32(mnem->modrm.reg);
		MNEM("btr %s, %s", rm, reg);
	}
	else {
		const char* rm = modrm_get_mnem16_nos(mnem);
		const char* reg = GET_REG16(mnem->modrm.reg);
		MNEM("btr %s, %s", rm, reg);
	}
}
static void btc(I80386_MNEM* mnem) {
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* rm = modrm_get_mnem32_nos(mnem);
		const char* reg = GET_REG32(mnem->modrm.reg);
		MNEM("btc %s, %s", rm, reg);
	}
	else {
		const char* rm = modrm_get_mnem16_nos(mnem);
		const char* reg = GET_REG16(mnem->modrm.reg);
		MNEM("btc %s, %s", rm, reg);
	}
}
static void bsf(I80386_MNEM* mnem) {
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* rm = modrm_get_mnem32_nos(mnem);
		const char* reg = GET_REG32(mnem->modrm.reg);
		MNEM("bsf %s, %s", reg, rm);
	}
	else {
		const char* rm = modrm_get_mnem16_nos(mnem);
		const char* reg = GET_REG16(mnem->modrm.reg);
		MNEM("bsf %s, %s", reg, rm);
	}
}
static void bsr(I80386_MNEM* mnem) {
	fetch_modrm(mnem);
	if (mnem->operand_size) {
		const char* rm = modrm_get_mnem32_nos(mnem);
		const char* reg = GET_REG32(mnem->modrm.reg);
		MNEM("bsr %s, %s", reg, rm);
	}
	else {
		const char* rm = modrm_get_mnem16_nos(mnem);
		const char* reg = GET_REG16(mnem->modrm.reg);
		MNEM("bsr %s, %s", reg, rm);
	}
}

/* Prefixes */
static int rep(I80386_MNEM* mnem) {
	/* rep/repz/repnz (F2/F3) b1111001Z */
	mnem->internal_flags |= INTERNAL_FLAG_F1;     /* Set F1 */
	mnem->internal_flags &= ~INTERNAL_FLAG_F1Z;   /* Clr F1Z */
	mnem->internal_flags |= (mnem->opcode & 0x1); /* Set F1Z */
	fetch_byte(mnem, &mnem->opcode);
	return I80386_DECODE_REQ_CYCLE;
}
static int segment_override(I80386_MNEM* mnem) {
	/* (26/2E/36/3E) b001SR110 */
	mnem->segment_prefix = SR;
	fetch_byte(mnem, &mnem->opcode);
	return I80386_DECODE_REQ_CYCLE;
}
static int segment_override_extended(I80386_MNEM* mnem) {
	/* (64/65) b01100SRX */
	mnem->segment_prefix = mnem->opcode & 0x7;
	fetch_byte(mnem, &mnem->opcode);
	return I80386_DECODE_REQ_CYCLE;
}
static int lock(I80386_MNEM* mnem) {
	/* lock the bus (F0/F1) b11110000 */
	MNEM0("lock ");
	fetch_byte(mnem, &mnem->opcode);
	return I80386_DECODE_REQ_CYCLE;
}
static int operand_size(I80386_MNEM* mnem) {
	mnem->operand_size ^= 1;
	fetch_byte(mnem, &mnem->opcode);
	return I80386_DECODE_REQ_CYCLE;
}
static int address_size(I80386_MNEM* mnem) {
	mnem->addressing_size ^= 1;
	fetch_byte(mnem, &mnem->opcode);
	return I80386_DECODE_REQ_CYCLE;
}

/* Fetch */
static void i80386_next(I80386_MNEM* mnem, const I80386_SEGMENT_REGISTER* sdescriptor, uint32_t offset) {
	mnem->sdescriptor.selector = sdescriptor->selector;
	mnem->sdescriptor.desc.base = sdescriptor->desc.base;
	mnem->sdescriptor.desc.limit = sdescriptor->desc.limit;
	mnem->sdescriptor.desc.access = sdescriptor->desc.access;
	mnem->sdescriptor.desc.flags = sdescriptor->desc.flags;

	mnem->offset32 = offset;
	mnem->counter = 0;

	mnem->modrm.byte = 0;
	mnem->sib.byte = 0;
	mnem->segment_prefix = 0xFF;
	mnem->internal_flags = 0;
	
	mnem->str[0] = '\0'; 
	mnem->addressing_segment[0] = '\0';
	mnem->addressing_offset[0] = '\0';
	mnem->addressing_str[0] = '\0';

	mnem->step_into_has_target = 0;
	mnem->step_into_address.base = 0;
	mnem->step_into_address.offset = 0;

	mnem->step_over_has_target = 0;
	mnem->step_over_address.base = 0;
	mnem->step_over_address.offset = 0;

	mnem->operand_size = mnem->sdescriptor.desc.flags.default_size;
	mnem->addressing_size = mnem->sdescriptor.desc.flags.default_size;

	mnem->effective_address.stack_address = 0;
	mnem->effective_address.valid = 0;
	mnem->effective_address.segment_index = 0;
	mnem->effective_address.base = 0;
	mnem->effective_address.scale = 0;
	mnem->effective_address.index = 0;
	mnem->effective_address.logical_address.offset = 0;
	mnem->effective_address.logical_address.base = 0;
}
static void i80386_fetch(I80386_MNEM* mnem, const I80386_SEGMENT_REGISTER* sdescriptor, uint32_t offset) {
	i80386_next(mnem, sdescriptor, offset);
	fetch_byte(mnem, &mnem->opcode);
}

/* Decode */
static void i80386_decode_opcode_80(I80386_MNEM* mnem) {
	/* 0x80 - 0x83 b100000SW */
	fetch_modrm(mnem);
	switch (mnem->modrm.reg) {
		case 0b000: /* ADD */
			add_rm_imm(mnem);
			break;
		case 0b001: /* OR */
			or_rm_imm(mnem);
			break;
		case 0b010: /* ADC */
			adc_rm_imm(mnem);
			break;
		case 0b011: /* SBB */
			sbb_rm_imm(mnem);
			break;
		case 0b100: /* AND */
			and_rm_imm(mnem);
			break;
		case 0b101: /* SUB */
			sub_rm_imm(mnem);
			break;
		case 0b110: /* XOR */
			xor_rm_imm(mnem);
			break;
		case 0b111: /* CMP */
			cmp_rm_imm(mnem);
			break;
	}
}
static void i80386_decode_opcode_c0(I80386_MNEM* mnem) {
	/* 0xC0 - 0xC1 b1100000W (Shift Immed group 2) */
	fetch_modrm(mnem);
	switch (mnem->modrm.reg) {
		case 0b000:
			rol_rm_imm(mnem);
			break;
		case 0b001:
			ror_rm_imm(mnem);
			break;
		case 0b010:
			rcl_rm_imm(mnem);
			break;
		case 0b011:
			rcr_rm_imm(mnem);
			break;
		case 0b100:
			shl_rm_imm(mnem);
			break;
		case 0b101:
			shr_rm_imm(mnem);
			break;
		case 0b110:
			sal_rm_imm(mnem);
			break;
		case 0b111:
			sar_rm_imm(mnem);
			break;
	}
}
static void i80386_decode_opcode_d0(I80386_MNEM* mnem) {
	/* 0xD0 - 0xD3 b110100VW */
	fetch_modrm(mnem);
	switch (mnem->modrm.reg) {
		case 0b000:
			rol_rm_cl(mnem);
			break;
		case 0b001:
			ror_rm_cl(mnem);
			break;
		case 0b010:
			rcl_rm_cl(mnem);
			break;
		case 0b011:
			rcr_rm_cl(mnem);
			break;
		case 0b100:
			shl_rm_cl(mnem);
			break;
		case 0b101:
			shr_rm_cl(mnem);
			break;
		case 0b110:
			sal_rm_cl(mnem);
			break;
		case 0b111:
			sar_rm_cl(mnem);
			break;
	}
}
static void i80386_decode_opcode_f6(I80386_MNEM* mnem) {
	/* F6/F7 b1111011W */
	fetch_modrm(mnem);
	switch (mnem->modrm.reg) {
		case 0b000:
			test_rm_imm(mnem);
			break;
		case 0b001:
			test_rm_imm(mnem);
			break;
		case 0b010:
			not(mnem);
			break;
		case 0b011:
			neg(mnem);
			break;
		case 0b100:
			mul_accum_rm(mnem);
			break;
		case 0b101:
			imul_accum_rm(mnem);
			break;
		case 0b110:
			div_accum_rm(mnem);
			break;
		case 0b111:
			idiv_accum_rm(mnem);
			break;
	}
}
static void i80386_decode_opcode_fe(I80386_MNEM* mnem) {
	/* FE b1111111W */
	fetch_modrm(mnem);
	switch (mnem->modrm.reg) {
		case 0b000:
			inc_rm(mnem);
			break;
		case 0b001:
			dec_rm(mnem);
			break;

		case 0b010:
		case 0b011:
		case 0b100:
		case 0b101:
		case 0b110:
		case 0b111:
			/* BAD - exception #UD */
			break;
	}
}
static void i80386_decode_opcode_ff(I80386_MNEM* mnem) {
	/* FF b1111111W */
	fetch_modrm(mnem);
	switch (mnem->modrm.reg) {
		case 0b000:
			inc_rm(mnem);
			break;
		case 0b001:
			dec_rm(mnem);
			break;
		case 0b010:
			call_intra_indirect(mnem);
			break;
		case 0b011:
			call_inter_indirect(mnem);
			break;
		case 0b100:
			jmp_intra_indirect(mnem);
			break;
		case 0b101:
			jmp_inter_indirect(mnem);
			break;
		case 0b110:
			push_rm(mnem);
			break;
		case 0b111:
			push_rm(mnem);
			break;
	}
}
static void i80386_decode_opcode_0f00(I80386_MNEM* cpu) {
	/* 0F 00 b00000000 (Group 6) */
	fetch_modrm(cpu);
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
	}
}
static void i80386_decode_opcode_0f01(I80386_MNEM* mnem) {
	/* 0F 01 b00000001 (Group 7) */
	fetch_modrm(mnem);
	switch (mnem->modrm.reg) {
		case 0b000:
			sgdt(mnem);
			break;
		case 0b001:
			sidt(mnem);
			break;
		case 0b010:
			lgdt(mnem);
			break;
		case 0b011:
			lidt(mnem);
			break;
		case 0b100:
			smsw(mnem);
			break;
		case 0b110:
			lmsw(mnem);
			break;
	}
}
static void i80386_decode_opcode_0f(I80386_MNEM* mnem) {
	/* 0x0F XX (2-byte opcode map) */
	if (!fetch_byte(mnem, &mnem->opcode)) {
		return;
	}

	switch (mnem->opcode) {
		case 0x00: /* Group 6 */
			i80386_decode_opcode_0f00(mnem);
			break;
		case 0x01: /* Group 7 */
			i80386_decode_opcode_0f01(mnem);
			break;
		case 0x02:
			lar(mnem);
			break;
		case 0x03:
			lsl(mnem);
			break;
		case 0x05:
			loadall(mnem);
			break;
		case 0x06:
			clts(mnem);
			break;

		case 0x20:
			mov_cr(mnem);
			break;
		case 0x21:
			mov_dr(mnem);
			break;
		case 0x22:
			mov_cr(mnem);
			break;
		case 0x23:
			mov_dr(mnem);
			break;
		case 0x24:
			mov_tr(mnem);
			break;
		case 0x26:
			mov_tr(mnem);
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
			jcc_long(mnem);
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
			setcc(mnem);
			break;

		case 0xA0: /* Push FS */
			push_seg(mnem);
			break;
		case 0xA1: /* Pop FS */
			pop_seg(mnem);
			break;
		case 0xA3:
			bt(mnem);
			break;
		case 0xA4:
			shld_rm_imm(mnem);
			break;
		case 0xA5:
			shld_rm_cl(mnem);
			break;

		case 0xA8: /* Push GS */
			push_seg(mnem);
			break;
		case 0xA9: /* Pop GS */
			pop_seg(mnem);
			break;

		case 0xAB:
			bts(mnem);
			break;
		case 0xAC:
			shrd_rm_imm(mnem);
			break;
		case 0xAD:
			shrd_rm_cl(mnem);
			break;
		case 0xAF:
			imul_reg_rm(mnem);
			break;

		case 0xB2:
			lss(mnem);
			break;
		case 0xB3:
			btr(mnem);
			break;
		case 0xB4:
			lfs(mnem);
			break;
		case 0xB5:
			lgs(mnem);
			break;
		case 0xB6:
		case 0xB7:
			movzx(mnem);
			break;
		case 0xBB:
			btc(mnem);
			break;
		case 0xBC:
			bsf(mnem);
			break;
		case 0xBD:
			bsr(mnem);
			break;
		case 0xBE:
		case 0xBF:
			movsx(mnem);
			break;
	}
}

static int i80386_decode_opcode(I80386_MNEM* mnem) {
	switch (mnem->opcode) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
			add_rm_reg(mnem);
			break;
		case 0x04:
		case 0x05:
			add_accum_imm(mnem);
			break;
		case 0x06:
			push_seg(mnem);
			break;
		case 0x07:
			pop_seg(mnem);
			break;
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
			or_rm_reg(mnem);
			break;
		case 0x0C:
		case 0x0D:
			or_accum_imm(mnem);
			break;
		case 0x0E:
			push_seg(mnem);
			break;
		case 0x0F:
			i80386_decode_opcode_0f(mnem);
			break;
		
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
			adc_rm_reg(mnem);
			break;
		case 0x14:
		case 0x15:
			adc_accum_imm(mnem);
			break;
		case 0x16:
			push_seg(mnem);
			break;
		case 0x17:
			pop_seg(mnem);
			break;
		case 0x18:
		case 0x19:
		case 0x1A:
		case 0x1B:
			sbb_rm_reg(mnem);
			break;
		case 0x1C:
		case 0x1D:
			sbb_accum_imm(mnem);
			break;
		case 0x1E:
			push_seg(mnem);
			break;
		case 0x1F:
			pop_seg(mnem);
			break;
		
		case 0x20:
		case 0x21:
		case 0x22:
		case 0x23:
			and_rm_reg(mnem);
			break;
		case 0x24:
		case 0x25:
			and_accum_imm(mnem);
			break;
		case 0x26:
			return segment_override(mnem);
		case 0x27:
			daa(mnem);
			break;
		case 0x28:
		case 0x29:
		case 0x2A:
		case 0x2B:
			sub_rm_reg(mnem);
			break;
		case 0x2C:
		case 0x2D:
			sub_accum_imm(mnem);
			break;
		case 0x2E:
			return segment_override(mnem);
		case 0x2F:
			das(mnem);
			break;
		
		case 0x30:
		case 0x31:
		case 0x32:
		case 0x33:
			xor_rm_reg(mnem);
			break;
		case 0x34:
		case 0x35:
			xor_accum_imm(mnem);
			break;
		case 0x36:
			return segment_override(mnem);
		case 0x37:
			aaa(mnem);
			break;
		case 0x38:
		case 0x39:
		case 0x3A:
		case 0x3B:
			cmp_rm_reg(mnem);
			break;
		case 0x3C:
		case 0x3D:
			cmp_accum_imm(mnem);
			break;
		case 0x3E:
			return segment_override(mnem);
		case 0x3F:
			aas(mnem);
			break;

		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:
		case 0x46:
		case 0x47:
			inc_reg(mnem);
			break;

		case 0x48:
		case 0x49:
		case 0x4A:
		case 0x4B:
		case 0x4C:
		case 0x4D:
		case 0x4E:
		case 0x4F:
			dec_reg(mnem);
			break;

		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:
		case 0x54:
		case 0x55:
		case 0x56:
		case 0x57:
			push_reg(mnem);
			break;

		case 0x58:
		case 0x59:
		case 0x5A:
		case 0x5B:
		case 0x5C:
		case 0x5D:
		case 0x5E:
		case 0x5F:
			pop_reg(mnem);
			break;

		case 0x60:
			pusha(mnem);
			break;
		case 0x61:
			popa(mnem);
			break;
		case 0x62:
			bound(mnem);
			break;
		case 0x63:
			arpl(mnem);
			break;
		case 0x64:
			return segment_override_extended(mnem);
		case 0x65:
			return segment_override_extended(mnem);
		case 0x66:
			return operand_size(mnem);
		case 0x67:
			return address_size(mnem);
		case 0x68:
			push_imm(mnem);
			break;
		case 0x69:
			imul_reg_rm_imm(mnem);
			break;
		case 0x6A:
			push_imm(mnem);
			break;
		case 0x6B:
			imul_reg_rm_imm(mnem);
			break;
		case 0x6C:
		case 0x6D:
			ins(mnem);
			break;
		case 0x6E:
		case 0x6F:
			outs(mnem);
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
			jcc_short(mnem);
			break;

		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
			i80386_decode_opcode_80(mnem);
			break;
		case 0x84:
		case 0x85:
			test_rm_reg(mnem);
			break;
		case 0x86:
		case 0x87:
			xchg_rm_reg(mnem);
			break;
		case 0x88:
		case 0x89:
		case 0x8A:
		case 0x8B:
			mov_rm_reg(mnem);
			break;
		case 0x8C:
			mov_seg(mnem);
			break;
		case 0x8D:
			lea(mnem);
			break;
		case 0x8E:
			mov_seg(mnem);
			break;
		case 0x8F:
			pop_rm(mnem);
			break;

		case 0x90:
			nop(mnem);
			break;
		case 0x91:
		case 0x92:
		case 0x93:
		case 0x94:
		case 0x95:
		case 0x96:
		case 0x97:
			xchg_accum_reg(mnem);
			break;
		case 0x98:
			cbw(mnem);
			break;
		case 0x99:
			cwd(mnem);
			break; 
		case 0x9A:
			call_inter_direct(mnem);
			break;
		case 0x9B:
			wait(mnem);
			break;
		case 0x9C:
			pushf(mnem);
			break;
		case 0x9D:
			popf(mnem);
			break;
		case 0x9E:
			sahf(mnem);
			break;
		case 0x9F:
			lahf(mnem);
			break;

		case 0xA0:
		case 0xA1:
		case 0xA2:
		case 0xA3:
			mov_accum_mem(mnem);
			break;
		case 0xA4:
		case 0xA5:
			movs(mnem);
			break;
		case 0xA6:
		case 0xA7:
			cmps(mnem);
			break;
		case 0xA8:
		case 0xA9:
			test_accum_imm(mnem);
			break;
		case 0xAA:
		case 0xAB:
			stos(mnem);
			break;
		case 0xAC:
		case 0xAD:
			lods(mnem);
			break;
		case 0xAE:
		case 0xAF:
			scas(mnem);
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
			mov_reg_imm(mnem);
			break;

		case 0xC0:
		case 0xC1:
			i80386_decode_opcode_c0(mnem);
			break;
		case 0xC2:
			ret_intra_add_imm(mnem);
			break;
		case 0xC3:
			ret_intra(mnem);
			break;
		case 0xC4:
			les(mnem);
			break;
		case 0xC5:
			lds(mnem);
			break;
		case 0xC6:
		case 0xC7:
			mov_rm_imm(mnem);
			break;
		case 0xC8:
			enter(mnem);
			break;
		case 0xC9:
			leave(mnem);
			break;
		case 0xCA:
			ret_inter_add_imm(mnem);
			break;
		case 0xCB:
			ret_inter(mnem);
			break;
		case 0xCC:
			int3(mnem);
			break;
		case 0xCD:
			int_(mnem);
			break;
		case 0xCE:
			into(mnem);
			break;
		case 0xCF:
			iret(mnem);
			break;
			
		case 0xD0:
		case 0xD1:
		case 0xD2:
		case 0xD3:
			i80386_decode_opcode_d0(mnem);
			break;
		case 0xD4:
			aam(mnem);
			break;
		case 0xD5:
			aad(mnem);
			break;
		case 0xD6: /* 8086 undocumented */
			salc(mnem);
			break;
		case 0xD7:
			xlat(mnem);
			break;
		case 0xD8:
		case 0xD9:
		case 0xDA:
		case 0xDB:
		case 0xDC:
		case 0xDD:
		case 0xDE:
		case 0xDF:
			esc(mnem);
			break;

		case 0xE0:
			loopnz(mnem);
			break;
		case 0xE1:
			loopz(mnem);
			break;
		case 0xE2:
			loop(mnem);
			break;
		case 0xE3:
			jcxz(mnem);
			break;
		case 0xE4:
		case 0xE5:
			in_accum_imm(mnem);
			break;
		case 0xE6:
		case 0xE7:
			out_accum_imm(mnem);
			break;
		case 0xE8:
			call_intra_direct(mnem);
			break;
		case 0xE9:
			jmp_intra_direct(mnem);
			break;
		case 0xEA:
			jmp_inter_direct(mnem);
			break;
		case 0xEB:
			jmp_intra_direct_short(mnem);
			break;
		case 0xEC:
		case 0xED:
			in_accum_dx(mnem);
			break;
		case 0xEE:
		case 0xEF:
			out_accum_dx(mnem);
			break;

		case 0xF0:
		case 0xF1:
			return lock(mnem);
		case 0xF2:
		case 0xF3:
			return rep(mnem);
		case 0xF4:
			hlt(mnem);
			break;
		case 0xF5:
			cmc(mnem);
			break;
		case 0xF6:
		case 0xF7:
			i80386_decode_opcode_f6(mnem);
			break;
		case 0xF8:
			clc(mnem);
			break;
		case 0xF9:
			stc(mnem);
			break;
		case 0xFA:
			cli(mnem);
			break;
		case 0xFB:
			sti(mnem);
			break;
		case 0xFC:
			cld(mnem);
			break;
		case 0xFD:
			std(mnem);
			break;
		case 0xFE:
			i80386_decode_opcode_fe(mnem);
			break;
		case 0xFF:
			i80386_decode_opcode_ff(mnem);
			break;
	}
	return I80386_DECODE_OK;
}
static int i80386_decode_instruction(I80386_MNEM* mnem) {
	int r = 0;
	do {
		r = i80386_decode_opcode(mnem);
	} while (r == I80386_DECODE_REQ_CYCLE);
	return r;
}

int i80386_mnem(I80386_MNEM* mnem) {
	/* Fetch, Decode, Disassemble at cs:eip */
	i80386_fetch(mnem, &mnem->state->segment_registers[SEG_CS], mnem->state->eip);
	return i80386_decode_instruction(mnem);
}
int i80386_mnem_at(I80386_MNEM* mnem, uint32_t offset) {
	/* Fetch, Decode, Disassemble at base+offset */
	i80386_fetch(mnem, &mnem->state->segment_registers[SEG_CS], mnem->state->eip+offset);
	return i80386_decode_instruction(mnem);
}
int i80386_mnem_at_bo(I80386_MNEM* mnem, uint32_t base, uint32_t offset) {
	/* Fetch, Decode, Disassemble at base+offset */
	I80386_SEGMENT_REGISTER sdescriptor = {
		.selector = (base >> 4) & 0xFFFF,
		.desc.base = base,
		.desc.limit = mnem->state->cs.desc.limit,
		.desc.access.byte = mnem->state->cs.desc.access.byte,
		.desc.flags.byte = mnem->state->cs.desc.flags.byte,
	};
	i80386_fetch(mnem, &sdescriptor, offset);
	return i80386_decode_instruction(mnem);
}
int i80386_mnem_at_so(I80386_MNEM* mnem, uint16_t selector, uint32_t offset) {
	/* Fetch, Decode, Disassemble at selector:offset */
	I80386_SEGMENT_REGISTER sdescriptor = {
		.selector = selector,
		.desc.base = selector << 4,
		.desc.limit = mnem->state->cs.desc.limit,
		.desc.access.byte = mnem->state->cs.desc.access.byte,
		.desc.flags.byte = mnem->state->cs.desc.flags.byte,
	};
	i80386_fetch(mnem, &sdescriptor, offset);
	return i80386_decode_instruction(mnem);
}

uint32_t i80386_mnem_get_step_into_target(I80386_MNEM* mnem) {
	if (mnem->step_into_has_target) {
		return i80386_get_physical_address_bo(mnem->step_into_address.base, mnem->step_into_address.offset);
	}
	else {
		return i80386_get_physical_address_bo(mnem->sdescriptor.desc.base, EIP);
	}
}
uint32_t i80386_mnem_get_step_over_target(I80386_MNEM* mnem) {
	if (mnem->step_over_has_target) {
		return i80386_get_physical_address_bo(mnem->step_over_address.base, mnem->step_over_address.offset);
	}
	else {
		return i80386_get_physical_address_bo(mnem->sdescriptor.desc.base, EIP);
	}
}

#endif

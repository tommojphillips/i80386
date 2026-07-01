/* i8086_muldiv.c
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 MUL/DIV Microcode Emulation
 */

#include <stdint.h>

#include "i8086.h"

#define SF cpu->status.sf
#define CF cpu->status.cf
#define ZF cpu->status.zf
#define PF cpu->status.pf
#define OF cpu->status.of
#define AF cpu->status.af

#define SET_ZF(r) ZF = (r) == 0

#define SET_PF(r) { \
	uint8_t pf = (r);\
	pf ^= (pf >> 4); \
	pf ^= (pf >> 2); \
	pf ^= (pf >> 1); \
	PF = (~pf) & 1; }

#define SET_SF8(r) SF = ((r) & 0x80) >> 7
#define SET_ZF8(r) SET_ZF(r)
#define SET_PF8(r) SET_PF(r)

#define SET_SF16(r) SF = ((r) & 0x8000) >> 15
#define SET_ZF16(r) SET_ZF(r)
#define SET_PF16(r) SET_PF(r & 0xFF)

#ifdef I8086_MULDIV_DBG
#define dbg_print(x) printf(x)
#else
#define dbg_print(x)
#endif

 /* DBZ */
#define INT_DBZ 0 // ITC 0
extern void i8086_int(I8086* cpu, uint8_t type);

/* 8bit MUL/DIV */

static void cor_rcl8(uint8_t operand, uint8_t* sigma, uint8_t* cf) {
	uint8_t tmp_cf = 0;
	if (cf) tmp_cf = *cf;

	uint8_t old_cf = tmp_cf;
	tmp_cf = (operand >> 7) & 1;
	operand = (operand << 1) | old_cf;
	if (cf) *cf = tmp_cf;
	if (sigma) *sigma = operand;
}
static void cor_sub8(uint8_t operand1, uint8_t operand2, uint8_t* sigma, uint8_t* cf, uint8_t* of, uint8_t* af) {
	uint16_t tmp = (operand1 - operand2);
	if (af) *af = ((operand1 ^ operand2 ^ tmp) & 0x10) != 0;
	if (of) *of = (((operand1 ^ operand2) & (operand1 ^ tmp)) & 0x80) != 0;
	if (cf) *cf = (operand2) > (operand1);
	if (sigma) *sigma = (tmp & 0xFF);
}
static void cor_neg8(uint8_t operand, uint8_t* sigma, uint8_t* cf) {
	cor_sub8(0, operand, sigma, cf, NULL, NULL);
}

static void cor_rcr8(uint8_t operand, uint8_t* sigma, uint8_t* cf) {
	uint8_t carry = 0;
	if (cf) carry = *cf;

	uint8_t old_cf = carry;
	carry = (operand & 1);
	operand = (operand >> 1) | (old_cf << 7);
	if (cf) *cf = carry;
	if (sigma) *sigma = operand;
}
static void cor_add8(uint8_t operand1, uint8_t operand2, uint8_t* sigma, uint8_t* out_cf) {
	uint16_t tmp = ((uint16_t)operand1 + (uint16_t)operand2);
	if (out_cf) *out_cf = (tmp) > 0xFF;
	if (sigma) *sigma = (tmp & 0xFF);
}
static void cor_adc8(uint8_t operand1, uint8_t operand2, uint8_t* sigma, uint8_t in_cf, uint8_t* out_cf, uint8_t* out_of, uint8_t* out_af) {
	uint16_t tmp = ((uint16_t)operand1 + (uint16_t)operand2 + in_cf);
	if (out_af) *out_af = ((operand1 ^ operand2 ^ tmp) & 0x10) != 0;
	if (out_of) *out_of = (((tmp ^ operand1) & (tmp ^ operand2)) & 0x80) != 0;
	if (out_cf) *out_cf = (tmp) > 0xFF;
	if (sigma) *sigma = (tmp & 0xFF);
}

static void cor_negate8_ln7(uint8_t* tmpb, uint8_t* neg_flag) {
	uint8_t sigma;
	uint8_t carry;

	/* 1BB: | LRCY tmpb */
	carry = ((*tmpb & 0x80) != 0);

	/* 1BC: | NEG tmpb */
	cor_neg8(*tmpb, &sigma, NULL);

	/* 1BD: | NCY 11 */
	if (carry) {
		/* 1BE: sigma -> tmpb */
		*tmpb = sigma;

		/* 1BE: | CF1 */
		*neg_flag = !(*neg_flag);
	}
}
static void cor_negate8(uint8_t* tmpa, uint8_t* tmpb, uint8_t* tmpc, uint8_t carry, uint8_t* neg_flag) {
	uint8_t sigma;

	/* 1B5: | NCY 7 */
	if (carry) {
		/* 1B6: | NEG tmpc */
		cor_neg8(*tmpc, &sigma, &carry);

		/* 1B7: sigma -> tmpc */
		*tmpc = sigma;

		/* 1B7: | NOT tmpa */
		sigma = ~(*tmpa);

		/* 1B8: | CY 6 */
		if (!carry) {
			/* 1B9: | NEG tmpa */
			cor_neg8(*tmpa, &sigma, NULL);
		}

		/* 1BA: sigma -> tmpa */
		*tmpa = sigma;

		/* 1BA: | CF1 */
		*neg_flag = !(*neg_flag);
	}

	cor_negate8_ln7(tmpb, neg_flag);
}

static int cord8(I8086* cpu, uint8_t* tmpc, uint8_t* tmpa, uint8_t tmpb, uint8_t* out_cf) {
	uint8_t cf = 0;
	uint8_t tmp_cf = 0;
	uint8_t tmp_of = 0;
	uint8_t tmp_af = 0;
	uint8_t sigma = 0;
	int internal_counter = 0;

	/* 188: SUBT tmpa */
	cor_sub8(*tmpa, tmpb, &sigma, &tmp_cf, &tmp_of, &tmp_af);

	/* 189: MAXC */
	internal_counter = 8;

	/* 189: F */
	AF = tmp_af;
	CF = tmp_cf;
	OF = tmp_of;
	SET_SF8(sigma);
	SET_PF8(sigma);
	SET_ZF8(sigma);
	cf = tmp_cf;

	/* 18A: NCY INT0 */
	if (!cf) {
		/* division overflow */
		return -1;
	}

	while (internal_counter > 0) {
		/* 18B: | LRCY tmpc */
		cor_rcl8(*tmpc, &sigma, &cf);

		/* 18C: sigma -> tmpc */
		*tmpc = sigma;

		/* 18C: | LRCY tmpa */
		cor_rcl8(*tmpa, &sigma, &cf);

		/* 18D: sigma -> tmpa */
		*tmpa = sigma;

		/* 18D: | SUBT tmpa */
		cor_sub8(*tmpa, tmpb, &sigma, &tmp_cf, &tmp_of, &tmp_af);

		/* 18E: | CY 13 */
		if (cf) {
			/* 195: | RCY */
			cf = 0;

			/* 196: sigma -> tmpa */
			*tmpa = sigma;
		}
		else {
			/* 18F: F */
			AF = tmp_af;
			CF = tmp_cf;
			OF = tmp_of;
			SET_SF8(sigma);
			SET_PF8(sigma);
			SET_ZF8(sigma);
			cf = tmp_cf;

			/* 190: NCY 14 */
			if (!cf) {
				/* 196: sigma -> tmpa */
				*tmpa = sigma;
			}
		}

		/* 191:/196: | NCZ 3 */
		internal_counter -= 1;
	}

	/* 192: | LRCY tmpc */
	cor_rcl8(*tmpc, &sigma, &cf);

	/* 193: sigma -> tmpc */
	*tmpc = sigma;

	/* 194 */
	cor_rcl8(*tmpc, NULL, &cf);
	CF = cf;

	/* write back */
	if (out_cf) *out_cf = cf;

	return 0;
}
static void corx8(uint8_t tmpb, uint8_t carry, uint8_t* tmpa, uint8_t* tmpc) {
	uint8_t sigma = 0;
	int internal_counter = 0;

	/* 17F: ZERO -> tmpa */
	*tmpa = 0;

	/* 17F: RRCY tmpc */
	cor_rcr8(*tmpc, &sigma, &carry);

	/* 180: sigma -> tmpc */
	*tmpc = sigma;

	/* 180: | MAXC */
	internal_counter = 8;

	while (internal_counter > 0) {

		/* 181: | NCY 8 */
		if (carry) {
			/* 182: | ADD tmpa */
			cor_add8(*tmpa, tmpb, &sigma, &carry);

			/* 183: sigma -> tmpa */
			*tmpa = sigma;

			/* 183: | F ; We dont bother setting flags as they are overwritten after CORX. */
		}

		/* 184: | RRCY tmpa */
		cor_rcr8(*tmpa, &sigma, &carry);

		/* 185: sigma -> tmpa */
		*tmpa = sigma;

		/* 185: | RRCY tmpc */
		cor_rcr8(*tmpc, &sigma, &carry);

		/* 186: sigma -> tmpc */
		*tmpc = sigma;

		/* 186: | NCZ 5 */
		internal_counter -= 1;
	}

	/* 187: | RTN */
}

void mc_div8(I8086* cpu, uint16_t dividend, uint8_t divisor, uint8_t is_signed, uint8_t* out_quotient, uint8_t* out_remainder) {
	uint8_t tmpa = 0;
	uint8_t tmpc = 0;
	uint8_t tmpb = 0;
	uint8_t sigma = 0;

	uint8_t cf = 0;
	uint8_t f1 = (cpu->internal_flags & INTERNAL_FLAG_F1) >> 1;

	/* 160: X -> tmpa */
	tmpa = (dividend >> 8) & 0xFF;

	/* 161: A -> tmpc */
	tmpc = dividend & 0xFF;

	/* 161: | LRCY tmpa */
	cor_rcl8(tmpa, NULL, &cf);

	/* 162: M -> tmpb */
	tmpb = divisor;

	/* 162: | X0 PREIDIV */
	if (is_signed) {
		cor_negate8(&tmpa, &tmpb, &tmpc, cf, &f1);
	}

	/* 163: | UNC CORD */
	if (cord8(cpu, &tmpc, &tmpa, tmpb, &cf) != 0) {
		/* division overflow */
		dbg_print("CORD8: divide overflow\n");
		i8086_int(cpu, INT_DBZ);
		return;
	}

	/* 164: | NOT tmpc */
	sigma = ~tmpc;

	/* 165: X -> tmpb */
	tmpb = (dividend >> 8) & 0xFF;

	/* 165: | X0 POSTIDIV */
	if (is_signed) {

		/* 1C4: | NCY INT0 */
		if (!cf) {
			dbg_print("POSTIDIV8: divide overflow\n");
			i8086_int(cpu, INT_DBZ);
			return;
		}

		/* 1C5: | LRCY tmpb */
		cor_rcl8(tmpb, NULL, &cf);

		/* 1C6: | NEG tmpa */
		cor_neg8(tmpa, &sigma, NULL);

		if (cf) {
			/* 1C8: sigma -> tmpa */
			tmpa = sigma;
		}

		/* 1C9: INC tmpc */
		sigma = tmpc + 1;

		/* 1CA: F1 8 */
		if (!f1) {
			/* 1CB: NOT tmpc */
			sigma = ~tmpc;
		}

		/* 1CC: | CCOF RTN */
		CF = 0;
		OF = 0;
	}

	/* 166: sigma -> A */
	if (out_quotient) *out_quotient = sigma;

	/* 167: tmpa -> X */
	if (out_remainder) *out_remainder = tmpa;

	/* 167: | RNI */
}
void mc_mul8(I8086* cpu, uint8_t multiplicand, uint8_t multiplier, uint8_t is_signed, uint8_t* product_lo, uint8_t* product_hi) {
	uint8_t tmpa = 0;
	uint8_t tmpc = 0;
	uint8_t tmpb = 0;
	uint8_t sigma = 0;

	uint8_t cf = 0;
	uint8_t af = 0;
	uint8_t f1 = (cpu->internal_flags & INTERNAL_FLAG_F1) >> 1;

	/* 150: A -> tmpc */
	tmpc = multiplicand;

	/* 150: | LRCY tmpc */
	cf = (tmpc & 0x80) != 0;

	/* 151: M -> tmpb */
	tmpb = multiplier;

	/* 151: | X0 PREIMUL */
	if (is_signed) {
		/* 1C0: | NEG tmpc */
		cor_neg8(tmpc, &sigma, NULL);

		/* 1C1: | NCY 7 */
		if (cf) {
			/* 1C2: sigma -> tmpc */
			tmpc = sigma;

			/* 1C2: CF1 */
			f1 = !f1;
		}

		cor_negate8_ln7(&tmpb, &f1);
	}

	/* 152: | UNC CORX */
	corx8(tmpb, cf, &tmpa, &tmpc);

	/* 153: | F1 NEGATE */
	if (f1) {
		cor_negate8(&tmpa, &tmpb, &tmpc, 1, &f1);
	}

	/* 154: | X0 IMULCOF */
	if (is_signed) {
		/* 1CD: ZERO -> tmpb */
		tmpb = 0;

		/* 1CD: | LRCY tmpc */
		cf = (tmpc & 0x80) != 0;

		/* 1CE: | ADC tmpa */
		cor_adc8(tmpa, tmpb, &sigma, cf, NULL, NULL, &af);
	}
	else {
		/* 1D2: PASS tmpa */
		sigma = tmpa;
	}

	/* 1CF:/1D3: F */
	AF = af;
	SET_SF8(sigma);
	SET_PF8(sigma);
	SET_ZF8(sigma);

	/* 1D0: Z 8 */
	if (sigma != 0) {
		/* 1D1: | SCOF RTN */
		CF = 1;
		OF = 1;
	}
	else {
		CF = 0;
		OF = 0;
	}

	/* 155: tmpc -> A */
	*product_lo = tmpc;

	/* 157: tmpa -> X */
	*product_hi = tmpa;

	/* 157: | RNI */
}

/* 16bit MUL/DIV */

static void cor_rcl16(uint16_t operand, uint16_t* sigma, uint8_t* cf) {
	uint8_t tmp_cf = 0;
	if (cf) tmp_cf = *cf;

	uint8_t old_cf = tmp_cf;
	tmp_cf = (operand >> 15) & 1;
	if (cf) *cf = tmp_cf;
	operand = (operand << 1) | old_cf;
	if (sigma) *sigma = operand;
}
static void cor_sub16(uint16_t operand1, uint16_t operand2, uint16_t* sigma, uint8_t* cf, uint8_t* of, uint8_t* af) {
	uint32_t tmp = (operand1 - operand2);
	if (af) *af = ((operand1 ^ operand2 ^ tmp) & 0x10) != 0;
	if (of) *of = (((operand1 ^ operand2) & (operand1 ^ tmp)) & 0x8000) != 0;
	if (cf) *cf = (operand2) > (operand1);
	if (sigma) *sigma = (tmp & 0xFFFF);
}
static void cor_neg16(uint16_t operand, uint16_t* sigma, uint8_t* cf) {
	cor_sub16(0, operand, sigma, cf, NULL, NULL);
}

static void cor_rcr16(uint16_t operand, uint16_t* sigma, uint8_t* cf) {
	uint8_t carry = 0;
	if (cf) carry = *cf;

	uint8_t old_cf = carry;
	carry = (operand & 1);
	operand = (operand >> 1) | (old_cf << 15);
	if (cf) *cf = carry;
	if (sigma) *sigma = operand;
}
static void cor_add16(uint16_t operand1, uint16_t operand2, uint16_t* sigma, uint8_t* out_cf) {
	uint32_t tmp = ((uint32_t)operand1 + (uint32_t)operand2);
	if (out_cf) *out_cf = tmp > 0xFFFF;
	if (sigma) *sigma = (tmp & 0xFFFF);
}
static void cor_adc16(uint16_t operand1, uint16_t operand2, uint16_t* sigma, uint8_t in_cf, uint8_t* out_cf, uint8_t* out_of, uint8_t* out_af) {
	uint32_t tmp = ((uint32_t)operand1 + (uint32_t)operand2 + in_cf);
	if (out_af) *out_af = ((operand1 ^ operand2 ^ tmp) & 0x10) != 0;
	if (out_of) *out_of = (((tmp ^ operand1) & (tmp ^ operand2)) & 0x8000) != 0;
	if (out_cf) *out_cf = tmp > 0xFFFF;
	if (sigma) *sigma = (tmp & 0xFFFF);
}

static void cor_negate16_ln7(uint16_t* tmpb, uint8_t* neg_flag) {
	uint16_t sigma;
	uint8_t carry;

	/* 1BB: | LRCY tmpb */
	carry = ((*tmpb & 0x8000) != 0);

	/* 1BC: | NEG tmpb */
	cor_neg16(*tmpb, &sigma, NULL);

	/* 1BD: | NCY 11 */
	if (carry) {
		/* 1BE: sigma -> tmpb */
		*tmpb = sigma;

		/* 1BE: | CF1 */
		*neg_flag = !(*neg_flag);
	}
}
static void cor_negate16(uint16_t* tmpa, uint16_t* tmpb, uint16_t* tmpc, uint8_t carry, uint8_t* neg_flag) {
	uint16_t sigma;

	/* 1B5: | NCY 7 */
	if (carry) {
		/* 1B6: | NEG tmpc */
		cor_neg16(*tmpc, &sigma, &carry);

		/* 1B7: sigma -> tmpc */
		*tmpc = sigma;

		/* 1B7: | NOT tmpa */
		sigma = ~(*tmpa);

		/* 1B8: | CY 6 */
		if (!carry) {
			/* 1B9: | NEG tmpa */
			cor_neg16(*tmpa, &sigma, NULL);
		}

		/* 1BA: sigma -> tmpa */
		*tmpa = sigma;

		/* 1BA: | CF1 */
		*neg_flag = !(*neg_flag);
	}

	cor_negate16_ln7(tmpb, neg_flag);
}

static int cord16(I8086* cpu, uint16_t* tmpc, uint16_t* tmpa, uint16_t tmpb, uint8_t* out_cf) {
	uint8_t cf = 0;
	uint8_t tmp_cf = 0;
	uint8_t tmp_of = 0;
	uint8_t tmp_af = 0;
	uint16_t sigma = 0;
	int internal_counter = 0;

	/* 188: SUBT tmpa */
	cor_sub16(*tmpa, tmpb, &sigma, &tmp_cf, &tmp_of, &tmp_af);

	/* 189: MAXC */
	internal_counter = 16;

	/* 189: F */
	AF = tmp_af;
	CF = tmp_cf;
	OF = tmp_of;
	SET_SF16(sigma);
	SET_PF16(sigma);
	SET_ZF16(sigma);
	cf = tmp_cf;

	/* 18A: NCY INT0 */
	if (!cf) {
		/* division overflow */
		return -1;
	}

	while (internal_counter > 0) {
		/* 18B: | LRCY tmpc */
		cor_rcl16(*tmpc, &sigma, &cf);

		/* 18C: sigma -> tmpc */
		*tmpc = sigma;

		/* 18C: | LRCY tmpa */
		cor_rcl16(*tmpa, &sigma, &cf);

		/* 18D: sigma -> tmpa */
		*tmpa = sigma;

		/* 18D: | SUBT tmpa */
		cor_sub16(*tmpa, tmpb, &sigma, &tmp_cf, &tmp_of, &tmp_af);

		/* 18E: | CY 13 */
		if (cf) {
			/* 195: | RCY */
			cf = 0;

			/* 196: sigma -> tmpa */
			*tmpa = sigma;
		}
		else {
			/* 18F: F */
			AF = tmp_af;
			CF = tmp_cf;
			OF = tmp_of;
			SET_SF16(sigma);
			SET_PF16(sigma);
			SET_ZF16(sigma);
			cf = tmp_cf;

			/* 190: NCY 14 */
			if (!cf) {
				/* 196: sigma -> tmpa */
				*tmpa = sigma;
			}
		}

		/* 191:/196: | NCZ 3 */
		internal_counter -= 1;
	}

	/* 192: | LRCY tmpc */
	cor_rcl16(*tmpc, &sigma, &cf);

	/* 193: sigma -> tmpc */
	*tmpc = sigma;

	/* 194 */
	cor_rcl16(*tmpc, NULL, &cf);
	CF = cf;

	/* write back */
	if (out_cf) *out_cf = cf;

	return 0;
}
static void corx16(uint16_t tmpb, uint8_t carry, uint16_t* tmpa, uint16_t* tmpc) {
	uint16_t sigma = 0;
	int internal_counter = 0;

	/* 17F: ZERO -> tmpa */
	*tmpa = 0;

	/* 17F: RRCY tmpc */
	cor_rcr16(*tmpc, &sigma, &carry);

	/* 180: sigma -> tmpc */
	*tmpc = sigma;

	/* 180: | MAXC */
	internal_counter = 16;

	while (internal_counter > 0) {

		/* 181: | NCY 8 */
		if (carry) {
			/* 182: | ADD tmpa */
			cor_add16(*tmpa, tmpb, &sigma, &carry);

			/* 183: sigma -> tmpa */
			*tmpa = sigma;

			/* 183: | F ; We dont bother setting flags as they are overwritten after CORX. */
		}

		/* 184: | RRCY tmpa */
		cor_rcr16(*tmpa, &sigma, &carry);

		/* 185: sigma -> tmpa */
		*tmpa = sigma;

		/* 185: | RRCY tmpc */
		cor_rcr16(*tmpc, &sigma, &carry);

		/* 186: sigma -> tmpc */
		*tmpc = sigma;

		/* 186: | NCZ 5 */
		internal_counter -= 1;
	}

	/* 187: | RTN */
}

void mc_div16(I8086* cpu, uint32_t dividend, uint16_t divisor, uint8_t is_signed, uint16_t* out_quotient, uint16_t* out_remainder) {
	uint16_t tmpa = 0;
	uint16_t tmpc = 0;
	uint16_t tmpb = 0;
	uint16_t sigma = 0;

	uint8_t cf = 0;
	uint8_t f1 = (cpu->internal_flags & INTERNAL_FLAG_F1) >> 1;

	/* 168: DE -> tmpa */
	tmpa = (dividend >> 16) & 0xFFFF;

	/* 169: XA -> tmpc */
	tmpc = dividend & 0xFFFF;

	/* 169: | LRCY tmpa */
	cor_rcl16(tmpa, NULL, &cf);

	/* 16A: M -> tmpb */
	tmpb = divisor;

	/* 16A: | X0 PREIDIV */
	if (is_signed) {
		cor_negate16(&tmpa, &tmpb, &tmpc, cf, &f1);
	}

	/* 16B: | UNC CORD */
	if (cord16(cpu, &tmpc, &tmpa, tmpb, &cf) != 0) {
		/* division overflow */
		dbg_print("CORD16: divide overflow\n");
		i8086_int(cpu, INT_DBZ);
		return;
	}

	/* 16C: | NOT tmpc */
	sigma = ~tmpc;

	/* 16D: DE -> tmpb */
	tmpb = (dividend >> 16) & 0xFFFF;

	/* 16D: | X0 POSTIDIV */
	if (is_signed) {

		/* 1C4: | NCY INT0 */
		if (!cf) {
			dbg_print("POSTIDIV16: divide overflow\n");
			i8086_int(cpu, INT_DBZ);
			return;
		}

		/* 1C5: | LRCY tmpb */
		cor_rcl16(tmpb, NULL, &cf);

		/* 1C6: | NEG tmpa */
		cor_neg16(tmpa, &sigma, NULL);

		if (cf) {
			/* 1C8: sigma -> tmpa */
			tmpa = sigma;
		}

		/* 1C9: INC tmpc */
		sigma = tmpc + 1;

		/* 1CA: F1 8 */
		if (!f1) {
			/* 1CB: NOT tmpc */
			sigma = ~tmpc;
		}

		/* 1CC: | CCOF RTN */
		CF = 0;
		OF = 0;
	}

	/* 16E: sigma -> XA */
	if (out_quotient) *out_quotient = sigma;

	/* 16F: tmpa -> DE */
	if (out_remainder) *out_remainder = tmpa;

	/* 16F: | RNI */
}
void mc_mul16(I8086* cpu, uint16_t multiplicand, uint16_t multiplier, uint8_t is_signed, uint16_t* product_lo, uint16_t* product_hi) {
	uint16_t tmpa = 0;
	uint16_t tmpc = 0;
	uint16_t tmpb = 0;
	uint16_t sigma = 0;

	uint8_t cf = 0;
	uint8_t af = 0;
	uint8_t f1 = (cpu->internal_flags & INTERNAL_FLAG_F1) >> 1;

	/* 158: XA -> tmpc */
	tmpc = multiplicand;

	/* 158: | LRCY tmpc */
	cf = (tmpc & 0x8000) != 0;

	/* 159: M -> tmpb */
	tmpb = multiplier;

	/* 159: | X0 PREIMUL */
	if (is_signed) {
		/* 1C0: | NEG tmpc */
		cor_neg16(tmpc, &sigma, NULL);

		/* 1C1: | NCY 7 */
		if (cf) {
			/* 1C2: sigma -> tmpc */
			tmpc = sigma;

			/* 1C2: CF1 */
			f1 = !f1;
		}

		cor_negate16_ln7(&tmpb, &f1);
	}

	/* 15A: | UNC CORX */
	corx16(tmpb, cf, &tmpa, &tmpc);

	/* 15B: | F1 NEGATE */
	if (f1) {
		cor_negate16(&tmpa, &tmpb, &tmpc, 1, &f1);
	}

	/* 15C: | X0 IMULCOF */
	if (is_signed) {
		/* 1CD: ZERO -> tmpb */
		tmpb = 0;

		/* 1CD: | LRCY tmpc */
		cf = (tmpc & 0x8000) != 0;

		/* 1CE: | ADC tmpa */
		cor_adc16(tmpa, tmpb, &sigma, cf, NULL, NULL, &af);
	}
	else {
		/* 1D2: PASS tmpa */
		sigma = tmpa;
	}

	/* 1CF:/1D3: F */
	AF = af;
	SET_SF16(sigma);
	SET_PF16(sigma);
	SET_ZF16(sigma);

	/* 1D0: Z 8 */
	if (sigma != 0) {
		/* 1D1: | SCOF RTN */
		CF = 1;
		OF = 1;
	}
	else {
		CF = 0;
		OF = 0;
	}

	/* 15D: tmpc -> XA */
	*product_lo = tmpc;

	/* 15F: tmpa -> DE */
	*product_hi = tmpa;

	/* 15F: | RNI */
}

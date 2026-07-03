/* I80286.h
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 CPU
 */

#ifndef I80286_H
#define I80286_H

#include <stdint.h>

#define REG_AL 0
#define REG_CL 1
#define REG_DL 2
#define REG_BL 3
#define REG_AH 4
#define REG_CH 5
#define REG_DH 6
#define REG_BH 7

#define REG_AX 0
#define REG_CX 1
#define REG_DX 2
#define REG_BX 3
#define REG_SP 4
#define REG_BP 5
#define REG_SI 6
#define REG_DI 7

#define SEG_ES 0
#define SEG_CS 1
#define SEG_SS 2
#define SEG_DS 3

#define I80286_REGISTER_COUNT 8
#define I80286_SEGMENT_COUNT  4

#define I80286_DECODE_OK        0 /* instruction was decoded */
#define I80286_DECODE_REQ_CYCLE 1 /* prefix bytes and string operations require multiple decode cycles */
#define I80286_DECODE_UNDEFINED 2 /* undefined instruction */

/* 24bit address */
typedef uint32_t uint24_t;
typedef int32_t int24_t;

/* 16bit Program Status Word */
typedef struct I80286_PROGRAM_STATUS_WORD {
	union {
		uint16_t word;
		struct {
			uint8_t cf : 1; /* carry flag */
			uint8_t r0 : 1; /* rev 0 */
			uint8_t pf : 1; /* parity flag */
			uint8_t r1 : 1; /* rev 1 */
			uint8_t af : 1; /* aux carry flag */
			uint8_t r2 : 1; /* rev 2 */
			uint8_t zf : 1; /* zero flag */
			uint8_t sf : 1; /* sign flag */
			uint8_t tf : 1; /* trap flag */
			uint8_t in : 1; /* interrupt flag */
			uint8_t df : 1; /* direction flag */
			uint8_t of : 1; /* overflow flag */
			uint8_t pl : 1; /* iopl */
			uint8_t io : 1; /* iopl */
			uint8_t nt : 1; /* nt */
			uint8_t r4 : 1; /* rev 4 */
		} bits;
	} u;
} I80286_PROGRAM_STATUS_WORD;

/* 16bit Machine Status Word */
typedef struct I80286_MACHINE_STATUS_WORD {
	union {
		uint16_t word;
		struct {
			uint8_t pe  : 1; /* Protected mode */
			uint8_t mp  : 1; /* Monitor processor extention */
			uint8_t em  : 1; /* Emulate processor extention */
			uint8_t ts  : 1; /* Task switched */
			uint8_t r0  : 1; /* rev 0 */
			uint8_t r1  : 1; /* rev 1 */
			uint8_t r2  : 1; /* rev 2 */
			uint8_t r3  : 1; /* rev 3 */
			uint8_t r4  : 1; /* rev 4 */
			uint8_t r5  : 1; /* rev 5 */
			uint8_t r6  : 1; /* rev 6 */
			uint8_t r7  : 1; /* rev 7 */
			uint8_t r8  : 1; /* rev 8 */
			uint8_t r9  : 1; /* rev 9 */
			uint8_t r10 : 1; /* rev 10 */
			uint8_t r11 : 1; /* rev 11 */
		} bits;
	} u;
} I80286_MACHINE_STATUS_WORD;

/* 16bit register */
typedef struct I80286_REG16 {
	union {
		uint16_t r16;
		struct {
			uint8_t l;
			uint8_t h;
		} r8;
	} u;
} I80286_REG16;

/* 8bit Mod R/M byte */
typedef struct I80286_MOD_RM {
	union {
		uint8_t byte;
		struct {
			uint8_t rm  : 3; /* r/m */
			uint8_t reg : 3; /* register */
			uint8_t mod : 2; /* mode */
		} bits;
	} u;
} I80286_MOD_RM;

typedef struct I80286_DESCRIPTOR {
	uint16_t limit; /* limit */
	uint24_t base;  /* base */
	uint8_t access; /* access byte */
} I80286_DESCRIPTOR;

typedef struct I80286_SEGMENT_DESCRIPTOR {
	uint16_t selector;            /* 16bit visible selector */
	I80286_DESCRIPTOR descriptor; /* 48bit hidden descriptor */
} I80286_SEGMENT_DESCRIPTOR;

/* I80286 Function pointers */
typedef struct I80286_FUNCS {
	uint8_t(*read_mem_byte)(uint24_t);         /* read mem byte */
	uint8_t(*read_io_byte)(uint16_t);          /* read io byte */
	void(*write_mem_byte)(uint24_t, uint8_t);  /* write mem byte */
	void(*write_io_byte)(uint16_t, uint8_t);   /* write io byte */
} I80286_FUNCS;

#define INTERNAL_FLAG_F1Z 0x01
#define INTERNAL_FLAG_F1  0x02

/* I80286 CPU State */
typedef struct I80286 {
	I80286_REG16 registers[I80286_REGISTER_COUNT];                 /* general registers */
	I80286_SEGMENT_DESCRIPTOR segments[I80286_SEGMENT_COUNT];      /* segment registers */
	I80286_DESCRIPTOR idtr;
	I80286_DESCRIPTOR gdtr;
	I80286_SEGMENT_DESCRIPTOR ldt;
	I80286_DESCRIPTOR tss;
	uint16_t tr;
	I80286_PROGRAM_STATUS_WORD psw;                                /* program status word */
	I80286_MACHINE_STATUS_WORD msw;                                /* machine status word */
	uint16_t ip;                                                   /* instruction pointer */
	uint8_t opcode;                                                /* current opcode */
	I80286_MOD_RM modrm;                                           /* current mod r/m byte (if applicable) */
	uint8_t segment_prefix;                                        /* current segment override prefix byte (if applicable) */
	uint8_t lock_prefix;                                           /* lock prefix byte (if applicable) */
	uint8_t internal_flags;                                        /* cpu internal flags */
	uint8_t tf_latch;                                              /* trap latch */
	uint8_t int_latch;                                             /* interrupt latch */
	uint8_t int_delay;                                             /* interrupt delay */
	uint8_t nmi;                                                   /* NMI pin */
	uint8_t intr;                                                  /* INTR pin */
	uint8_t intr_type;                                             /* Hardware interrupt type. 0-255 (INTR) */
	uint8_t instruction_len;                                       /* Instruction length */
	uint8_t halt;                                                  /* halt state */
	uint8_t cpl;                                                   /* current privilege level */
	uint64_t cycles;
	I80286_FUNCS funcs;                                            /* cpu memory function pointers */
} I80286;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the CPU. Sets all function pointers to NULL
	cpu: the cpu instance */
void i80286_init(I80286* cpu);

/* Reset The CPU to it's reset state.
	cpu: the cpu instance */
void i80286_reset(I80286* cpu);

/* Fetch, Execute the next instruction
	cpu: the cpu instance */
int i80286_execute(I80286* cpu);

/* request hardware interrupt
	cpu:  the cpu instance
	type: the interrupt number 0-0xFF */
void i80286_intr(I80286* cpu, uint8_t type);

/* request non maskable interrupt
	cpu:  the cpu instance */
void i80286_nmi(I80286* cpu);

uint24_t i80286_get_physical_address(uint16_t segment, uint16_t address);

#ifdef __cplusplus
};
#endif
#endif

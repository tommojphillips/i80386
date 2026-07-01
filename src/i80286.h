/* I8086.h
 * Thomas J. Armytage 2025 ( https://github.com/tommojphillips/ )
 * Intel 8086 CPU
 */

#ifndef I8086_H
#define I8086_H

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

#define I8086_REGISTER_COUNT 8
#define I8086_SEGMENT_COUNT  4

#define I8086_DECODE_OK        0 /* instruction was decoded */
#define I8086_DECODE_REQ_CYCLE 1 /* prefix bytes and string operations require multiple decode cycles */
#define I8086_DECODE_UNDEFINED 2 /* undefined instruction */

//#define I8086_ENABLE_INTERRUPT_HOOKS

/* 20bit address */
typedef uint32_t uint20_t;
typedef int32_t int20_t;

#pragma warning( push )
/* ignore unnamed_structure warning */
#pragma warning( disable : 4201)

/* 16bit Program Status Word */
typedef struct I8086_PROGRAM_STATUS_WORD {
	union {
		uint16_t word;
		struct {
			uint8_t cf : 1; // carry flag
			uint8_t r0 : 1; // rev 0
			uint8_t pf : 1; // parity flag
			uint8_t r1 : 1; // rev 1
			uint8_t af : 1; // aux carry flag
			uint8_t r2 : 1; // rev 2
			uint8_t zf : 1; // zero flag
			uint8_t sf : 1; // sign flag
			uint8_t tf : 1; // trap flag
			uint8_t in : 1; // interrupt flag
			uint8_t df : 1; // direction flag
			uint8_t of : 1; // overflow flag
			uint8_t r3 : 1; // rev 3
			uint8_t r4 : 1; // rev 4
			uint8_t r5 : 1; // rev 5
			uint8_t r6 : 1; // rev 6
		};
	};
} I8086_PROGRAM_STATUS_WORD;

/* 16bit register */
typedef struct I8086_REG16 {
	union {
		uint16_t r16;
		struct {
			uint8_t l;
			uint8_t h;
		};
	};
} I8086_REG16;

/* 8bit Mod R/M byte */
typedef struct I8086_MOD_RM {
	union {
		uint8_t byte;
		struct {
			uint8_t rm  : 3; // r/m
			uint8_t reg : 3; // register
			uint8_t mod : 2; // mode
		};
	};
} I8086_MOD_RM;

#pragma warning( pop ) 

/* I8086 Function pointers */
typedef struct I8086_FUNCS {
	uint8_t(*read_mem_byte)(uint20_t);  // read mem byte
	uint8_t(*read_io_byte)(uint16_t);   // read io byte

	void(*write_mem_byte)(uint20_t, uint8_t);  // write mem byte
	void(*write_io_byte)(uint16_t, uint8_t);   // write io byte

} I8086_FUNCS;

#ifdef I8086_ENABLE_INTERRUPT_HOOKS
typedef struct I8086 I8086;
/* I8086 Hook
 cpu: the cpu instance that invoked this hook
 return: 1 if the hook has handled the interrupt.
         0 if the cpu should handle the interrupt. */
typedef int (*I8086_INT_CB)(I8086* cpu);

#define I8086_MAX_CB 1
typedef struct {
	uint8_t type;
	I8086_INT_CB cb;
} I8086_INT_CB_ENTRY;
#endif

#define INTERNAL_FLAG_F1Z 0x01
#define INTERNAL_FLAG_F1  0x02

/* I8086 CPU State */
typedef struct I8086 {
	I8086_REG16 registers[I8086_REGISTER_COUNT]; // general registers
	uint16_t segments[I8086_SEGMENT_COUNT];      // segment registers
	I8086_PROGRAM_STATUS_WORD status;            // program status word	
	uint16_t ip;                                 // instruction pointer
	uint8_t opcode;                              // current opcode
	I8086_MOD_RM modrm;                          // current mod r/m byte (if applicable)
	uint8_t segment_prefix;                      // current segment override prefix byte (if applicable)
	uint8_t internal_flags;                      // cpu internal flags
	uint8_t tf_latch;                            // trap latch
	uint8_t int_latch;                           // interrupt latch
	uint8_t int_delay;                           // interrupt delay	
	uint8_t nmi;                                 // NMI pin
	uint8_t intr;                                // INTR pin
	uint8_t intr_type;                           // Hardware interrupt type. 0-255 (INTR)
	uint8_t instruction_len;
	uint16_t ea_offset;
	uint16_t ea_segment;
	uint64_t cycles;

	I8086_FUNCS funcs;                           // cpu memory function pointers

#ifdef I8086_ENABLE_INTERRUPT_HOOKS
	I8086_INT_CB_ENTRY int_cb[I8086_MAX_CB];
	uint8_t int_cb_count;
#endif
} I8086;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the CPU. Sets all function pointers to NULL
	cpu: the cpu instance */
void i8086_init(I8086* cpu);

/* Reset The CPU to it's reset state.
	cpu: the cpu instance */
void i8086_reset(I8086* cpu);

/* Fetch, Execute the next instruction
	cpu: the cpu instance */
int i8086_execute(I8086* cpu);

/* request hardware interrupt
	cpu:  the cpu instance
	type: the interrupt number 0-0xFF */
void i8086_intr(I8086* cpu, uint8_t type);

/* request non maskable interrupt
	cpu:  the cpu instance */
void i8086_nmi(I8086* cpu);

uint20_t i8086_get_physical_address(uint16_t segment, uint16_t address);

#ifdef I8086_ENABLE_INTERRUPT_HOOKS
/* setup an interrupt callback on type 
	cpu: the cpu instance
	cb: the interrupt callback function
	type: the interrupt type to hook */
void i8086_set_interrupt_cb(I8086* cpu, I8086_INT_CB cb, uint8_t type);

/* remove an interrupt callback on type
	cpu: the cpu instance
	type: the interrupt type to unhook */
void i8086_remove_interrupt_cb(I8086* cpu, uint8_t type);

/* find an interrupt callback on type
	cpu: the cpu instance
	type: the interrupt type to find */
I8086_INT_CB i8086_find_interrupt_cb(I8086* cpu, uint8_t type);
#else
/* HOOKS NOT ENABLED */
#define i8086_set_interrupt_cb(cpu, hook, type)
/* HOOKS NOT ENABLED */
#define i8086_remove_interrupt_cb(cpu, type)
/* HOOKS NOT ENABLED */
#define i8086_find_interrupt_cb(cpu, type)
#endif

#ifdef __cplusplus
};
#endif
#endif

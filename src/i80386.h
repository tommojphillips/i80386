/* I80386.h
 * Thomas J. Armytage 2025-2026 ( https://github.com/tommojphillips/ )
 * Intel 80386 CPU (Intel 80386EX KU80C386EX33)
 */

#ifndef I80386_H
#define I80386_H

/* Enable undefined SIB byte behaviour on i80386EX. 
	SIB is undefined when scale!=0b00 && index==0b100.
	Base register incorrectly gets multiplied by 1, 2, 4 or 8. */
#define _386_SIB_UNDEFINED_

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

#define REG_EAX 0
#define REG_ECX 1
#define REG_EDX 2
#define REG_EBX 3
#define REG_ESP 4
#define REG_EBP 5
#define REG_ESI 6
#define REG_EDI 7

#define REG_CR0 0
#define REG_CR1 1
#define REG_CR2 2
#define REG_CR3 3

#define REG_DR0 0
#define REG_DR1 1
#define REG_DR2 2
#define REG_DR3 3
#define REG_DR4 4
#define REG_DR5 5
#define REG_DR6 6
#define REG_DR7 7

#define REG_TR6 0
#define REG_TR7 1

#define SEG_ES 0
#define SEG_CS 1
#define SEG_SS 2
#define SEG_DS 3
#define SEG_FS 4
#define SEG_GS 5
#define SEG_LDT 6
#define SEG_TR 7

#define I80386_REGISTER_COUNT         8
#define I80386_SEGMENT_COUNT          6
#define I80386_DEBUG_REGISTER_COUNT   8
#define I80386_TEST_REGISTER_COUNT    8
#define I80386_CONTROL_REGISTER_COUNT 8

#define I80386_DECODE_OK           0 /* instruction was decoded */
#define I80386_DECODE_REQ_CYCLE    1 /* prefix bytes and string operations require multiple decode cycles */
#define I80386_DECODE_UNDEFINED    2 /* undefined instruction */
#define I80386_DECODE_TRIPLE_FAULT 3 /* cpu has triple faulted */

#pragma warning(push)
#pragma warning(disable : 4201)  /* C4201 - nameless struct */

/* 16bit Program Status Word */
typedef struct I80386_PROGRAM_STATUS_WORD {
	union {
		uint16_t word;
		struct {
			uint8_t cf   : 1; /* carry flag */
			uint8_t r0   : 1; /* rev 0 */
			uint8_t pf   : 1; /* parity flag */
			uint8_t r1   : 1; /* rev 1 */
			uint8_t af   : 1; /* aux carry flag */
			uint8_t r2   : 1; /* rev 2 */
			uint8_t zf   : 1; /* zero flag */
			uint8_t sf   : 1; /* sign flag */
			uint8_t tf   : 1; /* trap flag */
			uint8_t in   : 1; /* interrupt flag */
			uint8_t df   : 1; /* direction flag */
			uint8_t of   : 1; /* overflow flag */
			uint8_t iopl : 2; /* i/o privilege level */
			uint8_t nt   : 1; /* nested task */
			uint8_t r3   : 1; /* rev 3 */			
		};
	};
} I80386_PROGRAM_STATUS_WORD;

/* 32bit EFlags */
typedef struct I80386_EFLAGS {
	union {
		uint32_t dword;
		struct {
			I80386_PROGRAM_STATUS_WORD psw;
			uint8_t rf : 1; /* resume flag */
			uint8_t vm : 1; /* virtual 8086 mode */
		};
	};
} I80386_EFLAGS;

/* 16bit Machine Status Word */
typedef struct I80386_MACHINE_STATUS_WORD {
	union {
		uint16_t word;
		struct I80386_MACHINE_STATUS_BITS {
			uint8_t pe  : 1; /* Protected mode */
			uint8_t mp  : 1; /* Monitor coprocessor extention */
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
		};
	};
} I80386_MACHINE_STATUS_WORD;

/* Control Register 0 */
typedef struct I80386_CR0 {
	union {
		uint32_t dword;
		struct {
			I80386_MACHINE_STATUS_WORD msw;
			uint8_t r16 : 1; /* rev 16 */
			uint8_t r17 : 1; /* rev 17 */
			uint8_t r18 : 1; /* rev 18 */
			uint8_t r19 : 1; /* rev 19 */
			uint8_t r20 : 1; /* rev 20 */
			uint8_t r21 : 1; /* rev 21 */
			uint8_t r22 : 1; /* rev 22 */
			uint8_t r32 : 1; /* rev 23 */
			uint8_t r24 : 1; /* rev 24 */
			uint8_t r25 : 1; /* rev 25 */
			uint8_t r26 : 1; /* rev 26 */
			uint8_t r27 : 1; /* rev 27 */
			uint8_t r28 : 1; /* rev 28 */
			uint8_t r29 : 1; /* rev 29 */
			uint8_t r30 : 1; /* rev 30 */
			uint8_t pg  : 1; /* PG - paging enable */
		};
	};
} I80386_CR0;

/* Debug Register 6 */
typedef struct I80386_DR6 {
	union {
		uint32_t dword;
		struct {
			uint8_t bp0 : 1; /* breakpoint 00 */
			uint8_t bp1 : 1; /* breakpoint 01 */
			uint8_t bp2 : 1; /* breakpoint 02 */
			uint8_t bp3 : 1; /* breakpoint 03 */
			uint8_t r04 : 1; /* rev 04 */
			uint8_t r05 : 1; /* rev 05 */
			uint8_t r06 : 1; /* rev 06 */
			uint8_t r07 : 1; /* rev 07 */
			uint8_t r08 : 1; /* rev 08 */
			uint8_t r09 : 1; /* rev 09 */
			uint8_t r10 : 1; /* rev 10 */
			uint8_t r11 : 1; /* rev 11 */
			uint8_t r12 : 1; /* rev 12 */
			uint8_t d : 1; /* d */
			uint8_t t : 1; /* s */
			uint8_t r : 1; /* t */
			uint8_t r16 : 1; /* rev 16 */
			uint8_t r17 : 1; /* rev 17 */
			uint8_t r18 : 1; /* rev 18 */
			uint8_t r19 : 1; /* rev 19 */
			uint8_t r20 : 1; /* rev 20 */
			uint8_t r21 : 1; /* rev 21 */
			uint8_t r22 : 1; /* rev 22 */
			uint8_t r23 : 1; /* rev 23 */
			uint8_t r24 : 1; /* rev 24 */
			uint8_t r25 : 1; /* rev 25 */
			uint8_t r26 : 1; /* rev 26 */
			uint8_t r27 : 1; /* rev 27 */
			uint8_t r28 : 1; /* rev 28 */
			uint8_t r29 : 1; /* rev 29 */
			uint8_t r30 : 1; /* rev 30 */
			uint8_t r31 : 1; /* rev 31 */
		};
	};
} I80386_DR6;

#define DR7_BITS_RW_BREAK_ON_INSTRUCTION_EXECUTION b00 /* break on instruction execution only */
#define DR7_BITS_RW_BREAK_ON_DATA_WRITES           b01 /* break on data writes only */
#define DR7_BITS_RW_UNDEFINED                      b10
#define DR7_BITS_RW_BREAK_ON_DATA_READS_AND_WRITES b11 /* break on data reads/writes only. NOT instruction fetches */

#define DR7_BITS_LEN_1BYTE     b00 /* 1byte length */
#define DR7_BITS_LEN_2BYTE     b01 /* 2byte length */
#define DR7_BITS_LEN_UNDEFINED b10
#define DR7_BITS_LEN_4BYTE     b11 /* 4byte length */

/* Debug Register 7 */
typedef struct I80386_DR7 {
	union {
		uint32_t dword;
		struct {
			uint8_t l0 : 1; /* local0 */
			uint8_t g0 : 1; /* global0 */
			uint8_t l1 : 1; /* local1 */
			uint8_t g1 : 1; /* global1 */
			uint8_t l2 : 1; /* local2 */
			uint8_t g2 : 1; /* global2 */
			uint8_t l3 : 1; /* local3 */
			uint8_t g3 : 1; /* global3 */
			uint8_t le : 1; /* locale */
			uint8_t ge : 1; /* globale */
			uint8_t r10 : 1; /* rev 10 */
			uint8_t r11 : 1; /* rev 11 */
			uint8_t r12 : 1; /* rev 12 */
			uint8_t r13 : 1; /* rev 13 */
			uint8_t r14 : 1; /* rev 14 */
			uint8_t r15 : 1; /* rev 15 */
			uint8_t rw0 : 2; /* rw0 */
			uint8_t len0 : 2; /* len0 */
			uint8_t rw1 : 2; /* rw1 */
			uint8_t len1 : 2; /* len1 */
			uint8_t rw2 : 2; /* rw2 */
			uint8_t len2 : 2; /* len2 */
			uint8_t rw3 : 2; /* rw3 */
			uint8_t len3 : 2; /* len3 */
		};
	};
} I80386_DR7;

typedef enum I80386_TASK_SWITCH_REASON {
	TASK_SWITCH_CALL,
	TASK_SWITCH_JMP,
	TASK_SWITCH_INT,
	TASK_SWITCH_IRET
} I80386_TASK_SWITCH_REASON;

typedef struct I80386_TASK_STATE_SEGMENT {
	union {
		uint32_t values[26];
		struct {
			uint32_t back_link;
			uint32_t esp0;
			uint16_t ss0;
			uint16_t r0;
			uint32_t esp1;
			uint16_t ss1;
			uint16_t r1;
			uint32_t esp2;
			uint16_t ss2;
			uint16_t r2;
			uint32_t cr3;
			uint32_t eip;
			uint32_t eflags;
			uint32_t eax;
			uint32_t ecx;
			uint32_t edx;
			uint32_t ebx;
			uint32_t esp;
			uint32_t ebp;
			uint32_t esi;
			uint32_t edi;
			uint16_t es;
			uint16_t r3;
			uint16_t cs;
			uint16_t r4;
			uint16_t ss;
			uint16_t r5;
			uint16_t ds;
			uint16_t r6;
			uint16_t fs;
			uint16_t r7;
			uint16_t gs;
			uint16_t r8;
			uint16_t ldt;
			uint16_t r9;
			uint16_t t : 1;
			uint16_t io_map_base;
		};
	};
} I80386_TASK_STATE_SEGMENT;

/* 32bit register */
typedef struct I80386_REG32 {
	union {
		uint32_t r32;
		uint16_t r16;
		struct {
			uint8_t l;
			uint8_t h;
		} r8;
	};
} I80386_REG32;

/* 8bit Mod R/M byte */
typedef struct I80386_MOD_RM {
	union {
		uint8_t byte;
		struct {
			uint8_t rm  : 3; /* r/m */
			uint8_t reg : 3; /* register */
			uint8_t mod : 2; /* mode */
		};
	};
} I80386_MOD_RM;

/* 8bit SIB byte */
typedef struct I80386_SIB {
	union {
		uint8_t byte;
		struct {
			uint8_t base  : 3;
			uint8_t index : 3;
			uint8_t scale : 2;
		};
	};
} I80386_SIB;

#pragma pack(push, 1)
typedef struct I80386_DESCRIPTOR_ACCESS_RIGHTS {
	union {
		uint16_t word;
		union {
			struct {
				uint8_t type         : 4; /* type */				
				uint8_t s            : 1; /* 0 = system segment; 1 = code/data segment */	
				uint8_t dpl          : 2; /* descriptor privilege level */
				uint8_t present      : 1; /* present bit */
			uint8_t limit_hi     : 4; /* limit. bits 16-19 */
			uint8_t available    : 1; /* available for programmers use */
			uint8_t zero         : 1;
			uint8_t default_size : 1; /* only applies to code segment */
			uint8_t granularity  : 1; /* granularity bit. Specifies the units in which the limit field is interpreted. When the bit is clear, the
			                             limit is interpreted in 1-byte units; when set, the limit is interpreted in 4-kilobyte units. */
		};
		struct {
				uint8_t accessed : 1; /* accessed bit */
				uint8_t rw       : 1; /* readable/writable */
				uint8_t dc       : 1; /* expand-down/comforming */
				uint8_t e        : 1; /* 0 = data segment; 1 = code segment */
				uint8_t b4       : 1;
				uint8_t b5       : 1;
				uint8_t b6       : 1;
				uint8_t b7       : 1;

				uint8_t b8       : 1;
				uint8_t b9       : 1;
				uint8_t b10      : 1;
				uint8_t b11      : 1;
				uint8_t b12      : 1;
				uint8_t b13      : 1;
				uint8_t big      : 1; /* only applies to data segment */
				uint8_t b15      : 1;
			};
		};
	};
} I80386_DESCRIPTOR_ACCESS_RIGHTS;
#pragma pack(pop)

typedef struct I80386_DESCRIPTOR_TABLE_ENTRY {
	union {
		uint64_t qword;
		uint8_t bytes[8];
		struct {
			uint16_t limit_lo; /* segment limit. bits 0-15 */
			uint16_t base_lo;  /* segment base. bits 0-15 */
			uint8_t base_mi;   /* segment base. bits 16-23 */
			I80386_DESCRIPTOR_ACCESS_RIGHTS ar;
			uint8_t base_hi;   /* segment base. bits 24-31 */
		};
	};
} I80386_DESCRIPTOR_TABLE_ENTRY;

typedef struct I80386_DESCRIPTOR_TABLE_REGISTER {
	uint32_t base;  /* base */
	uint32_t limit; /* limit */
} I80386_DESCRIPTOR_TABLE_REGISTER;

typedef struct I80386_DESCRIPTOR_CACHE {	
	I80386_DESCRIPTOR_ACCESS_RIGHTS ar;
	uint32_t base;                  /* base */
	uint32_t limit;                 /* limit */
} I80386_DESCRIPTOR_CACHE;

typedef struct I80386_SEGMENT_REGISTER {
	uint16_t selector;            /* visible selector */		
	I80386_DESCRIPTOR_CACHE desc; /* invisible portion */
} I80386_SEGMENT_REGISTER;

typedef uint8_t(*I80386_READ_MEMORY_BYTE)(void* user_param, uint32_t address);
typedef uint8_t(*I80386_READ_IO_BYTE)(void* user_param, uint16_t address);
typedef void(*I80386_WRITE_MEMORY_BYTE)(void* user_param, uint32_t address, uint8_t value);
typedef void(*I80386_WRITE_IO_BYTE)(void* user_param, uint16_t address, uint8_t value);

typedef int(*I80386_FETCH_BYTE)(void* user_param, uint8_t* value);
typedef int(*I80386_FETCH_WORD)(void* user_param, uint16_t* value);
typedef int(*I80386_FETCH_DWORD)(void* user_param, uint32_t* value);

/* I80386 Function pointers */
typedef struct I80386_FUNCS {
	I80386_READ_MEMORY_BYTE exe_mem_byte;    /* fetch opcode byte */
	I80386_READ_MEMORY_BYTE read_mem_byte;   /* read mem byte */
	I80386_WRITE_MEMORY_BYTE write_mem_byte; /* write mem byte */
	I80386_READ_IO_BYTE read_io_byte;        /* read io byte */
	I80386_WRITE_IO_BYTE write_io_byte;      /* write io byte */
	void* user_param;                        /* user parameter passed to function/s */
} I80386_FUNCS;

typedef struct I80386_LOGICAL_ADDRESS {
	uint32_t base;
	uint32_t offset;
} I80386_LOGICAL_ADDRESS;

typedef struct I80386_EFFECTIVE_ADDRESS {
	I80386_LOGICAL_ADDRESS logical_address;
	uint8_t segment_index;
	uint8_t stack_address;
	uint8_t valid;
} I80386_EFFECTIVE_ADDRESS;

typedef struct I80386_LINEAR_ADDRESS {
	union {
		uint32_t dword;
		struct {
			uint32_t offset : 12; /* byte offset */
			uint32_t page   : 10; /* page table index */
			uint32_t dir    : 10; /* page directory index */
		};
	};
} I80386_LINEAR_ADDRESS;

/* Translation Page Table Entry */
typedef struct I80386_PAGE_TABLE_ENTRY {
	union {
		uint32_t dword;
		struct {
			uint32_t present            : 1; /* Present */
			uint32_t rw                 : 1; /* 0 = read only; 1 = read and write */
			uint32_t us                 : 1; /* 0 = supervisor; 1 = user */
			uint32_t r3                 : 1; 
			uint32_t r4                 : 1;
			uint32_t accessed           : 1; /* Accessed */
			uint32_t dirty              : 1; /* 0 = page is unmodified; 1 = page is dirty (only valid in PTE) */
			uint32_t r7                 : 1;
			uint32_t r8                 : 1;
			uint32_t available          : 3; /* Available for systems programmer use */
			uint32_t page_frame_address : 20;
		};
	};
} I80386_PAGE_TABLE_ENTRY;

#define I80386_TLB_SIZE 64

/* Translation Lookaside Buffer Entry */
typedef struct I80386_TLB_ENTRY {
	uint32_t linear_page;
	uint32_t physical_page;
	union {
		uint32_t flags;
		struct {
			uint32_t rw      : 1; /* 0 = read only; 1 = read and write */
			uint32_t us      : 1; /* 0 = supervisor; 1 = user */		
			uint32_t valid   : 1; /* 0 = invalid; 1 = valid */
		};
	};
} I80386_TLB_ENTRY;

/* Translation Lookaside Buffer */
typedef struct I80386_TLB {
	int count;
	int index;
	I80386_TLB_ENTRY entries[I80386_TLB_SIZE];
} I80386_TLB;

#define I80386_GATE_TYPE_AVAL_286 0x1
#define I80386_GATE_TYPE_LDT      0x2
#define I80386_GATE_TYPE_BUSY_286 0x3
#define I80386_GATE_TYPE_CALL_286 0x4
#define I80386_GATE_TYPE_TASK     0x5
#define I80386_GATE_TYPE_INT_286  0x6
#define I80386_GATE_TYPE_TRAP_286 0x7
#define I80386_GATE_TYPE_AVAL_386 0x9
#define I80386_GATE_TYPE_BUSY_386 0xB
#define I80386_GATE_TYPE_CALL_386 0xC
#define I80386_GATE_TYPE_INT_386  0xE
#define I80386_GATE_TYPE_TRAP_386 0xF

#define I80386_INTERRUPT_TYPE_SOFTWARE 0x1
#define I80386_INTERRUPT_TYPE_HARDWARE 0x2

typedef struct I80386_GATE {
	union {
		uint64_t qword;
		struct {
			uint16_t offset_lo;
			uint16_t selector;
			uint8_t param_count; /* only for CALL; reserved for IDT */
			union {
				uint8_t access;
				struct {
					uint8_t type    : 5;
					uint8_t dpl     : 2; /* descriptor privilege level */
					uint8_t present : 1; /* present bit */
				};
			};
			uint16_t offset_hi;
		};
	};
} I80386_GATE;

#define	OPERAND_TYPE_MEMORY           0
#define	OPERAND_TYPE_GENERAL_REGISTER 1
#define	OPERAND_TYPE_SEGMENT_REGISTER 2
#define	OPERAND_TYPE_CONTROL_REGISTER 3
#define	OPERAND_TYPE_DEBUG_REGISTER   4
#define	OPERAND_TYPE_TEST_REGISTER    5
#define	OPERAND_TYPE_IMMEDIATE        6
#define	OPERAND_TYPE_REL              7
#define	OPERAND_TYPE_FAR_POINTER      8
#define	OPERAND_TYPE_FLAGS            9

#define	OPERAND_SIZE_BYTE   1
#define	OPERAND_SIZE_WORD   2
#define	OPERAND_SIZE_DWORD  4
#define	OPERAND_SIZE_QWORD  8

/* r/m operand */
typedef struct I80386_OPERAND {
	uint8_t type;
	uint8_t size;
	union {
		struct {
			I80386_EFFECTIVE_ADDRESS ea;
		} mem;
		struct {
			uint8_t index;
		} reg;
		struct {
			uint32_t value;
		} imm;
		struct {
			int32_t disp;
		} rel; 
		struct {
			uint32_t offset;
			uint16_t selector;
		} far_ptr;
	};
} I80386_OPERAND;

#define	I80386_EXCEPTION_STATE_NONE         0
#define	I80386_EXCEPTION_STATE_STD          1
#define	I80386_EXCEPTION_STATE_DOUBLE_FAULT 2
#define	I80386_EXCEPTION_STATE_TRIPLE_FAULT 3

#define	I80386_EXCEPTION_CLASS_BENIGN       0
#define	I80386_EXCEPTION_CLASS_CONTRIBUTORY 1
#define	I80386_EXCEPTION_CLASS_PAGE_FAULT   2

/* i80386 Exception */
typedef struct I80386_EXCEPTION {
	uint8_t number;    /* std_fault_exception_number */
	uint8_t df_number; /* double_fault_exception_number */
	uint8_t tf_number; /* triple_fault_exception_number */
	uint8_t state;     /* exception_state */
	uint16_t code;     /* exception_code */
} I80386_EXCEPTION;

#define INTERNAL_FLAG_F1Z 0x01
#define INTERNAL_FLAG_F1  0x02

/* i80386 Loadall descriptor cache */
typedef struct I80386_LOADALL_DESC_CACHE {
	uint32_t ar;
	uint32_t base;
	uint32_t limit;
} I80386_LOADALL_DESC_CACHE;

/* i80386 Loadall state */
typedef struct I80386_LOADALL {
	uint32_t cr0;
	uint32_t eflags;
	uint32_t eip;
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t dr6;
	uint32_t dr7;
	uint32_t tr;
	uint32_t ldt_selector;
	uint32_t gs_selector;
	uint32_t fs_selector;
	uint32_t ds_selector;
	uint32_t ss_selector;
	uint32_t cs_selector;
	uint32_t es_selector;
	I80386_LOADALL_DESC_CACHE tss;
	I80386_LOADALL_DESC_CACHE idt;
	I80386_LOADALL_DESC_CACHE gdt;
	I80386_LOADALL_DESC_CACHE ldt;
	I80386_LOADALL_DESC_CACHE gs;
	I80386_LOADALL_DESC_CACHE fs;
	I80386_LOADALL_DESC_CACHE ds;
	I80386_LOADALL_DESC_CACHE ss;
	I80386_LOADALL_DESC_CACHE cs;
	I80386_LOADALL_DESC_CACHE es;
	uint32_t length;
} I80386_LOADALL;

/* i80386 State */
typedef struct I80386 {
	union {
		I80386_REG32 general_registers[I80386_REGISTER_COUNT];             /* general registers */
		struct {
			union {
				uint32_t eax;
				uint16_t ax;
				struct {
					uint8_t al;
					uint8_t ah;
				};
			};
			union {
				uint32_t ecx;
				uint16_t cx;
				struct {
					uint8_t cl;
					uint8_t ch;
				};
			};
			union {
				uint32_t edx;
				uint16_t dx;
				struct {
					uint8_t dl;
					uint8_t dh;
				};
			};
			union {
				uint32_t ebx;
				uint16_t bx;
				struct {
					uint8_t bl;
					uint8_t bh;
				};
			};
			union {
				uint32_t esp;
				uint16_t sp;
			};
			union {
				uint32_t ebp;
				uint16_t bp;
			};
			union {
				uint32_t esi;
				uint16_t si;
			};
			union {
				uint32_t edi;
				uint16_t di;
			};
		};
	};
	union {
		I80386_SEGMENT_REGISTER segment_registers[I80386_SEGMENT_COUNT];      /* segment registers */
		struct {
			I80386_SEGMENT_REGISTER es;
			I80386_SEGMENT_REGISTER cs;
			I80386_SEGMENT_REGISTER ss;
			I80386_SEGMENT_REGISTER ds;
			I80386_SEGMENT_REGISTER fs;
			I80386_SEGMENT_REGISTER gs;
		};
	};
	union {
		uint32_t control_registers[I80386_CONTROL_REGISTER_COUNT]; /* control registers */
		I80386_MACHINE_STATUS_WORD msw;
		struct {
			I80386_CR0 cr0; /* CR0 */
			uint32_t cr1;   /* Reserved */
			uint32_t cr2;   /* Page fault linear address */
			uint32_t cr3;   /* Page directory base register (PDBR) */
		};
	};
	union {
		uint32_t debug_registers[I80386_DEBUG_REGISTER_COUNT];     /* debug registers */
		struct {
			uint32_t dr0; /* linear address breakpoint0 */
			uint32_t dr1; /* linear address breakpoint1 */
			uint32_t dr2; /* linear address breakpoint2 */
			uint32_t dr3; /* linear address breakpoint3 */
			uint32_t dr4; /* reverved */
			uint32_t dr5; /* reverved */
			I80386_DR6 dr6;
			I80386_DR7 dr7;
		};
	};
	union {
		uint32_t test_registers[I80386_TEST_REGISTER_COUNT];       /* test registers */
		struct {
			uint32_t tr0; /* */
			uint32_t tr1; /* */
			uint32_t tr2; /* */
			uint32_t tr3; /* */
			uint32_t tr4; /* */
			uint32_t tr5; /* */
			uint32_t tr6; /* */
			uint32_t tr7; /* */
		};
	};
	I80386_DESCRIPTOR_TABLE_REGISTER gdtr;                         /* global descriptor table register */
	I80386_DESCRIPTOR_TABLE_REGISTER idtr;                         /* interrupt descriptor table register */
	I80386_SEGMENT_REGISTER tr;                                    /* task descriptor table register */
	I80386_SEGMENT_REGISTER ldtr;                                  /* local descriptor table register */
	I80386_TASK_STATE_SEGMENT tss;
	union {
		I80386_PROGRAM_STATUS_WORD psw;                            /* program status word */
		I80386_EFLAGS eflags;                                      /* extended flags */
	};
	union {
		uint16_t ip;                                               /* 16bit instruction pointer */
		uint32_t eip;                                              /* 32bit instruction pointer */
	};
	I80386_EFFECTIVE_ADDRESS effective_address;                    /* effective address calculation infomation */
	uint8_t opcode;                                                /* current opcode */
	I80386_MOD_RM modrm;                                           /* current mod r/m byte */
	I80386_SIB sib;                                                /* current sib byte */
	uint8_t segment_override;                                      /* current segment prefix byte */
	uint8_t lock_prefix;                                           /* current lock prefix byte */
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
	uint8_t address_size;                                          /* default address size attribute. 0 = 16bit addressing; 1 = 32bit addressing */
	uint8_t operand_size;                                          /* default operand size attribute. 0 = 16bit operand; 1 = 32bit operand */
	I80386_EXCEPTION exception;                                    /* exception information */
	uint64_t cycles;
	I80386_FUNCS funcs;                                            /* memory function pointers */
	I80386_TLB tlb;                                                /* translation lookaside buffer */
} I80386;

#pragma warning(pop) /* C4201 - nameless struct */

/* Assert struct sizes */
static_assert(sizeof(I80386_MOD_RM) == 1, "Struct: I80386_MOD_RM not 1 byte");
static_assert(sizeof(I80386_SIB) == 1, "Struct: I80386_SIB not 1 byte");
static_assert(sizeof(I80386_PROGRAM_STATUS_WORD) == 2, "Struct: I80386_PROGRAM_STATUS_WORD not 2 bytes");
static_assert(sizeof(I80386_MACHINE_STATUS_WORD) == 2, "Struct: I80386_MACHINE_STATUS_WORD not 2 bytes");
static_assert(sizeof(I80386_EFLAGS) == 4, "Struct: I80386_EFLAGS not 4 bytes");
static_assert(sizeof(I80386_CR0) == 4, "Struct: I80386_CR0 not 4 bytes");
static_assert(sizeof(I80386_DR6) == 4, "Struct: I80386_DR6 not 4 bytes");
static_assert(sizeof(I80386_DR7) == 4, "Struct: I80386_DR7 not 4 bytes");
static_assert(sizeof(I80386_TASK_STATE_SEGMENT) == 104, "Struct: I80386_TASK_STATE_SEGMENT not 104 bytes");
static_assert(sizeof(I80386_DESCRIPTOR_ACCESS_RIGHTS) == 2, "Struct: I80386_DESCRIPTOR_ACCESS_RIGHTS not 2 bytes");
static_assert(sizeof(I80386_DESCRIPTOR_TABLE_ENTRY) == 8, "Struct: I80386_DESCRIPTOR_TABLE_ENTRY not 8 bytes");
static_assert(sizeof(I80386_LINEAR_ADDRESS) == 4, "Struct: I80386_LINEAR_ADDRESS not 4 bytes");
static_assert(sizeof(I80386_PAGE_TABLE_ENTRY) == 4, "Struct: I80386_PAGE_TABLE_ENTRY not 4 bytes");
static_assert(sizeof(I80386_GATE) == 8, "Struct: I80386_GATE not 8 bytes");
static_assert(sizeof(I80386_LOADALL) == 208, "Struct: I80386_LOADALL not 208 bytes");

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the CPU. Sets all function pointers to NULL
	cpu: the cpu instance */
void i80386_init(I80386* cpu);

/* Reset The CPU to it's reset state.
	cpu: the cpu instance */
void i80386_reset(I80386* cpu);

/* Fetch, Execute the next instruction
	cpu: the cpu instance */
int i80386_execute(I80386* cpu);

/* request hardware interrupt
	cpu:  the cpu instance
	type: the interrupt number 0-0xFF */
void i80386_intr(I80386* cpu, uint8_t type);

/* request non maskable interrupt
	cpu:  the cpu instance */
void i80386_nmi(I80386* cpu);

/* Convert Logical Address to Physical Address  (base + offset) */
uint32_t i80386_get_physical_address_bo(uint32_t base, uint32_t offset);

/* Convert Logical Address to Physical Address  ((selector << 4) + offset) */
uint32_t i80386_get_physical_address_so(uint16_t selector, uint32_t offset);

/* Get descriptor table entry - Returns 1 if success, otherwise 0 */
int i80386_read_descriptor_table_entry(const I80386* cpu, uint16_t selector, I80386_DESCRIPTOR_TABLE_ENTRY* entry);

/* Set descriptor table entry - Returns 1 if success, otherwise 0 */
int i80386_write_descriptor_table_entry(const I80386* cpu, uint16_t selector, const I80386_DESCRIPTOR_TABLE_ENTRY* entry);

/* Update segment descriptor cache */
void i80386_update_segment_descriptor_cache(const I80386_DESCRIPTOR_TABLE_ENTRY* entry, I80386_DESCRIPTOR_CACHE* cache);
/* Update system descriptor cache */
void i80386_update_system_descriptor_cache(const I80386_DESCRIPTOR_TABLE_ENTRY* entry, I80386_DESCRIPTOR_CACHE* cache);

/* Load segment descriptor cache */
int i80386_load_segment_register(I80386* cpu, I80386_SEGMENT_REGISTER* sreg, int sreg_index, uint16_t selector);

/* Copy segment descriptor */
void i80386_copy_segment_descriptor(I80386_SEGMENT_REGISTER* dest, const I80386_SEGMENT_REGISTER* src);

/* Resolve base from segment selector */
int i80386_resolve_segment_selector(const I80386* cpu, uint16_t selector, uint32_t* base);

int i80386_modrm_get_segment(const I80386* cpu, uint8_t address_size, I80386_MOD_RM modrm, I80386_SIB sib, I80386_EFFECTIVE_ADDRESS* effective_address,
	uint8_t segment_prefix);
int i80386_modrm_get_offset(const I80386* cpu, uint8_t address_size, I80386_MOD_RM modrm, I80386_SIB sib, I80386_EFFECTIVE_ADDRESS* effective_address,
	I80386_FETCH_BYTE fetch_byte, I80386_FETCH_WORD fetch_word, I80386_FETCH_DWORD fetch_dword, void* user_param);

/* Convert segmented address to a linear address */
int i80386_segment_translation(I80386* cpu, uint32_t base, uint32_t offset, uint32_t* linear_address);
/* Convert a linear address to a physical address */
int i80386_page_translation(I80386* cpu, uint32_t linear_address, int is_write, uint32_t* physical_address);
/* Convert segmented address to a physical address */
int i80386_address_translation(I80386* cpu, uint32_t base, uint32_t offset, int is_write, uint32_t* physical_address);

#ifdef __cplusplus
};
#endif
#endif

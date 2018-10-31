#include "common.h"
#include "dmi.h"
#include "jtag.h"

#define DEBUG_RAM 0x400

/* load bin file stage 1, store base address in t0
 * assembly source
 *
 * #define DEBUG_RAM 0x400
 * #define RESUME 0x804
 * .text
 * .global _start
 * _start:
 * // during program loading, the core is runing a loop in bootrom
 * // no one needs t0(bootrom + debug rom)
 * // we can safely override it
 * lwu t0, (DEBUG_RAM + 8)(zero)
 * j RESUME
 * address:.word 0
 */
static const int address_idx = 2;
static uint32_t stage_1_machine_code[] = {
  0x40806283, 0x4000006f, 0x00000000
};

/* load bin file stage 2, store one word to address t0
 * assembly source
 *
 * #define DEBUG_RAM 0x400
 * #define RESUME 0x804
 * .text
 * .global _start
 * _start:
 * lwu s0, (DEBUG_RAM + 16)(zero)
 * sw s0, 0(t0)
 * addi t0, t0, 4
 * j RESUME
 * data: .word 0
 */
static const int data_idx = 4;
static uint32_t stage_2_machine_code[] = {
  0x41006403, 0x0082a023, 0x00428293, 0x3f80006f, 0x00000000
};

/* load bin file stage 3, set pc to entry address
 * assembly source
 *
 * #define DCSR 0x7b0
 * #define DPC 0x7b1
 * #define RESUME 0x804
 * .text
 * .global _start
 * _start:
 * ld t0, addr
 * csrw DPC, t0
 * csrsi DCSR, 3  # set dcsr.prv to machine mode
 * j RESUME
 *
 * .align 3
 * addr: .quad 0
 */
static uint32_t stage_3_machine_code[] = {
  0x00000297, 0x0182b283, 0x7b129073, 0x7b01e073, 0x3f40006f, 0x00000013, 0, 0
};

void load_program(const char *bin_file, uint64_t hartid, uint32_t base) {
  // load base address to t0
  stage_1_machine_code[address_idx] = base;
  // write debug ram
  for (unsigned i = 0; i < sizeof(stage_1_machine_code) / sizeof(uint32_t); i++)
    rw_debug_reg(OP_WRITE, DEBUG_RAM + i, stage_1_machine_code[i]);
  // send debug request to hart 0, let it execute code we just written
  rw_debug_reg(OP_WRITE, 0x10, 1ULL << 33 | hartid << 2);

  // wait for hart to clear debug interrupt
  while((rw_debug_reg(OP_READ, 0x10, hartid << 2) >> 33) & 1ULL);

  for (unsigned i = 0; i < sizeof(stage_2_machine_code) / sizeof(uint32_t); i++)
    rw_debug_reg(OP_WRITE, DEBUG_RAM + i, stage_2_machine_code[i]);

  FILE *f = fopen(bin_file, "r");
  assert(f);
  uint32_t code;
  while(fread(&code, sizeof(uint32_t), 1, f) == 1) {
    rw_debug_reg(OP_WRITE, DEBUG_RAM + data_idx, code);
    // send a debug interrupt to hart 0, let it store one word for us
    rw_debug_reg(OP_WRITE, 0x10, 1ULL << 33 | hartid << 2);
    while((rw_debug_reg(OP_READ, 0x10, hartid << 2) >> 33) & 1ULL);
  }
  fclose(f);
}

void start_program(uint64_t hartid, uint64_t entry) {
  stage_3_machine_code[6] = entry & 0xffffffff;
  stage_3_machine_code[7] = entry >> 32;
  // bin file loading finished, write entry address to dpc
  for (unsigned i = 0; i < sizeof(stage_3_machine_code) / sizeof(uint32_t); i++)
    rw_debug_reg(OP_WRITE, DEBUG_RAM + i, stage_3_machine_code[i]);
  rw_debug_reg(OP_WRITE, 0x10, 1ULL << 33 | hartid << 2);
  while((rw_debug_reg(OP_READ, 0x10, hartid << 2) >> 33) & 1ULL);
}

/* load bin file stage 4, read memory to make sure it's correctly loaded
 * assembly source
 *
 * #define DEBUG_RAM 0x400
 * #define RESUME 0x804
 * .text
 * .global _start
 * _start:
 * lwu s1, (DEBUG_RAM + 16)(zero)
 * lwu s0, 0(s1)
 * sw s0, (DEBUG_RAM + 20)(zero)
 * j RESUME
 * address: .word 0
 * data: .word 0
 */
static const int s4_addr_idx = 4;
static const int s4_data_idx = 5;
uint32_t stage_4_machine_code[] = {
  0x41006483, 0x0004e403, 0x40802a23, 0x3f80006f, 0x00000000, 0x00000000
};

void check_loaded_program(const char *bin_file, uint64_t hartid, uint32_t base) {
  for (unsigned i = 0; i < sizeof(stage_4_machine_code) / sizeof(uint32_t); i++)
    rw_debug_reg(OP_WRITE, DEBUG_RAM + i, stage_4_machine_code[i]);

  FILE *f = fopen(bin_file, "r");
  assert(f);
  uint32_t code;
  while(fread(&code, sizeof(uint32_t), 1, f) == 1) {
    rw_debug_reg(OP_WRITE, DEBUG_RAM + s4_addr_idx, base);
    // send a debug interrupt to hart, let it store one word for us
    rw_debug_reg(OP_WRITE, 0x10, 1ULL << 33 | hartid << 2);
    while((rw_debug_reg(OP_READ, 0x10, 0ULL) >> 33) & 1ULL);
    uint32_t loaded_code = rw_debug_reg(OP_READ, DEBUG_RAM + s4_data_idx, hartid << 2);
    if (code != loaded_code) {
      printf("addr: %x code: %x loaded_code: %x\n", base, code, loaded_code);
    }
    base += 4;
  }
  fclose(f);
}

union {
  struct {
	uint32_t debugVersion   : 4;
	uint32_t debugAddrBits  : 6;
	uint32_t dbusStatus     : 2;
	uint32_t dbusIdleCycles : 3;
  };
  uint64_t bits;
} dtminfo;

typedef union {
  struct {
	uint32_t op : 2;
	uint32_t data : 32;
	uint32_t addr : 6;
  };
  uint32_t bits;
} DMI;

union {
  struct {
	uint32_t sbaccess8       : 1;
	uint32_t sbaccess16      : 1;
	uint32_t sbaccess32      : 1;
	uint32_t sbaccess64      : 1;
	uint32_t sbaccess128     : 1;
	uint32_t sbasize         : 7;
	uint32_t sberror         : 3;
	uint32_t sbadondata      : 1;
	uint32_t sbautoincrement : 1;
	uint32_t sbaccess        : 3;
	uint32_t sbreadonaddr    : 1;
	uint32_t sbbusy          : 1;
	uint32_t sbbusyerror     : 1;
	uint32_t sb_unused       : 6;
	uint32_t sbversion       : 3;
	uint32_t sbxxx           : 32;
  };
  uint64_t bits;
} SBCS;

void init_dtm() {
  // get dtm info
  dtminfo.bits = rw_jtag_reg(REG_DTM_INFO, 0, REG_DTM_INFO_WIDTH);



  DMI dmi = {
	.op = 1,
	.data = 0x0,
	.addr = 0x38,
  };


  SBCS.bits = rw_jtag_reg(REG_DEBUG_ACCESS, dmi.bits, 2 + 32 + 6);
  printf("sbversion: %llx\n", SBCS.bits);

  sleep(1);
  SBCS.bits = rw_jtag_reg(REG_DEBUG_ACCESS, 0, 2 + 32 + 6);
  printf("sbversion: %llx\n", SBCS.bits);

  sleep(1);
  SBCS.bits = rw_jtag_reg(REG_DEBUG_ACCESS, dmi.bits, 2 + 32 + 6);
  printf("sbversion: %llx\n", SBCS.bits);



  // int dbusIdleCycles = get_bits(dtminfo, 12, 10);
  // int dbusStatus = get_bits(dtminfo, 9, 8);
  // int debugAddrBits = get_bits(dtminfo, 7, 4);  // TODO AddrBits now has length 6!!!
  // int debugVersion = get_bits(dtminfo, 3, 0);

  printf("dbusIdleCycles: %d\ndbusStatus: %d\ndebugAddrBits: %d\ndebugVersion: %d\n",
      dtminfo.dbusIdleCycles, dtminfo.dbusStatus, dtminfo.debugAddrBits, dtminfo.debugVersion);

}

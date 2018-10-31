#include "common.h"
#include "dmi.h"

// control plane register address space;
// 31     23        21     11       0;
// +-------+--------+-------+-------+;
// | cpIdx | tabIdx |  col  |  row  |;
// +-------+--------+-------+-------+;
//  \- 8 -/ \-  2 -/ \- 10-/ \- 12 -/;
const int rowIdxLen = 12;
const int colIdxLen = 10;
const int tabIdxLen = 2;
const int cpIdxLen = 8;

const int rowIdxLow = 0;
#define colIdxLow (rowIdxLow + rowIdxLen)
#define tabIdxLow (colIdxLow + colIdxLen)
#define cpIdxLow  (tabIdxLow + tabIdxLen)

#define rowIdxHigh (colIdxLow - 1)
#define colIdxHigh (tabIdxLow - 1)
#define tabIdxHigh (cpIdxLow  - 1)
const int cpIdxHigh  = 31;

// control plane index;
const int coreCpIdx = 0;
const int memCpIdx = 1;
const int cacheCpIdx = 2;
const int ioCpIdx = 3;

// tables;
const int ptabIdx = 0;
const int stabIdx = 1;
const int ttabIdx = 2;

static const char *rw_tables[] = {"r","w"};
static const char *cp_tables[] = {"core", "mem", "cache","io"};

static const char *tab_tables[3][3] = {
  {"p"},
  {"p", "s"},
  {"p", "s"}
};

static const char *col_tables[3][3][4] = {
  {{"dsid", "base", "size", "hartid"}},
  {{"size", "freq", "inc"}, {"cached", "uncached"}},
  {{"mask"}, {"access", "miss", "usage"}}
};

int get_cp_addr(int cpIdx, int tabIdx, int col, int row) {
  int addr = cpIdx << cpIdxLow | tabIdx << tabIdxLow |
    col << colIdxLow | row << rowIdxLow;
  return addr;
}

static inline int string_to_idx(const char *name, const char **table, int size) {
  for (int i = 0; i < size; i++)
    if (!strcmp(name, table[i]))
      return i;
  return -1;
}

int query_rw_tables(const char *value) {
  return string_to_idx(value, rw_tables,sizeof(rw_tables) / sizeof(char *));
}

int query_cp_tables(const char *value) {
  return string_to_idx(value, cp_tables,sizeof(cp_tables) / sizeof(char *));
}

int query_tab_tables(const char *value, int cpIdx) {
  return string_to_idx(value, tab_tables[cpIdx],sizeof(tab_tables[cpIdx]) / sizeof(char *));
}

int query_col_tables(const char *value, int cpIdx, int tabIdx) {
  return string_to_idx(value, col_tables[cpIdx][tabIdx],
      sizeof(col_tables[cpIdx][tabIdx]) / sizeof(char *));
}

uint64_t read_cp_reg(int addr) {
  struct DMI_Req req;
  struct DMI_Resp resp;
  // write sbaddr0
  req.opcode = OP_WRITE;
  req.addr = 0x16;
  req.data = (uint32_t)addr;
  // set rw to 1(read)
  req.data |= (uint64_t)1 << 32;
  send_debug_request(req);
  // read sbdata0
  req.opcode = OP_READ;
  req.addr = 0x18;
  req.data = 0;
  resp = send_debug_request(req);
  uint64_t val = resp.data;

  // read sbdata1
  req.opcode = OP_READ;
  req.addr = 0x19;
  req.data = 0;
  resp = send_debug_request(req);
  val = val | ((uint64_t)resp.data << 32);

  return val;
}

void write_cp_reg(int addr, uint64_t value) {
  struct DMI_Req req;
  // write sbaddr0
  req.opcode = OP_WRITE;
  req.addr = 0x16;
  req.data = (uint32_t)addr;
  send_debug_request(req);
  // write sbdata0
  req.opcode = OP_WRITE;
  req.addr = 0x18;
  req.data = (uint32_t)value;
  send_debug_request(req);
  // write sbdata1
  req.opcode = OP_WRITE;
  req.addr = 0x19;
  req.data = (uint32_t)(value >> 32);
  send_debug_request(req);
}

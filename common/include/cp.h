#ifndef __CP_H__
#define __CP_H__

#include "common.h"

int get_cp_addr(int cpIdx, int tabIdx, int col, int row);
uint64_t read_cp_reg(int addr);
void write_cp_reg(int addr, uint64_t value);
int query_rw_tables(const char *value);
int query_cp_tables(const char *value);
int query_tab_tables(const char *value, int cpIdx);
int query_col_tables(const char *value, int cpIdx, int tabIdx);

#endif

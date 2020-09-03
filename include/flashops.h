#pragma once
#include <spinand.h>
#include <stdbool.h>
int snand_read(struct spinand_device *snand, size_t offs, size_t len,
	       bool ecc_enabled, bool read_oob, FILE *fp);
void snand_scan_bbm(struct spinand_device *snand);
int snand_write(struct spinand_device *snand, size_t offs, bool ecc_enabled,
		bool write_oob, bool erase_rest, FILE *fp, size_t old_bbm_offs,
		size_t old_bbm_len, size_t bbm_offs, size_t bbm_len);

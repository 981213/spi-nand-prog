#include <spinand.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <flashops.h>
int snand_read(struct spinand_device *snand, size_t offs, size_t len,
	       bool ecc_enabled, bool read_oob, FILE *fp)
{
	struct nand_device *nand = spinand_to_nand(snand);
	size_t page_size = nanddev_page_size(nand);
	size_t oob_size = nanddev_per_page_oobsize(nand);
	size_t fwrite_size;
	struct nand_page_io_req io_req;
	size_t rdlen = 0;
	uint8_t *buf;
	int ret;

	if (offs % page_size) {
		fprintf(stderr, "Reading should start at page boundary.\n");
		return -EINVAL;
	}

	if (!len)
		len = nanddev_size(nand) - offs;

	buf = malloc(page_size + oob_size);
	if (!buf)
		return -ENOMEM;

	memset(&io_req, 0, sizeof(io_req));
	io_req.databuf.in = buf;
	io_req.datalen = page_size;
	io_req.dataoffs = 0;
	fwrite_size = page_size;
	if (read_oob) {
		io_req.oobbuf.in = buf + page_size;
		io_req.ooblen = oob_size;
		io_req.ooboffs = 0;
		fwrite_size += oob_size;
	}
	nanddev_offs_to_pos(nand, offs, &io_req.pos);

	while (rdlen < len) {
		printf("reading offset (%lX block %u page %u)\r", offs + rdlen,
		       io_req.pos.eraseblock, io_req.pos.page);
		ret = spinand_read_page(snand, &io_req, ecc_enabled);
		if (ret > 0) {
			printf("\necc corrected %d bitflips.\n", ret);
		} else if (ret < 0) {
			printf("\nreading failed. errno %d\n", ret);
			memset(buf, 0, fwrite_size);
		}
		fwrite(buf, 1, fwrite_size, fp);
		rdlen += page_size;
		nanddev_pos_next_page(nand, &io_req.pos);
	}
	printf("\n\ndone.\n");
	free(buf);
	return 0;
}

bool snand_isbad(struct spinand_device *snand, const struct nand_pos *pos,
		 size_t bbm_offs, size_t bbm_len)
{
	struct nand_device *nand = spinand_to_nand(snand);
	size_t page_size = nanddev_page_size(nand);
	struct nand_page_io_req req;
	size_t i;

	u8 marker[8] = {};
	if (bbm_len > 8) {
		fprintf(stderr, "bbm too long.\n");
		return true;
	}

	if (!bbm_len) {
		bbm_offs = page_size;
		bbm_len = 2;
	}

	memset(&req, 0, sizeof(req));
	req.pos = *pos;
	req.pos.page = 0;
	if (bbm_offs < page_size) {
		req.databuf.in = marker;
		req.datalen = bbm_len;
		req.dataoffs = bbm_offs;
	} else {
		req.oobbuf.in = marker;
		req.ooblen = bbm_len;
		req.ooboffs = bbm_offs - page_size;
	}
	spinand_read_page(snand, &req, false);

	for (i = 0; i < bbm_len; i++)
		if (marker[i] != 0xff)
			return true;
	return false;
}

int snand_markbad(struct spinand_device *snand, const struct nand_pos *pos,
		  size_t bbm_offs, size_t bbm_len)
{
	struct nand_device *nand = spinand_to_nand(snand);
	size_t page_size = nanddev_page_size(nand);
	struct nand_page_io_req req;
	u8 marker[8];
	if (bbm_len > 8) {
		fprintf(stderr, "bbm too long.\n");
		return -EINVAL;
	}

	if (!bbm_len) {
		bbm_offs = page_size;
		bbm_len = 2;
	}

	memset(&req, 0, sizeof(req));
	memset(marker, 0, sizeof(marker));
	req.pos = *pos;
	req.pos.page = 0;
	if (bbm_offs < page_size) {
		req.databuf.out = marker;
		req.datalen = bbm_len;
		req.dataoffs = bbm_offs;
	} else {
		req.oobbuf.out = marker;
		req.ooblen = bbm_len;
		req.ooboffs = bbm_offs - page_size;
	}

	return spinand_write_page(snand, &req, false);
}

int snand_erase_remark(struct spinand_device *snand, const struct nand_pos *pos,
		       size_t old_bbm_offs, size_t old_bbm_len, size_t bbm_offs,
		       size_t bbm_len)
{
	int ret;
	if (snand_isbad(snand, pos, old_bbm_offs, old_bbm_len)) {
		printf("bad block: target %u block %u.\n", pos->target,
		       pos->eraseblock);
		goto BAD_BLOCK;
	}

	ret = spinand_erase(snand, pos);
	if (ret) {
		printf("erase failed: target %u block %u. ret: %d\n",
		       pos->target, pos->eraseblock, ret);
		goto BAD_BLOCK;
	}

	return 0;
BAD_BLOCK:
	snand_markbad(snand, pos, bbm_offs, bbm_len);
	return -EIO;
}

int snand_write(struct spinand_device *snand, size_t offs, bool ecc_enabled,
		bool write_oob, bool erase_rest, FILE *fp, size_t old_bbm_offs,
		size_t old_bbm_len, size_t bbm_offs, size_t bbm_len)
{
	struct nand_device *nand = spinand_to_nand(snand);
	size_t page_size = nanddev_page_size(nand);
	size_t oob_size = nanddev_per_page_oobsize(nand);
	size_t eb_size = nanddev_eraseblock_size(nand);
	size_t flash_size = nanddev_size(nand);
	size_t fread_len, actual_read_len = 0;
	struct nand_page_io_req wr_req, rd_req;
	size_t cur_offs = offs, eb_rd_offs = 0;
	uint8_t *buf, *rdbuf;
	int ret;

	if (offs % eb_size) {
		fprintf(stderr, "Writing should start at eb boundary.\n");
		return -EINVAL;
	}

	buf = malloc((page_size + oob_size) * 2);
	if (!buf)
		return -ENOMEM;

	rdbuf = buf + page_size + oob_size;

	memset(&wr_req, 0, sizeof(wr_req));
	wr_req.databuf.out = buf;
	wr_req.datalen = page_size;
	wr_req.dataoffs = 0;
	fread_len = page_size;
	if (write_oob) {
		wr_req.oobbuf.out = buf + page_size;
		wr_req.ooblen = oob_size;
		wr_req.ooboffs = 0;
		fread_len += oob_size;
	}

	if (fp)
		actual_read_len = fread_len; // for the EOF check in loop.

	nanddev_offs_to_pos(nand, offs, &wr_req.pos);

	while (cur_offs < flash_size) {
		if (!wr_req.pos.page) {
			eb_rd_offs = 0;
			printf("erasing %lX (block %u)\r", cur_offs,
			       wr_req.pos.eraseblock);
			ret = snand_erase_remark(snand, &wr_req.pos,
						 old_bbm_offs, old_bbm_len,
						 bbm_offs, bbm_len);
			if (ret) {
				printf("\nskipping current block: %d\n", ret);
				cur_offs += eb_size;
				nanddev_pos_next_eraseblock(nand, &wr_req.pos);
				continue;
			}
		}

		if (actual_read_len == fread_len) {
			actual_read_len = fread(buf, 1, fread_len, fp);
			printf("writing %lu bytes to %lX (block %u page %u)\r",
			       actual_read_len, cur_offs, wr_req.pos.eraseblock,
			       wr_req.pos.page);
			if (actual_read_len < fread_len)
				memset(buf + actual_read_len, 0xff,
				       fread_len - actual_read_len);

			eb_rd_offs += actual_read_len;

			ret = spinand_write_page(snand, &wr_req, ecc_enabled);
			if (ret) {
				printf("\npage writing failed.\n");
				goto BAD_BLOCK;
			}

			if (ecc_enabled && !write_oob) {
				rd_req = wr_req;
				rd_req.databuf.out = rdbuf;
				rd_req.oobbuf.out = rdbuf + page_size;
				ret = spinand_read_page(snand, &rd_req,
							ecc_enabled);
				if (ret > 0) {
					printf("\necc corrected %d bitflips.\n",
					       ret);
				} else if (ret < 0) {
					printf("\nreading failed. errno %d\n",
					       ret);
					goto BAD_BLOCK;
				}
				if (memcmp(buf, rdbuf, fread_len)) {
					printf("\ndata verification failed.\n");
					goto BAD_BLOCK;
				}
			}
			cur_offs += page_size;
			nanddev_pos_next_page(nand, &wr_req.pos);
		} else if (erase_rest) {
			nanddev_pos_next_eraseblock(nand, &wr_req.pos);
			cur_offs = nanddev_pos_to_offs(nand, &wr_req.pos);
		} else {
			break;
		}

		continue;
	BAD_BLOCK:
		snand_markbad(snand, &wr_req.pos, bbm_offs, bbm_len);
		fseek(fp, -eb_rd_offs, SEEK_CUR);
		nanddev_pos_next_eraseblock(nand, &wr_req.pos);
		cur_offs = nanddev_pos_to_offs(nand, &wr_req.pos);
	}
	printf("\ndone.\n");
	return 0;
}

void snand_scan_bbm(struct spinand_device *snand)
{
	struct nand_device *nand = spinand_to_nand(snand);
	size_t eb_size = nanddev_eraseblock_size(nand);
	size_t flash_size = nanddev_size(nand);
	size_t offs = 0;
	struct nand_pos pos;
	nanddev_offs_to_pos(nand, 0, &pos);
	while (offs < flash_size) {
		printf("scaning block %u\r", pos.eraseblock);
		if (snand_isbad(snand, &pos, 0, 0))
			printf("\ntarget %u block %u is bad.\n", pos.target,
			       pos.eraseblock);
		nanddev_pos_next_eraseblock(nand, &pos);
		offs += eb_size;
	}
	printf("\ndone.\n");
}

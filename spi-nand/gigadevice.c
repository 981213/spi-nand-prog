// SPDX-License-Identifier: GPL-2.0
/*
 * Author:
 *	Chuanhong Guo <gch981213@gmail.com>
 */

#include <spinand.h>

#define SPINAND_MFR_GIGADEVICE			0xC8

#define GD5FXGQ4XA_STATUS_ECC_1_7_BITFLIPS	(1 << 4)
#define GD5FXGQ4XA_STATUS_ECC_8_BITFLIPS	(3 << 4)

#define GD5FXGQ5XE_STATUS_ECC_1_4_BITFLIPS	(1 << 4)
#define GD5FXGQ5XE_STATUS_ECC_4_BITFLIPS	(3 << 4)

#define GD5FXGQXXEXXG_REG_STATUS2		0xf0

#define GD5FXGQ4UXFXXG_STATUS_ECC_MASK		(7 << 4)
#define GD5FXGQ4UXFXXG_STATUS_ECC_NO_BITFLIPS	(0 << 4)
#define GD5FXGQ4UXFXXG_STATUS_ECC_1_3_BITFLIPS	(1 << 4)
#define GD5FXGQ4UXFXXG_STATUS_ECC_UNCOR_ERROR	(7 << 4)

static SPINAND_OP_VARIANTS(read_cache_variants,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(read_cache_variants_f,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP_3A(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP_3A(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP_3A(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP_3A(false, 0, 0, NULL, 0));

static SPINAND_OP_VARIANTS(read_cache_variants_1gq5,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(read_cache_variants_2gq5,
		SPINAND_PAGE_READ_FROM_CACHE_QUADIO_OP(0, 4, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X4_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_DUALIO_OP(0, 2, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_X2_OP(0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(true, 0, 1, NULL, 0),
		SPINAND_PAGE_READ_FROM_CACHE_OP(false, 0, 1, NULL, 0));

static SPINAND_OP_VARIANTS(write_cache_variants,
		SPINAND_PROG_LOAD_X4(true, 0, NULL, 0),
		SPINAND_PROG_LOAD(true, 0, NULL, 0));

static SPINAND_OP_VARIANTS(update_cache_variants,
		SPINAND_PROG_LOAD_X4(false, 0, NULL, 0),
		SPINAND_PROG_LOAD(false, 0, NULL, 0));

static int gd5fxgq4xa_ecc_get_status(struct spinand_device *spinand,
					 u8 status)
{
	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case GD5FXGQ4XA_STATUS_ECC_1_7_BITFLIPS:
		/* 1-7 bits are flipped. return the maximum. */
		return 7;

	case GD5FXGQ4XA_STATUS_ECC_8_BITFLIPS:
		return 8;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	default:
		break;
	}

	return -EINVAL;
}

static int gd5fxgq4uexxg_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	u8 status2;
	struct spi_mem_op op = SPINAND_GET_FEATURE_OP(GD5FXGQXXEXXG_REG_STATUS2,
						      &status2);
	int ret;

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case GD5FXGQ4XA_STATUS_ECC_1_7_BITFLIPS:
		/*
		 * Read status2 register to determine a more fine grained
		 * bit error status
		 */
		ret = spi_mem_exec_op(spinand->spimem, &op);
		if (ret)
			return ret;

		/*
		 * 4 ... 7 bits are flipped (1..4 can't be detected, so
		 * report the maximum of 4 in this case
		 */
		/* bits sorted this way (3...0): ECCS1,ECCS0,ECCSE1,ECCSE0 */
		return ((status & STATUS_ECC_MASK) >> 2) |
			((status2 & STATUS_ECC_MASK) >> 4);

	case GD5FXGQ4XA_STATUS_ECC_8_BITFLIPS:
		return 8;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	default:
		break;
	}

	return -EINVAL;
}

static int gd5fxgq5xexxg_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	u8 status2;
	struct spi_mem_op op = SPINAND_GET_FEATURE_OP(GD5FXGQXXEXXG_REG_STATUS2,
						      &status2);
	int ret;

	switch (status & STATUS_ECC_MASK) {
	case STATUS_ECC_NO_BITFLIPS:
		return 0;

	case GD5FXGQ5XE_STATUS_ECC_1_4_BITFLIPS:
		/*
		 * Read status2 register to determine a more fine grained
		 * bit error status
		 */
		ret = spi_mem_exec_op(spinand->spimem, &op);
		if (ret)
			return ret;

		/*
		 * 1 ... 4 bits are flipped (and corrected)
		 */
		/* bits sorted this way (1...0): ECCSE1, ECCSE0 */
		return ((status2 & STATUS_ECC_MASK) >> 4) + 1;

	case STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	default:
		break;
	}

	return -EINVAL;
}

static int gd5fxgq4ufxxg_ecc_get_status(struct spinand_device *spinand,
					u8 status)
{
	switch (status & GD5FXGQ4UXFXXG_STATUS_ECC_MASK) {
	case GD5FXGQ4UXFXXG_STATUS_ECC_NO_BITFLIPS:
		return 0;

	case GD5FXGQ4UXFXXG_STATUS_ECC_1_3_BITFLIPS:
		return 3;

	case GD5FXGQ4UXFXXG_STATUS_ECC_UNCOR_ERROR:
		return -EBADMSG;

	default: /* (2 << 4) through (6 << 4) are 4-8 corrected errors */
		return ((status & GD5FXGQ4UXFXXG_STATUS_ECC_MASK) >> 4) + 2;
	}

	return -EINVAL;
}

static const struct spinand_info gigadevice_spinand_table[] = {
	SPINAND_INFO("GD5F1GQ4xA",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xf1),
		     NAND_MEMORG(1, 2048, 64, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4xa_ecc_get_status)),
	SPINAND_INFO("GD5F2GQ4xA",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xf2),
		     NAND_MEMORG(1, 2048, 64, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4xa_ecc_get_status)),
	SPINAND_INFO("GD5F4GQ4xA",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xf4),
		     NAND_MEMORG(1, 2048, 64, 64, 4096, 80, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4xa_ecc_get_status)),
	SPINAND_INFO("GD5F4GQ4RC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE, 0xa4, 0x68),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_f,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4ufxxg_ecc_get_status)),
	SPINAND_INFO("GD5F4GQ4UC",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE, 0xb4, 0x68),
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_f,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4ufxxg_ecc_get_status)),
	SPINAND_INFO("GD5F1GQ4UExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xd1),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F1GQ4RExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xc1),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F2GQ4UExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xd2),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F2GQ4RExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_ADDR, 0xc2),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F1GQ4UFxxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE, 0xb1, 0x48),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_f,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4ufxxg_ecc_get_status)),
	SPINAND_INFO("GD5F1GQ5UExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x51),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_1gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq5xexxg_ecc_get_status)),
	SPINAND_INFO("GD5F1GQ5RExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x41),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_1gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq5xexxg_ecc_get_status)),
	SPINAND_INFO("GD5F2GQ5UExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x52),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_2gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq5xexxg_ecc_get_status)),
	SPINAND_INFO("GD5F2GQ5RExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x42),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_2gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq5xexxg_ecc_get_status)),
	SPINAND_INFO("GD5F4GQ6UExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x55),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 2, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_2gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq5xexxg_ecc_get_status)),
	SPINAND_INFO("GD5F4GQ6RExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x45),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 2, 1),
		     NAND_ECCREQ(4, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_2gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq5xexxg_ecc_get_status)),
	SPINAND_INFO("GD5F1GM7UExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x91),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_1gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F1GM7RExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x81),
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 20, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_1gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F2GM7UExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x92),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_1gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F2GM7RExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x82),
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 40, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_1gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F4GM8UExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x95),
		     NAND_MEMORG(1, 2048, 128, 64, 4096, 80, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_1gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
	SPINAND_INFO("GD5F4GM8RExxG",
		     SPINAND_ID(SPINAND_READID_METHOD_OPCODE_DUMMY, 0x85),
		     NAND_MEMORG(1, 2048, 128, 64, 4096, 80, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_INFO_OP_VARIANTS(&read_cache_variants_1gq5,
					      &write_cache_variants,
					      &update_cache_variants),
		     SPINAND_HAS_QE_BIT,
		     SPINAND_ECCINFO(gd5fxgq4uexxg_ecc_get_status)),
};

static const struct spinand_manufacturer_ops gigadevice_spinand_manuf_ops = {
};

const struct spinand_manufacturer gigadevice_spinand_manufacturer = {
	.id = SPINAND_MFR_GIGADEVICE,
	.name = "GigaDevice",
	.chips = gigadevice_spinand_table,
	.nchips = ARRAY_SIZE(gigadevice_spinand_table),
	.ops = &gigadevice_spinand_manuf_ops,
};

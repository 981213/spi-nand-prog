// SPDX-License-Identifier: GPL-2.0+
/*
 * This is based on include/linux/spi/spi-mem.h in Linux
 * Original file header:
 * 
 * Copyright (C) 2018 Exceet Electronics GmbH
 * Copyright (C) 2018 Bootlin
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <spi.h>
#include <spi-mem.h>

static int spi_check_buswidth_req(struct spi_mem *mem, u8 buswidth, bool tx)
{
	u32 mode = mem->spi_mode;

	switch (buswidth) {
	case 1:
		return 0;

	case 2:
		if ((tx &&
		     (mode & (SPI_TX_DUAL | SPI_TX_QUAD | SPI_TX_OCTAL))) ||
		    (!tx &&
		     (mode & (SPI_RX_DUAL | SPI_RX_QUAD | SPI_RX_OCTAL))))
			return 0;

		break;

	case 4:
		if ((tx && (mode & (SPI_TX_QUAD | SPI_TX_OCTAL))) ||
		    (!tx && (mode & (SPI_RX_QUAD | SPI_RX_OCTAL))))
			return 0;

		break;

	case 8:
		if ((tx && (mode & SPI_TX_OCTAL)) ||
		    (!tx && (mode & SPI_RX_OCTAL)))
			return 0;

		break;

	default:
		break;
	}

	return -EOPNOTSUPP;
}

bool spi_mem_default_supports_op(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	if (spi_check_buswidth_req(mem, op->cmd.buswidth, true))
		return false;

	if (op->addr.nbytes &&
	    spi_check_buswidth_req(mem, op->addr.buswidth, true))
		return false;

	if (op->dummy.nbytes &&
	    spi_check_buswidth_req(mem, op->dummy.buswidth, true))
		return false;

	if (op->data.dir != SPI_MEM_NO_DATA &&
	    spi_check_buswidth_req(mem, op->data.buswidth,
				   op->data.dir == SPI_MEM_DATA_OUT))
		return false;

	return true;
}

static bool spi_mem_buswidth_is_valid(u8 buswidth)
{
	if ((buswidth != 0) && (buswidth != 1) && (buswidth != 2) &&
	    (buswidth != 4) && (buswidth != 8))
		return false;

	return true;
}

static int spi_mem_check_op(const struct spi_mem_op *op)
{
	if (!op->cmd.buswidth)
		return -EINVAL;

	if ((op->addr.nbytes && !op->addr.buswidth) ||
	    (op->dummy.nbytes && !op->dummy.buswidth) ||
	    (op->data.nbytes && !op->data.buswidth))
		return -EINVAL;

	if (!spi_mem_buswidth_is_valid(op->cmd.buswidth) ||
	    !spi_mem_buswidth_is_valid(op->addr.buswidth) ||
	    !spi_mem_buswidth_is_valid(op->dummy.buswidth) ||
	    !spi_mem_buswidth_is_valid(op->data.buswidth))
		return -EINVAL;

	return 0;
}

static bool spi_mem_internal_supports_op(struct spi_mem *mem,
					 const struct spi_mem_op *op)
{
	if (mem->ops->supports_op)
		return mem->ops->supports_op(mem, op);

	return spi_mem_default_supports_op(mem, op);
}

/**
 * spi_mem_supports_op() - Check if a memory device and the controller it is
 *			   connected to support a specific memory operation
 * @mem: the SPI memory
 * @op: the memory operation to check
 *
 * Some controllers are only supporting Single or Dual IOs, others might only
 * support specific opcodes, or it can even be that the controller and device
 * both support Quad IOs but the hardware prevents you from using it because
 * only 2 IO lines are connected.
 *
 * This function checks whether a specific operation is supported.
 *
 * Return: true if @op is supported, false otherwise.
 */
bool spi_mem_supports_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	if (spi_mem_check_op(op))
		return false;

	return spi_mem_internal_supports_op(mem, op);
}

/**
 * spi_mem_exec_op() - Execute a memory operation
 * @mem: the SPI memory
 * @op: the memory operation to execute
 *
 * Executes a memory operation.
 *
 * This function first checks that @op is supported and then tries to execute
 * it.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int spi_mem_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	int ret;

	ret = spi_mem_check_op(op);
	if (ret)
		return ret;

	if (!spi_mem_internal_supports_op(mem, op))
		return -EOPNOTSUPP;

	return mem->ops->exec_op(mem, op);
}

/**
 * spi_mem_adjust_op_size() - Adjust the data size of a SPI mem operation to
 *			      match controller limitations
 * @mem: the SPI memory
 * @op: the operation to adjust
 *
 * Some controllers have FIFO limitations and must split a data transfer
 * operation into multiple ones, others require a specific alignment for
 * optimized accesses. This function allows SPI mem drivers to split a single
 * operation into multiple sub-operations when required.
 *
 * Return: a negative error code if the controller can't properly adjust @op,
 *	   0 otherwise. Note that @op->data.nbytes will be updated if @op
 *	   can't be handled in a single step.
 */
int spi_mem_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	if (mem->ops->adjust_op_size)
		return mem->ops->adjust_op_size(mem, op);

	return 0;
}

static ssize_t spi_mem_no_dirmap_read(struct spi_mem_dirmap_desc *desc,
				      u64 offs, size_t len, void *buf)
{
	struct spi_mem_op op = desc->info.op_tmpl;
	int ret;

	op.addr.val = desc->info.offset + offs;
	op.data.buf.in = buf;
	op.data.nbytes = len;
	ret = spi_mem_adjust_op_size(desc->mem, &op);
	if (ret)
		return ret;

	ret = spi_mem_exec_op(desc->mem, &op);
	if (ret)
		return ret;

	return op.data.nbytes;
}

static ssize_t spi_mem_no_dirmap_write(struct spi_mem_dirmap_desc *desc,
				       u64 offs, size_t len, const void *buf)
{
	struct spi_mem_op op = desc->info.op_tmpl;
	int ret;

	op.addr.val = desc->info.offset + offs;
	op.data.buf.out = buf;
	op.data.nbytes = len;
	ret = spi_mem_adjust_op_size(desc->mem, &op);
	if (ret)
		return ret;

	ret = spi_mem_exec_op(desc->mem, &op);
	if (ret)
		return ret;

	return op.data.nbytes;
}

/**
 * spi_mem_dirmap_create() - Create a direct mapping descriptor
 * @mem: SPI mem device this direct mapping should be created for
 * @info: direct mapping information
 *
 * This function is creating a direct mapping descriptor which can then be used
 * to access the memory using spi_mem_dirmap_read() or spi_mem_dirmap_write().
 * If the SPI controller driver does not support direct mapping, this function
 * falls back to an implementation using spi_mem_exec_op(), so that the caller
 * doesn't have to bother implementing a fallback on his own.
 *
 * Return: a valid pointer in case of success, and ERR_PTR() otherwise.
 */
struct spi_mem_dirmap_desc *
spi_mem_dirmap_create(struct spi_mem *mem,
		      const struct spi_mem_dirmap_info *info)
{
	struct spi_mem_dirmap_desc *desc;
	int ret = -EOPNOTSUPP;

	/* Make sure the number of address cycles is between 1 and 8 bytes. */
	if (!info->op_tmpl.addr.nbytes || info->op_tmpl.addr.nbytes > 8)
		return ERR_PTR(-EINVAL);

	/* data.dir should either be SPI_MEM_DATA_IN or SPI_MEM_DATA_OUT. */
	if (info->op_tmpl.data.dir == SPI_MEM_NO_DATA)
		return ERR_PTR(-EINVAL);

	desc = calloc(1, sizeof(*desc));
	if (!desc)
		return ERR_PTR(-ENOMEM);

	desc->mem = mem;
	desc->info = *info;
	if (mem->ops->dirmap_create)
		ret = mem->ops->dirmap_create(desc);

	if (ret) {
		desc->nodirmap = true;
		if (!spi_mem_supports_op(desc->mem, &desc->info.op_tmpl))
			ret = -EOPNOTSUPP;
		else
			ret = 0;
	}

	if (ret) {
		free(desc);
		return ERR_PTR(ret);
	}

	return desc;
}

/**
 * spi_mem_dirmap_destroy() - Destroy a direct mapping descriptor
 * @desc: the direct mapping descriptor to destroy
 *
 * This function destroys a direct mapping descriptor previously created by
 * spi_mem_dirmap_create().
 */
void spi_mem_dirmap_destroy(struct spi_mem_dirmap_desc *desc)
{
	if (!desc->nodirmap && desc->mem->ops->dirmap_destroy)
		desc->mem->ops->dirmap_destroy(desc);

	free(desc);
}

/**
 * spi_mem_dirmap_read() - Read data through a direct mapping
 * @desc: direct mapping descriptor
 * @offs: offset to start reading from. Note that this is not an absolute
 *	  offset, but the offset within the direct mapping which already has
 *	  its own offset
 * @len: length in bytes
 * @buf: destination buffer. This buffer must be DMA-able
 *
 * This function reads data from a memory device using a direct mapping
 * previously instantiated with spi_mem_dirmap_create().
 *
 * Return: the amount of data read from the memory device or a negative error
 * code. Note that the returned size might be smaller than @len, and the caller
 * is responsible for calling spi_mem_dirmap_read() again when that happens.
 */
ssize_t spi_mem_dirmap_read(struct spi_mem_dirmap_desc *desc,
			    u64 offs, size_t len, void *buf)
{
	ssize_t ret;

	if (desc->info.op_tmpl.data.dir != SPI_MEM_DATA_IN)
		return -EINVAL;

	if (!len)
		return 0;

	if (desc->nodirmap) {
		ret = spi_mem_no_dirmap_read(desc, offs, len, buf);
	} else if (desc->mem->ops->dirmap_read) {
		ret = desc->mem->ops->dirmap_read(desc, offs, len, buf);
	} else {
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/**
 * spi_mem_dirmap_write() - Write data through a direct mapping
 * @desc: direct mapping descriptor
 * @offs: offset to start writing from. Note that this is not an absolute
 *	  offset, but the offset within the direct mapping which already has
 *	  its own offset
 * @len: length in bytes
 * @buf: source buffer. This buffer must be DMA-able
 *
 * This function writes data to a memory device using a direct mapping
 * previously instantiated with spi_mem_dirmap_create().
 *
 * Return: the amount of data written to the memory device or a negative error
 * code. Note that the returned size might be smaller than @len, and the caller
 * is responsible for calling spi_mem_dirmap_write() again when that happens.
 */
ssize_t spi_mem_dirmap_write(struct spi_mem_dirmap_desc *desc,
			     u64 offs, size_t len, const void *buf)
{
	ssize_t ret;

	if (desc->info.op_tmpl.data.dir != SPI_MEM_DATA_OUT)
		return -EINVAL;

	if (!len)
		return 0;

	if (desc->nodirmap) {
		ret = spi_mem_no_dirmap_write(desc, offs, len, buf);
	} else if (desc->mem->ops->dirmap_write) {
		ret = desc->mem->ops->dirmap_write(desc, offs, len, buf);
	} else {
		ret = -EOPNOTSUPP;
	}

	return ret;
}

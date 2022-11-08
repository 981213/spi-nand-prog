#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libusb-1.0/libusb.h>
#include <spi.h>
#include <spi-mem.h>

#define FX2_BUF_SIZE 512
#define FX2_VID 0x1209
#define FX2_PID 0x0001
#define FX2_EPOUT (2 | LIBUSB_ENDPOINT_OUT)
#define FX2_EPIN (6 | LIBUSB_ENDPOINT_IN)
#define FX2_MAX_TRANSFER 0xfc0000

#define FX2QSPI_CS 0x80
#define FX2QSPI_QUAD 0x40
#define FX2QSPI_DUAL 0x20
#define FX2QSPI_READ 0x10

static u8 fx2_op_buffer[FX2_BUF_SIZE];
typedef struct {
	libusb_context *ctx;
	libusb_device_handle *handle;
} fx2qspi_priv;

static fx2qspi_priv _priv;

static int fx2qspi_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	if (op->data.nbytes > FX2_MAX_TRANSFER)
		op->data.nbytes = FX2_MAX_TRANSFER;
	return 0;
}

static void fx2qspi_fill_op(u8 buswidth, bool is_read, u16 len, size_t *ptr)
{

	if (buswidth == 4)
		fx2_op_buffer[*ptr] = FX2QSPI_CS | FX2QSPI_QUAD;
	else if (buswidth == 2)
		fx2_op_buffer[*ptr] = FX2QSPI_CS | FX2QSPI_DUAL;
	else
		fx2_op_buffer[*ptr] = FX2QSPI_CS;
	if (is_read)
		fx2_op_buffer[*ptr] |= FX2QSPI_READ;
	fx2_op_buffer[(*ptr)++] |= ((len >> 8) & 0xff);
	fx2_op_buffer[(*ptr)++] = len & 0xff;
}

static int fx2qspi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	size_t ptr = 0;
	int i, llen, alen, ret;
	fx2qspi_priv *priv = spi_mem_get_drvdata(mem);
	fx2qspi_fill_op(op->cmd.buswidth, false, 1, &ptr);
	fx2_op_buffer[ptr++] = op->cmd.opcode;
	if (op->addr.nbytes) {
		fx2qspi_fill_op(op->addr.buswidth, false, op->addr.nbytes,
				&ptr);
		for (i = op->addr.nbytes - 1; i >= 0; i--)
			fx2_op_buffer[ptr++] = (op->addr.val >> (i * 8)) & 0xff;
	}
	if (op->dummy.nbytes) {
		fx2qspi_fill_op(op->dummy.buswidth, false, op->dummy.nbytes,
				&ptr);
		for (i = 0; i < op->dummy.nbytes; i++)
			fx2_op_buffer[ptr++] = 0;
	}
	if (op->data.nbytes) {
		fx2qspi_fill_op(op->data.buswidth,
				op->data.dir == SPI_MEM_DATA_IN,
				op->data.nbytes, &ptr);
	}

	ret = libusb_bulk_transfer(priv->handle, FX2_EPOUT, fx2_op_buffer, ptr,
				   &alen, 10);
	if (ret)
		return ret;

	if (op->data.nbytes) {
		if (op->data.dir == SPI_MEM_DATA_OUT) {
			ret = libusb_bulk_transfer(priv->handle, FX2_EPOUT,
						   (unsigned char *)op->data.buf.out,
						   op->data.nbytes, &alen, 20);
			if (ret)
				return ret;
		} else if (op->data.dir == SPI_MEM_DATA_IN) {
			llen = op->data.nbytes;
			ptr = 0;
			while (llen) {
				if (llen >= FX2_BUF_SIZE)
					ret = libusb_bulk_transfer(
						priv->handle, FX2_EPIN,
						op->data.buf.in + ptr,
						FX2_BUF_SIZE, &alen, 20);
				else
					ret = libusb_bulk_transfer(
						priv->handle, FX2_EPIN,
						fx2_op_buffer, FX2_BUF_SIZE,
						&alen, 20);
				if (ret)
					return ret;
				if (llen < FX2_BUF_SIZE)
					memcpy(op->data.buf.in + ptr,
					       fx2_op_buffer, alen);
				ptr += alen;
				llen -= alen;
			}
		}
	}

	fx2_op_buffer[0] = 0;
	return libusb_bulk_transfer(priv->handle, FX2_EPOUT, fx2_op_buffer, 1,
				    &alen, 20) ?
		       -ETIMEDOUT :
		       0;
}

static const struct spi_controller_mem_ops _fx2qspi_mem_ops = {
	.adjust_op_size = fx2qspi_adjust_op_size,
	.exec_op = fx2qspi_exec_op,
};

static struct spi_mem _fx2qspi_mem = {
	.ops = &_fx2qspi_mem_ops,
	.spi_mode = SPI_TX_DUAL | SPI_TX_QUAD | SPI_RX_DUAL | SPI_RX_QUAD,
	.name = "fx2qspi",
	.drvpriv = &_priv,
};

static int fx2qspi_reset(fx2qspi_priv *priv)
{
	int i, actual_len, ret;
	memset(fx2_op_buffer, 0, sizeof(fx2_op_buffer));
	// write 4096 bytes of 0
	for (i = 0; i < 4; i++) {
		ret = libusb_bulk_transfer(priv->handle, FX2_EPOUT,
					   fx2_op_buffer, FX2_BUF_SIZE,
					   &actual_len, 5);
		if (ret)
			return ret;
	}
	// tell fx2 to send garbage data back
	fx2_op_buffer[0] = 0x60;
	fx2_op_buffer[1] = 0x60;
	ret = libusb_bulk_transfer(priv->handle, FX2_EPOUT, fx2_op_buffer, 3,
				   &actual_len, 1);
	if (ret)
		return ret;
	return libusb_bulk_transfer(priv->handle, FX2_EPIN, fx2_op_buffer,
				    FX2_BUF_SIZE, &actual_len, 1);
}

struct spi_mem *fx2qspi_probe()
{
	int ret;
	fx2qspi_priv *priv = &_priv;

	ret = libusb_init(&priv->ctx);
	if (ret < 0) {
		perror("libusb: init");
		return NULL;
	}

	libusb_set_option(priv->ctx, LIBUSB_OPTION_LOG_LEVEL,
			  LIBUSB_LOG_LEVEL_INFO);
	priv->handle =
		libusb_open_device_with_vid_pid(priv->ctx, FX2_VID, FX2_PID);
	if (!priv->handle) {
		perror("libusb: open");
		goto ERR_1;
	}

	libusb_set_auto_detach_kernel_driver(priv->handle, 1);

	ret = libusb_claim_interface(priv->handle, 0);
	if (ret < 0) {
		perror("libusb: claim_if");
		goto ERR_2;
	}

	if (fx2qspi_reset(priv))
		goto ERR_3;

	return &_fx2qspi_mem;
ERR_3:
	libusb_release_interface(priv->handle, 0);
ERR_2:
	libusb_close(priv->handle);
ERR_1:
	libusb_exit(priv->ctx);
	return NULL;
}

void fx2qspi_remove(struct spi_mem *mem)
{
	fx2qspi_priv *priv = spi_mem_get_drvdata(mem);
	libusb_release_interface(priv->handle, 0);
	libusb_close(priv->handle);
	libusb_exit(priv->ctx);
}

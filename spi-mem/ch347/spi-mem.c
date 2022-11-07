#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <spi.h>
#include <spi-mem.h>
#include "ch347.h"

static int ch347_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op) {
    size_t left_data = CH347_SPI_MAX_TRX - 1 - op->addr.nbytes - op->dummy.nbytes;
    if (op->data.nbytes > left_data)
        op->data.nbytes = left_data;
    return 0;
}

static int ch347_mem_exec_op(struct spi_mem *mem, const struct spi_mem_op *op) {
    struct ch347_priv *priv = mem->drvpriv;
    uint8_t buf[16];
    int p;
    int i, ret;

    buf[0] = op->cmd.opcode;

    if (op->addr.nbytes > 4)
        return -EINVAL;
    if (op->addr.nbytes) {
        uint32_t tmp = op->addr.val;
        for (i = op->addr.nbytes; i; i--) {
            buf[i] = tmp & 0xff;
            tmp >>= 8;
        }
    }

    p = op->addr.nbytes + 1;

    for (i = 0; i < op->dummy.nbytes; i++)
        buf[p++] = 0;

    if (sizeof(buf) - p >= op->data.nbytes) {
        ch347_set_cs(priv, 0, 0, 1);
        uint8_t *data_ptr = buf + p;
        if (op->data.dir == SPI_MEM_DATA_OUT && op->data.nbytes) {
            const uint8_t *ptr = op->data.buf.out;
            for (i = 0; i < op->data.nbytes; i++)
                buf[p++] = ptr[i];
        } else if (op->data.dir == SPI_MEM_DATA_IN && op->data.nbytes) {
            for (i = 0; i < op->data.nbytes; i++)
                buf[p++] = 0;
        }
        ret = ch347_spi_trx_full_duplex(priv, buf, p);
        if (op->data.dir == SPI_MEM_DATA_IN && op->data.nbytes) {
            uint8_t *ptr = op->data.buf.in;
            for (i = 0; i < op->data.nbytes; i++)
                ptr[i] = data_ptr[i];
        }
    } else {
        ch347_set_cs(priv, 0, 0, 0);
        ret = ch347_spi_tx(priv, buf, p);
        if (ret)
            return ret;
        if (op->data.dir == SPI_MEM_DATA_OUT && op->data.nbytes)
            ret = ch347_spi_tx(priv, op->data.buf.out, op->data.nbytes);
        else if (op->data.dir == SPI_MEM_DATA_IN && op->data.nbytes)
            ret = ch347_spi_rx(priv, op->data.buf.in, op->data.nbytes);
        ch347_set_cs(priv, 0, 1, 0);
    }


    return ret;
}

static const struct spi_controller_mem_ops ch347_mem_ops = {
        .adjust_op_size = ch347_adjust_op_size,
        .exec_op = ch347_mem_exec_op,
};

static struct spi_mem ch347_mem = {
        .ops = &ch347_mem_ops,
        .spi_mode = 0,
        .name = "ch347",
        .drvpriv = NULL,
};

struct spi_mem *ch347_probe() {
    struct ch347_priv *priv;
    int ret;
    priv = ch347_open();
    if (!priv)
        return NULL;
    ret = ch347_setup_spi(priv, 3, false, false, false);
    if (ret)
        return false;
    int freq = 30000;
    ch347_mem.drvpriv = priv;
    ret = ch347_set_spi_freq(priv, &freq);
    return ret ? NULL : &ch347_mem;
}

void ch347_remove(struct spi_mem *mem) {
    ch347_close((struct ch347_priv *) mem->drvpriv);
}
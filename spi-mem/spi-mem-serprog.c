#include <spi-mem.h>
#include <serprog.h>
#include <linux-types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <termios.h>

static int serial_fd;
u8 zero_buf[4];

static int serial_config(int fd, int speed)
{
	struct termios tty;
	if (tcgetattr(fd, &tty) != 0) {
		perror("serial: tcgetattr");
		return -1;
	}

	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);

	tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
	tty.c_cflag |= (CS8 | CLOCAL | CREAD);
	tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG | IEXTEN);
	tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | IGNCR | INLCR);
	tty.c_oflag &= ~OPOST;

	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		perror("serial: tcsetattr");
		return -1;
	}
	return 0;
}

static int serial_init(const char *devpath)
{
	int ret;

	// Use O_NDELAY to ignore DCD state
	serial_fd = open(devpath, O_RDWR | O_NOCTTY | O_NDELAY);
	if (serial_fd < 0) {
		perror("serial: open");
		return -EINVAL;
	}

	/* Ensure that we use blocking I/O */
	ret = fcntl(serial_fd, F_GETFL);
	if (ret == -1) {
		perror("serial: fcntl_getfl");
		goto ERR;
	}

	ret = fcntl(serial_fd, F_SETFL, ret & ~O_NONBLOCK);
	if (ret != 0) {
		perror("serial: fcntl_setfl");
		goto ERR;
	}

	if (serial_config(serial_fd, B4000000) != 0) {
		ret = -EINVAL;
		goto ERR;
	}
	ret = tcflush(serial_fd, TCIOFLUSH);
	if (ret != 0) {
		perror("serial: flush");
		goto ERR;
	}
	return 0;
ERR:
	close(serial_fd);
	return ret;
}

static int serprog_sync()
{
	char c;
	int ret;
	c = S_CMD_SYNCNOP;
	write(serial_fd, &c, 1);
	ret = read(serial_fd, &c, 1);
	if (ret != 1) {
		perror("serprog: sync r1");
		return -EINVAL;
	}
	if (c != S_NAK) {
		fprintf(stderr, "serprog: sync NAK failed.\n");
		return -EINVAL;
	}
	ret = read(serial_fd, &c, 1);
	if (ret != 1) {
		perror("serprog: sync r2");
		return -EINVAL;
	}
	if (c != S_ACK) {
		fprintf(stderr, "serprog: sync ACK failed.\n");
		return -EINVAL;
	}
	return 0;
}

static int serprog_check_ack()
{
	unsigned char c;
	if (read(serial_fd, &c, 1) <= 0) {
		perror("serprog: exec_op: read status");
		return errno;
	}
	if (c == S_NAK) {
		fprintf(stderr, "serprog: exec_op: NAK\n");
		return -EINVAL;
	}
	if (c != S_ACK) {
		fprintf(stderr,
			"serprog: exec_op: invalid response 0x%02X from device.\n",
			c);
		return -EINVAL;
	}
	return 0;
}

static int serprog_exec_op(u8 command, u32 parmlen, u8 *params,
		    u32 retlen, void *retparms)
{
	unsigned char c;
	if (write(serial_fd, &command, 1) < 0) {
		perror("serprog: exec_op: write cmd");
		return errno;
	}
	if (write(serial_fd, params, parmlen) < 0) {
		perror("serprog: exec_op: write param");
		return errno;
	}
	if (serprog_check_ack() < 0)
		return -EINVAL;
	if (retlen) {
		if (read(serial_fd, retparms, retlen) != retlen) {
			perror("serprog: exec_op: read return buffer");
			return 1;
		}
	}
	return 0;
}

static int serprog_set_spi_speed(u32 speed)
{
	u8 buf[4];
	int i;

	buf[0] = speed & 0xff;
	buf[1] = (speed >> (1 * 8)) & 0xff;
	buf[2] = (speed >> (2 * 8)) & 0xff;
	buf[3] = (speed >> (3 * 8)) & 0xff;

	if (serprog_exec_op(S_CMD_S_SPI_FREQ, 4, buf, 4, buf) < 0)
		return -EINVAL;

	speed = buf[0];
	speed |= buf[1] << (1 * 8);
	speed |= buf[2] << (2 * 8);
	speed |= buf[3] << (3 * 8);
	printf("serprog: SPI clock frequency is set to %u Hz.\n", speed);
	return 0;
}

static int serprog_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	size_t left_data = 0xffffff - 1 - op->addr.nbytes - op->dummy.nbytes;
	if (op->data.nbytes > left_data)
		op->data.nbytes = left_data;
	return 0;
}

static int serprog_mem_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	size_t tr_cnt = 1, i;
	u32 wrlen, rdlen, tmp;
	u8 buf[10];
	ssize_t rwdone, rwpending, rwsize;

	wrlen = 1 + op->addr.nbytes + op->dummy.nbytes;

	if (op->data.dir == SPI_MEM_DATA_OUT)
		wrlen += op->data.nbytes;
	if (op->data.dir == SPI_MEM_DATA_IN)
		rdlen = op->data.nbytes;
	else
		rdlen = 0;

	if (wrlen & 0xff000000) {
		fprintf(stderr, "serprog: too much data to send.\n");
		return -E2BIG;
	}

	if (rdlen & 0xff000000) {
		fprintf(stderr, "serprog: too much data to receive.\n");
		return -E2BIG;
	}

	buf[0] = S_CMD_O_SPIOP;
	buf[1] = wrlen & 0xff;
	buf[2] = (wrlen >> 8) & 0xff;
	buf[3] = (wrlen >> 16) & 0xff;
	buf[4] = rdlen & 0xff;
	buf[5] = (rdlen >> 8) & 0xff;
	buf[6] = (rdlen >> 16) & 0xff;

	if (write(serial_fd, buf, 7) != 7) {
		perror("serprog: spimem_exec_op: write serprog cmd");
		return errno;
	}

	buf[0] = op->cmd.opcode;
	if (write(serial_fd, buf, 1) != 1) {
		perror("serprog: spimem_exec_op: write opcode");
		return errno;
	}

	if (op->addr.nbytes > 4)
		return -EINVAL;
	if (op->addr.nbytes) {
		tmp = op->addr.val;
		for (i = op->addr.nbytes; i; i--) {
			buf[i - 1] = tmp & 0xff;
			tmp >>= 8;
		}
		if (write(serial_fd, buf, op->addr.nbytes) != op->addr.nbytes) {
			perror("serprog: spimem_exec_op: write addr");
			return errno;
		}
	}

	if (op->dummy.nbytes) {
		buf[0] = 0;
		for (i = 0; i < op->dummy.nbytes; i++) {
			if (write(serial_fd, buf, 1) != 1) {
				perror("serprog: spimem_exec_op: write dummy");
				return errno;
			}
		}
	}

	if (op->data.dir == SPI_MEM_DATA_OUT && op->data.nbytes) {
		rwpending = op->data.nbytes;
		rwdone = 0;
		while (rwpending) {
			rwsize = write(serial_fd, op->data.buf.out + rwdone, rwpending);
			if (rwsize < 0) {
				perror("serprog: spimem_exec_op: write data");
				return errno;
			}
			rwpending -= rwsize;
			rwdone += rwsize;
		}
	}

	if (serprog_check_ack() < 0)
		return -EINVAL;
	if (op->data.dir == SPI_MEM_DATA_IN && op->data.nbytes) {
		rwpending = op->data.nbytes;
		rwdone = 0;
		while (rwpending) {
			rwsize = read(serial_fd, op->data.buf.in + rwdone, rwpending);
			if (rwsize < 0) {
				perror("serprog: spimem_exec_op: read data");
				return errno;
			}
			rwpending -= rwsize;
			rwdone += rwsize;
		}
	}
	return 0;
}

static const struct spi_controller_mem_ops _serprog_mem_ops = {
	.adjust_op_size = serprog_adjust_op_size,
	.exec_op = serprog_mem_exec_op,
};

static struct spi_mem _serprog_mem = {
	.ops = &_serprog_mem_ops,
	.spi_mode = 0,
	.name = "serprog",
	.drvpriv = NULL,
};

static int serprog_init(const char *devpath, u32 speed)
{
	int ret;
	ret = serial_init(devpath);
	if (ret < 0)
		return ret;
	ret = serprog_sync();
	if (ret < 0)
		goto ERR;
	ret = serprog_set_spi_speed(speed);
	if (ret < 0)
		goto ERR;
	return 0;
ERR:
	close(serial_fd);
	return ret;
}

struct spi_mem *serprog_probe(const char *devpath)
{
	return serprog_init(devpath, 24000000) ? NULL : &_serprog_mem;
}

void serprog_remove(struct spi_mem *mem)
{
	close(serial_fd);
}
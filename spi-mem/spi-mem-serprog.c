#include <spi-mem.h>
#include <serprog.h>
#include <linux-types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>

static HANDLE hComPort;
static DCB dcbOriginal;

static int serial_config(HANDLE hDevice, int speed)
{
	DCB dcb;

	memset(&dcb, 0, sizeof(dcb));

	dcb.DCBlength = sizeof(dcb);
	if (!GetCommState(hDevice, &dcb)) {
		fprintf(stderr, "serial: failed to get port config, error = %d\n",
			GetLastError());
		return -1;
	}

	dcb.BaudRate = speed;
	dcb.ByteSize = 8;
	dcb.fParity = 0;
	dcb.StopBits = ONESTOPBIT;
	dcb.fInX = 0;
	dcb.fOutX = 0;

	if (!SetCommState(hDevice, &dcb)) {
		fprintf(stderr, "serial: failed to set port config, error = %d\n",
			GetLastError());
		return -1;
	}

	return 0;
}

static int serial_init(const char *devpath)
{
	DWORD dwErrors;
	char dev[16];
	uint32_t com;
	char *end;
	int ret;

	if (!strncmp(devpath, "\\\\.\\", 4))
		devpath += 4;

	if (strncasecmp(devpath, "COM", 3)) {
		fprintf(stderr, "serial: not a serial device path\n");
		return -EINVAL;
	}

	com = strtoul(devpath + 3, &end, 10);
	if (!com || com > 255 || *end) {
		fprintf(stderr, "serial: not a valid serial device\n");
		return -EINVAL;
	}

	/* Make sure to support COM port >= 10 */
	snprintf(dev, sizeof(dev), "\\\\.\\COM%u", com);

	hComPort = CreateFile(dev, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			      OPEN_EXISTING, 0, NULL);
	if (hComPort == INVALID_HANDLE_VALUE) {
		fprintf(stderr, "serial: failed to open port, error = %u\n",
			GetLastError());
		return -ENODEV;
	}

	memset(&dcbOriginal, 0, sizeof(dcbOriginal));

	dcbOriginal.DCBlength = sizeof(dcbOriginal);
	if (!GetCommState(hComPort, &dcbOriginal)) {
		fprintf(stderr, "serial: failed to get port config, error = %u\n",
			GetLastError());
		ret = -EIO;
		goto cleanup;
	}

	if (!SetupComm(hComPort, 1024, 1024)) {
		fprintf(stderr, "serial: failed to get port FIFO size, error = %u\n",
			GetLastError());
		ret = -EIO;
		goto cleanup;
	}

	if (serial_config(hComPort, 4000000) != 0) {
		ret = -EIO;
		goto cleanup;
	}

	if (!ClearCommError(hComPort, &dwErrors, NULL)) {
		fprintf(stderr, "serial: failed to clear port error, error = %u\n",
			GetLastError());
		ret = -EIO;
		goto cleanup;
	}

	if (!PurgeComm(hComPort, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT |
				 PURGE_TXCLEAR)) {
		fprintf(stderr, "serial: failed to flush port, error = %u\n",
			GetLastError());
		ret = -EIO;
		goto cleanup;
	}

	return 0;

cleanup:
	CloseHandle(hComPort);

	return ret;
}

static int serial_cleanup(void)
{
	if (!SetCommState(hComPort, &dcbOriginal)) {
		fprintf(stderr, "serial: failed to restore port config, error = %u\n",
			GetLastError());
	}

	CloseHandle(hComPort);

	return 0;
}

static int serial_read(void *buf, size_t len)
{
	DWORD dwBytesRead;

	if (!ReadFile(hComPort, buf, len, &dwBytesRead, NULL)) {
		fprintf(stderr, "serial: read failed, error = %u\n",
			GetLastError());
		return -EIO;
	}

	return dwBytesRead;
}

static int serial_write(const void *buf, size_t len)
{
	DWORD dwBytesWritten;

	if (!WriteFile(hComPort, buf, len, &dwBytesWritten, NULL)) {
		fprintf(stderr, "serial: write failed, error = %u\n",
			GetLastError());
		return -EIO;
	}

	FlushFileBuffers(hComPort);

	return dwBytesWritten;
}
#else
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

static int serial_fd;

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

static int serial_cleanup(void)
{
	return close(serial_fd);
}

static int serial_read(void *buf, size_t len)
{
	return read(serial_fd, buf, len);
}

static int serial_write(const void *buf, size_t len)
{
	return write(serial_fd, buf, len);
}
#endif

static int serprog_sync()
{
	char c;
	int ret;
	c = S_CMD_SYNCNOP;
	serial_write(&c, 1);
	ret = serial_read(&c, 1);
	if (ret != 1) {
		perror("serprog: sync r1");
		return -EINVAL;
	}
	if (c != S_NAK) {
		fprintf(stderr, "serprog: sync NAK failed.\n");
		return -EINVAL;
	}
	ret = serial_read(&c, 1);
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
	if (serial_read(&c, 1) <= 0) {
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
	if (serial_write(&command, 1) < 0) {
		perror("serprog: exec_op: write cmd");
		return errno;
	}
	if (serial_write(params, parmlen) < 0) {
		perror("serprog: exec_op: write param");
		return errno;
	}
	if (serprog_check_ack() < 0)
		return -EINVAL;
	if (retlen) {
		if (serial_read(retparms, retlen) != retlen) {
			perror("serprog: exec_op: read return buffer");
			return 1;
		}
	}
	return 0;
}

static int serprog_get_cmdmap(u32 *cmdmap)
{
	u8 buf[32];

	if (serprog_exec_op(S_CMD_Q_CMDMAP, 0, NULL, 32, buf) < 0)
		return -EINVAL;

	*cmdmap = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
	return 0;
}

static int serprog_set_spi_speed(u32 speed)
{
	u8 buf[4];
	u32 cmdmap = 0;
	int ret;

	ret = serprog_get_cmdmap(&cmdmap);
	if (ret < 0)
		return ret;

	if (!(cmdmap & (1 << S_CMD_S_SPI_FREQ))) {
		printf("serprog: programmer do not support set SPI clock freq.\n");
		return 0;
	}

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
	size_t i;
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

	if (serial_write(buf, 7) != 7) {
		perror("serprog: spimem_exec_op: write serprog cmd");
		return errno;
	}

	buf[0] = op->cmd.opcode;
	if (serial_write(buf, 1) != 1) {
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
		if (serial_write(buf, op->addr.nbytes) != op->addr.nbytes) {
			perror("serprog: spimem_exec_op: write addr");
			return errno;
		}
	}

	if (op->dummy.nbytes) {
		buf[0] = 0;
		for (i = 0; i < op->dummy.nbytes; i++) {
			if (serial_write(buf, 1) != 1) {
				perror("serprog: spimem_exec_op: write dummy");
				return errno;
			}
		}
	}

	if (op->data.dir == SPI_MEM_DATA_OUT && op->data.nbytes) {
		rwpending = op->data.nbytes;
		rwdone = 0;
		while (rwpending) {
			rwsize = serial_write(op->data.buf.out + rwdone, rwpending);
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
			rwsize = serial_read(op->data.buf.in + rwdone, rwpending);
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
	serial_cleanup();
	return ret;
}

struct spi_mem *serprog_probe(const char *devpath)
{
	return serprog_init(devpath, 24000000) ? NULL : &_serprog_mem;
}

void serprog_remove(struct spi_mem *mem)
{
	serial_cleanup();
}

#include <spi-mem-drvs.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <spinand.h>
#include <flashops.h>

static int no_ecc = 0;
static int with_oob = 0;
static int erase_rest = 0;
static size_t offs = 0;
static size_t length = 0;
static const char *drv = "ch347";
static const char *drvarg = NULL;
static const struct option long_opts[] = {
	{ "no-ecc", no_argument, &no_ecc, 1 },
	{ "with-oob", no_argument, &with_oob, 1 },
	{ "erase-rest", no_argument, &erase_rest, 1 },
	{ "offset", required_argument, NULL, 'o' },
	{ "length", required_argument, NULL, 'l' },
	{ "driver", required_argument, NULL, 'd' },
	{ "driver-arg", required_argument, NULL, 'a' },
	{ 0, 0, NULL, 0 },
};

int main(int argc, char *argv[])
{
	int ret = 0;
	const char *fpath = NULL;
	FILE *fp = NULL;
	char opt;
	int long_optind = 0;
	int left_argc;
	struct spinand_device *snand;
	struct spi_mem *mem;

	while ((opt = getopt_long(argc, argv, "o:l:d:a:", long_opts,
				  &long_optind)) >= 0) {
		switch (opt) {
		case 'o':
			offs = strtoul(optarg, NULL, 0);
			break;
		case 'l':
			length = strtoul(optarg, NULL, 0);
			break;
		case 'd':
			drv = optarg;
			break;
		case 'a':
			drvarg = optarg;
			break;
		case '?':
			puts("???");
			return -1;
		default:
			break;
		}
	}

	left_argc = argc - optind;
	if (left_argc < 1) {
		puts("missing action.");
		return -1;
	}

	//reuse opt here. It's now actual action.
	opt = argv[optind][0];

	switch (opt) {
	case 'r':
	case 'w':
		if (left_argc < 2) {
			puts("missing filename.");
			return -1;
		}
		fpath = argv[optind + 1];
		break;
	case 'e':
		break;
	default:
		puts("unknown operation.");
		return -1;
	}

	mem = spi_mem_probe(drv, drvarg);
	if (!mem) {
		fprintf(stderr, "device not found.\n");
		return -1;
	}

	snand = spinand_probe(mem);
	if (!snand) {
		fprintf(stderr, "unknown SPI NAND.\n");
		goto CLEANUP1;
	}
	if (fpath) {
		fp = fopen(fpath, opt == 'r' ? "wb" : "rb");
		if (!fp) {
			perror("failed to open file");
			goto CLEANUP2;
		}
	}
	switch (opt) {
	case 'r':
		snand_read(snand, offs, length, !no_ecc, with_oob, fp);
		break;
	case 'w':
		snand_write(snand, offs, !no_ecc, with_oob, erase_rest, fp, 0,
			    0, 0, 0);
		break;
	case 'e':
		snand_write(snand, offs, false, false, true, NULL, 0, 0, 0, 0);
		break;
	}
	fclose(fp);

CLEANUP2:
	spinand_remove(snand);
CLEANUP1:
	spi_mem_remove(drv, mem);
	return ret;
}

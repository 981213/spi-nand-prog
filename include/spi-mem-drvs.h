#pragma once
#include <spi-mem.h>

struct spi_mem *spi_mem_probe(const char *drv, const char *drvarg);
void spi_mem_remove(const char *drv, struct spi_mem *mem);
struct spi_mem *fx2qspi_probe();
void fx2qspi_remove(struct spi_mem *mem);
struct spi_mem *serprog_probe(const char *devpath);
void serprog_remove(struct spi_mem *mem);

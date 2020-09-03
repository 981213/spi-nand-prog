#pragma once
#include <spi-mem.h>

struct spi_mem *fx2qspi_probe();
void fx2qspi_remove(struct spi_mem *mem);

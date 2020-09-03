#include <spi-mem-drvs.h>
#include <stdio.h>
#include <string.h>

struct spi_mem *spi_mem_probe(const char *drv, const char *drvarg)
{
	if(!strcmp(drv, "fx2qspi"))
		return fx2qspi_probe();
	if(!strcmp(drv, "serprog"))
		return serprog_probe(drvarg);
	return NULL;
}

void spi_mem_remove(const char *drv, struct spi_mem *mem)
{
	if(!strcmp(drv, "fx2qspi"))
		return fx2qspi_remove(mem);
	if(!strcmp(drv, "serprog"))
		return serprog_remove(mem);
}

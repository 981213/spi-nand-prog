cmake_minimum_required(VERSION 3.1)
set(EXE_NAME spi-nand-prog)
project(${EXE_NAME} C)
find_package(PkgConfig)
pkg_check_modules(libusb-1.0 REQUIRED libusb-1.0)

include_directories(include ${libusb-1.0_INCLUDEDIR})

set(SPI_MEM_SRCS
	spi-mem/spi-mem.c
	spi-mem/spi-mem-drvs.c
	spi-mem/spi-mem-fx2qspi.c
	spi-mem/spi-mem-serprog.c
	spi-mem/ch347/ch347.c
	spi-mem/ch347/spi-mem.c
)

set(SPI_NAND_SRCS
	spi-nand/core.c
	spi-nand/gigadevice.c
	spi-nand/macronix.c
	spi-nand/micron.c
	spi-nand/paragon.c
	spi-nand/toshiba.c
	spi-nand/winbond.c
)
add_executable(${EXE_NAME} ${SPI_MEM_SRCS} ${SPI_NAND_SRCS} main.c flashops.c)
target_link_libraries(${EXE_NAME} ${libusb-1.0_LIBRARIES})

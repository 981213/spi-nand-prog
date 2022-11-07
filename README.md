# SPI-NAND Programmer

## About

A SPI-NAND flash programmer software botched together using SPI-MEM and SPI-NAND framework taken from Linux v5.8.

## Features

* Reading/Writing SPI NAND
* Operations with on-die ECC enabled/disabled
* Operations with OOB data included or not
* Skip bad blocks during writing
* Data verification for writing when on-die ECC is enabled

## Supported devices

[WCH CH347](https://www.wch.cn/products/CH347.html)

The default driver. No extra arguments needed. 

[dword1511/stm32-vserprog](https://github.com/dword1511/stm32-vserprog)

add the following arguments to select this driver:

```
-d serprog -a /dev/ttyACM0
```

## Usage
```
spi-nand-prog <operation> [file name] [arguments]

Operations: read/write/erase
Arguments:
 -d <driver>: hardware driver to be used.
 -a <arg>: additional argument provided to current driver.
 -o <offset>: Flash offset. Should be aligned to page boundary when reading and block boundary when writing. default: 0
 -l <length>: read length. default: flash_size
 --no-ecc: disable on-die ECC. This also disables data verification when writing.
 --with-oob: include OOB data during operation.
```

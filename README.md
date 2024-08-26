# `ftdiflash` (w/ [@mikegoelzer](https://github.com/mikegoelzer)'s modifications)

Simple SPI flash programmer for use with FTDI USB devices. Modified from original Adafruit repo to use ADBUS3 as CS pin (instead of ADBUS4 as iceprog does). This is the normal usage of FTDI USB MPSSE mode for SPI.

See [Adafruit's original guide](https://learn.adafruit.com/programming-spi-flash-prom-with-an-ft232h-breakout/overview) for their FT232H breakout board.

# Build

```
make
sudo make install    # writes ftdiflash to /usr/local/bin
```

# Usage

```
# Read first 512 bytes of flash to null.bin (from first interface 'A' on FTDI chip)
sudo ftdiflash -I A -d i:0x0403:0x6014 -v -R 512 -o 0 null.bin
```

# Wiring

(Specific to [@mikegoelzer](https://github.com/mikegoelzer)'s FT4232H breakout board.)

```
ADBUS0 -> SCK (white wire); output
ADBUS1 -> DO/MOSI (orange wire); output
ADBUS2 -> DI/MISO (yellow wire); input
ADBUS3 -> CS (green wire); output
```

# Acknowledgements

This is a modified version of the [iceprog](https://github.com/cliffordwolf/icestorm) tool from the excellent icestorm FPGA toolchain by [Claire Xenia Wolf](https://github.com/cliffordwolf).

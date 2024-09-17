# `ftdiflash` (w/ [@mikegoelzer](https://github.com/mikegoelzer)'s modifications)

Simple SPI flash programmer for use with FTDI USB devices. Modified from original Adafruit repo to use ADBUS3 as CS pin (instead of ADBUS4 as iceprog does). This is the normal usage of FTDI USB MPSSE mode for SPI.

See [Adafruit's original guide](https://learn.adafruit.com/programming-spi-flash-prom-with-an-ft232h-breakout/overview) for their FT232H breakout board.

# Build

```
make
sudo make install    # writes ftdiflash to /usr/local/bin
```

**Pro-tip**: don't install `libftdi1-dev` and if you have already, do `sudo apt remove libftdi1-dev`

# Usage

### List FTDI Devices

```
sudo ftdiflash -l
```

### Reading

Read first 512 bytes from an **FT232H** (which has only one interface) and write them to `out.bin`:
```
sudo ftdiflash -I A -d i:0x0403:0x6014 -v -R 512 -o 0 out.bin
```

(replace with `/dev/null` if you don't want to save the output)

# Read first 512 bytes of from second interface (`B`) from an **FT4232H**:

```
sudo ftdiflash -I B -d i:0x0403:0x6011 -v -R 512 -o 0 null.bin
```

### Writing

Generate and the file `write.bin` (from `write.txt` ASCII source) to the second interface (`B`) of an **FT4232H** starting at the 0th byte:

```
# generate: write.txt -> write.bin (256 bytes)
xxd -r -p write.txt > write.bin

# note: automatically first erases the 64k sector starting at 0th byte
sudo ftdiflash -I B -d i:0x0403:0x6011 -v -o 0 write.bin

# print the just written bytes back to the terminal
sudo ftdiflash -I B -d i:0x0403:0x6011 -v -R 256 -o 0 /dev/null
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

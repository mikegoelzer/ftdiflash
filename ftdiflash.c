/*
 *	ftdiflash
 *  modified version of iceprog -- simple programming tool for FTDI-based Lattice iCE programmers
 *
 *  Copyright (C) 2015  Clifford Wolf <clifford@clifford.at>
 *	Modified 2017 Dean Miller <dean@adafruit.com>
 *  
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#define _GNU_SOURCE

#include <ftdi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

struct ftdi_context ftdic;
bool ftdic_open = false;
bool verbose = false;
bool ftdic_latency_set = false;
unsigned char ftdi_latency;
void flash_power_up();

void check_rx()
{
	while (1) {
		uint8_t data;
		int rc = ftdi_read_data(&ftdic, &data, 1);
		if (rc <= 0) break;
		fprintf(stderr, "unexpected rx byte: %02X\n", data);
	}
}

void error()
{
	check_rx();
	fprintf(stderr, "ABORT.\n");
	flash_power_up(); // allow spi flash chip to come out of EBh mode lockup
	if (ftdic_open) {
		if (ftdic_latency_set)
			ftdi_set_latency_timer(&ftdic, ftdi_latency);
		ftdi_usb_close(&ftdic);
	}
	ftdi_deinit(&ftdic);
	exit(1);
}

uint8_t recv_byte()
{
	uint8_t data;
	while (1) {
		int rc = ftdi_read_data(&ftdic, &data, 1);
		if (rc < 0) {
			fprintf(stderr, "Read error.\n");
			error();
		}
		if (rc == 1)
			break;
		usleep(100);
	}
	return data;
}

void send_byte(uint8_t data)
{
	int rc = ftdi_write_data(&ftdic, &data, 1);
	if (rc != 1) {
		fprintf(stderr, "Write error (single byte, rc=%d, expected %d).\n", rc, 1);
		error();
	}
}

void send_spi(uint8_t *data, int n)
{
	if (n < 1)
		return;

	send_byte(0x11);
	send_byte(n-1);
	send_byte((n-1) >> 8);

	int rc = ftdi_write_data(&ftdic, data, n);
	if (rc != n) {
		fprintf(stderr, "Write error (chunk, rc=%d, expected %d).\n", rc, n);
		error();
	}
}

void xfer_spi(uint8_t *data, int n)
{
	if (n < 1)
		return;

	send_byte(0x31);
	send_byte(n-1);
	send_byte((n-1) >> 8);

	int rc = ftdi_write_data(&ftdic, data, n);
	if (rc != n) {
		fprintf(stderr, "Write error (chunk, rc=%d, expected %d).\n", rc, n);
		error();
	}

	for (int i = 0; i < n; i++)
		data[i] = recv_byte();
}

void set_gpio(int slavesel_b, int creset_b)
{
	uint8_t gpio = 1;

	if (slavesel_b) {
		// MODIFICATION: 
		//   - don't use non-standard ADBUS4 as CS as iceprog does
		//   - instead use normal CS on ADBUS3 below
		//   - see FTDI AN_135 FTDI MPSSE Basics Version 1.1 (p. 19) for
		//     example (https://www.ftdichip.com/Support/Documents/AppNotes/AN_135_MPSSE_Basics.pdf)

		// ADBUS3 (normal CS)
		gpio |= 0x08;

		// uncomment to revert to ADBUS4 as CS (as iceprog does)
		// ADBUS4 (GPIOL0)
		// gpio |= 0x10;
	}

	if (creset_b) {
		// ADBUS7 (GPIOL3)
		gpio |= 0x80;
	}

	send_byte(0x80); // set low byte
	send_byte(gpio); // gpio pin state of low byte
	//send_byte(0x93); // direction of low byte pins
	send_byte(0x9B); // 93->9B: also set ADBUS3 dir bit to output as well
}

void flash_read_id()
{
	fprintf(stderr, "read flash ID..\n");

	uint8_t data[21] = { 0x9F };
	set_gpio(0, 0);
	xfer_spi(data, 4);
	set_gpio(1, 0);

	fprintf(stderr, "flash ID:");
	for (int i = 1; i < 4; i++)
		fprintf(stderr, " 0x%02X", data[i]);
	fprintf(stderr, "\n");
}

void flash_power_up()
{
	uint8_t data[1] = { 0xAB };
	set_gpio(0, 0);
	xfer_spi(data, 1);
	set_gpio(1, 0);
}

void flash_power_down()
{
	uint8_t data[1] = { 0xB9 };
	set_gpio(0, 0);
	xfer_spi(data, 1);
	set_gpio(1, 0);
}

void flash_write_enable()
{
	if (verbose)
		fprintf(stderr, "write enable..\n");

	uint8_t data[1] = { 0x06 };
	set_gpio(0, 0);
	xfer_spi(data, 1);
	set_gpio(1, 0);
}

void flash_bulk_erase()
{
	fprintf(stderr, "bulk erase..\n");

	uint8_t data[1] = { 0xc7 };
	set_gpio(0, 0);
	xfer_spi(data, 1);
	set_gpio(1, 0);
}

void flash_64kB_sector_erase(int addr)
{
	fprintf(stderr, "erase 64kB sector at 0x%06X..\n", addr);

	uint8_t command[4] = { 0xd8, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };
	set_gpio(0, 0);
	send_spi(command, 4);
	set_gpio(1, 0);
}

void flash_prog(int addr, uint8_t *data, int n)
{
	if (verbose)
		fprintf(stderr, "prog 0x%06X +0x%03X..\n", addr, n);

	uint8_t command[4] = { 0x02, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };
	set_gpio(0, 0);
	send_spi(command, 4);
	send_spi(data, n);
	set_gpio(1, 0);

	if (verbose)
		for (int i = 0; i < n; i++)
			fprintf(stderr, "%02x%c", data[i], i == n-1 || i % 32 == 31 ? '\n' : ' ');
}

void flash_read(int addr, uint8_t *data, int n)
{
	if (verbose)
		fprintf(stderr, "read 0x%06X +0x%03X..\n", addr, n);

	uint8_t command[4] = { 0x03, (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr };
	set_gpio(0, 0);
	send_spi(command, 4);
	memset(data, 0, n);
	xfer_spi(data, n);
	set_gpio(1, 0);

	if (verbose)
		for (int i = 0; i < n; i++)
			fprintf(stderr, "%02x%c", data[i], i == n-1 || i % 32 == 31 ? '\n' : ' ');
}

void flash_wait()
{
	if (verbose)
		fprintf(stderr, "waiting..");

	while (1)
	{
		uint8_t data[2] = { 0x05 };

		set_gpio(0, 0);
		xfer_spi(data, 2);
		set_gpio(1, 0);

		if ((data[1] & 0x01) == 0)
			break;

		if (verbose) {
			fprintf(stderr, ".");
			fflush(stdout);
		}
		usleep(1000);
	}

	if (verbose)
		fprintf(stderr, "\n");
}

void help(const char *progname)
{
	fprintf(stderr, "\n");
	fprintf(stderr, "ftdiflash -- simple programming tool for programming SPI flash with an FTDI\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Usage: %s [options] <filename>\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "    -d <device-string>\n");
	fprintf(stderr, "        use the specified USB device:\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "            d:<devicenode>                (e.g. d:002/005)\n");
	fprintf(stderr, "            i:<vendor>:<product>          (e.g. i:0x0403:0x6010)\n");
	fprintf(stderr, "            i:<vendor>:<product>:<index>  (e.g. i:0x0403:0x6010:0)\n");
	fprintf(stderr, "            s:<vendor>:<product>:<serial-string>\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -I [ABCD]\n");
	fprintf(stderr, "        connect to the specified interface on the FTDI chip\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -r\n");
	fprintf(stderr, "        read first 256 kB from flash and write to file\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -R <size_in_bytes>\n");
	fprintf(stderr, "        read the specified number of bytes from flash\n");
	fprintf(stderr, "        (append 'k' to the argument for size in kilobytes, or\n");
	fprintf(stderr, "        'M' for size in megabytes)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -o <offset_in_bytes>\n");
	fprintf(stderr, "        start address for read/write (instead of zero)\n");
	fprintf(stderr, "        (append 'k' to the argument for size in kilobytes, or\n");
	fprintf(stderr, "        'M' for size in megabytes)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -c\n");
	fprintf(stderr, "        do not write flash, only verify (check)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -b\n");
	fprintf(stderr, "        bulk erase entire flash before writing\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -n\n");
	fprintf(stderr, "        do not erase flash before writing\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -t\n");
	fprintf(stderr, "        just read the flash ID sequence\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -v\n");
	fprintf(stderr, "        verbose output\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "    -l\n");
	fprintf(stderr, "        list available FTDI devices\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Without -b or -n, ftdiflash will erase aligned chunks of 64kB in write mode.\n");
	fprintf(stderr, "This means that some data after the written data (or even before when -o is\n");
	fprintf(stderr, "used) may be erased as well.\n");
	fprintf(stderr, "\n");
	exit(1);
}

void list_devices() {
    struct ftdi_context ftdic;
    struct ftdi_device_list *devlist, *curdev;
    char manufacturer[128], description[128], serial[128];
    int i = 0;
    int vid = 0x0403;
    int pids[] = {0x6001, 0x6010, 0x6011, 0x6014, 0x6015};
    /*
     * PID 6001 for FT232B/R and FT245B/R
     * PID 6010 for FT2232D/H
     * PID 6011 for FT4232H
     * PID 6014 for FT232H
     * PID 6015 for all FT-X series
     * PID 601B/601C for FT4222H
     * PID 6040 for FT2233HP
     * PID 6041 for FT4233HP
     * PID 6042 for FT2232HP
     * PID 6043 for FT4232HP
     * PID 6044 for FT233HP
     * PID 6045 for FT232HP
     * PID 6048 for FT4232HA
     * PID 6049 for FT232RN
     */
    int num_pids = sizeof(pids) / sizeof(pids[0]);

    ftdi_init(&ftdic);
    
    printf("Available FTDI devices:\n");

    int devices_found = 0;
    for (int j = 0; j < num_pids; j++) {
        int ret = ftdi_usb_find_all(&ftdic, &devlist, vid, pids[j]);
        if (ret < 0) {
            fprintf(stderr, "ftdi_usb_find_all failed for VID:PID 0x%04x:0x%04x: %d (%s)\n", 
                    vid, pids[j], ret, ftdi_get_error_string(&ftdic));
            continue;
        }

        for (curdev = devlist; curdev != NULL; i++) {
            devices_found++;
            printf("\nDev: %d (VID:PID 0x%04x:0x%04x)\n", i, vid, pids[j]);
            if (ftdi_usb_get_strings(&ftdic, curdev->dev, manufacturer, 128, description, 128, serial, 128) < 0) {
                fprintf(stderr, "ftdi_usb_get_strings failed: %d (%s)\n", ret, ftdi_get_error_string(&ftdic));
                continue;
            }
            printf("  Manufacturer:  %s\n", manufacturer);
            printf("  Description:   %s\n", description);
            printf("  Serial:        %s\n", serial);
            printf("  CLI hint:      -d i:0x%04x:0x%04x\n\n", vid, pids[j]);
            curdev = curdev->next;
        }

        ftdi_list_free(&devlist);
    }
    
    if (devices_found == 0) {
        printf("None\n");
    }

    ftdi_deinit(&ftdic);
}

int main(int argc, char **argv)
{
	int read_size = 256 * 1024;
	int rw_offset = 0;

	bool read_mode = false;
	bool check_mode = false;
	bool bulk_erase = false;
	bool dont_erase = false;
	bool prog_sram = false;
	bool test_mode = false;
	const char *filename = NULL;
	const char *devstr = NULL;
	enum ftdi_interface ifnum = INTERFACE_A;

	int opt;
	char *endptr;
	while ((opt = getopt(argc, argv, "d:I:rR:o:cbnStvl")) != -1)
	{
		switch (opt)
		{
		case 'd':
			devstr = optarg;
			break;
		case 'I':
			if (!strcmp(optarg, "A")) ifnum = INTERFACE_A;
			else if (!strcmp(optarg, "B")) ifnum = INTERFACE_B;
			else if (!strcmp(optarg, "C")) ifnum = INTERFACE_C;
			else if (!strcmp(optarg, "D")) ifnum = INTERFACE_D;
			else help(argv[0]);
			break;
		case 'r':
			read_mode = true;
			break;
		case 'R':
			read_mode = true;
			read_size = strtol(optarg, &endptr, 0);
			if (!strcmp(endptr, "k")) read_size *= 1024;
			if (!strcmp(endptr, "M")) read_size *= 1024 * 1024;
			break;
		case 'o':
			rw_offset = strtol(optarg, &endptr, 0);
			if (!strcmp(endptr, "k")) rw_offset *= 1024;
			if (!strcmp(endptr, "M")) rw_offset *= 1024 * 1024;
			break;
		case 'c':
			check_mode = true;
			break;
		case 'b':
			bulk_erase = true;
			break;
		case 'n':
			dont_erase = true;
			break;
		case 't':
			test_mode = true;
			break;
		case 'v':
			verbose = true;
			break;
		case 'l':
			list_devices();
			return 0;
		default:
			help(argv[0]);
		}
	}

	if (read_mode + check_mode + prog_sram + test_mode > 1)
		help(argv[0]);

	if (bulk_erase && dont_erase)
		help(argv[0]);

	if (optind+1 != argc && !test_mode) {
		if (bulk_erase && optind == argc)
			filename = "/dev/null";
		else
			help(argv[0]);
	} else
		filename = argv[optind];

	// ---------------------------------------------------------
	// Initialize USB connection to FT232H
	// ---------------------------------------------------------

	fprintf(stderr, "init..\n");

	ftdi_init(&ftdic);
	ftdi_set_interface(&ftdic, ifnum);

	if (devstr != NULL) {
		if (ftdi_usb_open_string(&ftdic, devstr)) {
			fprintf(stderr, "FATAL: can't find FTDI USB device (device string %s, ifnum %d).\n", devstr, ifnum);
			exit(1);
		}
	} else {
		if (ftdi_usb_open(&ftdic, 0x0403, 0x6014)) {
			fprintf(stderr, "FATAL: can't find FTDI FT232H USB device (vedor_id 0x0403, device_id 0x6014, ifnum %d).\n", ifnum);
			exit(1);
		}
	}

	ftdic_open = true;

	if (ftdi_usb_reset(&ftdic)) {
		fprintf(stderr, "Failed to reset FTDI USB device.\n");
		exit(1);
	}

	if (ftdi_usb_purge_buffers(&ftdic)) {
		fprintf(stderr, "Failed to purge buffers on FTDI USB device.\n");
		error();
	}

	if (ftdi_get_latency_timer(&ftdic, &ftdi_latency) < 0) {
		fprintf(stderr, "Failed to get latency timer (%s).\n", ftdi_get_error_string(&ftdic));
		error();
	}

	/* 1 is the fastest polling, it means 1 kHz polling */
	if (ftdi_set_latency_timer(&ftdic, 1) < 0) {
		fprintf(stderr, "Failed to set latency timer (%s).\n", ftdi_get_error_string(&ftdic));
		error();
	}

	ftdic_latency_set = true;

	if (ftdi_set_bitmode(&ftdic, 0xff, BITMODE_MPSSE) < 0) {
		fprintf(stderr, "Failed set BITMODE_MPSSE on FTDI USB device.\n");
		error();
	}

	// enable clock divide by 5
	send_byte(0x8b);

	// set 6 MHz clock
	send_byte(0x86);
	send_byte(0x00);
	send_byte(0x00);

	set_gpio(1, 1);
	usleep(100000);


	if (test_mode)
	{
		fprintf(stderr, "reset..\n");

		set_gpio(1, 0);
		usleep(250000);

		flash_power_up();

		flash_read_id();

		flash_power_down();

		set_gpio(1, 1);
		usleep(250000);

		flash_power_up(); // allow spi flash chip to come out of EBh mode lockup
	}
	else
	{
		// ---------------------------------------------------------
		// Reset
		// ---------------------------------------------------------

		fprintf(stderr, "reset..\n");

		set_gpio(1, 0);
		usleep(250000);


		flash_power_up();

		flash_read_id();


		// ---------------------------------------------------------
		// Program
		// ---------------------------------------------------------

		if (!read_mode && !check_mode)
		{
			FILE *f = (strcmp(filename, "-") == 0) ? stdin :
				fopen(filename, "rb");
			if (f == NULL) {
				fprintf(stderr, "Error: Can't open '%s' for reading: %s\n", filename, strerror(errno));
				error();
			}

			if (!dont_erase)
			{
				if (bulk_erase)
				{
					flash_write_enable();
					flash_bulk_erase();
					flash_wait();
				}
				else
				{
					struct stat st_buf;
					if (stat(filename, &st_buf)) {
						fprintf(stderr, "Error: Can't stat '%s': %s\n", filename, strerror(errno));
						error();
					}

					fprintf(stderr, "file size: %d\n", (int)st_buf.st_size);

					int begin_addr = rw_offset & ~0xffff;
					int end_addr = (rw_offset + (int)st_buf.st_size + 0xffff) & ~0xffff;

					for (int addr = begin_addr; addr < end_addr; addr += 0x10000) {
						flash_write_enable();
						flash_64kB_sector_erase(addr);
						flash_wait();
					}
				}
			}

			fprintf(stderr, "programming..\n");
			
			for (int rc, addr = 0; true; addr += rc) {
				uint8_t buffer[256];
				int page_size = 256 - (rw_offset + addr) % 256;
				rc = fread(buffer, 1, page_size, f);
				if (rc <= 0) break;
				flash_write_enable();
				flash_prog(rw_offset + addr, buffer, rc);
				flash_wait();
			}

			if (f != stdin)
				fclose(f);
		}


		// ---------------------------------------------------------
		// Read/Verify
		// ---------------------------------------------------------

		if (read_mode)
		{
			FILE *f = (strcmp(filename, "-") == 0) ? stdout :
				fopen(filename, "wb");
			if (f == NULL) {
				fprintf(stderr, "Error: Can't open '%s' for writing: %s\n", filename, strerror(errno));
				error();
			}

			fprintf(stderr, "reading..\n");
			for (int addr = 0; addr < read_size; addr += 256) {
				uint8_t buffer[256];
				flash_read(rw_offset + addr, buffer, 256);
				fwrite(buffer, 256, 1, f);
			}

			if (f != stdout)
				fclose(f);
		}
		else
		{
			FILE *f = (strcmp(filename, "-") == 0) ? stdin :
				fopen(filename, "rb");
			if (f == NULL) {
				fprintf(stderr, "Error: Can't open '%s' for reading: %s\n", filename, strerror(errno));
				error();
			}

			fprintf(stderr, "reading..\n");
			for (int addr = 0; true; addr += 256) {
				uint8_t buffer_flash[256], buffer_file[256];
				int rc = fread(buffer_file, 1, 256, f);
				if (rc <= 0) break;
				flash_read(rw_offset + addr, buffer_flash, rc);
				if (memcmp(buffer_file, buffer_flash, rc)) {
					fprintf(stderr, "Found difference between flash and file!\n");
					error();
				}
			}

			fprintf(stderr, "VERIFY OK\n");

			if (f != stdin)
				fclose(f);
		}


		// ---------------------------------------------------------
		// Reset
		// ---------------------------------------------------------

		flash_power_down();

		set_gpio(1, 1);
		usleep(250000);

		flash_power_up(); // bring spi flash out of EBh mode lockup
	}


	// ---------------------------------------------------------
	// Exit
	// ---------------------------------------------------------

	fprintf(stderr, "Bye.\n");
	ftdi_set_latency_timer(&ftdic, ftdi_latency);
	ftdi_disable_bitbang(&ftdic);
	ftdi_usb_close(&ftdic);
	ftdi_deinit(&ftdic);
	return 0;
}


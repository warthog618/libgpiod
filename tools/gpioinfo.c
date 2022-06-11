// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <gpiod.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools-common.h"

static const struct option longopts[] = {
	{ "help",	no_argument,	NULL,	'h' },
	{ "version",	no_argument,	NULL,	'v' },
	{ GETOPT_NULL_LONGOPT },
};

static const char *const shortopts = "+hv";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] <gpiochip1> ...\n", get_progname());
	printf("\n");
	printf("Print information about all lines of the specified GPIO chip(s) (or all gpiochips if none are specified).\n");
	printf("\n");
	printf("Options:\n");
	printf("  -h, --help:\t\tdisplay this message and exit\n");
	printf("  -v, --version:\tdisplay the version and exit\n");
}

static void list_lines(struct gpiod_chip *chip)
{
	struct gpiod_chip_info *chip_info;
	struct gpiod_line_info *info;
	size_t offset, num_lines;

	chip_info = gpiod_chip_get_info(chip);
	if (!chip_info)
		die_perror("unable to retrieve the chip info from chip");

	num_lines = gpiod_chip_info_get_num_lines(chip_info);
	printf("%s - %zu lines:\n",
	       gpiod_chip_info_get_name(chip_info), num_lines);

	for (offset = 0; offset < num_lines; offset++) {
		info = gpiod_chip_get_line_info(chip, offset);
		if (!info)
			die_perror("unable to retrieve the line info from chip");
		print_line_info(info);
		gpiod_line_info_free(info);
	}
	gpiod_chip_info_free(chip_info);
}

int main(int argc, char **argv)
{
	int num_chips, i, optc, opti;
	struct gpiod_chip *chip;
	struct dirent **entries;

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'h':
			print_help();
			return EXIT_SUCCESS;
		case 'v':
			print_version();
			return EXIT_SUCCESS;
		case '?':
			die("try %s --help", get_progname());
		default:
			abort();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		num_chips = scandir("/dev/", &entries,
				    chip_dir_filter, alphasort);
		if (num_chips < 0)
			die_perror("unable to scan /dev");

		for (i = 0; i < num_chips; i++) {
			chip = chip_open_by_name(entries[i]->d_name);
			if (!chip)
				die_perror("unable to open %s",
					   entries[i]->d_name);

			list_lines(chip);

			gpiod_chip_close(chip);
			free(entries[i]);
		}
		free(entries);
	} else {
		for (i = 0; i < argc; i++) {
			chip = chip_open_lookup(argv[i]);
			if (!chip)
				die_perror("looking up chip %s", argv[i]);

			list_lines(chip);

			gpiod_chip_close(chip);
		}
	}

	return EXIT_SUCCESS;
}

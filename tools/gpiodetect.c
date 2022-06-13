// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <errno.h>
#include <getopt.h>
#include <gpiod.h>
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
	printf("Usage: %s [OPTIONS] [chip]...\n", get_progname());
	printf("\n");
	printf("List GPIO chips, print their labels and number of GPIO lines.\n");
	printf("\n");
	printf("Chips may be identified by number, name, or path.\n");
	printf("e.g. '0', 'gpiochip0', and '/dev/gpiochip0' all refer to the same chip.\n");
	printf("\n");
	printf("If no chips are specified then all chips are listed.\n");
	printf("\n");
	printf("Options:\n");
	printf("  -h, --help            display this help and exit\n");
	printf("  -v, --version         output version information and exit\n");
}

int parse_config(int argc, char **argv)
{
	int optc, opti;

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'h':
			print_help();
			exit(EXIT_SUCCESS);
		case 'v':
			print_version();
			exit(EXIT_SUCCESS);
		case '?':
			die("try %s --help", get_progname());
		default:
			abort();
		}
	}
	return optind;
}

void print_chip_info(const char * path)
{
	struct gpiod_chip *chip;
	struct gpiod_chip_info *info;

	chip = gpiod_chip_open(path);
	if (!chip) {
		print_perror("unable to open chip %s", path);
		return;
	}

	info = gpiod_chip_get_info(chip);
	if (!info)
		die_perror("unable to get info for %s", path);

	printf("%s [%s] (%zu lines)\n",
	       gpiod_chip_info_get_name(info),
	       gpiod_chip_info_get_label(info),
	       gpiod_chip_info_get_num_lines(info));

	gpiod_chip_info_free(info);
	gpiod_chip_close(chip);
}

int main(int argc, char **argv)
{
	int num_chips, i;
	char **paths;
	char * path;
	int ret = EXIT_SUCCESS;

	i = parse_config(argc, argv);
	argc -= i;
	argv += i;

	if (argc == 0) {
		num_chips = all_chip_paths(&paths);
		for (i = 0; i < num_chips; i++) {
			print_chip_info(paths[i]);
			free(paths[i]);
		}
		free(paths);
	}
	for (i = 0; i < argc; i++) {
		if (chip_path_lookup(argv[i], &path)) {
			print_chip_info(path);
			free(path);
		} else if (errno == ENOENT) {
			print_error("cannot find a GPIO chip character device corresponding to %s",
				    argv[i]);
			ret = EXIT_FAILURE;
		} else {
			print_perror("unable to open chip %s", path);
			ret = EXIT_FAILURE;
		}
	}

	return ret;
}

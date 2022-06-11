// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2022 Bartosz Golaszewski <bartekgola@gmail.com>

#include <errno.h>
#include <getopt.h>
#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools-common.h"

static const struct option longopts[] = {
	{ "chip",	required_argument,	NULL,	'c' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "info",	no_argument,		NULL,	'i' },
	{ "strict",	no_argument,		NULL,	's' },
	{ "version",	no_argument,		NULL,	'v' },
	{ GETOPT_NULL_LONGOPT },
};

static const char *const shortopts = "+c:hisv";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] <name>\n", get_progname());
	printf("\n");
	printf("Find a GPIO line by name.\n");
	printf("\n");
	printf("The output of this command can be used as input for gpioget/set.\n");
	printf("\n");
	printf("Options:\n");
	printf("  -c, --chip <chip>\trestrict scope to a particular chip\n");
	printf("  -h, --help\t\tdisplay this help and exit\n");
	printf("  -i, --info\t\tdisplay info for found lines\n");
	printf("  -s, --strict\t\tcheck all lines - don't assume line names are unique\n");
	printf("  -v, --version\t\toutput version information and exit\n");
	print_chip_help();
}

struct config {
	bool strict;
	bool display_info;
	const char *chip_id;
};

int parse_config(int argc, char **argv, struct config *cfg)
{
	int opti, optc;

	memset(cfg, 0, sizeof(*cfg));

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'c':
			cfg->chip_id = optarg;
			break;
		case 'i':
			cfg->display_info = true;
			break;
		case 's':
			cfg->strict = true;
			break;
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

int main(int argc, char **argv)
{
	int i, num_chips, num_lines, offset, num_found = 0, ret = EXIT_FAILURE;
	struct gpiod_chip *chip;
	struct gpiod_chip_info *chip_info;
	char **paths;
	const char *name;
	struct gpiod_line_info *line_info;
	struct config cfg;

	i = parse_config(argc, argv, &cfg);
	argc -= optind;
	argv += optind;

	if (argc != 1)
		die("exactly one GPIO line name must be specified");

	num_chips = chip_paths(cfg.chip_id, &paths);
	if ((cfg.chip_id != NULL)  && (num_chips == 0))
		die("cannot find a GPIO chip character device corresponding to %s", cfg.chip_id);

	for (i = 0; i < num_chips; i++) {
		chip = gpiod_chip_open(paths[i]);
		if (!chip) {
			if ((errno == EACCES) && (!cfg.chip_id))
				continue;

			die_perror("unable to open %s", paths[i]);
		}

		chip_info = gpiod_chip_get_info(chip);
		if (!chip_info)
			die_perror("unable to get info for %s", paths[i]);

		num_lines = gpiod_chip_info_get_num_lines(chip_info);
		for (offset = 0; offset < num_lines; offset++) {
			line_info = gpiod_chip_get_line_info(chip, offset);
			if (!line_info)
				die_perror("unable to retrieve the line info from chip %s",
					   gpiod_chip_get_path(chip));

			name = gpiod_line_info_get_name(line_info);
			if (name && strcmp(argv[0], gpiod_line_info_get_name(line_info)) == 0) {
				num_found++;
				printf("%s %u", gpiod_chip_info_get_name(chip_info), offset);
				if (cfg.display_info)
					print_line_info(line_info);
				printf("\n");
				if (!cfg.strict) {
					gpiod_chip_info_free(chip_info);
					gpiod_chip_close(chip);
					goto exit_paths;
				}
			}
		}
		gpiod_chip_info_free(chip_info);
		gpiod_chip_close(chip);
	}
	if (!num_found)
		print_error("cannot find line %s", argv[0]);
exit_paths:
	if (num_found == 1)
		ret = EXIT_SUCCESS;
	for (i = 0; i < num_chips; i++)
		free(paths[i]);
	free(paths);
	return ret;
}

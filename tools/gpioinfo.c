// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <errno.h>
#include <getopt.h>
#include <gpiod.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools-common.h"

static int by_name = 0;

static const struct option longopts[] = {
	{ "chip",	required_argument,	NULL,	'c' },
	{ "by-name",	no_argument,		&by_name,	1 },
	{ "help",	no_argument,		NULL,	'h' },
	{ "strict",	no_argument,		NULL,	's' },
	{ "version",	no_argument,		NULL,	'v' },
	{ GETOPT_NULL_LONGOPT },
};

static const char *const shortopts = "+c:shv";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] [line] ...\n", get_progname());
	printf("\n");
	printf("Print information about the specified line(s).\n");
	printf("\n");
	printf("Lines are specified by name, or optionally by offset if the chip option\n");
	printf("is provided.\n");
	printf("\n");
	printf("If no lines are specified than all lines are displayed.\n");
	printf("\n");
	printf("Options:\n");
	print_chip_help();
	printf("      --by-name\t\ttreat lines as names even if they would parse as an offset\n");
	printf("  -s, --strict\t\tcheck all lines - don't assume line names are unique\n");
	printf("  -h, --help\t\tdisplay this message and exit\n");
	printf("  -v, --version\t\tdisplay the version and exit\n");
}

static bool filter_line(struct gpiod_line_info *info, struct line_resolver *resolver) {
	int i;
	struct resolved_line *line;
	bool skip = true;
	const char *name;

	if (!resolver)
		return false;

	for (i = 0; i < resolver->num_lines; i++) {
		line = &resolver->lines[i];
		name = gpiod_line_info_get_name(info);
		if (((line->offset == (int)gpiod_line_info_get_offset(info)) ||
		     (name && strcmp(line->id, name) == 0)) &&
		    (resolver->strict || !line->found)) {
			skip = false;
			line->found = true;
			resolver->num_found++;
		}
	}
	return skip;
}

static bool filter_done(struct line_resolver *resolver) {
	return (resolver && !resolver->strict && resolver->num_found >= resolver->num_lines);
}

static void list_lines(struct gpiod_chip *chip, struct line_resolver *resolver)
{
	struct gpiod_chip_info *chip_info;
	struct gpiod_line_info *info;
	int offset, num_lines;

	chip_info = gpiod_chip_get_info(chip);
	if (!chip_info)
		die_perror("unable to retrieve the chip info from chip %s",
			   gpiod_chip_get_path(chip));

	num_lines = gpiod_chip_info_get_num_lines(chip_info);

	for (offset = 0; ((offset < num_lines) && !filter_done(resolver)); offset++) {
		info = gpiod_chip_get_line_info(chip, offset);
		if (!info)
			die_perror("unable to retrieve the line info from chip %s",
				   gpiod_chip_get_path(chip));

		if (filter_line(info, resolver))
			continue;

		if (resolver && resolver->num_lines) {
			printf("%s %u", gpiod_chip_info_get_name(chip_info), offset);
		} else {
			if (offset == 0)
				printf("%s - %u lines:\n",
				       gpiod_chip_info_get_name(chip_info), num_lines);
			printf("\tline %3u:", offset);
		}
		print_line_info(info);
		printf("\n");
		gpiod_line_info_free(info);
	}
	gpiod_chip_info_free(chip_info);
}

int main(int argc, char **argv)
{
	int optc, opti, num_chips, i, ret = EXIT_SUCCESS;
	struct gpiod_chip *chip;
	char *chip_id = NULL;
	bool strict = false;
	char ** paths;
	struct line_resolver *resolver = NULL;

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'c':
			chip_id = optarg;
			break;
		case 's':
			strict = true;
			break;
		case 'h':
			print_help();
			return EXIT_SUCCESS;
		case 'v':
			print_version();
			return EXIT_SUCCESS;
		case '?':
			die("try %s --help", get_progname());
		case 0:
			break;
		default:
			abort();
		}
	}

	argc -= optind;
	argv += optind;

	if (chip_id == NULL)
		by_name = true;

	num_chips = chip_paths(chip_id, &paths);
	if ((chip_id != NULL)  && (num_chips == 0))
		die("cannot find a GPIO chip character device corresponding to %s", chip_id);

	if (argc) {
		resolver = malloc(sizeof(*resolver) + argc * sizeof(struct resolved_line));
		if (resolver == NULL)
			die("out of memory");
		resolver->num_lines = argc;
		resolver->num_found = 0;
		resolver->strict = strict;
		for (i = 0; i < argc; i++) {
			resolver->lines[i].id = argv[i];
			resolver->lines[i].found = false;
			resolver->lines[i].offset = by_name ? -1 : parse_uint(argv[i]);
		}
	}

	for (i = 0; i < num_chips; i++) {
		chip = gpiod_chip_open(paths[i]);
		if (!chip) {
			if ((errno != EACCES) && (chip_id != NULL)) {
				log_perror("unable to open chip %s", paths[i]);
				ret = EXIT_FAILURE;
			}
			continue;
		}
		list_lines(chip, resolver);
		gpiod_chip_close(chip);
		free(paths[i]);
	}
	free(paths);

	if (resolver) {
		for (i = 0; i < resolver->num_lines; i++) {
			if (!resolver->lines[i].found) {
				fprintf(stderr, "cannot find line %s\n", resolver->lines[i].id);
				ret = EXIT_FAILURE;
			}
		}
		if (resolver->num_lines != resolver->num_found)
			ret = EXIT_FAILURE;
		free(resolver);
	}
	return ret;
}

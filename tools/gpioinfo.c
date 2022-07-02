// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2022 Bartosz Golaszewski <bartekgola@gmail.com>

#include <getopt.h>
#include <gpiod.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools-common.h"

static int by_name;
static const struct option longopts[] = {
	{ "by-name",	no_argument,		&by_name,	1 },
	{ "chip",	required_argument,	NULL,	'c' },
	{ "help",	no_argument,		NULL,	'h' },
	{ "strict",	no_argument,		NULL,	's' },
	{ "version",	no_argument,		NULL,	'v' },
	{ GETOPT_NULL_LONGOPT },
};

static const char *const shortopts = "+c:hsv";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] [line] ...\n", get_progname());
	printf("\n");
	printf("Print information about GPIO lines.\n");
	printf("\n");
	printf("Lines are specified by name, or optionally by offset if the chip option\n");
	printf("is provided.\n");
	printf("\n");
	printf("If no lines are specified than all lines are displayed.\n");
	printf("\n");
	printf("Options:\n");
	printf("      --by-name\t\ttreat lines as names even if they would parse as an offset\n");
	printf("  -c, --chip <chip>\trestrict scope to a particular chip\n");
	printf("  -h, --help\t\tdisplay this help and exit\n");
	printf("  -s, --strict\t\tcheck all lines - don't assume line names are unique\n");
	printf("  -v, --version\t\toutput version information and exit\n");
	print_chip_help();
}

struct config {
	bool strict;
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
		case 0:
			break;
		default:
			abort();
		}
	}

	return optind;
}

static bool filter_line(struct gpiod_line_info *info, const char *chip_path,
			struct line_resolver *resolver)
{
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
		    (resolver->strict || !line->chip_path)) {
			skip = false;
			line->chip_path = chip_path;
			resolver->num_found++;
		}
	}
	return skip;
}

static bool filter_done(struct line_resolver *resolver)
{
	return (resolver && !resolver->strict && resolver->num_found >= resolver->num_lines);
}

static void list_lines(struct gpiod_chip *chip, const char *chip_path,
		       struct line_resolver *resolver)
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

		if (filter_line(info, chip_path, resolver))
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
	int num_chips, i, ret = EXIT_SUCCESS;
	struct gpiod_chip *chip;
	char **paths;
	struct line_resolver *resolver = NULL;
	struct resolved_line *line;
	struct config cfg;

	i = parse_config(argc, argv, &cfg);
	argc -= i;
	argv += i;

	if (!cfg.chip_id)
		by_name = true;

	num_chips = chip_paths(cfg.chip_id, &paths);
	if ((cfg.chip_id != NULL)  && (num_chips == 0))
		die("cannot find a GPIO chip character device corresponding to %s", cfg.chip_id);

	if (argc) {
		resolver = malloc(sizeof(*resolver) + argc * sizeof(struct resolved_line));
		if (resolver == NULL)
			die("out of memory");
		resolver->num_lines = argc;
		resolver->num_found = 0;
		resolver->strict = cfg.strict;
		for (i = 0; i < argc; i++) {
			line = &resolver->lines[i];
			line->id = argv[i];
			line->chip_path = NULL;  // doubles as found flag
			line->offset = by_name ? -1 : parse_uint(argv[i]);
			line->id_is_name = (line->offset == -1);
		}
	}

	for (i = 0; i < num_chips; i++) {
		chip = gpiod_chip_open(paths[i]);
		if (chip) {
			list_lines(chip, paths[i], resolver);
			gpiod_chip_close(chip);
		} else {
			print_perror("unable to open chip %s", paths[i]);
			ret = EXIT_FAILURE;
			if (cfg.chip_id)
				return EXIT_FAILURE;
		}
		free(paths[i]);
	}
	free(paths);

	if (resolver) {
		for (i = 0; i < resolver->num_lines; i++) {
			if (resolver->lines[i].chip_path)
				continue;
			if (cfg.chip_id && !resolver->lines[i].id_is_name)
				print_error("offset %s is out of range on chip %s",
					resolver->lines[i].id, cfg.chip_id);
			else
				print_error("cannot find line %s", resolver->lines[i].id);
			ret = EXIT_FAILURE;
		}
		if (resolver->num_lines != resolver->num_found)
			ret = EXIT_FAILURE;
		free(resolver);
	}
	return ret;
}

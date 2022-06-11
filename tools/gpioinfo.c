// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>
// SPDX-FileCopyrightText: 2022 Kent Gibson <warthog618@gmail.com>

#include <getopt.h>
#include <gpiod.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools-common.h"

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] [line]...\n", get_progname());
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
	int by_name;
};

int parse_config(int argc, char **argv, struct config *cfg)
{
	int opti, optc;
	const char *const shortopts = "+c:hsv";
	const struct option longopts[] = {
		{ "by-name",	no_argument,	&cfg->by_name,	1 },
		{ "chip",	required_argument,	NULL,	'c' },
		{ "help",	no_argument,	NULL,		'h' },
		{ "strict",	no_argument,	NULL,		's' },
		{ "version",	no_argument,	NULL,		'v' },
		{ GETOPT_NULL_LONGOPT },
	};

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


// minimal version similar to tools-common that indicates if a line should be
// printed rather than storing details into the resolver.
// Does not die on non-unique lines.
static bool resolve_line(struct line_resolver *resolver,
			  struct gpiod_line_info *info, int chip_num)
{
	struct resolved_line *line;
	const char *name;
	int i;
	unsigned int offset;
	bool resolved = false;

	offset = gpiod_line_info_get_offset(info);
	for (i = 0; i < resolver->num_lines; i++) {
		line = &resolver->lines[i];
		// already resolved by offset?
		if (line->resolved &&
		    (line->offset == offset) &&
		    (line->chip_num == chip_num)) {
			resolved = true;
		}
		if (line->resolved && !resolver->strict)
			continue;

		// else resolve by name
		name = gpiod_line_info_get_name(info);
		if (name && (strcmp(line->id, name) == 0)) {
			line->resolved = true;
			line->offset = offset;
			line->chip_num = chip_num;
			resolved = true;
		}
	}

	return resolved;
}

static void print_line_info(struct gpiod_line_info *info)
{
	const char *name;

	name = gpiod_line_info_get_name(info);
	if (!name)
		name = "unnamed";

	printf("%-16s\t", name);
	print_line_attributes(info);
}

// based on resolve_lines, but prints lines immediately rather than collecting
// details in the resolver.
static void list_lines(struct line_resolver *resolver, struct gpiod_chip *chip,
		       int chip_num, bool offsets_ok)
{
	struct gpiod_chip_info *chip_info;
	struct gpiod_line_info *info;
	int offset, num_lines;

	chip_info = gpiod_chip_get_info(chip);
	if (!chip_info)
		die_perror("unable to read info from chip %s",
			   gpiod_chip_get_path(chip));

	num_lines = gpiod_chip_info_get_num_lines(chip_info);

	if ((chip_num == 0) && offsets_ok)
		resolve_lines_by_offset(resolver, num_lines);

	for (offset = 0;
	     ((offset < num_lines) &&
	      !(resolver->num_lines && resolve_done(resolver)));
	     offset++) {
		info = gpiod_chip_get_line_info(chip, offset);
		if (!info)
			die_perror("unable to read info for line %d from %s",
				   offset, gpiod_chip_info_get_name(chip_info));

		if (resolver->num_lines &&
		    !resolve_line(resolver, info, chip_num))
			continue;

		if (resolver->num_lines) {
			printf("%s %u", gpiod_chip_info_get_name(chip_info),
			       offset);
		} else {
			if (offset == 0)
				printf("%s - %u lines:\n",
				       gpiod_chip_info_get_name(chip_info),
				       num_lines);

			printf("\tline %3u:", offset);
		}
		fputc('\t', stdout);
		print_line_info(info);
		fputc('\n', stdout);
		gpiod_line_info_free(info);
		resolver->num_found++;
	}
	gpiod_chip_info_free(chip_info);
}

int main(int argc, char **argv)
{
	struct gpiod_chip *chip;
	struct line_resolver *resolver = NULL;
	struct config cfg;
	char **paths;
	int num_chips, i, ret = EXIT_SUCCESS;
	bool offsets_ok;

	i = parse_config(argc, argv, &cfg);
	argc -= i;
	argv += i;

	if (!cfg.chip_id)
		cfg.by_name = true;

	num_chips = chip_paths(cfg.chip_id, &paths);
	if (cfg.chip_id  && (num_chips == 0))
		die("cannot find GPIO chip character device '%s'", cfg.chip_id);

	resolver = resolver_init(argc, argv, num_chips, cfg.strict,
				 cfg.by_name);

	for (i = 0; i < num_chips; i++) {
		chip = gpiod_chip_open(paths[i]);
		if (chip) {
			offsets_ok = (cfg.chip_id && !cfg.by_name);
			list_lines(resolver, chip, i, offsets_ok);
			gpiod_chip_close(chip);
		} else {
			print_perror("unable to open chip '%s'", paths[i]);

			if (cfg.chip_id)
				return EXIT_FAILURE;

			ret = EXIT_FAILURE;
		}
		free(paths[i]);
	}
	free(paths);

	validate_resolution(resolver, cfg.chip_id);
	if (argc && resolver->num_found != argc)
		ret = EXIT_FAILURE;
	free(resolver);
	return ret;
}

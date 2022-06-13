// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <getopt.h>
#include <gpiod.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tools-common.h"

static int by_name = 0;
static int numeric = 0;
static const struct option longopts[] = {
	{ "active-low",		no_argument,		NULL,	'l' },
	{ "as-is",		no_argument,		NULL,	'a' },
	{ "bias",		required_argument,	NULL,	'b' },
	{ "by-name",		no_argument,		&by_name,	1 },
	{ "chip",		required_argument,	NULL,	'c' },
	{ "help",		no_argument,		NULL,	'h' },
	{ "hold-period",	required_argument,	NULL,	'p' },
	{ "numeric",		no_argument,		&numeric,	1 },
	{ "strict",		no_argument,		NULL,	's' },
	{ "version",		no_argument,		NULL,	'v' },
	{ GETOPT_NULL_LONGOPT },
};

static const char *const shortopts = "+ab:c:hlp:sv";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] <line> ...\n", get_progname());
	printf("\n");
	printf("Read values of GPIO lines.\n");
	printf("\n");
	printf("Lines are specified by name, or optionally by offset if the chip option\n");
	printf("is provided.\n");
	printf("\n");
	printf("Options:\n");
	printf("  -a, --as-is           leave the line direction unchanged, not forced to input\n");
	print_bias_help();
	printf("      --by-name         treat lines as names even if they would parse as an offset\n");
	printf("  -c, --chip <chip>     restrict scope to a particular chip\n");
	printf("  -h, --help            display this help and exit\n");
	printf("  -l, --active-low      treat the line as active low\n");
	printf("  -p, --hold-period <period>\n");
	printf("                        apply a settling period between requesting the line(s)\n");
	printf("                        and reading the value(s)\n");
	printf("      --numeric         display line values as '0' (inactive) or '1' (active)\n");
	printf("  -s, --strict          abort if requested line names are not unique\n");
	printf("  -v, --version         output version information and exit\n");
	print_chip_help();
	print_period_help();
}
struct config {
	bool active_low;
	bool strict;
	int bias;
	int direction;
	unsigned int hold_period_us;
	const char * chip_id;
};

int parse_config(int argc, char **argv, struct config *cfg)
{
	int opti, optc;

	memset(cfg, 0, sizeof(*cfg));
	cfg->direction = GPIOD_LINE_DIRECTION_INPUT;

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'a':
			cfg->direction = GPIOD_LINE_DIRECTION_AS_IS;
			break;
		case 'b':
			cfg->bias = parse_bias_or_die(optarg);
			break;
		case 'c':
			cfg->chip_id = optarg;
			break;
		case 'l':
			cfg->active_low = true;
			break;
		case 'p':
			cfg->hold_period_us = parse_period_or_die(optarg);
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

int main(int argc, char **argv)
{
	int i, num_lines, ret, *values;
	struct gpiod_request_config *req_cfg;
	struct gpiod_line_request *request;
	struct gpiod_line_config *line_cfg;
	struct gpiod_chip *chip;
	unsigned int *offsets;
	struct line_resolver *resolver;
	struct resolved_line *line;
	const char * chip_path;
	struct config cfg;

	i = parse_config(argc, argv, &cfg);
	argc -= i;
	argv += i;

	if (argc < 1)
		die("at least one GPIO line must be specified");

	resolver = resolve_lines(argc, argv, cfg.chip_id, cfg.strict, by_name);

	offsets = calloc(resolver->num_lines, sizeof(*offsets));
	values = calloc(resolver->num_lines, sizeof(*values));
	if (!offsets || ! values)
		die("out of memory");

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		die_perror("unable to allocate the line config structure");

	gpiod_line_config_set_direction_default(line_cfg, cfg.direction);

	if (cfg.bias)
		gpiod_line_config_set_bias_default(line_cfg, cfg.bias);

	if (cfg.active_low)
		gpiod_line_config_set_active_low_default(line_cfg, true);

	req_cfg = gpiod_request_config_new();
	if (!req_cfg)
		die_perror("unable to allocate the request config structure");

	gpiod_request_config_set_consumer(req_cfg, "gpioget");
	for (i = 0; i < resolver->num_chips; i++) {
		chip_path = resolver->chip_paths[i];
		chip = gpiod_chip_open(chip_path);
		if (!chip)
			die_perror("unable to open chip %s", chip_path);
		num_lines = get_line_offsets_and_values(resolver, chip_path, offsets, NULL);
		gpiod_request_config_set_offsets(req_cfg, num_lines, offsets);

		request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
		if (!request)
			die_perror("unable to request lines");

		if (cfg.hold_period_us)
			usleep(cfg.hold_period_us);

		ret = gpiod_line_request_get_values(request, values);
		if (ret)
			die_perror("unable to read GPIO line values");

		set_line_values(resolver, chip_path, values);

		gpiod_line_request_release(request);
		gpiod_chip_close(chip);
	}
	for (i = 0;;) {
		line = &resolver->lines[i];
		if (numeric)
			printf("%d", line->value);
		else
			printf("%s=%s", line->id, line->value ? "active" : "inactive");
		i++;
		if (i == resolver->num_lines)
			break;
		printf(" ");
	}
	printf("\n");

	free_line_resolver(resolver);
	gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);
	free(offsets);
	free(values);

	return EXIT_SUCCESS;
}

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

static const char *const shortopts = "+ac:b:lp:shv";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] <line> ...\n", get_progname());
	printf("\n");
	printf("Read line value(s) from a GPIO chip.\n");
	printf("\n");
	printf("Lines are specified by name, or optionally by offset if the chip option\n");
	printf("is provided.\n");
	printf("\n");
	printf("Options:\n");
	print_chip_help();
	printf("  -a, --as-is\t\tleave the line direction unchanged, not forced to input\n");
	printf("  -l, --active-low\ttreat the line as active low\n");
	print_bias_help();
	printf("  -p, --hold-period <period>\n");
	printf("\t\t\tapply a settling period between requesting the line(s)\n");
	printf("\t\t\tand reading the value(s)\n");
	printf("      --by-name\t\ttreat lines as names even if they would parse as an offset\n");
	printf("      --numeric\t\tdisplay line values as 0 (inactive) or 1 (active)\n");
	printf("  -s, --strict\t\tabort if requested line names are not unique\n");
	printf("  -h, --help\t\tdisplay this message and exit\n");
	printf("  -v, --version\t\tdisplay the version and exit\n");
	print_period_help();
}

int main(int argc, char **argv)
{
	int direction = GPIOD_LINE_DIRECTION_INPUT;
	int optc, opti, i, num_lines, bias = 0, ret, *values;
	struct gpiod_request_config *req_cfg;
	struct gpiod_line_request *request;
	struct gpiod_line_config *line_cfg;
	struct gpiod_chip *chip;
	bool active_low = false, strict = false;
	unsigned int *offsets, hold_period_us = 0;
	struct line_resolver *resolver;
	char *chip_id = NULL;

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'a':
			direction = GPIOD_LINE_DIRECTION_AS_IS;
			break;
		case 'b':
			bias = parse_bias_or_die(optarg);
			break;
		case 'c':
			chip_id = optarg;
			break;
		case 'l':
			active_low = true;
			break;
		case 'p':
			hold_period_us = parse_period_or_die(optarg);
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

	if (argc < 1)
		die("at least one GPIO line must be specified");

	resolver = resolve_lines(argc, argv, chip_id, strict, by_name);

	offsets = calloc(resolver->num_lines, sizeof(*offsets));
	values = calloc(resolver->num_lines, sizeof(*values));
	if (!offsets || ! values)
		die("out of memory");

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		die_perror("unable to allocate the line config structure");

	gpiod_line_config_set_direction_default(line_cfg, direction);

	if (bias)
		gpiod_line_config_set_bias_default(line_cfg, bias);

	if (active_low)
		gpiod_line_config_set_active_low_default(line_cfg, true);

	req_cfg = gpiod_request_config_new();
	if (!req_cfg)
		die_perror("unable to allocate the request config structure");

	gpiod_request_config_set_consumer(req_cfg, "gpioget");
	for (i = 0; i < resolver->num_chips; i++) {
		chip = gpiod_chip_open(resolver->chip_paths[i]);
		if (!chip)
			die_perror("unable to open chip %s", resolver->chip_paths[i]);
		num_lines = get_line_offsets_and_values(resolver, i, offsets, NULL);
		gpiod_request_config_set_offsets(req_cfg, num_lines, offsets);

		request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
		if (!request)
			die_perror("unable to request lines");

		if (hold_period_us)
			usleep(hold_period_us);

		ret = gpiod_line_request_get_values(request, values);
		if (ret)
			die_perror("unable to read GPIO line values");

		set_line_values(resolver, i, values);

		gpiod_line_request_release(request);
		gpiod_chip_close(chip);
	}
	for (i = 0;;) {
		if (numeric)
			printf("%d", resolver->lines[i].value);
		else
			printf("%s=%s", resolver->lines[i].id,
			       resolver->lines[i].value ? "active" : "inactive");
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

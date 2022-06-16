// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Kent Gibson <warthog618@gmail.com>

#include <getopt.h>
#include <gpiod.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tools-common.h"

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] <line> ...\n", get_progname());
	printf("\n");
	printf("Wait for changes to info on GPIO lines and print them to standard output.\n");
	printf("\n");
	printf("Lines are specified by name, or optionally by offset if the chip option\n");
	printf("is provided.\n");
	printf("\n");
	printf("Options:\n");
	printf("      --banner\t\tdisplay a banner on successful startup\n");
	printf("      --by-name\t\ttreat lines as names even if they would parse as an offset\n");
	printf("  -c, --chip <chip>\trestrict scope to a particular chip\n");
	printf("  -h, --help\t\tdisplay this help and exit\n");
	printf("      --localtime\treport event time as a local time (default is monotonic)\n");
	printf("  -s, --strict\t\tabort if requested line names are not unique\n");
	printf("      --utc\t\treport event time as UTC (default is monotonic)\n");
	printf("  -v, --version\t\toutput version information and exit\n");
	print_chip_help();
}

struct config {
	bool strict;
	const char *chip_id;
	int by_name;
	int event_clock_mode;
	int banner;
};

int parse_config(int argc, char **argv, struct config *cfg)
{
	int opti, optc;
	static const char *const shortopts = "+c:shv";
	const struct option longopts[] = {
		{ "banner",	no_argument,	&cfg->banner,	1 },
		{ "by-name",	no_argument,	&cfg->by_name,	1 },
		{ "chip",	required_argument, NULL,	'c' },
		{ "help",	no_argument,	NULL,		'h' },
		{ "localtime",	no_argument,	&cfg->event_clock_mode,	2 },
		{ "strict",	no_argument,	NULL,		's' },
		{ "utc",	no_argument,	&cfg->event_clock_mode,	1 },
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

static void print_banner(int num_lines, char **lines)
{
	int i;

	if (num_lines > 1) {
		printf("Watching lines ");
		for (i = 0; i < num_lines - 1; i++)
			printf("%s, ", lines[i]);
		printf("and %s...\n", lines[i]);
	} else {
		printf("Watching line %s ...\n", lines[0]);
	}
}

static void event_print(struct gpiod_info_event *event, struct config *cfg)
{
	struct gpiod_line_info *info;
	uint64_t evtime, before, after, mono;
	char *evname;
	int evtype;
	struct timespec ts;

	info = gpiod_info_event_get_line_info(event);
	evtime = gpiod_info_event_get_timestamp_ns(event);
	evtype = gpiod_info_event_get_event_type(event);

	switch (evtype) {
	case GPIOD_INFO_EVENT_LINE_REQUESTED:
		evname = "REQUESTED";
		break;
	case GPIOD_INFO_EVENT_LINE_RELEASED:
		evname = "RELEASED ";
		break;
	case GPIOD_INFO_EVENT_LINE_CONFIG_CHANGED:
		evname = "RECONFIG ";
		break;
	default:
		evname = "UNKNOWN  ";
	}

	if (cfg->event_clock_mode) {
		/*
		 * map clock monotonic to realtime,
		 * as uAPI only supports CLOCK_MONOTONIC
		 */
		clock_gettime(CLOCK_REALTIME, &ts);
		before = ts.tv_nsec + ts.tv_sec * 1000000000;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		mono = ts.tv_nsec + ts.tv_sec * 1000000000;

		clock_gettime(CLOCK_REALTIME, &ts);
		after = ts.tv_nsec + ts.tv_sec * 1000000000;

		evtime += (after/2 - mono + before/2);
	}

	print_event_time(evtime, cfg->event_clock_mode);
	printf(" %s", evname);

	if (cfg->chip_id)
		printf(" %s %d", cfg->chip_id,
		       gpiod_line_info_get_offset(info));

	print_line_info(info);
	printf("\n");
}

int main(int argc, char **argv)
{
	int i, j;
	struct gpiod_chip **chips;
	struct pollfd *pollfds;
	struct gpiod_chip *chip;
	struct line_resolver *resolver;
	struct gpiod_info_event *event;
	struct config cfg;

	i = parse_config(argc, argv, &cfg);
	argc -= optind;
	argv += optind;

	if (argc < 1)
		die("at least one GPIO line must be specified");

	if (argc > 64)
		die("too many lines given");

	resolver = resolve_lines(argc, argv, cfg.chip_id, cfg.strict,
				 cfg.by_name);
	chips = calloc(resolver->num_chips, sizeof(*chips));
	pollfds = calloc(resolver->num_chips, sizeof(*pollfds));
	if (!pollfds)
		die("out of memory");

	for (i = 0; i < resolver->num_chips; i++) {
		chip = gpiod_chip_open(resolver->chip_paths[i]);
		if (!chip)
			die_perror("unable to open chip %s",
				   resolver->chip_paths[i]);

		for (j = 0; j < resolver->num_lines; j++)
			if (resolver->lines[j].chip_path ==
			    resolver->chip_paths[i])
				if (!gpiod_chip_watch_line_info(chip,
						resolver->lines[j].offset))
					die_perror("unable to watch line on chip %s",
						   resolver->chip_paths[i]);

		chips[i] = chip;
		pollfds[i].fd = gpiod_chip_get_fd(chip);
		pollfds[i].events = POLLIN;
	}

	if (cfg.banner)
		print_banner(argc, argv);

	for (;;) {
		fflush(stdout);

		if (poll(pollfds, resolver->num_chips, -1) < 0)
			die_perror("error polling for events");

		for (i = 0; i < resolver->num_chips; i++) {
			if (pollfds[i].revents == 0)
				continue;

			event = gpiod_chip_read_info_event(chips[i]);
			event_print(event, &cfg);
		}
	}

	for (i = 0; i < resolver->num_chips; i++)
		gpiod_chip_close(chips[i]);

	free(chips);
	free_line_resolver(resolver);

	return EXIT_SUCCESS;
}

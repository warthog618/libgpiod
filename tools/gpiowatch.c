// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <getopt.h>
#include <gpiod.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "tools-common.h"

static int by_name = 0;
static int event_clock_mode = 0;

static const struct option longopts[] = {
	{ "by-name",		no_argument,		&by_name,	1 },
	{ "chip",		required_argument,	NULL,	'c' },
	{ "help",		no_argument,		NULL,	'h' },
	{ "line-buffered",	no_argument,		NULL,	'L' },
	{ "localtime",		no_argument,		&event_clock_mode,	2 },
	{ "strict",		no_argument,		NULL,	's' },
	{ "utc",		no_argument,		&event_clock_mode,	1 },
	{ "version",		no_argument,		NULL,	'v' },
	{ GETOPT_NULL_LONGOPT },
};

static const char *const shortopts = "+c:Lshv";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] <line> ...\n", get_progname());
	printf("\n");
	printf("Wait for changed to info on GPIO lines and print them to standard output.\n");
	printf("\n");
	printf("Lines are specified by name, or optionally by offset if the chip option\n");
	printf("is provided.\n");
	printf("\n");
	printf("Options:\n");
	print_chip_help();
	printf("  -L, --line-buffered\tset standard output as line buffered\n");
	printf("      --by-name\t\ttreat lines as names even if they would parse as an offset\n");
	printf("      --localtime\treport event time as a local time (default is monotonic)\n");
	printf("      --utc\t\treport event time as UTC (default is monotonic)\n");
	printf("  -s, --strict\t\tabort if requested line names are not unique\n");
	printf("  -h, --help\t\tdisplay this message and exit\n");
	printf("  -v, --version\t\tdisplay the version and exit\n");
}

static void event_print(struct gpiod_info_event *event, const char *chip_id)
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
	if (event_clock_mode) {
		// map clock monotonic to realtime, as uAPI only supports CLOCK_MONOTONIC
		clock_gettime(CLOCK_REALTIME, &ts);
		before = ts.tv_nsec + ts.tv_sec * 1000000000;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		mono = ts.tv_nsec + ts.tv_sec * 1000000000;
		clock_gettime(CLOCK_REALTIME, &ts);
		after = ts.tv_nsec + ts.tv_sec * 1000000000;
		evtime += (after/2 - mono + before/2);
	}
	print_event_time(evtime, event_clock_mode);
	printf(" %s", evname);
	if (chip_id)
		printf(" %s %d", chip_id, gpiod_line_info_get_offset(info));
	print_line_info(info);
	printf("\n");
}

int main(int argc, char **argv)
{
	bool by_name = false, strict = false;
	int optc, opti, i, j;
	struct gpiod_chip **chips;
	struct pollfd *pollfds;
	struct gpiod_chip *chip;
	char *chip_id = NULL;
	struct line_resolver *resolver;
	struct gpiod_info_event *event;

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'c':
			chip_id = optarg;
			break;
		case 'L':
			setlinebuf(stdout);
			break;
		case 'N':
			by_name = true;
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

	if (argc > 64)
		die("too many lines given");

	resolver = resolve_lines(argc, argv, chip_id, strict, by_name);
	chips = calloc(resolver->num_chips, sizeof(*chips));
	pollfds = calloc(resolver->num_chips, sizeof(*pollfds));
	if (!pollfds)
		die("out of memory");
	for (i = 0; i < resolver->num_chips; i++) {
		chip = gpiod_chip_open(resolver->chip_paths[i]);
		if (!chip)
			die_perror("unable to open chip %s", resolver->chip_paths[i]);

		for (j = 0; j < resolver->num_lines; j++)
			if (resolver->lines[j].chip_idx == i)
				if (!gpiod_chip_watch_line_info(chip, resolver->lines[j].offset))
					die_perror("unable to watch line on chip %s",
						   resolver->chip_paths[i]);

		chips[i] = chip;
		pollfds[i].fd = gpiod_chip_get_fd(chip);
		pollfds[i].events = POLLIN;
	}

	for (;;) {
		if (poll(pollfds, resolver->num_chips, -1) < 0)
			die_perror("error polling for events");

		for (i = 0; i < resolver->num_chips; i++) {
			if (pollfds[i].revents == 0)
				continue;

			event = gpiod_chip_read_info_event(chips[i]);
			event_print(event, chip_id);
		}
	}
	for (i = 0; i < resolver->num_chips; i++)
		gpiod_chip_close(chips[i]);
	free(chips);
	free_line_resolver(resolver);

	return EXIT_SUCCESS;
}

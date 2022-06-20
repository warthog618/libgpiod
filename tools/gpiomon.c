// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <getopt.h>
#include <gpiod.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools-common.h"

#define EVENT_BUF_SIZE 32

static int by_name = 0;
static int event_clock_mode = 0;

static const struct option longopts[] = {
	{ "active-low",		no_argument,		NULL,	'l' },
	{ "bias",		required_argument,	NULL,	'b' },
	{ "by-name",		no_argument,		&by_name,	1 },
	{ "chip",		required_argument,	NULL,	'c' },
	{ "debounce-period",	required_argument,	NULL,	'p' },
	{ "edge",		required_argument,	NULL,	'e' },
	{ "format",		required_argument,	NULL,	'F' },
	{ "help",		no_argument,		NULL,	'h' },
	{ "line-buffered",	no_argument,		NULL,	'L' },
	{ "localtime",		no_argument,		&event_clock_mode,	2 },
	{ "num-events",		required_argument,	NULL,	'n' },
	{ "quiet",		no_argument,		NULL,	'q' },
	{ "strict",		no_argument,		NULL,	's' },
	{ "utc",		no_argument,		&event_clock_mode,	1 },
	{ "version",		no_argument,		NULL,	'v' },
	{ GETOPT_NULL_LONGOPT },
};

static const char *const shortopts = "+c:b:d:e:F:lLn:p:shv";

static void print_help(void)
{
	printf("Usage: %s [OPTIONS] <line> ...\n", get_progname());
	printf("\n");
	printf("Wait for events on GPIO lines and print them to standard output.\n");
	printf("\n");
	printf("Lines are specified by name, or optionally by offset if the chip option\n");
	printf("is provided.\n");
	printf("\n");
	printf("Options:\n");
	print_chip_help();
	printf("  -l, --active-low\ttreat the line as active low, flipping the sense of\n");
	printf("\t\t\trising and falling edges\n");
	print_bias_help();
	printf("\t\t\tto be applied to the line(s)\n");
	printf("  -e, --edge <edge>\tspecify the edges to monitor\n");
	printf("\t\t\t(possible values: falling, rising, both)\n");
	printf("\t\t\t(defaults to 'both')\n");
	printf("  -p, --debounce-period <period>\n");
	printf("\t\t\tdebounce the line(s) with the specified period\n");
	printf("  -L, --line-buffered\tset standard output as line buffered\n");
	printf("  -F, --format FMT\tspecify a custom output format\n");
	printf("  -q, --quiet\t\tdon't generate any output\n");
	printf("  -n, --num-events <NUM>\n");
	printf("\t\t\texit after processing NUM events\n");
	printf("      --by-name\t\ttreat lines as names even if they would parse as an offset\n");
	printf("      --localtime\treport event time as a local time (default is monotonic)\n");
	printf("      --utc\t\treport event time as UTC (default is monotonic)\n");
	printf("  -s, --strict\t\tabort if requested line names are not unique\n");
	printf("  -h, --help\t\tdisplay this message and exit\n");
	printf("  -v, --version\t\tdisplay the version and exit\n");
	print_period_help();
	printf("\n");
	printf("Format specifiers:\n");
	printf("  %%o   GPIO line offset\n");
	printf("  %%l   GPIO line name\n");
	printf("  %%c   GPIO chip path\n");
	printf("  %%e   event type (0 - falling edge, 1 rising edge)\n");
	printf("  %%E   event type (falling, rising)\n");
	printf("  %%s   seconds part of the event timestamp\n");
	printf("  %%n   nanoseconds part of the event timestamp\n");
	printf("  %%u   event timestamp as datetime (requires --utc-time)\n");
}

static void event_print_custom(struct gpiod_edge_event *event, int chip_idx,
			       struct line_resolver *resolver, char *evt_fmt)
{
	char *prev, *curr, fmt;
	uint64_t evtime;
	int evtype;
	unsigned int offset;
	const char *lname;

	offset = gpiod_edge_event_get_line_offset(event);
	evtime = gpiod_edge_event_get_timestamp_ns(event);
	evtype = gpiod_edge_event_get_event_type(event);

	for (prev = curr = evt_fmt;;) {
		curr = strchr(curr, '%');
		if (!curr) {
			fputs(prev, stdout);
			break;
		}

		if (prev != curr)
			fwrite(prev, curr - prev, 1, stdout);

		fmt = *(curr + 1);

		switch (fmt) {
		case 'c':
			printf("%s", resolver->chip_paths[chip_idx]);
			break;
		case 'e':
			if (evtype == GPIOD_EDGE_EVENT_RISING_EDGE)
				fputc('1', stdout);
			else
				fputc('0', stdout);
			break;
		case 'E':
			if (evtype == GPIOD_EDGE_EVENT_RISING_EDGE)
				fputs("rising", stdout);
			else
				fputs("falling", stdout);
			break;
		case 'l':
			lname = get_line_name(resolver, chip_idx, offset);
			if (!lname)
				lname = "??";
			printf("%s", lname);
			break;
		case 'o':
			printf("%u", offset);
			break;
		case 'n':
			printf("%"PRIu64, evtime % 1000000000);
			break;
		case 's':
			printf("%"PRIu64, evtime / 1000000000);
			break;
		case 'u':
			print_event_time(evtime, event_clock_mode);
			break;
		case '%':
			fputc('%', stdout);
			break;
		case '\0':
			fputc('%', stdout);
			goto end;
		default:
			fwrite(curr, 2, 1, stdout);
			break;
		}

		curr += 2;
		prev = curr;
	}

end:
	fputc('\n', stdout);
}

static void event_print_human_readable(struct gpiod_edge_event *event,
				       const char *chip_id,
				       int chip_idx,
				       struct line_resolver *resolver)
{
	unsigned int offset;
	uint64_t evtime;
	char *evname;
	const char *lname;

	offset = gpiod_edge_event_get_line_offset(event);
	evtime = gpiod_edge_event_get_timestamp_ns(event);

	if (gpiod_edge_event_get_event_type(event) == GPIOD_EDGE_EVENT_RISING_EDGE)
		evname = "RISING ";
	else
		evname = "FALLING";
	lname = get_line_name(resolver, chip_idx, offset);
	print_event_time(evtime, event_clock_mode);
	if (lname)
		if (chip_id)
			printf(" %s chip: %s offset: %u name: %s\n",
			       evname, chip_id, offset, lname);
		else
			printf(" %s %s\n", evname, lname);
	else
		printf(" %s chip: %s offset: %u\n", evname, chip_id, offset);
}

static void event_print(struct gpiod_edge_event *event, const char *chip_id, int chip_idx,
			struct line_resolver *resolver, char *fmt)
{
	if (fmt)
		event_print_custom(event, chip_idx, resolver, fmt);
	else
		event_print_human_readable(event, chip_id, chip_idx, resolver);
}

static int parse_edge_or_die(const char *option)
{
	if (strcmp(option, "rising") == 0)
		return GPIOD_LINE_EDGE_RISING;
	if (strcmp(option, "falling") == 0)
		return GPIOD_LINE_EDGE_FALLING;
	if (strcmp(option, "both") != 0)
		die("invalid edge: %s", option);
	return GPIOD_LINE_EDGE_BOTH;
}

static void handle_signal(int signum UNUSED)
{
	// mimic not catching the signal
        exit(128+signum);
	// exit will flush stdout
}

int main(int argc, char **argv)
{
	bool active_low = false, by_name = false, strict = false, quiet = false;
	int num_lines, events_wanted = 0, events_done = 0;
	struct gpiod_edge_event_buffer *event_buffer;
	int optc, opti, ret, i, j, edge = GPIOD_LINE_EDGE_BOTH, bias = 0;
	struct gpiod_request_config *req_cfg;
	struct gpiod_line_request **requests;
	struct pollfd *pollfds;
	struct gpiod_line_config *line_cfg;
	unsigned int *offsets, debounce_period_us = 0;
	struct gpiod_edge_event *event;
	struct gpiod_chip *chip;
	char *chip_id = NULL, *fmt = NULL;
	struct line_resolver *resolver;

	// Hmmm, catching these allows flushing of stdout on exit...
	// and consider adding signalfd in poll.  Benefit??
	signal(SIGINT, handle_signal);
        signal(SIGTERM, handle_signal);

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'b':
			bias = parse_bias_or_die(optarg);
			break;
		case 'c':
			chip_id = optarg;
			break;
		case 'e':
			edge = parse_edge_or_die(optarg);
			break;
		case 'F':
			fmt = optarg;
			break;
		case 'l':
			active_low = true;
			break;
		case 'L':
			setlinebuf(stdout);
			break;
		case 'n':
			events_wanted = parse_uint_or_die(optarg);
			break;
		case 'N':
			by_name = true;
			break;
		case 'p':
			debounce_period_us = parse_period_or_die(optarg);
			break;
		case 'q':
			quiet = true;
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

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		die_perror("unable to allocate the line config structure");

	if (bias)
		gpiod_line_config_set_bias_default(line_cfg, bias);
	if (active_low)
		gpiod_line_config_set_active_low_default(line_cfg, true);
	if (debounce_period_us)
		gpiod_line_config_set_debounce_period_us_default(line_cfg, debounce_period_us);
	if (event_clock_mode)
		gpiod_line_config_set_event_clock_default(line_cfg, GPIOD_LINE_EVENT_CLOCK_REALTIME);
	gpiod_line_config_set_edge_detection_default(line_cfg, edge);

	req_cfg = gpiod_request_config_new();
	if (!req_cfg)
		die_perror("unable to allocate the request config structure");

	gpiod_request_config_set_consumer(req_cfg, "gpiomon");

	event_buffer = gpiod_edge_event_buffer_new(EVENT_BUF_SIZE);
	if (!event_buffer)
		die_perror("unable to allocate the line event buffer");

	resolver = resolve_lines(argc, argv, chip_id, strict, by_name);
	requests = calloc(resolver->num_chips, sizeof(*requests));
	pollfds = calloc(resolver->num_chips, sizeof(*pollfds));
	offsets = calloc(resolver->num_lines, sizeof(*offsets));
	if (!requests || !pollfds || !offsets)
		die("out of memory");
	for (i = 0; i < resolver->num_chips; i++) {
		num_lines = get_line_offsets_and_values(resolver, i, offsets, NULL);
		gpiod_request_config_set_offsets(req_cfg, num_lines, offsets);

		chip = gpiod_chip_open(resolver->chip_paths[i]);
		if (!chip)
			die_perror("unable to open chip %s", resolver->chip_paths[i]);

		requests[i] = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
		if (!requests[i])
			die_perror("unable to request lines on chip %s",
				   resolver->chip_paths[i]);

		pollfds[i].fd = gpiod_line_request_get_fd(requests[i]);
		pollfds[i].events = POLLIN;
		gpiod_chip_close(chip);
	}
	gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);

	for (;;) {
		if (poll(pollfds, resolver->num_chips, -1) < 0)
			die_perror("error polling for events");

		for (i = 0; i < resolver->num_chips; i++) {
			if (pollfds[i].revents == 0)
				continue;

			ret = gpiod_line_request_read_edge_event(requests[i], event_buffer,
								 EVENT_BUF_SIZE);
			if (ret < 0)
				die_perror("error reading line events");

			for (j = 0; j < ret; j++) {
				event = gpiod_edge_event_buffer_get_event(event_buffer, j);
				if (!event)
					die_perror("unable to retrieve event from buffer");

				if (!quiet)
					event_print(event, chip_id, i, resolver, fmt);

				events_done++;

				if (events_wanted && events_done >= events_wanted)
					goto done;
			}
		}
	}
done:
	for (i = 0; i < resolver->num_chips; i++)
		gpiod_line_request_release(requests[i]);
	free(requests);
	free_line_resolver(resolver);
	gpiod_edge_event_buffer_free(event_buffer);
	free(offsets);

	return EXIT_SUCCESS;
}

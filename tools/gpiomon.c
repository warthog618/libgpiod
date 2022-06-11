// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2022 Bartosz Golaszewski <bartekgola@gmail.com>

#include <getopt.h>
#include <gpiod.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools-common.h"

#define EVENT_BUF_SIZE 32

static int by_name;
static int event_clock_mode;
static int banner;
static const struct option longopts[] = {
	{ "active-low",		no_argument,		NULL,	'l' },
	{ "banner",		no_argument,		&banner,	1 },
	{ "bias",		required_argument,	NULL,	'b' },
	{ "by-name",		no_argument,		&by_name,	1 },
	{ "chip",		required_argument,	NULL,	'c' },
	{ "debounce-period",	required_argument,	NULL,	'p' },
	{ "edge",		required_argument,	NULL,	'e' },
	{ "format",		required_argument,	NULL,	'F' },
	{ "help",		no_argument,		NULL,	'h' },
	{ "localtime",		no_argument,		&event_clock_mode,	2 },
	{ "num-events",		required_argument,	NULL,	'n' },
	{ "quiet",		no_argument,		NULL,	'q' },
	{ "silent",		no_argument,		NULL,	'q' },
	{ "strict",		no_argument,		NULL,	's' },
	{ "utc",		no_argument,		&event_clock_mode,	1 },
	{ "version",		no_argument,		NULL,	'v' },
	{ GETOPT_NULL_LONGOPT },
};

static const char *const shortopts = "+b:c:e:hF:ln:p:qshv";

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
	printf("      --banner\t\tdisplay a banner on successful startup\n");
	print_bias_help();
	printf("      --by-name\t\ttreat lines as names even if they would parse as an offset\n");
	printf("  -c, --chip <chip>\trestrict scope to a particular chip\n");
	printf("  -e, --edge <edge>\tspecify the edges to monitor.\n");
	printf("\t\t\tPossible values: 'falling', 'rising', 'both'.\n");
	printf("\t\t\t(default is 'both')\n");
	printf("  -h, --help\t\tdisplay this help and exit\n");
	printf("  -F, --format <fmt>\tspecify a custom output format\n");
	printf("  -l, --active-low\ttreat the line as active low, flipping the sense of\n");
	printf("\t\t\trising and falling edges\n");
	printf("      --localtime\treport event time as a local time (default is monotonic)\n");
	printf("  -n, --num-events <num>\n");
	printf("\t\t\texit after processing num events\n");
	printf("  -p, --debounce-period <period>\n");
	printf("\t\t\tdebounce the line(s) with the specified period\n");
	printf("  -q, --quiet\t\tdon't generate any output\n");
	printf("  -s, --strict\t\tabort if requested line names are not unique\n");
	printf("      --utc\t\treport event time as UTC (default is monotonic)\n");
	printf("  -v, --version\t\toutput version information and exit\n");
	print_chip_help();
	print_period_help();
	printf("\n");
	printf("Format specifiers:\n");
	printf("  %%o   GPIO line offset\n");
	printf("  %%l   GPIO line name\n");
	printf("  %%c   GPIO chip path\n");
	printf("  %%e   numeric event type ('0' - falling edge or '1' - rising edge)\n");
	printf("  %%E   event type (falling or rising)\n");
	printf("  %%s   seconds part of the event timestamp\n");
	printf("  %%n   nanoseconds part of the event timestamp\n");
	printf("  %%T   event timestamp as datetime (UTC if --utc or local time if --localtime)\n");
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

struct config {
	bool active_low;
	bool quiet;
	bool strict;
	int bias;
	int edge;
	int events_wanted;
	unsigned int debounce_period_us;
	const char *chip_id;
	const char *fmt;
};

int parse_config(int argc, char **argv, struct config *cfg)
{
	int opti, optc;

	memset(cfg, 0, sizeof(*cfg));
	cfg->edge = GPIOD_LINE_EDGE_BOTH;

	for (;;) {
		optc = getopt_long(argc, argv, shortopts, longopts, &opti);
		if (optc < 0)
			break;

		switch (optc) {
		case 'b':
			cfg->bias = parse_bias_or_die(optarg);
			break;
		case 'c':
			cfg->chip_id = optarg;
			break;
		case 'e':
			cfg->edge = parse_edge_or_die(optarg);
			break;
		case 'F':
			cfg->fmt = optarg;
			break;
		case 'l':
			cfg->active_low = true;
			break;
		case 'n':
			cfg->events_wanted = parse_uint_or_die(optarg);
			break;
		case 'p':
			cfg->debounce_period_us = parse_period_or_die(optarg);
			break;
		case 'q':
			cfg->quiet = true;
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
		printf("Monitoring lines ");
		for (i = 0; i < num_lines - 1; i++)
			printf("%s, ", lines[i]);
		printf("and %s...\n", lines[i]);
	} else {
		printf("Monitoring line %s ...\n", lines[0]);
	}
}

static void event_print_custom(struct gpiod_edge_event *event, const char *chip_path,
			       struct line_resolver *resolver, const char *evt_fmt)
{
	const char *lname, *prev, *curr;
	char  fmt;
	uint64_t evtime;
	int evtype;
	unsigned int offset;

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
			printf("%s", chip_path);
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
			lname = get_line_name(resolver, chip_path, offset);
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
		case 'T':
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
				       const char *chip_path,
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
	lname = get_line_name(resolver, chip_path, offset);
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

static void event_print(struct gpiod_edge_event *event, const char *chip_id,
			const char *chip_path,	struct line_resolver *resolver,
			const char *fmt)
{
	if (fmt)
		event_print_custom(event, chip_path, resolver, fmt);
	else
		event_print_human_readable(event, chip_id, chip_path, resolver);
}

int main(int argc, char **argv)
{
	int num_lines, events_done = 0;
	struct gpiod_edge_event_buffer *event_buffer;
	int ret, i, j;
	struct gpiod_request_config *req_cfg;
	struct gpiod_line_request **requests;
	struct pollfd *pollfds;
	struct gpiod_line_config *line_cfg;
	unsigned int *offsets;
	struct gpiod_edge_event *event;
	struct gpiod_chip *chip;
	struct line_resolver *resolver;
	const char *chip_path;
	struct config cfg;

	i = parse_config(argc, argv, &cfg);
	argc -= i;
	argv += i;

	if (argc < 1)
		die("at least one GPIO line must be specified");

	if (argc > 64)
		die("too many lines given");

	line_cfg = gpiod_line_config_new();
	if (!line_cfg)
		die_perror("unable to allocate the line config structure");

	if (cfg.bias)
		gpiod_line_config_set_bias_default(line_cfg, cfg.bias);
	if (cfg.active_low)
		gpiod_line_config_set_active_low_default(line_cfg, true);
	if (cfg.debounce_period_us)
		gpiod_line_config_set_debounce_period_us_default(line_cfg, cfg.debounce_period_us);
	if (event_clock_mode)
		gpiod_line_config_set_event_clock_default(line_cfg,
							  GPIOD_LINE_EVENT_CLOCK_REALTIME);
	gpiod_line_config_set_edge_detection_default(line_cfg, cfg.edge);

	req_cfg = gpiod_request_config_new();
	if (!req_cfg)
		die_perror("unable to allocate the request config structure");

	gpiod_request_config_set_consumer(req_cfg, "gpiomon");

	event_buffer = gpiod_edge_event_buffer_new(EVENT_BUF_SIZE);
	if (!event_buffer)
		die_perror("unable to allocate the line event buffer");

	resolver = resolve_lines(argc, argv, cfg.chip_id, cfg.strict, by_name);
	requests = calloc(resolver->num_chips, sizeof(*requests));
	pollfds = calloc(resolver->num_chips, sizeof(*pollfds));
	offsets = calloc(resolver->num_lines, sizeof(*offsets));
	if (!requests || !pollfds || !offsets)
		die("out of memory");
	for (i = 0; i < resolver->num_chips; i++) {
		chip_path = resolver->chip_paths[i];
		num_lines = get_line_offsets_and_values(resolver, chip_path, offsets, NULL);
		gpiod_request_config_set_offsets(req_cfg, num_lines, offsets);

		chip = gpiod_chip_open(chip_path);
		if (!chip)
			die_perror("unable to open chip %s", chip_path);

		requests[i] = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
		if (!requests[i])
			die_perror("unable to request lines on chip %s", chip_path);

		pollfds[i].fd = gpiod_line_request_get_fd(requests[i]);
		pollfds[i].events = POLLIN;
		gpiod_chip_close(chip);
	}
	gpiod_request_config_free(req_cfg);
	gpiod_line_config_free(line_cfg);

	if (banner)
		print_banner(argc, argv);

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

				if (!cfg.quiet)
					event_print(event, cfg.chip_id, resolver->chip_paths[i],
						    resolver, cfg.fmt);

				events_done++;

				if (cfg.events_wanted && events_done >= cfg.events_wanted)
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

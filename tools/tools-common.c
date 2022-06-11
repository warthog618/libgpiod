// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

/* Common code for GPIO tools. */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <gpiod.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "tools-common.h"

const char *get_progname(void)
{
	return program_invocation_name;
}

void print_error(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	fprintf(stderr, "%s: ", program_invocation_name);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\n");
	va_end(va);
}

void print_perror(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	fprintf(stderr, "%s: ", program_invocation_name);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, ": %s\n", strerror(errno));
	va_end(va);
}

void die(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	fprintf(stderr, "%s: ", program_invocation_name);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\n");
	va_end(va);

	exit(EXIT_FAILURE);
}

void die_perror(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	fprintf(stderr, "%s: ", program_invocation_name);
	vfprintf(stderr, fmt, va);
	fprintf(stderr, ": %s\n", strerror(errno));
	va_end(va);

	exit(EXIT_FAILURE);
}

void print_version(void)
{
	printf("%s (libgpiod) v%s\n",
	       program_invocation_short_name, gpiod_version_string());
	printf("Copyright (C) 2017-2018 Bartosz Golaszewski\n");
	printf("License: LGPLv2.1\n");
	printf("This is free software: you are free to change and redistribute it.\n");
	printf("There is NO WARRANTY, to the extent permitted by law.\n");
}

int parse_bias_or_die(const char *option)
{
	if (strcmp(option, "pull-down") == 0)
		return GPIOD_LINE_BIAS_PULL_DOWN;
	if (strcmp(option, "pull-up") == 0)
		return GPIOD_LINE_BIAS_PULL_UP;
	if (strcmp(option, "disabled") == 0)
		return GPIOD_LINE_BIAS_DISABLED;
	if (strcmp(option, "as-is") != 0)
		die("invalid bias: %s", option);
	return 0;
}

int parse_period(const char *option)
{
	unsigned long p, m = 0;
	char *end;

	p = strtoul(option, &end, 10);
	switch (*end) {
		case 'u':
			m = 1;
			end++;
			break;
		case 'm':
			m = 1000;
			end++;
			break;
		case 's':
			m = 1000000;
			break;
		case '\0':
			break;
		default:
			return -1;
	}
	if (m) {
		if (*end != 's')
			return -1;
		end++;
	} else {
		m = 1000;
	}
	p *= m;
	if (*end != '\0' || p > INT_MAX)
		return -1;

	return p;
}

unsigned int parse_period_or_die(const char *option)
{
	int period = parse_period(option);

	if (period < 0)
		die("invalid period: %s", option);
	return period;
}

int parse_periods_or_die(char *option, unsigned int **periods)
{
	int i, num_periods = 1;
	unsigned int *pp;
	char *end;

	for (i = 0; option[i] != '\0'; i++)
		if (option[i] == ',')
			num_periods++;

	pp = calloc(num_periods, sizeof(*pp));
	for (i = 0; i < num_periods - 1; i++) {
		for (end = option; *end != ','; end++) {};
		*end = '\0';
		pp[i] = parse_period_or_die(option);
		option = end + 1;
	}
	pp[i] = parse_period_or_die(option);
	*periods = pp;
	return num_periods;
}

int parse_uint(const char *option)
{
	unsigned long o;
	char *end;

	o = strtoul(option, &end, 10);
	if (*end == '\0' && o <= INT_MAX)
		return o;

	return -1;
}

unsigned int parse_uint_or_die(const char *option)
{
	int i = parse_uint(option);

	if (i < 0)
		die("invalid number: %s", option);

	return i;
}

void print_bias_help(void)
{
	printf("  -b, --bias <bias>     specify the line bias\n");
	printf("                        (possible values: 'as-is', 'pull-down', 'pull-up', 'disabled')\n");
	printf("                        (defaults to 'as-is')\n");
}

void print_chip_help(void)
{
	printf("\nChips:\n");
	printf("    A GPIO chip may be identified by number, name, or path.\n");
	printf("    e.g. '0', 'gpiochip0', and '/dev/gpiochip0' all refer to the same chip.\n");
}

void print_period_help(void)
{
	printf("\nPeriods:\n");
	printf("    Periods are taken as milliseconds unless units are specified. e.g. 10us.\n");
	printf("    Supported units are 's', 'ms', and 'us'.\n");
}

#define TIME_BUFFER_SIZE 20

// mode:
// 0: monotonic time
// 1: utc time
// 2: local time
void print_event_time(uint64_t evtime, int mode)
{
	time_t evtsec;
	struct tm t;
	char tbuf[TIME_BUFFER_SIZE];
	char *tz;

	if (mode) {
		evtsec = evtime / 1000000000;
		if (mode == 2) {
			localtime_r(&evtsec, &t);
			tz = "";
		} else {
			gmtime_r(&evtsec, &t);
			tz = "Z";
		}
		strftime(tbuf, TIME_BUFFER_SIZE, "%FT%T", &t);
		printf("%s.%09"PRIu64"%s", tbuf, evtime % 1000000000, tz);
	} else {
		printf("%8"PRIu64".%09"PRIu64,
		       evtime / 1000000000, evtime % 1000000000);
	}
}


typedef bool (*is_set_func)(struct gpiod_line_info *);

struct flag {
	const char *name;
	is_set_func is_set;
};

static bool line_bias_is_pullup(struct gpiod_line_info *info)
{
	return gpiod_line_info_get_bias(info) == GPIOD_LINE_BIAS_PULL_UP;
}

static bool line_bias_is_pulldown(struct gpiod_line_info *info)
{
	return gpiod_line_info_get_bias(info) == GPIOD_LINE_BIAS_PULL_DOWN;
}

static bool line_bias_is_disabled(struct gpiod_line_info *info)
{
	return gpiod_line_info_get_bias(info) == GPIOD_LINE_BIAS_DISABLED;
}

static bool line_drive_is_open_drain(struct gpiod_line_info *info)
{
	return gpiod_line_info_get_drive(info) == GPIOD_LINE_DRIVE_OPEN_DRAIN;
}

static bool line_drive_is_open_source(struct gpiod_line_info *info)
{
	return gpiod_line_info_get_drive(info) == GPIOD_LINE_DRIVE_OPEN_SOURCE;
}

static bool line_edge_detection_is_both(struct gpiod_line_info *info)
{
	return gpiod_line_info_get_edge_detection(info) == GPIOD_LINE_BIAS_PULL_UP;
}

static bool line_edge_detection_is_rising(struct gpiod_line_info *info)
{
	return gpiod_line_info_get_edge_detection(info) == GPIOD_LINE_EDGE_RISING;
}

static bool line_edge_detection_is_falling(struct gpiod_line_info *info)
{
	return gpiod_line_info_get_edge_detection(info) == GPIOD_LINE_EDGE_FALLING;
}

static bool line_event_clock_realtime(struct gpiod_line_info *info)
{
	return gpiod_line_info_get_event_clock(info) == GPIOD_LINE_EVENT_CLOCK_REALTIME;
}

static const struct flag flags[] = {
	{
		.name = "used",
		.is_set = gpiod_line_info_is_used,
	},
	{
		.name = "open-drain",
		.is_set = line_drive_is_open_drain,
	},
	{
		.name = "open-source",
		.is_set = line_drive_is_open_source,
	},
	{
		.name = "pull-up",
		.is_set = line_bias_is_pullup,
	},
	{
		.name = "pull-down",
		.is_set = line_bias_is_pulldown,
	},
	{
		.name = "bias-disabled",
		.is_set = line_bias_is_disabled,
	},
	{
		.name = "both-edges",
		.is_set = line_edge_detection_is_both,
	},
	{
		.name = "rising-edges",
		.is_set = line_edge_detection_is_rising,
	},
	{
		.name = "falling-edges",
		.is_set = line_edge_detection_is_falling,
	},
	{
		.name = "event-clock-realtime",
		.is_set = line_event_clock_realtime,
	},
};

static PRINTF(3, 4) void prinfo(bool *of, unsigned int prlen, const char *fmt, ...)
{
	char *buf, *buffmt = NULL;
	size_t len;
	va_list va;
	int rv;

	va_start(va, fmt);
	rv = vasprintf(&buf, fmt, va);
	va_end(va);
	if (rv < 0)
		die("vasprintf: %s\n", strerror(errno));

	len = strlen(buf) - 1;

	if (len >= prlen || *of) {
		*of = true;
		printf("%s", buf);
	} else {
		rv = asprintf(&buffmt, "%%%us", prlen);
		if (rv < 0)
			die("asprintf: %s\n", strerror(errno));

		printf(buffmt, buf);
	}

	free(buf);
	if (fmt)
		free(buffmt);
}

void print_line_info(struct gpiod_line_info *info)
{
	bool of = false;
	const char *name, *consumer;
	size_t i;
	int direction;
	unsigned long debounce;

	name = gpiod_line_info_get_name(info);
	consumer = gpiod_line_info_get_consumer(info);
	direction = gpiod_line_info_get_direction(info);
	debounce = gpiod_line_info_get_debounce_period_us(info);

	name ? prinfo(&of, 16, "\t%s", name) : prinfo(&of, 16, "\tunnamed");

	if (!gpiod_line_info_is_used(info))
		prinfo(&of, 12, "\tunused");
	else
		consumer ? prinfo(&of, 12, "\t%s", consumer)
			 : prinfo(&of, 12, "\tkernel");

	printf("\t[%s", direction == GPIOD_LINE_DIRECTION_INPUT ? "input" : "output");

	if (gpiod_line_info_is_active_low(info))
		printf(" active-low");

	for (i = 0; i < ARRAY_SIZE(flags); i++) {
		if (flags[i].is_set(info))
			printf(" %s", flags[i].name);
	}

	if (debounce)
		printf(" debounce_period=%luus", debounce);

	printf("]");
}

static int chip_dir_filter(const struct dirent *entry)
{
	char *path;
	int ret = 0;
	struct stat sb;

	if (asprintf(&path, "/dev/%s", entry->d_name) < 0)
		return 0;

	if ((lstat(path, &sb) == 0) && (!S_ISLNK(sb.st_mode)) && gpiod_is_gpiochip_device(path))
		ret = 1;
	free(path);
	return ret;
}

static bool isuint(const char *str)
{
	for (; *str && isdigit(*str); str++)
		;

	return *str == '\0';
}

bool chip_path_lookup(const char *id, char **path_ptr)
{
	char *path;

	if (isuint(id)) {
		// by number
		if (asprintf(&path, "/dev/gpiochip%s", id) < 0)
			return false;
	} else if (strchr(id, '/')) {
		// by path
		if (asprintf(&path, "%s", id) < 0)
			return false;
	} else {
		// by device name
		if (asprintf(&path, "/dev/%s", id) < 0)
			return false;
	}
	if (!gpiod_is_gpiochip_device(path)) {
		free(path);
		return false;
	}
	*path_ptr = path;
	return true;
}

int chip_paths(const char *id, char ***paths_ptr)
{
	char *path;
	char **paths;

	if (id == NULL)
		return all_chip_paths(paths_ptr);

	if (!chip_path_lookup(id, &path))
		return 0;
	paths = malloc(sizeof(*paths));
	if (paths == NULL) {
		free(path);
		return 0;
	}
	paths[0] = path;
	*paths_ptr = paths;
	return 1;
}

int all_chip_paths(char ***paths_ptr)
{
	int i, j, num_chips, ret = 0;
	struct dirent **entries;
	char **paths;

	num_chips = scandir("/dev/", &entries, chip_dir_filter, alphasort);
	if (num_chips < 0)
		die_perror("unable to scan /dev");

	paths = calloc(num_chips, sizeof(*paths));
	if (paths == NULL)
		goto free_entries;

	for (i = 0; i < num_chips; i++) {
		if (asprintf(&paths[i], "/dev/%s", entries[i]->d_name) < 0) {
			for (j = 0; j < i; j++)
				free(paths[j]);
			free(paths);
			return 0;
		}
	}
	*paths_ptr = paths;
	ret = num_chips;
free_entries:
	for (i = 0; i < num_chips; i++)
		free(entries[i]);
	free(entries);
	return ret;
}

static bool resolve_line(struct gpiod_line_info *info, const char * chip_path,
			 struct line_resolver *resolver)
{
	int i, found_idx = -1;
	unsigned int offset;
	struct resolved_line *line;
	const char *name;

	offset = gpiod_line_info_get_offset(info);
	for (i = 0; i < resolver->num_lines; i++) {
		line = &resolver->lines[i];
		name = gpiod_line_info_get_name(info);
		if ((!line->id_is_name && (line->offset == (int)offset)) ||
		    (name && strcmp(line->id, name) == 0)) {
			if (resolver->strict && line->chip_path)
				die("line %s is not unique", line->id);
			if (found_idx != -1)
				die("lines %s and %s are the same",
				    resolver->lines[found_idx].id, line->id);
			found_idx = i;
			line->chip_path = chip_path;
			line->offset = offset;
			resolver->num_found++;
		}
	}
	return (found_idx != -1);
}

static bool resolve_done(struct line_resolver *resolver)
{
	return (!resolver->strict && resolver->num_found >= resolver->num_lines);
}


struct line_resolver *resolve_lines(int num_lines, char **lines, const char *chip_id,
				    bool strict, bool by_name)
{
	char ** paths;
	int num_chips, i, j, offset;
	size_t resolver_size;
	struct line_resolver *resolver;
	struct resolved_line *line;
	struct gpiod_chip *chip;
	struct gpiod_chip_info *chip_info;
	struct gpiod_line_info *line_info;
	bool chip_used;

	if (chip_id == NULL)
		by_name = true;

	num_chips = chip_paths(chip_id, &paths);
	if ((chip_id != NULL)  && (num_chips == 0))
		die("cannot find a GPIO chip character device corresponding to %s", chip_id);

	resolver_size = sizeof(*resolver) + num_lines * sizeof(*line);
	resolver = malloc(resolver_size);
	if (resolver == NULL)
		die("out of memory");

	memset(resolver, 0, resolver_size);

	resolver->num_lines = num_lines;
	resolver->strict = strict;
	for (i = 0; i < num_lines; i++) {
		line = &resolver->lines[i];
		line->id = lines[i];
		line->offset = by_name ? -1: parse_uint(lines[i]);
		line->id_is_name = (line->offset == -1);
	}

	for (i = 0; (i < num_chips) && !resolve_done(resolver); i++) {
		chip_used = false;
		chip = gpiod_chip_open(paths[i]);
		if (!chip) {
			if ((errno == EACCES) && (chip_id == NULL))
				continue;

			die_perror("unable to open chip %s", paths[i]);
		}

		chip_info = gpiod_chip_get_info(chip);
		if (!chip_info)
			die_perror("unable to get info for %s", paths[i]);

		num_lines = gpiod_chip_info_get_num_lines(chip_info);
		gpiod_chip_info_free(chip_info);
		for (offset = 0; (offset < num_lines) && !resolve_done(resolver); offset++) {
			line_info = gpiod_chip_get_line_info(chip, offset);
			if (!line_info)
				die_perror("unable to retrieve the line info from chip %s",
					   paths[i]);

			if (resolve_line(line_info, paths[i], resolver))
				chip_used = true;

			gpiod_line_info_free(line_info);
		}
		if (chip_used) {
			resolver->num_chips++;
		} else {
			free(paths[i]);
			paths[i] = NULL;
		}
		gpiod_chip_close(chip);
	}
	for (i = 0 ; i < resolver->num_lines; i++) {
		if (resolver->lines[i].chip_path)
			continue;
		if (chip_id && !resolver->lines[i].id_is_name)
			print_error("offset %s is out of range on chip %s",
				    resolver->lines[i].id, chip_id);
		else
			print_error("cannot find line %s", resolver->lines[i].id);
	}

	if (resolver->num_found != resolver->num_lines)
		exit(EXIT_FAILURE);

	// condense paths to remove freed paths
	for (i = 0; i < resolver->num_chips; i++) {
		if (paths[i] == NULL)
			// note the limit is the uncondensed size
			for (j = i + 1; j < num_chips; j++) {
				if (paths[j] != NULL) {
					paths[i] = paths[j];
					paths[j] = NULL;
					break;
				}
			}
	}
	resolver->chip_paths = paths;

	return resolver;
}

void free_line_resolver(struct line_resolver *resolver)
{
	int i;

	if (!resolver)
		return;

	for (i = 0; i < resolver->num_chips; i++)
		free(resolver->chip_paths[i]);
	free(resolver->chip_paths);
	free(resolver);
}

int get_line_offsets_and_values(struct line_resolver *resolver, const char * chip_path,
				unsigned int *offsets, int *values)
{
	int i, num_lines = 0;
	struct resolved_line *line;

	for (i = 0; i < resolver->num_lines; i++) {
		line = &resolver->lines[i];
		if (line->chip_path == chip_path) {
			offsets[num_lines] = line->offset;
			if (values)
				values[num_lines] = line->value;
			num_lines++;
		}
	}
	return num_lines;
}

const char *get_line_name(struct line_resolver *resolver, const char * chip_path,
			  unsigned int offset)
{
	int i;

	for (i = 0; i < resolver->num_lines; i++)
		if ((resolver->lines[i].offset == (int)offset) &&
		    (resolver->lines[i].chip_path == chip_path) &&
		    (resolver->lines[i].id_is_name))
			return resolver->lines[i].id;
	return 0;
}

void set_line_values(struct line_resolver *resolver, const char * chip_path, int *values)
{
	int i, j;

	for (i = 0, j = 0; i < resolver->num_lines; i++) {
		if (resolver->lines[i].chip_path == chip_path) {
			resolver->lines[i].value = values[j];
			j++;
		}
	}
}

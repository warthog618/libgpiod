// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

/* Common code for GPIO tools. */

#include <ctype.h>
#include <errno.h>
#include <gpiod.h>
#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>

#include "tools-common.h"

const char *get_progname(void)
{
	return program_invocation_name;
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

int parse_bias(const char *option)
{
	if (strcmp(option, "pull-down") == 0)
		return GPIOD_LINE_BIAS_PULL_DOWN;
	if (strcmp(option, "pull-up") == 0)
		return GPIOD_LINE_BIAS_PULL_UP;
	if (strcmp(option, "disable") == 0)
		return GPIOD_LINE_BIAS_DISABLED;
	if (strcmp(option, "as-is") != 0)
		die("invalid bias: %s", option);
	return 0;
}

void print_bias_help(void)
{
	printf("Biases:\n");
	printf("  as-is:\tleave bias unchanged\n");
	printf("  disable:\tdisable bias\n");
	printf("  pull-up:\tenable pull-up\n");
	printf("  pull-down:\tenable pull-down\n");
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
};

static PRINTF(3, 4) void prinfo(bool *of,
				unsigned int prlen, const char *fmt, ...)
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

void print_line_info(struct gpiod_line_info * info)
{
	bool flag_printed, of, active_low;
	const char *name, *consumer;
	size_t i, offset;
	int direction;

	offset = gpiod_line_info_get_offset(info);
	name = gpiod_line_info_get_name(info);
	consumer = gpiod_line_info_get_consumer(info);
	direction = gpiod_line_info_get_direction(info);
	active_low = gpiod_line_info_is_active_low(info);

	of = false;

	printf("\tline ");
	prinfo(&of, 3, "%zu", offset);
	printf(": ");

	name ? prinfo(&of, 12, "\"%s\"", name) : prinfo(&of, 12, "unnamed");
	printf(" ");

	if (!gpiod_line_info_is_used(info))
		prinfo(&of, 12, "unused");
	else
		consumer ? prinfo(&of, 12, "\"%s\"", consumer)
			 : prinfo(&of, 12, "kernel");

	printf(" ");

	prinfo(&of, 8, "%s ", direction == GPIOD_LINE_DIRECTION_INPUT
						? "input" : "output");
	prinfo(&of, 13, "%s ", active_low ? "active-low" : "active-high");

	flag_printed = false;
	for (i = 0; i < ARRAY_SIZE(flags); i++) {
		if (flags[i].is_set(info)) {
			if (flag_printed)
				printf(" ");
			else
				printf("[");
			printf("%s", flags[i].name);
			flag_printed = true;
		}
	}
	if (flag_printed)
		printf("]");
	printf("\n");
}

int make_signalfd(void)
{
	sigset_t sigmask;
	int sigfd, rv;

	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGTERM);
	sigaddset(&sigmask, SIGINT);

	rv = sigprocmask(SIG_BLOCK, &sigmask, NULL);
	if (rv < 0)
		die("error masking signals: %s", strerror(errno));

	sigfd = signalfd(-1, &sigmask, 0);
	if (sigfd < 0)
		die("error creating signalfd: %s", strerror(errno));

	return sigfd;
}

int chip_dir_filter(const struct dirent *entry)
{
	bool is_chip;
	char *path;
	int ret;

	ret = asprintf(&path, "/dev/%s", entry->d_name);
	if (ret < 0)
		return 0;

	is_chip = gpiod_is_gpiochip_device(path);
	free(path);
	return !!is_chip;
}

struct gpiod_chip *chip_open_by_name(const char *name)
{
	struct gpiod_chip *chip;
	char *path;
	int ret;

	ret = asprintf(&path, "/dev/%s", name);
	if (ret < 0)
		return NULL;

	chip = gpiod_chip_open(path);
	free(path);

	return chip;
}

static struct gpiod_chip *chip_open_by_number(unsigned int num)
{
	struct gpiod_chip *chip;
	char *path;
	int ret;

	ret = asprintf(&path, "/dev/gpiochip%u", num);
	if (ret < 0)
		return NULL;

	chip = gpiod_chip_open(path);
	free(path);

	return chip;
}

static bool isuint(const char *str)
{
	for (; *str && isdigit(*str); str++)
		;

	return *str == '\0';
}

struct gpiod_chip *chip_open_lookup(const char *device)
{
	struct gpiod_chip *chip;

	if (isuint(device)) {
		chip = chip_open_by_number(strtoul(device, NULL, 10));
	} else {
		if (strncmp(device, "/dev/", 5))
			chip = chip_open_by_name(device);
		else
			chip = gpiod_chip_open(device);
	}

	return chip;
}

// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Kent Gibson <warthog618@gmail.com>

/* Minimal example of toggling a single line. */

#include <errno.h>
#include <gpiod.h>
#include <gpiod-ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static enum gpiod_line_value toggle_line_value(enum gpiod_line_value value)
{
	return (value == GPIOD_LINE_VALUE_ACTIVE) ? GPIOD_LINE_VALUE_INACTIVE :
						    GPIOD_LINE_VALUE_ACTIVE;
}

static const char * value_str(enum gpiod_line_value value)
{
	if (value == GPIOD_LINE_VALUE_ACTIVE)
		return "Active";
	else if (value == GPIOD_LINE_VALUE_INACTIVE) {
		return "Inactive";
	} else {
		return "Unknown";
	}
}

int main(void)
{
	/* Example configuration - customize to suit your situation. */
	static const char *const chip_path = "/dev/gpiochip0";
	static const unsigned int line_offset = 5;

	enum gpiod_line_value value = GPIOD_LINE_VALUE_ACTIVE;
	struct gpiod_line_request *request;

	request = gpiod_ext_request_output(chip_path, line_offset, value);
	if (!request) {
		fprintf(stderr, "failed to request line: %s\n",
			strerror(errno));
		return EXIT_FAILURE;
	}

	for (;;) {
		printf("%d=%s\n", line_offset, value_str(value));
		sleep(1);
		value = toggle_line_value(value);
		gpiod_line_request_set_value(request, line_offset, value);
	}

	gpiod_line_request_release(request);

	return EXIT_SUCCESS;
}

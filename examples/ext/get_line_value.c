// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Kent Gibson <warthog618@gmail.com>

/* Minimal example of reading a single line. */

#include <errno.h>
#include <gpiod.h>
#include <gpiod-ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int print_value(unsigned int offset, enum gpiod_line_value value)
{
	if (value == GPIOD_LINE_VALUE_ACTIVE)
		printf("%d=Active\n", offset);
	else if (value == GPIOD_LINE_VALUE_INACTIVE) {
		printf("%d=Inactive\n", offset);
	} else {
		fprintf(stderr, "error reading value: %s\n",
			strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int main(void)
{
	/* Example configuration - customize to suit your situation. */
	static const char *const chip_path = "/dev/gpiochip0";
	static const unsigned int line_offset = 5;

	struct gpiod_line_request *request;
	enum gpiod_line_value value;
	int ret;

	request = gpiod_ext_request_input(chip_path, line_offset);
	if (!request) {
		fprintf(stderr, "failed to request line: %s\n",
			strerror(errno));
		return EXIT_FAILURE;
	}

	value = gpiod_line_request_get_value(request, line_offset);
	ret = print_value(line_offset, value);

	/* not strictly required here, but if the app wasn't exiting... */
	gpiod_line_request_release(request);

	return ret;
}

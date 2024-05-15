// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Kent Gibson <warthog618@gmail.com>

/* Minimal example of watching for rising edges on a single line. */

#include <errno.h>
#include <gpiod-ext.h>
#include <gpiod.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *edge_event_type_str(unsigned int id)
{
	switch (id) {
	case GPIOD_EDGE_EVENT_RISING_EDGE:
		return "Rising";
	case GPIOD_EDGE_EVENT_FALLING_EDGE:
		return "Falling";
	default:
		return "Unknown";
	}
}

int main(void)
{
	/* Example configuration - customize to suit your situation. */
	static const char *const chip_path = "/dev/gpiochip0";
	static const unsigned int line_offset = 5;

	struct gpiod_line_request *request;
	struct gpio_v2_line_event *event;
	ssize_t rd, event_size;
	int fd, ret;

	request = gpiod_ext_request_input(chip_path, line_offset);
	if (!request) {
		fprintf(stderr, "failed to request line: %s\n",
			strerror(errno));
		return EXIT_FAILURE;
	}

	ret = gpiod_ext_set_edge_detection(request, GPIOD_LINE_EDGE_RISING);
	if (ret == -1) {
		fprintf(stderr, "error configuring edge events: %s\n",
			strerror(errno));
		return EXIT_FAILURE;
	}

	/*
	 * Allocate space for reading a single event.
	 * Refer to the watch_multiple_line_values example for an example of
	 * using an event buffer to read multiple events from the kernel at
	 * once.
	 */
	event_size = sizeof(*event);
	event = malloc(event_size);
	fd = gpiod_line_request_get_fd(request);

	for (;;) {
		/* Blocks until an event is available. */
		rd = read(fd, event, event_size);
		if (rd < 0) {
			fprintf(stderr, "error reading edge event: %s\n",
				strerror(errno));
			return EXIT_FAILURE;
		} else if (rd < event_size) {
			fprintf(stderr, "short edge event read\n");
			return EXIT_FAILURE;
		}
		printf("offset: %d  type: %-7s  event #%d\n",
		       event->offset,
		       edge_event_type_str(event->id),
		       event->line_seqno);
	}
}

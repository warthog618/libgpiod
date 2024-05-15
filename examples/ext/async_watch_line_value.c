// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2023 Kent Gibson <warthog618@gmail.com>

/* Minimal example of asynchronously watching for edges on a single line. */

#include <errno.h>
#include <gpiod-ext.h>
#include <gpiod.h>
#include <linux/gpio.h>
#include <poll.h>
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
	struct pollfd pollfd;
	ssize_t rd, event_size;
	int ret;

	request = gpiod_ext_request_input(chip_path, line_offset);
	if (!request) {
		fprintf(stderr, "failed to request line: %s\n",
			strerror(errno));
		return EXIT_FAILURE;
	}

	/* Assume a button connecting the pin to ground, so pull it up... */
	ret = gpiod_ext_set_bias(request, GPIOD_LINE_BIAS_PULL_UP);
	if (ret == -1) {
		fprintf(stderr, "error configuring pull-up: %s\n",
			strerror(errno));
		return EXIT_FAILURE;
	}

	ret = gpiod_ext_set_edge_detection(request, GPIOD_LINE_EDGE_BOTH);
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

	pollfd.fd = gpiod_line_request_get_fd(request);
	pollfd.events = POLLIN;

	for (;;) {
		/*
		 * Here we only poll the request fd, but other fds could be added
		 * to the poll to monitor multiple event sources at once.
		 */
		ret = poll(&pollfd, 1, -1);
		if (ret == -1) {
			fprintf(stderr, "error waiting for edge events: %s\n",
				strerror(errno));
			return EXIT_FAILURE;
		}
		/* read will not block as request fd is now readable */
		rd = read(pollfd.fd, event, event_size);
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

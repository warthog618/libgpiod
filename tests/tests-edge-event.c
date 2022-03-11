// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <glib.h>
#include <gpiod.h>
#include <poll.h>

#include "gpiod-test.h"
#include "gpiod-test-helpers.h"
#include "gpiod-test-sim.h"

#define GPIOD_TEST_GROUP "edge-event"

GPIOD_TEST_CASE(edge_event_buffer_capacity)
{
	g_autoptr(struct_gpiod_edge_event_buffer) buffer = NULL;

	buffer = gpiod_test_create_edge_event_buffer_or_fail(32);

	g_assert_cmpuint(gpiod_edge_event_buffer_get_capacity(buffer), ==, 32);
}

GPIOD_TEST_CASE(edge_event_buffer_max_capacity)
{
	g_autoptr(struct_gpiod_edge_event_buffer) buffer = NULL;

	buffer = gpiod_test_create_edge_event_buffer_or_fail(16 * 64 * 2);

	g_assert_cmpuint(gpiod_edge_event_buffer_get_capacity(buffer),
			 ==, 16 * 64);
}

GPIOD_TEST_CASE(edge_event_wait_timeout)
{
	static const guint offset = 4;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	gint ret;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_edge_detection_default(line_cfg,
						     GPIOD_LINE_EDGE_BOTH);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	ret = gpiod_line_request_wait_edge_event(request, 1000000);
	g_assert_cmpint(ret, ==, 0);
}

GPIOD_TEST_CASE(cannot_request_lines_in_output_mode_with_edge_detection)
{
	static const guint offset = 4;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_edge_detection_default(line_cfg,
						     GPIOD_LINE_EDGE_BOTH);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
	g_assert_null(request);
	gpiod_test_expect_errno(EINVAL);
}

static gpointer falling_and_rising_edge_events(gpointer data)
{
	GPIOSimChip *sim = data;

	g_usleep(1000);

	g_gpiosim_chip_set_pull(sim, 2, G_GPIOSIM_PULL_UP);

	g_usleep(1000);

	g_gpiosim_chip_set_pull(sim, 2, G_GPIOSIM_PULL_DOWN);

	return NULL;
}

GPIOD_TEST_CASE(read_both_events)
{
	static const guint offset = 2;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(GThread) thread = NULL;
	g_autoptr(struct_gpiod_edge_event_buffer) buffer = NULL;
	struct gpiod_edge_event *event;
	guint64 ts_rising, ts_falling;
	gint ret;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();
	buffer = gpiod_test_create_edge_event_buffer_or_fail(64);

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_config_set_edge_detection_default(line_cfg,
						     GPIOD_LINE_EDGE_BOTH);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	thread = g_thread_new("request-release",
			      falling_and_rising_edge_events, sim);
	g_thread_ref(thread);

	/* First event. */

	ret = gpiod_line_request_wait_edge_event(request, 1000000000);
	g_assert_cmpint(ret, >, 0);
	gpiod_test_join_thread_and_return_if_failed(thread);

	ret = gpiod_line_request_read_edge_event(request, buffer, 1);
	g_assert_cmpint(ret, ==, 1);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpuint(gpiod_edge_event_buffer_get_num_events(buffer), ==, 1);
	event = gpiod_edge_event_buffer_get_event(buffer, 0);
	g_assert_nonnull(event);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpint(gpiod_edge_event_get_event_type(event),
			==, GPIOD_EDGE_EVENT_RISING_EDGE);
	g_assert_cmpuint(gpiod_edge_event_get_line_offset(event), ==, 2);
	ts_rising = gpiod_edge_event_get_timestamp(event);

	/* Second event. */

	ret = gpiod_line_request_wait_edge_event(request, 1000000000);
	g_assert_cmpint(ret, >, 0);
	gpiod_test_join_thread_and_return_if_failed(thread);

	ret = gpiod_line_request_read_edge_event(request, buffer, 1);
	g_assert_cmpint(ret, ==, 1);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpuint(gpiod_edge_event_buffer_get_num_events(buffer), ==, 1);
	event = gpiod_edge_event_buffer_get_event(buffer, 0);
	g_assert_nonnull(event);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpint(gpiod_edge_event_get_event_type(event),
			==, GPIOD_EDGE_EVENT_FALLING_EDGE);
	g_assert_cmpuint(gpiod_edge_event_get_line_offset(event), ==, 2);
	ts_falling = gpiod_edge_event_get_timestamp(event);

	g_thread_join(thread);

	g_assert_cmpuint(ts_falling, >, ts_rising);
}

GPIOD_TEST_CASE(read_rising_edge_event)
{
	static const guint offset = 2;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(GThread) thread = NULL;
	g_autoptr(struct_gpiod_edge_event_buffer) buffer = NULL;
	struct gpiod_edge_event *event;
	gint ret;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();
	buffer = gpiod_test_create_edge_event_buffer_or_fail(64);

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_config_set_edge_detection_default(line_cfg,
						     GPIOD_LINE_EDGE_RISING);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	thread = g_thread_new("edge-generator",
			      falling_and_rising_edge_events, sim);
	g_thread_ref(thread);

	/* First event. */

	ret = gpiod_line_request_wait_edge_event(request, 1000000000);
	g_assert_cmpint(ret, >, 0);
	gpiod_test_join_thread_and_return_if_failed(thread);

	ret = gpiod_line_request_read_edge_event(request, buffer, 1);
	g_assert_cmpint(ret, ==, 1);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpuint(gpiod_edge_event_buffer_get_num_events(buffer), ==, 1);
	event = gpiod_edge_event_buffer_get_event(buffer, 0);
	g_assert_nonnull(event);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpint(gpiod_edge_event_get_event_type(event),
			==, GPIOD_EDGE_EVENT_RISING_EDGE);
	g_assert_cmpuint(gpiod_edge_event_get_line_offset(event), ==, 2);

	/* Second event. */

	ret = gpiod_line_request_wait_edge_event(request, 1000000);
	g_assert_cmpint(ret, ==, 0); /* Time-out. */

	g_thread_join(thread);
}

GPIOD_TEST_CASE(read_falling_edge_event)
{
	static const guint offset = 2;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(GThread) thread = NULL;
	g_autoptr(struct_gpiod_edge_event_buffer) buffer = NULL;
	struct gpiod_edge_event *event;
	gint ret;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();
	buffer = gpiod_test_create_edge_event_buffer_or_fail(64);

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_config_set_edge_detection_default(line_cfg,
						     GPIOD_LINE_EDGE_FALLING);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	thread = g_thread_new("request-release",
			      falling_and_rising_edge_events, sim);
	g_thread_ref(thread);

	/* First event is the second generated. */

	ret = gpiod_line_request_wait_edge_event(request, 1000000000);
	g_assert_cmpint(ret, >, 0);
	gpiod_test_join_thread_and_return_if_failed(thread);

	ret = gpiod_line_request_read_edge_event(request, buffer, 1);
	g_assert_cmpint(ret, ==, 1);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpuint(gpiod_edge_event_buffer_get_num_events(buffer), ==, 1);
	event = gpiod_edge_event_buffer_get_event(buffer, 0);
	g_assert_nonnull(event);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpint(gpiod_edge_event_get_event_type(event),
			==, GPIOD_EDGE_EVENT_FALLING_EDGE);
	g_assert_cmpuint(gpiod_edge_event_get_line_offset(event), ==, 2);

	/* No more events. */

	ret = gpiod_line_request_wait_edge_event(request, 1000000);
	g_assert_cmpint(ret, ==, 0); /* Time-out. */

	g_thread_join(thread);
}

GPIOD_TEST_CASE(read_rising_edge_event_polled)
{
	static const guint offset = 2;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(GThread) thread = NULL;
	g_autoptr(struct_gpiod_edge_event_buffer) buffer = NULL;
	struct gpiod_edge_event *event;
	struct timespec ts;
	struct pollfd pfd;
	gint ret, fd;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();
	buffer = gpiod_test_create_edge_event_buffer_or_fail(64);

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_config_set_edge_detection_default(line_cfg,
						     GPIOD_LINE_EDGE_RISING);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	thread = g_thread_new("edge-generator",
			      falling_and_rising_edge_events, sim);
	g_thread_ref(thread);

	/* First event. */

	fd = gpiod_line_request_get_fd(request);

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = fd;
	pfd.events = POLLIN | POLLPRI;

	ts.tv_sec = 1;
	ts.tv_nsec = 0;

	ret = ppoll(&pfd, 1, &ts, NULL);
	g_assert_cmpint(ret, >, 0);
	gpiod_test_join_thread_and_return_if_failed(thread);

	ret = gpiod_line_request_read_edge_event(request, buffer, 1);
	g_assert_cmpint(ret, ==, 1);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpuint(gpiod_edge_event_buffer_get_num_events(buffer), ==, 1);
	event = gpiod_edge_event_buffer_get_event(buffer, 0);
	g_assert_nonnull(event);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpint(gpiod_edge_event_get_event_type(event),
			==, GPIOD_EDGE_EVENT_RISING_EDGE);
	g_assert_cmpuint(gpiod_edge_event_get_line_offset(event), ==, 2);

	/* Second event. */

	ret = gpiod_line_request_wait_edge_event(request, 1000000);
	g_assert_cmpint(ret, ==, 0); /* Time-out. */

	g_thread_join(thread);
}

static gpointer rising_edge_events_on_two_offsets(gpointer data)
{
	GPIOSimChip *sim = data;

	g_usleep(1000);

	g_gpiosim_chip_set_pull(sim, 2, G_GPIOSIM_PULL_UP);

	g_usleep(1000);

	g_gpiosim_chip_set_pull(sim, 3, G_GPIOSIM_PULL_UP);

	return NULL;
}

GPIOD_TEST_CASE(seqno)
{
	static const guint offsets[] = { 2, 3 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(GThread) thread = NULL;
	g_autoptr(struct_gpiod_edge_event_buffer) buffer = NULL;
	struct gpiod_edge_event *event;
	gint ret;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();
	buffer = gpiod_test_create_edge_event_buffer_or_fail(64);

	gpiod_request_config_set_offsets(req_cfg, 2, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_config_set_edge_detection_default(line_cfg,
						     GPIOD_LINE_EDGE_BOTH);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	thread = g_thread_new("request-release",
			      rising_edge_events_on_two_offsets, sim);
	g_thread_ref(thread);

	/* First event. */

	ret = gpiod_line_request_wait_edge_event(request, 1000000000);
	g_assert_cmpint(ret, >, 0);
	gpiod_test_join_thread_and_return_if_failed(thread);

	ret = gpiod_line_request_read_edge_event(request, buffer, 1);
	g_assert_cmpint(ret, ==, 1);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpuint(gpiod_edge_event_buffer_get_num_events(buffer), ==, 1);
	event = gpiod_edge_event_buffer_get_event(buffer, 0);
	g_assert_nonnull(event);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpuint(gpiod_edge_event_get_line_offset(event), ==, 2);
	g_assert_cmpuint(gpiod_edge_event_get_global_seqno(event), ==, 1);
	g_assert_cmpuint(gpiod_edge_event_get_line_seqno(event), ==, 1);

	/* Second event. */

	ret = gpiod_line_request_wait_edge_event(request, 1000000000);
	g_assert_cmpint(ret, >, 0);
	gpiod_test_join_thread_and_return_if_failed(thread);

	ret = gpiod_line_request_read_edge_event(request, buffer, 1);
	g_assert_cmpint(ret, ==, 1);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpuint(gpiod_edge_event_buffer_get_num_events(buffer), ==, 1);
	event = gpiod_edge_event_buffer_get_event(buffer, 0);
	g_assert_nonnull(event);
	gpiod_test_join_thread_and_return_if_failed(thread);

	g_assert_cmpuint(gpiod_edge_event_get_line_offset(event), ==, 3);
	g_assert_cmpuint(gpiod_edge_event_get_global_seqno(event), ==, 2);
	g_assert_cmpuint(gpiod_edge_event_get_line_seqno(event), ==, 1);
}

GPIOD_TEST_CASE(event_copy)
{
	static const guint offset = 2;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(GThread) thread = NULL;
	g_autoptr(struct_gpiod_edge_event_buffer) buffer = NULL;
	g_autoptr(struct_gpiod_edge_event) copy = NULL;
	struct gpiod_edge_event *event;
	gint ret;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();
	buffer = gpiod_test_create_edge_event_buffer_or_fail(64);

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_config_set_edge_detection_default(line_cfg,
						     GPIOD_LINE_EDGE_BOTH);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	g_gpiosim_chip_set_pull(sim, 2, G_GPIOSIM_PULL_UP);

	ret = gpiod_line_request_wait_edge_event(request, 1000000000);
	g_assert_cmpint(ret, >, 0);
	gpiod_test_return_if_failed();

	ret = gpiod_line_request_read_edge_event(request, buffer, 1);
	g_assert_cmpint(ret, ==, 1);
	gpiod_test_return_if_failed();

	event = gpiod_edge_event_buffer_get_event(buffer, 0);
	g_assert_nonnull(event);
	gpiod_test_return_if_failed();

	copy = gpiod_edge_event_copy(event);
	g_assert_nonnull(copy);
	g_assert_true(copy != event);
}

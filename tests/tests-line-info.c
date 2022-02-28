// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <errno.h>
#include <glib.h>
#include <gpiod.h>

#include "gpiod-test.h"
#include "gpiod-test-helpers.h"
#include "gpiod-test-sim.h"

#define GPIOD_TEST_GROUP "line-info"

GPIOD_TEST_CASE(get_line_info_good)
{
	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_line_info) info = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));

	info = gpiod_chip_get_line_info(chip, 3);
	g_assert_nonnull(info);
	g_assert_cmpuint(gpiod_line_info_get_offset(info), ==, 3);
}

GPIOD_TEST_CASE(get_line_info_offset_out_of_range)
{
	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_line_info) info = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));

	info = gpiod_chip_get_line_info(chip, 8);
	g_assert_null(info);
	gpiod_test_expect_errno(EINVAL);
}

GPIOD_TEST_CASE(line_info_basic_properties)
{
	static const struct gpiod_test_line_name names[] = {
		{ .offset = 1, .name = "foo", },
		{ .offset = 2, .name = "bar", },
		{ .offset = 4, .name = "baz", },
		{ .offset = 5, .name = "xyz", },
		{ }
	};

	static const struct gpiod_test_hog hogs[] = {
		{
			.offset = 3,
			.name = "hog3",
			.direction = G_GPIOSIM_HOG_DIR_OUTPUT_HIGH,
		},
		{
			.offset = 4,
			.name = "hog4",
			.direction = G_GPIOSIM_HOG_DIR_OUTPUT_LOW,
		},
		{ }
	};

	g_autoptr(GPIOSimChip) sim = NULL;
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_line_info) info4 = NULL;
	g_autoptr(struct_gpiod_line_info) info6 = NULL;

	sim = g_gpiosim_chip_new(
			"num-lines", 8,
			"line-names", gpiod_test_package_line_names(names),
			"hogs", gpiod_test_package_hogs(hogs),
			NULL);

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	info4 = gpiod_test_get_line_info_or_fail(chip, 4);
	info6 = gpiod_test_get_line_info_or_fail(chip, 6);

	g_assert_cmpuint(gpiod_line_info_get_offset(info4), ==, 4);
	g_assert_cmpstr(gpiod_line_info_get_name(info4), ==, "baz");
	g_assert_cmpstr(gpiod_line_info_get_consumer(info4), ==, "hog4");
	g_assert_true(gpiod_line_info_is_used(info4));
	g_assert_cmpint(gpiod_line_info_get_direction(info4), ==,
			GPIOD_LINE_DIRECTION_OUTPUT);
	g_assert_cmpint(gpiod_line_info_get_edge_detection(info4), ==,
			GPIOD_LINE_EDGE_NONE);
	g_assert_false(gpiod_line_info_is_active_low(info4));
	g_assert_cmpint(gpiod_line_info_get_bias(info4), ==,
			GPIOD_LINE_BIAS_UNKNOWN);
	g_assert_cmpint(gpiod_line_info_get_drive(info4), ==,
			GPIOD_LINE_DRIVE_PUSH_PULL);
	g_assert_cmpint(gpiod_line_info_get_event_clock(info4), ==,
			GPIOD_LINE_EVENT_CLOCK_MONOTONIC);
	g_assert_false(gpiod_line_info_is_debounced(info4));
	g_assert_cmpuint(gpiod_line_info_get_debounce_period_us(info4), ==, 0);
}

GPIOD_TEST_CASE(copy_line_info)
{
	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_line_info) info = NULL;
	g_autoptr(struct_gpiod_line_info) copy = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	info = gpiod_test_get_line_info_or_fail(chip, 3);

	copy = gpiod_line_info_copy(info);
	g_assert_nonnull(copy);
	g_assert_true(info != copy);
	g_assert_cmpuint(gpiod_line_info_get_offset(info), ==,
			 gpiod_line_info_get_offset(copy));
}

GPIOD_TEST_CASE(active_high)
{
	static const guint offset = 5;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(struct_gpiod_line_info) info = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_active_low_default(line_cfg, true);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);
	info = gpiod_chip_get_line_info(chip, 5);

	g_assert_true(gpiod_line_info_is_active_low(info));
}

GPIOD_TEST_CASE(edge_detection_settings)
{
	static const guint offsets[] = { 0, 1, 2, 3 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(struct_gpiod_line_info) info0 = NULL;
	g_autoptr(struct_gpiod_line_info) info1 = NULL;
	g_autoptr(struct_gpiod_line_info) info2 = NULL;
	g_autoptr(struct_gpiod_line_info) info3 = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 4, offsets);
	gpiod_line_config_set_edge_detection_override(line_cfg,
						GPIOD_LINE_EDGE_RISING, 1);
	gpiod_line_config_set_edge_detection_override(line_cfg,
						GPIOD_LINE_EDGE_FALLING, 2);
	gpiod_line_config_set_edge_detection_override(line_cfg,
						GPIOD_LINE_EDGE_BOTH, 3);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);
	info0 = gpiod_chip_get_line_info(chip, 0);
	info1 = gpiod_chip_get_line_info(chip, 1);
	info2 = gpiod_chip_get_line_info(chip, 2);
	info3 = gpiod_chip_get_line_info(chip, 3);

	g_assert_cmpint(gpiod_line_info_get_edge_detection(info0), ==,
			GPIOD_LINE_EDGE_NONE);
	g_assert_cmpint(gpiod_line_info_get_edge_detection(info1), ==,
			GPIOD_LINE_EDGE_RISING);
	g_assert_cmpint(gpiod_line_info_get_edge_detection(info2), ==,
			GPIOD_LINE_EDGE_FALLING);
	g_assert_cmpint(gpiod_line_info_get_edge_detection(info3), ==,
			GPIOD_LINE_EDGE_BOTH);
}

GPIOD_TEST_CASE(bias_settings)
{
	static const guint offsets[] = { 0, 1, 2, 3 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(struct_gpiod_line_info) info0 = NULL;
	g_autoptr(struct_gpiod_line_info) info1 = NULL;
	g_autoptr(struct_gpiod_line_info) info2 = NULL;
	g_autoptr(struct_gpiod_line_info) info3 = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 4, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_config_set_bias_override(line_cfg,
					    GPIOD_LINE_BIAS_DISABLED, 1);
	gpiod_line_config_set_bias_override(line_cfg,
					    GPIOD_LINE_BIAS_PULL_DOWN, 2);
	gpiod_line_config_set_bias_override(line_cfg,
					    GPIOD_LINE_BIAS_PULL_UP, 3);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);
	info0 = gpiod_chip_get_line_info(chip, 0);
	info1 = gpiod_chip_get_line_info(chip, 1);
	info2 = gpiod_chip_get_line_info(chip, 2);
	info3 = gpiod_chip_get_line_info(chip, 3);

	g_assert_cmpint(gpiod_line_info_get_bias(info0), ==,
			GPIOD_LINE_BIAS_UNKNOWN);
	g_assert_cmpint(gpiod_line_info_get_bias(info1), ==,
			GPIOD_LINE_BIAS_DISABLED);
	g_assert_cmpint(gpiod_line_info_get_bias(info2), ==,
			GPIOD_LINE_BIAS_PULL_DOWN);
	g_assert_cmpint(gpiod_line_info_get_bias(info3), ==,
			GPIOD_LINE_BIAS_PULL_UP);
}

GPIOD_TEST_CASE(drive_settings)
{
	static const guint offsets[] = { 0, 1, 2 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(struct_gpiod_line_info) info0 = NULL;
	g_autoptr(struct_gpiod_line_info) info1 = NULL;
	g_autoptr(struct_gpiod_line_info) info2 = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 3, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_config_set_drive_override(line_cfg,
					     GPIOD_LINE_DRIVE_OPEN_DRAIN, 1);
	gpiod_line_config_set_drive_override(line_cfg,
					     GPIOD_LINE_DRIVE_OPEN_SOURCE, 2);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);
	info0 = gpiod_chip_get_line_info(chip, 0);
	info1 = gpiod_chip_get_line_info(chip, 1);
	info2 = gpiod_chip_get_line_info(chip, 2);

	g_assert_cmpint(gpiod_line_info_get_drive(info0), ==,
			GPIOD_LINE_DRIVE_PUSH_PULL);
	g_assert_cmpint(gpiod_line_info_get_drive(info1), ==,
			GPIOD_LINE_DRIVE_OPEN_DRAIN);
	g_assert_cmpint(gpiod_line_info_get_drive(info2), ==,
			GPIOD_LINE_DRIVE_OPEN_SOURCE);
}

GPIOD_TEST_CASE(debounce_period)
{
	static const guint offset = 5;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(struct_gpiod_line_info) info = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_edge_detection_default(line_cfg,
						     GPIOD_LINE_EDGE_BOTH);
	gpiod_line_config_set_debounce_period_us_default(line_cfg, 1000);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);
	info = gpiod_chip_get_line_info(chip, 5);

	g_assert_cmpuint(gpiod_line_info_get_debounce_period_us(info),
			 ==, 1000);
}

GPIOD_TEST_CASE(event_clock)
{
	static const guint offsets[] = { 0, 1 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(struct_gpiod_line_info) info0 = NULL;
	g_autoptr(struct_gpiod_line_info) info1 = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 2, offsets);
	gpiod_line_config_set_event_clock_override(line_cfg,
					GPIOD_LINE_EVENT_CLOCK_REALTIME, 1);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);
	info0 = gpiod_chip_get_line_info(chip, 0);
	info1 = gpiod_chip_get_line_info(chip, 1);

	g_assert_cmpint(gpiod_line_info_get_event_clock(info0), ==,
			GPIOD_LINE_EVENT_CLOCK_MONOTONIC);
	g_assert_cmpint(gpiod_line_info_get_event_clock(info1), ==,
			GPIOD_LINE_EVENT_CLOCK_REALTIME);
}

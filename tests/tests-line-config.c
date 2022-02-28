// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <errno.h>
#include <glib.h>
#include <gpiod.h>
#include <stdint.h>

#include "gpiod-test.h"
#include "gpiod-test-helpers.h"
#include "gpiod-test-sim.h"

#define GPIOD_TEST_GROUP "line-config"

GPIOD_TEST_CASE(default_config)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_cmpint(gpiod_line_config_get_direction_default(config), ==,
			GPIOD_LINE_DIRECTION_AS_IS);
	g_assert_cmpint(gpiod_line_config_get_edge_detection_default(config),
			==, GPIOD_LINE_EDGE_NONE);
	g_assert_cmpint(gpiod_line_config_get_bias_default(config), ==,
			GPIOD_LINE_BIAS_AS_IS);
	g_assert_cmpint(gpiod_line_config_get_drive_default(config), ==,
			GPIOD_LINE_DRIVE_PUSH_PULL);
	g_assert_false(gpiod_line_config_get_active_low_default(config));
	g_assert_cmpuint(
		gpiod_line_config_get_debounce_period_us_default(config), ==,
		0);
	g_assert_cmpint(gpiod_line_config_get_event_clock_default(config), ==,
			GPIOD_LINE_EVENT_CLOCK_MONOTONIC);
	g_assert_cmpint(gpiod_line_config_get_output_value_default(config), ==,
			GPIOD_LINE_VALUE_INACTIVE);
	g_assert_cmpuint(gpiod_line_config_get_num_overrides(config),
			 ==, 0);
}

GPIOD_TEST_CASE(defaults_are_used_for_non_overridden_offsets)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_cmpint(gpiod_line_config_get_direction_offset(config, 4), ==,
			GPIOD_LINE_DIRECTION_AS_IS);
	g_assert_cmpint(gpiod_line_config_get_edge_detection_offset(config, 4),
			==, GPIOD_LINE_EDGE_NONE);
	g_assert_cmpint(gpiod_line_config_get_bias_offset(config, 4), ==,
			GPIOD_LINE_BIAS_AS_IS);
	g_assert_cmpint(gpiod_line_config_get_drive_offset(config, 4), ==,
			GPIOD_LINE_DRIVE_PUSH_PULL);
	g_assert_false(gpiod_line_config_get_active_low_offset(config, 4));
	g_assert_cmpuint(
		gpiod_line_config_get_debounce_period_us_offset(config, 4), ==,
		0);
	g_assert_cmpint(gpiod_line_config_get_event_clock_offset(config, 4),
			==, GPIOD_LINE_EVENT_CLOCK_MONOTONIC);
	g_assert_cmpint(gpiod_line_config_get_output_value_offset(config, 4),
			==, GPIOD_LINE_VALUE_INACTIVE);
	g_assert_cmpuint(gpiod_line_config_get_num_overrides(config),
			 ==, 0);
}

GPIOD_TEST_CASE(set_and_clear_direction_override)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_cmpint(gpiod_line_config_get_direction_default(config), ==,
			GPIOD_LINE_DIRECTION_AS_IS);
	gpiod_line_config_set_direction_override(config,
						 GPIOD_LINE_DIRECTION_OUTPUT,
						 3);

	g_assert_cmpint(gpiod_line_config_get_direction_default(config), ==,
			GPIOD_LINE_DIRECTION_AS_IS);
	g_assert_cmpint(gpiod_line_config_get_direction_offset(config, 3), ==,
			GPIOD_LINE_DIRECTION_OUTPUT);
	g_assert_true(gpiod_line_config_direction_is_overridden(config, 3));
	gpiod_line_config_clear_direction_override(config, 3);
	g_assert_cmpint(gpiod_line_config_get_direction_offset(config, 3), ==,
			GPIOD_LINE_DIRECTION_AS_IS);
	g_assert_false(gpiod_line_config_direction_is_overridden(config, 3));
}

GPIOD_TEST_CASE(invalid_direction)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	gpiod_line_config_set_direction_default(config, INT32_MAX);
	g_assert_cmpint(gpiod_line_config_get_direction_default(config),
			==, GPIOD_LINE_DIRECTION_AS_IS);
}

GPIOD_TEST_CASE(set_and_clear_edge_detection_override)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_cmpint(gpiod_line_config_get_edge_detection_default(config),
			==, GPIOD_LINE_EDGE_NONE);
	gpiod_line_config_set_edge_detection_override(config,
						GPIOD_LINE_EDGE_FALLING, 3);

	g_assert_cmpint(gpiod_line_config_get_edge_detection_default(config),
			==, GPIOD_LINE_EDGE_NONE);
	g_assert_cmpint(gpiod_line_config_get_edge_detection_offset(config, 3),
			==, GPIOD_LINE_EDGE_FALLING);
	g_assert_true(gpiod_line_config_edge_detection_is_overridden(config,
								     3));
	gpiod_line_config_clear_edge_detection_override(config, 3);
	g_assert_cmpint(gpiod_line_config_get_edge_detection_offset(config, 3),
			==, GPIOD_LINE_EDGE_NONE);
	g_assert_false(gpiod_line_config_edge_detection_is_overridden(config,
								      3));
}

GPIOD_TEST_CASE(invalid_edge)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	gpiod_line_config_set_edge_detection_default(config, INT32_MAX);
	g_assert_cmpint(gpiod_line_config_get_edge_detection_default(config),
			==, GPIOD_LINE_EDGE_NONE);
}

GPIOD_TEST_CASE(set_and_clear_bias_override)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_cmpint(gpiod_line_config_get_bias_default(config),
			==, GPIOD_LINE_BIAS_AS_IS);
	gpiod_line_config_set_bias_override(config, GPIOD_LINE_BIAS_PULL_UP, 0);

	g_assert_cmpint(gpiod_line_config_get_bias_default(config),
			==, GPIOD_LINE_BIAS_AS_IS);
	g_assert_cmpint(gpiod_line_config_get_bias_offset(config, 0),
			==, GPIOD_LINE_BIAS_PULL_UP);
	g_assert_true(gpiod_line_config_bias_is_overridden(config, 0));
	gpiod_line_config_clear_bias_override(config, 0);
	g_assert_cmpint(gpiod_line_config_get_bias_offset(config, 0),
			==, GPIOD_LINE_BIAS_AS_IS);
	g_assert_false(gpiod_line_config_bias_is_overridden(config, 0));
}

GPIOD_TEST_CASE(invalid_bias)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	gpiod_line_config_set_bias_default(config, INT32_MAX);
	g_assert_cmpint(gpiod_line_config_get_bias_default(config),
			==, GPIOD_LINE_BIAS_AS_IS);
}

GPIOD_TEST_CASE(set_and_clear_drive_override)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_cmpint(gpiod_line_config_get_drive_default(config),
			==, GPIOD_LINE_DRIVE_PUSH_PULL);
	gpiod_line_config_set_drive_override(config,
					     GPIOD_LINE_DRIVE_OPEN_DRAIN, 3);

	g_assert_cmpint(gpiod_line_config_get_drive_default(config),
			==, GPIOD_LINE_DRIVE_PUSH_PULL);
	g_assert_cmpint(gpiod_line_config_get_drive_offset(config, 3),
			==, GPIOD_LINE_DRIVE_OPEN_DRAIN);
	g_assert_true(gpiod_line_config_drive_is_overridden(config, 3));
	gpiod_line_config_clear_drive_override(config, 3);
	g_assert_cmpint(gpiod_line_config_get_drive_offset(config, 3),
			==, GPIOD_LINE_DRIVE_PUSH_PULL);
	g_assert_false(gpiod_line_config_drive_is_overridden(config, 3));
}

GPIOD_TEST_CASE(invalid_drive)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	gpiod_line_config_set_drive_default(config, INT32_MAX);
	g_assert_cmpint(gpiod_line_config_get_drive_default(config),
			==, GPIOD_LINE_BIAS_AS_IS);
}

GPIOD_TEST_CASE(set_and_clear_active_low_override)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_false(gpiod_line_config_get_active_low_default(config));
	gpiod_line_config_set_active_low_override(config, true, 3);

	g_assert_false(gpiod_line_config_get_active_low_default(config));
	g_assert_true(gpiod_line_config_get_active_low_offset(config, 3));
	g_assert_true(gpiod_line_config_active_low_is_overridden(config, 3));
	gpiod_line_config_clear_active_low_override(config, 3);
	g_assert_false(gpiod_line_config_get_active_low_offset(config, 3));
	g_assert_false(gpiod_line_config_active_low_is_overridden(config, 3));
}

GPIOD_TEST_CASE(set_and_clear_debounce_period_override)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_cmpuint(
		gpiod_line_config_get_debounce_period_us_default(config),
		==, 0);
	gpiod_line_config_set_debounce_period_us_override(config, 5000, 3);

	g_assert_cmpuint(
		gpiod_line_config_get_debounce_period_us_default(config),
		==, 0);
	g_assert_cmpuint(
		gpiod_line_config_get_debounce_period_us_offset(config, 3),
		==, 5000);
	g_assert_true(
		gpiod_line_config_debounce_period_us_is_overridden(config, 3));
	gpiod_line_config_clear_debounce_period_us_override(config, 3);
	g_assert_cmpuint(
		gpiod_line_config_get_debounce_period_us_offset(config, 3),
		==, 0);
	g_assert_false(
		gpiod_line_config_debounce_period_us_is_overridden(config, 3));
}

GPIOD_TEST_CASE(set_and_clear_event_clock_override)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_cmpint(gpiod_line_config_get_event_clock_default(config),
			==, GPIOD_LINE_EVENT_CLOCK_MONOTONIC);
	gpiod_line_config_set_event_clock_override(config,
					GPIOD_LINE_EVENT_CLOCK_REALTIME, 3);

	g_assert_cmpint(gpiod_line_config_get_event_clock_default(config),
			==, GPIOD_LINE_EVENT_CLOCK_MONOTONIC);
	g_assert_cmpint(gpiod_line_config_get_event_clock_offset(config, 3),
			==, GPIOD_LINE_EVENT_CLOCK_REALTIME);
	g_assert_true(gpiod_line_config_event_clock_is_overridden(config, 3));
	gpiod_line_config_clear_event_clock_override(config, 3);
	g_assert_cmpint(gpiod_line_config_get_event_clock_offset(config, 3),
			==, GPIOD_LINE_EVENT_CLOCK_MONOTONIC);
	g_assert_false(gpiod_line_config_event_clock_is_overridden(config, 3));
}

GPIOD_TEST_CASE(invalid_event_clock)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	gpiod_line_config_set_event_clock_default(config, INT32_MAX);
	g_assert_cmpint(gpiod_line_config_get_event_clock_default(config),
			==, GPIOD_LINE_EVENT_CLOCK_MONOTONIC);
}

GPIOD_TEST_CASE(set_and_clear_output_value_override)
{
	g_autoptr(struct_gpiod_line_config) config = NULL;

	config = gpiod_test_create_line_config_or_fail();

	g_assert_cmpint(gpiod_line_config_get_output_value_default(config),
			==, GPIOD_LINE_VALUE_INACTIVE);
	gpiod_line_config_set_output_value_override(config, 3,
						    GPIOD_LINE_VALUE_ACTIVE);

	g_assert_cmpint(gpiod_line_config_get_output_value_default(config),
			==, GPIOD_LINE_VALUE_INACTIVE);
	g_assert_cmpint(gpiod_line_config_get_output_value_offset(config, 3),
			==, GPIOD_LINE_VALUE_ACTIVE);
	g_assert_true(gpiod_line_config_output_value_is_overridden(config, 3));
	gpiod_line_config_clear_output_value_override(config, 3);
	g_assert_cmpint(gpiod_line_config_get_output_value_offset(config, 3),
			==, 0);
	g_assert_false(gpiod_line_config_output_value_is_overridden(config, 3));
}

GPIOD_TEST_CASE(set_multiple_output_values)
{
	static const guint offsets[] = { 3, 4, 5, 6 };
	static const gint values[] = { GPIOD_LINE_VALUE_ACTIVE,
				       GPIOD_LINE_VALUE_INACTIVE,
				       GPIOD_LINE_VALUE_ACTIVE,
				       GPIOD_LINE_VALUE_INACTIVE };

	g_autoptr(struct_gpiod_line_config) config = NULL;
	guint overridden_offsets[4], i;
	gint overriden_props[4];

	config = gpiod_test_create_line_config_or_fail();

	gpiod_line_config_set_output_values(config, 4, offsets, values);

	g_assert_cmpint(gpiod_line_config_get_output_value_default(config),
			==, 0);

	for (i = 0; i < 4; i++)
		g_assert_cmpint(
			gpiod_line_config_get_output_value_offset(config,
								  offsets[i]),
			==, values[i]);

	g_assert_cmpuint(gpiod_line_config_get_num_overrides(config),
			==, 4);
	gpiod_line_config_get_overrides(config,
					overridden_offsets, overriden_props);

	for (i = 0; i < 4; i++) {
		g_assert_cmpuint(overridden_offsets[i], ==, offsets[i]);
		g_assert_cmpint(overriden_props[i], ==,
				GPIOD_LINE_CONFIG_PROP_OUTPUT_VALUE);
	}
}

GPIOD_TEST_CASE(config_too_complex)
{
	static guint offsets[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 16, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	line_cfg = gpiod_test_create_line_config_or_fail();
	req_cfg = gpiod_test_create_request_config_or_fail();

	/*
	 * We need to make the line_config structure exceed the kernel's
	 * maximum of 10 attributes.
	 */
	gpiod_line_config_set_direction_override(line_cfg,
					GPIOD_LINE_DIRECTION_OUTPUT, 0);
	gpiod_line_config_set_direction_override(line_cfg,
					GPIOD_LINE_DIRECTION_INPUT, 1);
	gpiod_line_config_set_edge_detection_override(line_cfg,
						      GPIOD_LINE_EDGE_BOTH, 2);
	gpiod_line_config_set_debounce_period_us_override(line_cfg, 1000, 2);
	gpiod_line_config_set_active_low_override(line_cfg, true, 3);
	gpiod_line_config_set_direction_override(line_cfg,
					GPIOD_LINE_DIRECTION_OUTPUT, 4);
	gpiod_line_config_set_drive_override(line_cfg,
					     GPIOD_LINE_DRIVE_OPEN_DRAIN, 4);
	gpiod_line_config_set_direction_override(line_cfg,
					GPIOD_LINE_DIRECTION_OUTPUT, 8);
	gpiod_line_config_set_drive_override(line_cfg,
					     GPIOD_LINE_DRIVE_OPEN_SOURCE, 8);
	gpiod_line_config_set_direction_override(line_cfg,
						 GPIOD_LINE_DIRECTION_INPUT, 5);
	gpiod_line_config_set_bias_override(line_cfg,
					    GPIOD_LINE_BIAS_PULL_DOWN, 5);
	gpiod_line_config_set_event_clock_override(line_cfg,
					GPIOD_LINE_EVENT_CLOCK_REALTIME, 6);
	gpiod_line_config_set_output_value_override(line_cfg, 7,
						    GPIOD_LINE_VALUE_ACTIVE);

	gpiod_request_config_set_offsets(req_cfg, 12, offsets);

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
	g_assert_null(request);
	gpiod_test_expect_errno(E2BIG);
}

/*
 * This triggers the E2BIG error by exhausting the number of overrides in
 * the line_config structure instead of making the kernel representation too
 * complex.
 */
GPIOD_TEST_CASE(define_too_many_overrides)
{
	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 128, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	guint offsets[65], i;

	for (i = 0; i < 65; i++)
		offsets[i] = i;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	line_cfg = gpiod_test_create_line_config_or_fail();
	req_cfg = gpiod_test_create_request_config_or_fail();

	for (i = 0; i < 65; i++)
		gpiod_line_config_set_direction_override(line_cfg,
				GPIOD_LINE_DIRECTION_OUTPUT, offsets[i]);

	gpiod_request_config_set_offsets(req_cfg, 64, offsets);

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
	g_assert_null(request);
	gpiod_test_expect_errno(E2BIG);
}

GPIOD_TEST_CASE(ignore_overrides_for_offsets_not_in_request_config)
{
	static guint offsets[] = { 2, 3, 4, 6, 7 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(struct_gpiod_line_info) info3 = NULL;
	g_autoptr(struct_gpiod_line_info) info4 = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	line_cfg = gpiod_test_create_line_config_or_fail();
	req_cfg = gpiod_test_create_request_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 5, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_INPUT);
	gpiod_line_config_set_direction_override(line_cfg,
					GPIOD_LINE_DIRECTION_OUTPUT, 4);
	gpiod_line_config_set_direction_override(line_cfg,
					GPIOD_LINE_DIRECTION_OUTPUT, 5);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);
	info3 = gpiod_test_get_line_info_or_fail(chip, 3);
	info4 = gpiod_test_get_line_info_or_fail(chip, 4);

	g_assert_cmpint(gpiod_line_info_get_direction(info3), ==,
			GPIOD_LINE_DIRECTION_INPUT);
	g_assert_cmpint(gpiod_line_info_get_direction(info4), ==,
			GPIOD_LINE_DIRECTION_OUTPUT);

	gpiod_line_config_set_direction_override(line_cfg,
					GPIOD_LINE_DIRECTION_OUTPUT, 0);

	gpiod_test_reconfigure_lines_or_fail(request, line_cfg);
	/* Nothing to check, value successfully ignored. */
}

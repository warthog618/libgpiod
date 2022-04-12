// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <glib.h>
#include <gpiod.h>

#include "gpiod-test.h"
#include "gpiod-test-helpers.h"
#include "gpiod-test-sim.h"

#define GPIOD_TEST_GROUP "line-request"

GPIOD_TEST_CASE(request_fails_with_no_offsets)
{
	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 4, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;

	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	g_assert_cmpuint(gpiod_request_config_get_num_offsets(req_cfg), ==, 0);
	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
	g_assert_null(request);
	gpiod_test_expect_errno(EINVAL);
}

GPIOD_TEST_CASE(request_fails_with_duplicate_offsets)
{
	static const guint offsets[] = { 0, 2, 2, 3 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 4, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 4, offsets);

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
	g_assert_null(request);
	gpiod_test_expect_errno(EBUSY);
}

GPIOD_TEST_CASE(request_fails_with_offset_out_of_bounds)
{
	static const guint offsets[] = { 2, 6 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 4, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 2, offsets);

	request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
	g_assert_null(request);
	gpiod_test_expect_errno(EINVAL);
}

GPIOD_TEST_CASE(set_consumer)
{
	static const guint offset = 2;
	static const gchar *const consumer = "foobar";

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 4, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(struct_gpiod_line_info) info = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_request_config_set_consumer(req_cfg, consumer);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	info = gpiod_test_get_line_info_or_fail(chip, offset);

	g_assert_cmpstr(gpiod_line_info_get_consumer(info), ==, consumer);
}

GPIOD_TEST_CASE(empty_consumer)
{
	static const guint offset = 2;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 4, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	g_autoptr(struct_gpiod_line_info) info = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	info = gpiod_test_get_line_info_or_fail(chip, offset);

	g_assert_cmpstr(gpiod_line_info_get_consumer(info), ==, "?");
}

GPIOD_TEST_CASE(default_output_value)
{
	/*
	 * Have a hole in offsets on purpose - make sure it's not set by
	 * accident.
	 */
	static const guint offsets[] = { 0, 1, 3, 4 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	guint i;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 4, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_config_set_output_value_default(line_cfg,
						   GPIOD_LINE_VALUE_ACTIVE);

	g_gpiosim_chip_set_pull(sim, 2, G_GPIOSIM_PULL_DOWN);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	for (i = 0; i < 4; i++)
		g_assert_cmpint(g_gpiosim_chip_get_value(sim, offsets[i]),
				==, GPIOD_LINE_VALUE_ACTIVE);

	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 2), ==,
			GPIOD_LINE_VALUE_INACTIVE);
}

GPIOD_TEST_CASE(default_and_overridden_output_value)
{
	static const guint offsets[] = { 0, 1, 2, 3 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 4, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_config_set_output_value_default(line_cfg, 1);
	gpiod_line_config_set_output_value_override(line_cfg, 2, 0);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	g_assert_cmpint(g_gpiosim_chip_get_value(sim, offsets[0]),
			==, GPIOD_LINE_VALUE_ACTIVE);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, offsets[1]),
			==, GPIOD_LINE_VALUE_ACTIVE);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, offsets[2]),
			==, GPIOD_LINE_VALUE_INACTIVE);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, offsets[3]),
			==, GPIOD_LINE_VALUE_ACTIVE);
}

GPIOD_TEST_CASE(read_all_values)
{
	static const guint offsets[] = { 0, 2, 4, 5, 7 };
	static const gint pulls[] = { 0, 1, 0, 1, 1 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	gint ret, values[5];
	guint i;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 5, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_INPUT);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	for (i = 0; i < 5; i++)
		g_gpiosim_chip_set_pull(sim, offsets[i],
			pulls[i] ? G_GPIOSIM_PULL_UP : G_GPIOSIM_PULL_DOWN);

	ret = gpiod_line_request_get_values(request, values);
	g_assert_cmpint(ret, ==, 0);
	gpiod_test_return_if_failed();

	for (i = 0; i < 5; i++)
		g_assert_cmpint(values[i], ==, pulls[i]);
}

GPIOD_TEST_CASE(request_multiple_values_but_read_one)
{
	static const guint offsets[] = { 0, 2, 4, 5, 7 };
	static const gint pulls[] = { 0, 1, 0, 1, 1 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	gint ret;
	guint i;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 5, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_INPUT);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	for (i = 0; i < 5; i++)
		g_gpiosim_chip_set_pull(sim, offsets[i],
			pulls[i] ? G_GPIOSIM_PULL_UP : G_GPIOSIM_PULL_DOWN);

	ret = gpiod_line_request_get_value(request, 5);
	g_assert_cmpint(ret, ==, 1);
}

GPIOD_TEST_CASE(set_all_values)
{
	static const guint offsets[] = { 0, 2, 4, 5, 6 };
	static const gint values[] = { GPIOD_LINE_VALUE_ACTIVE,
				       GPIOD_LINE_VALUE_INACTIVE,
				       GPIOD_LINE_VALUE_ACTIVE,
				       GPIOD_LINE_VALUE_ACTIVE,
				       GPIOD_LINE_VALUE_ACTIVE };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	gint ret;
	guint i;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 5, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	ret = gpiod_line_request_set_values(request, values);
	g_assert_cmpint(ret, ==, 0);
	gpiod_test_return_if_failed();

	for (i = 0; i < 5; i++)
		g_assert_cmpint(g_gpiosim_chip_get_value(sim, offsets[i]),
				==, values[i]);
}

GPIOD_TEST_CASE(set_line_after_requesting)
{
	static const guint offsets[] = { 0, 1, 3, 4 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 4, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_config_set_output_value_default(line_cfg,
						   GPIOD_LINE_VALUE_INACTIVE);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	gpiod_line_request_set_value(request, 1, GPIOD_LINE_VALUE_ACTIVE);

	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 0), ==, 0);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 1), ==, 1);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 3), ==, 0);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 4), ==, 0);
}

GPIOD_TEST_CASE(request_survives_parent_chip)
{
	static const guint offset = 0;

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 4, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	gint ret;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 1, &offset);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_config_set_output_value_default(line_cfg,
						   GPIOD_LINE_VALUE_ACTIVE);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	g_assert_cmpint(gpiod_line_request_get_value(request, offset), ==,
			GPIOD_LINE_VALUE_ACTIVE);

	gpiod_chip_close(chip);
	chip = NULL;

	ret = gpiod_line_request_set_value(request, offset,
					   GPIOD_LINE_VALUE_ACTIVE);
	g_assert_cmpint(ret, ==, 0);
	gpiod_test_return_if_failed();

	ret = gpiod_line_request_set_value(request, offset,
					   GPIOD_LINE_VALUE_ACTIVE);
	g_assert_cmpint(ret, ==, 0);
	gpiod_test_return_if_failed();
}

GPIOD_TEST_CASE(request_with_overridden_direction)
{
	static const guint offsets[] = { 0, 1, 2, 3 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 4, NULL);
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
	gpiod_line_config_set_direction_override(line_cfg,
						 GPIOD_LINE_DIRECTION_INPUT, 3);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);
	info0 = gpiod_test_get_line_info_or_fail(chip, 0);
	info1 = gpiod_test_get_line_info_or_fail(chip, 1);
	info2 = gpiod_test_get_line_info_or_fail(chip, 2);
	info3 = gpiod_test_get_line_info_or_fail(chip, 3);

	g_assert_cmpint(gpiod_line_info_get_direction(info0), ==,
			GPIOD_LINE_DIRECTION_OUTPUT);
	g_assert_cmpint(gpiod_line_info_get_direction(info1), ==,
			GPIOD_LINE_DIRECTION_OUTPUT);
	g_assert_cmpint(gpiod_line_info_get_direction(info2), ==,
			GPIOD_LINE_DIRECTION_OUTPUT);
	g_assert_cmpint(gpiod_line_info_get_direction(info3), ==,
			GPIOD_LINE_DIRECTION_INPUT);
}

GPIOD_TEST_CASE(num_lines)
{
	static const guint offsets[] = { 0, 1, 2, 3, 7, 8, 11, 14 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 16, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	guint read_back[8], i;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 8, offsets);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	g_assert_cmpuint(gpiod_line_request_get_num_lines(request), ==, 8);
	gpiod_test_return_if_failed();
	gpiod_line_request_get_offsets(request, read_back);
	for (i = 0; i < 8; i++)
		g_assert_cmpuint(read_back[i], ==, offsets[i]);
}

GPIOD_TEST_CASE(active_low_read_value)
{
	static const guint offsets[] = { 2, 3 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	gint value;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 2, offsets);
	gpiod_line_config_set_direction_override(line_cfg,
					GPIOD_LINE_DIRECTION_INPUT, 2);
	gpiod_line_config_set_direction_override(line_cfg,
					GPIOD_LINE_DIRECTION_OUTPUT, 3);
	gpiod_line_config_set_active_low_default(line_cfg, true);
	gpiod_line_config_set_output_value_default(line_cfg,
						   GPIOD_LINE_VALUE_ACTIVE);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	g_gpiosim_chip_set_pull(sim, 2, G_GPIOSIM_PULL_DOWN);
	value = gpiod_line_request_get_value(request, 2);
	g_assert_cmpint(value, ==, GPIOD_LINE_VALUE_ACTIVE);

	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 3), ==, 0);
}

GPIOD_TEST_CASE(reconfigure_lines)
{
	static const guint offsets[] = { 0, 1, 2, 3 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 4, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	gint values[4], ret;
	guint i;

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 4, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);

	values[0] = 1;
	values[1] = 0;
	values[2] = 1;
	values[3] = 0;
	gpiod_line_config_set_output_values(line_cfg, 4, offsets, values);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	for (i = 0; i < 4; i++)
		g_assert_cmpint(g_gpiosim_chip_get_value(sim, offsets[i]),
				==, values[i]);

	values[0] = 0;
	values[1] = 1;
	values[2] = 0;
	values[3] = 1;
	gpiod_line_config_set_output_values(line_cfg, 4, offsets, values);

	ret = gpiod_line_request_reconfigure_lines(request, line_cfg);
	g_assert_cmpint(ret, ==, 0);
	gpiod_test_return_if_failed();

	for (i = 0; i < 4; i++)
		g_assert_cmpint(g_gpiosim_chip_get_value(sim, offsets[i]),
				==, values[i]);
}

GPIOD_TEST_CASE(request_lines_with_unordered_offsets)
{
	static const guint offsets[] = { 5, 1, 7, 2, 0, 6 };

	g_autoptr(GPIOSimChip) sim = g_gpiosim_chip_new("num-lines", 8, NULL);
	g_autoptr(struct_gpiod_chip) chip = NULL;
	g_autoptr(struct_gpiod_request_config) req_cfg = NULL;
	g_autoptr(struct_gpiod_line_config) line_cfg = NULL;
	g_autoptr(struct_gpiod_line_request) request = NULL;
	guint cfg_offsets[4];
	gint values[4];

	chip = gpiod_test_open_chip_or_fail(g_gpiosim_chip_get_dev_path(sim));
	req_cfg = gpiod_test_create_request_config_or_fail();
	line_cfg = gpiod_test_create_line_config_or_fail();

	gpiod_request_config_set_offsets(req_cfg, 6, offsets);
	gpiod_line_config_set_direction_default(line_cfg,
						GPIOD_LINE_DIRECTION_OUTPUT);
	gpiod_line_config_set_output_value_default(line_cfg, 1);

	values[0] = 0;
	values[1] = 1;
	values[2] = 0;
	values[3] = 0;
	cfg_offsets[0] = 7;
	cfg_offsets[1] = 1;
	cfg_offsets[2] = 6;
	cfg_offsets[3] = 0;
	gpiod_line_config_set_output_values(line_cfg, 4, cfg_offsets, values);

	request = gpiod_test_request_lines_or_fail(chip, req_cfg, line_cfg);

	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 0), ==,
			GPIOD_LINE_VALUE_INACTIVE);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 1), ==,
			GPIOD_LINE_VALUE_ACTIVE);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 2), ==,
			GPIOD_LINE_VALUE_ACTIVE);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 5), ==,
			GPIOD_LINE_VALUE_ACTIVE);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 6), ==,
			GPIOD_LINE_VALUE_INACTIVE);
	g_assert_cmpint(g_gpiosim_chip_get_value(sim, 7), ==,
			GPIOD_LINE_VALUE_INACTIVE);
}

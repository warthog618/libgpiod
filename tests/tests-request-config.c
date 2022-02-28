// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com>

#include <glib.h>
#include <gpiod.h>

#include "gpiod-test.h"
#include "gpiod-test-helpers.h"

#define GPIOD_TEST_GROUP "request-config"

GPIOD_TEST_CASE(default_config)
{
	g_autoptr(struct_gpiod_request_config) config = NULL;

	config = gpiod_test_create_request_config_or_fail();

	g_assert_null(gpiod_request_config_get_consumer(config));
	g_assert_cmpuint(gpiod_request_config_get_num_offsets(config), ==, 0);
	g_assert_cmpuint(gpiod_request_config_get_event_buffer_size(config),
			 ==, 0);
}

GPIOD_TEST_CASE(consumer)
{
	g_autoptr(struct_gpiod_request_config) config = NULL;

	config = gpiod_test_create_request_config_or_fail();

	gpiod_request_config_set_consumer(config, "foobar");
	g_assert_cmpstr(gpiod_request_config_get_consumer(config),
			==, "foobar");
}

GPIOD_TEST_CASE(offsets)
{
	static const guint offsets[] = { 0, 3, 4, 7 };

	g_autoptr(struct_gpiod_request_config) config = NULL;
	guint read_back[4], i;

	config = gpiod_test_create_request_config_or_fail();

	gpiod_request_config_set_offsets(config, 4, offsets);
	g_assert_cmpuint(gpiod_request_config_get_num_offsets(config), ==, 4);
	memset(read_back, 0, sizeof(read_back));
	gpiod_request_config_get_offsets(config, read_back);
	for (i = 0; i < 4; i++)
		g_assert_cmpuint(read_back[i], ==, offsets[i]);
}

GPIOD_TEST_CASE(max_offsets)
{
	static const guint offsets_good[] = {
		 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63
	};

	static const guint offsets_bad[] = {
		 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
		48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
		64
	};

	g_autoptr(struct_gpiod_request_config) config = NULL;

	config = gpiod_test_create_request_config_or_fail();

	gpiod_request_config_set_offsets(config, 64, offsets_good);
	g_assert_cmpuint(gpiod_request_config_get_num_offsets(config), ==, 64);

	gpiod_request_config_set_offsets(config, 65, offsets_bad);
	/* Should get truncated. */
	g_assert_cmpuint(gpiod_request_config_get_num_offsets(config), ==, 64);
}

GPIOD_TEST_CASE(event_buffer_size)
{
	g_autoptr(struct_gpiod_request_config) config = NULL;

	config = gpiod_test_create_request_config_or_fail();

	gpiod_request_config_set_event_buffer_size(config, 128);
	g_assert_cmpuint(gpiod_request_config_get_event_buffer_size(config),
			 ==, 128);
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2022 Bartosz Golaszewski <brgl@bgdev.pl> */

#ifndef __GPIOD_TEST_HELPERS_H__
#define __GPIOD_TEST_HELPERS_H__

#include <errno.h>
#include <glib.h>
#include <gpiod.h>

#include "gpiod-test-sim.h"

/*
 * These typedefs are needed to make g_autoptr work - it doesn't accept
 * regular 'struct typename' syntax.
 */

typedef struct gpiod_chip struct_gpiod_chip;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(struct_gpiod_chip, gpiod_chip_close);

typedef struct gpiod_line_info struct_gpiod_line_info;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(struct_gpiod_line_info, gpiod_line_info_free);

typedef struct gpiod_info_event struct_gpiod_info_event;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(struct_gpiod_info_event, gpiod_info_event_free);

typedef struct gpiod_line_config struct_gpiod_line_config;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(struct_gpiod_line_config, gpiod_line_config_free);

typedef struct gpiod_request_config struct_gpiod_request_config;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(struct_gpiod_request_config,
			      gpiod_request_config_free);

typedef struct gpiod_line_request struct_gpiod_line_request;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(struct_gpiod_line_request,
			      gpiod_line_request_release);

typedef struct gpiod_edge_event struct_gpiod_edge_event;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(struct_gpiod_edge_event, gpiod_edge_event_free);

typedef struct gpiod_edge_event_buffer struct_gpiod_edge_event_buffer;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(struct_gpiod_edge_event_buffer,
			      gpiod_edge_event_buffer_free);

#define gpiod_test_return_if_failed() \
	do { \
		if (g_test_failed()) \
			return; \
	} while (0)

#define gpiod_test_join_thread_and_return_if_failed(_thread) \
	do { \
		if (g_test_failed()) { \
			g_thread_join(_thread); \
			return; \
		} \
	} while (0)

#define gpiod_test_open_chip_or_fail(_path) \
	({ \
		struct gpiod_chip *_chip = gpiod_chip_open((_path)); \
		g_assert_nonnull(_chip); \
		gpiod_test_return_if_failed(); \
		_chip; \
	})

#define gpiod_test_get_line_info_or_fail(_chip, _offset) \
	({ \
		struct gpiod_line_info *_info = \
				gpiod_chip_get_line_info((_chip), (_offset)); \
		g_assert_nonnull(_info); \
		gpiod_test_return_if_failed(); \
		_info; \
	})

#define gpiod_test_create_line_config_or_fail() \
	({ \
		struct gpiod_line_config *_config = \
				gpiod_line_config_new(); \
		g_assert_nonnull(_config); \
		gpiod_test_return_if_failed(); \
		_config; \
	})

#define gpiod_test_create_edge_event_buffer_or_fail(_capacity) \
	({ \
		struct gpiod_edge_event_buffer *_buffer = \
				gpiod_edge_event_buffer_new(_capacity); \
		g_assert_nonnull(_buffer); \
		gpiod_test_return_if_failed(); \
		_buffer; \
	})

#define gpiod_test_create_request_config_or_fail() \
	({ \
		struct gpiod_request_config *_config = \
				gpiod_request_config_new(); \
		g_assert_nonnull(_config); \
		gpiod_test_return_if_failed(); \
		_config; \
	})

#define gpiod_test_request_lines_or_fail(_chip, _req_cfg, _line_cfg) \
	({ \
		struct gpiod_line_request *_request = \
			gpiod_chip_request_lines((_chip), \
						 (_req_cfg), (_line_cfg)); \
		g_assert_nonnull(_request); \
		gpiod_test_return_if_failed(); \
		_request; \
	})

#define gpiod_test_reconfigure_lines_or_fail(_request, _line_cfg) \
	do { \
		gint ret = gpiod_line_request_reconfigure_lines((_request), \
								(_line_cfg)); \
		g_assert_cmpint(ret, ==, 0); \
		gpiod_test_return_if_failed(); \
	} while (0)

#define gpiod_test_expect_errno(_expected) \
	g_assert_cmpint((_expected), ==, errno)

struct gpiod_test_line_name {
	guint offset;
	const gchar *name;
};

struct gpiod_test_hog {
	guint offset;
	const gchar *name;
	GPIOSimHogDir direction;
};

GVariant *
gpiod_test_package_line_names(const struct gpiod_test_line_name *names);
GVariant *gpiod_test_package_hogs(const struct gpiod_test_hog *hogs);

#endif /* __GPIOD_TEST_HELPERS_H__ */

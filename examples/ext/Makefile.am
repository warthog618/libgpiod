# SPDX-License-Identifier: GPL-2.0-or-later
# SPDX-FileCopyrightText: 2024 Kent Gibson <warthog618@gmail.com>

AM_CFLAGS = -I$(top_srcdir)/include/ -include $(top_builddir)/config.h
AM_CFLAGS += -Wall -Wextra -g -std=gnu89

LDADD = $(top_builddir)/lib/libgpiod.la

noinst_PROGRAMS = \
	async_watch_line_value \
	get_line_value \
	toggle_line_value \
	watch_line_rising \
	watch_line_value 


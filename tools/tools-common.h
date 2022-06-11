/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2017-2021 Bartosz Golaszewski <bartekgola@gmail.com> */

#ifndef __GPIOD_TOOLS_COMMON_H__
#define __GPIOD_TOOLS_COMMON_H__

#include <dirent.h>
#include <gpiod.h>

/*
 * Various helpers for the GPIO tools.
 *
 * NOTE: This is not a stable interface - it's only to avoid duplicating
 * common code.
 */

#define NORETURN		__attribute__((noreturn))
#define UNUSED			__attribute__((unused))
#define PRINTF(fmt, arg)	__attribute__((format(printf, fmt, arg)))
#define ARRAY_SIZE(x)		(sizeof(x) / sizeof(*(x)))

#define GETOPT_NULL_LONGOPT	NULL, 0, NULL, 0

struct resolved_line {
        // from the command line
        const char *id;
        // id is a name, not an offset
        bool id_is_name;
        // index into chip_paths in struct line_resolver
        // only valid if found
        int chip_idx;
        // if found then offset on chip
        // if not found then atoi(id) or -1
        int offset;
        // matching line found on a GPIO chip
        // if set then chip_idx and offset identify the physical line
        bool found;
        // place holder for line value in gpioget/set.
        int value;
};

// a resolver from requested line names/offsets to lines on the system
struct line_resolver {
        // number of chips the lines span, and number of entries is in chip_paths
        int num_chips;
        // paths to the relevant chips
        char **chip_paths;
        // number of lines in lines
        int num_lines;
        // number of lines found
        int num_found;
        // exhaustive search to check line names are unique
        bool strict;
        // descriptors for the requested lines
        struct resolved_line lines[];
};

const char *get_progname(void);
void log_perror(const char *fmt, ...) PRINTF(1, 2);
void die(const char *fmt, ...) NORETURN PRINTF(1, 2);
void die_perror(const char *fmt, ...) NORETURN PRINTF(1, 2);
void print_version(void);
int parse_bias_or_die(const char *option);
int parse_period(const char *option);
unsigned int parse_period_or_die(const char *option);
int parse_periods_or_die(char *option, unsigned int **periods);
int parse_uint(const char *option);
unsigned int parse_uint_or_die(const char *option);
void print_bias_help(void);
void print_chip_help(void);
void print_period_help(void);
void print_event_time(uint64_t evtime, int mode);
void print_line_info(struct gpiod_line_info *info);
bool chip_path_lookup(const char *id, char **path_ptr);
int chip_paths(const char *id, char ***paths_ptr);
int all_chip_paths(char ***paths_ptr);
struct line_resolver *resolve_lines(int num_lines, char **lines, const char *chip_id,
                                    bool strict, bool by_name);
void free_line_resolver(struct line_resolver *resolver);
int get_line_offsets_and_values(struct line_resolver *resolver, int chip_idx,
			        unsigned int *offsets, int *values);
const char *get_line_name(struct line_resolver *resolver, int chip_idx,
			   unsigned int offset);
void set_line_values(struct line_resolver *resolver, int chip_idx, int *values);

#endif /* __GPIOD_TOOLS_COMMON_H__ */

// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * This file is part of libgpiod.
 *
 * Copyright (C) 2017-2018 Bartosz Golaszewski <bartekgola@gmail.com>
 */

/* Low-level, core library code. */

#include <errno.h>
#include <fcntl.h>
#include <gpiod.h>
#include <limits.h>
#include <linux/gpio.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

enum {
	LINE_FREE = 0,
	LINE_REQUESTED_VALUES,
	LINE_REQUESTED_EVENTS,
};

struct line_fd_handle {
	int fd;
	int refcount;
};

struct gpiod_line {
	unsigned int offset;

	/* The direction of the GPIO line. */
	int direction;

	/* The active-state configuration. */
	int active_state;

	/* The logical value last written to the line. */
	int output_value;

	/* The GPIOLINE_FLAGs returned by GPIO_GET_LINEINFO_IOCTL. */
	__u32 info_flags;

	/* The GPIOD_LINE_REQUEST_FLAGs provided to request the line. */
	__u32 req_flags;

	/*
	 * Indicator of LINE_FREE, LINE_REQUESTED_VALUES or
	 * LINE_REQUESTED_EVENTS.
	 */
	int state;

	struct gpiod_chip *chip;
	struct line_fd_handle *fd_handle;

	char name[32];
	char consumer[32];
};

struct gpiod_chip {
	struct gpiod_line **lines;
	unsigned int num_lines;

	int fd;

	char name[32];
	char label[32];
};

static bool is_gpiochip_cdev(const char *path)
{
	char *name, *realname, *sysfsp, sysfsdev[16], devstr[16];
	struct stat statbuf;
	bool ret = false;
	int rv, fd;
	ssize_t rd;

	rv = lstat(path, &statbuf);
	if (rv)
		goto out;

	/*
	 * Is it a symbolic link? We have to resolve symbolic link before
	 * checking the rest.
	 */
	realname = S_ISLNK(statbuf.st_mode) ? realpath(path, NULL)
					    : strdup(path);
	if (realname == NULL)
		goto out;

	rv = stat(realname, &statbuf);
	if (rv)
		goto out_free_realname;

	/* Is it a character device? */
	if (!S_ISCHR(statbuf.st_mode)) {
		/*
		 * Passing a file descriptor not associated with a character
		 * device to ioctl() makes it set errno to ENOTTY. Let's do
		 * the same in order to stay compatible with the versions of
		 * libgpiod from before the introduction of this routine.
		 */
		errno = ENOTTY;
		goto out_free_realname;
	}

	/* Get the basename. */
	name = basename(realname);

	/* Do we have a corresponding sysfs attribute? */
	rv = asprintf(&sysfsp, "/sys/bus/gpio/devices/%s/dev", name);
	if (rv < 0)
		goto out_free_realname;

	if (access(sysfsp, R_OK) != 0) {
		/*
		 * This is a character device but not the one we're after.
		 * Before the introduction of this function, we'd fail with
		 * ENOTTY on the first GPIO ioctl() call for this file
		 * descriptor. Let's stay compatible here and keep returning
		 * the same error code.
		 */
		errno = ENOTTY;
		goto out_free_sysfsp;
	}

	/*
	 * Make sure the major and minor numbers of the character device
	 * correspond to the ones in the dev attribute in sysfs.
	 */
	snprintf(devstr, sizeof(devstr), "%u:%u",
		 major(statbuf.st_rdev), minor(statbuf.st_rdev));

	fd = open(sysfsp, O_RDONLY);
	if (fd < 0)
		goto out_free_sysfsp;

	memset(sysfsdev, 0, sizeof(sysfsdev));
	rd = read(fd, sysfsdev, sizeof(sysfsdev) - 1);
	close(fd);
	if (rd < 0)
		goto out_free_sysfsp;

	rd--; /* Ignore trailing newline. */
	if ((size_t)rd != strlen(devstr) ||
	    strncmp(sysfsdev, devstr, rd) != 0) {
		errno = ENODEV;
		goto out_free_sysfsp;
	}

	ret = true;

out_free_sysfsp:
	free(sysfsp);
out_free_realname:
	free(realname);
out:
	return ret;
}

struct gpiod_chip *gpiod_chip_open(const char *path)
{
	struct gpiochip_info info;
	struct gpiod_chip *chip;
	int rv, fd;

	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return NULL;

	/*
	 * We were able to open the file but is it really a gpiochip character
	 * device?
	 */
	if (!is_gpiochip_cdev(path))
		goto err_close_fd;

	chip = malloc(sizeof(*chip));
	if (!chip)
		goto err_close_fd;

	memset(chip, 0, sizeof(*chip));
	memset(&info, 0, sizeof(info));

	rv = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info);
	if (rv < 0)
		goto err_free_chip;

	chip->fd = fd;
	chip->num_lines = info.lines;

	/*
	 * GPIO device must have a name - don't bother checking this field. In
	 * the worst case (would have to be a weird kernel bug) it'll be empty.
	 */
	strncpy(chip->name, info.name, sizeof(chip->name));

	/*
	 * The kernel sets the label of a GPIO device to "unknown" if it
	 * hasn't been defined in DT, board file etc. On the off-chance that
	 * we got an empty string, do the same.
	 */
	if (info.label[0] == '\0')
		strncpy(chip->label, "unknown", sizeof(chip->label));
	else
		strncpy(chip->label, info.label, sizeof(chip->label));

	return chip;

err_free_chip:
	free(chip);
err_close_fd:
	close(fd);

	return NULL;
}

void gpiod_chip_close(struct gpiod_chip *chip)
{
	struct gpiod_line *line;
	unsigned int i;

	if (chip->lines) {
		for (i = 0; i < chip->num_lines; i++) {
			line = chip->lines[i];
			if (line) {
				gpiod_line_release(line);
				free(line);
			}
		}

		free(chip->lines);
	}

	close(chip->fd);
	free(chip);
}

const char *gpiod_chip_name(struct gpiod_chip *chip)
{
	return chip->name;
}

const char *gpiod_chip_label(struct gpiod_chip *chip)
{
	return chip->label;
}

unsigned int gpiod_chip_num_lines(struct gpiod_chip *chip)
{
	return chip->num_lines;
}

struct gpiod_line *
gpiod_chip_get_line(struct gpiod_chip *chip, unsigned int offset)
{
	struct gpiod_line *line;
	int rv;

	if (offset >= chip->num_lines) {
		errno = EINVAL;
		return NULL;
	}

	if (!chip->lines) {
		chip->lines = calloc(chip->num_lines,
				     sizeof(struct gpiod_line *));
		if (!chip->lines)
			return NULL;
	}

	if (!chip->lines[offset]) {
		line = malloc(sizeof(*line));
		if (!line)
			return NULL;

		memset(line, 0, sizeof(*line));

		line->offset = offset;
		line->chip = chip;

		chip->lines[offset] = line;
	} else {
		line = chip->lines[offset];
	}

	rv = gpiod_line_update(line);
	if (rv < 0)
		return NULL;

	return line;
}

static struct line_fd_handle *line_make_fd_handle(int fd)
{
	struct line_fd_handle *handle;

	handle = malloc(sizeof(*handle));
	if (!handle)
		return NULL;

	handle->fd = fd;
	handle->refcount = 0;

	return handle;
}

static void line_fd_incref(struct gpiod_line *line)
{
	line->fd_handle->refcount++;
}

static void line_fd_decref(struct gpiod_line *line)
{
	struct line_fd_handle *handle = line->fd_handle;

	handle->refcount--;

	if (handle->refcount == 0) {
		close(handle->fd);
		free(handle);
		line->fd_handle = NULL;
	}
}

static void line_set_fd(struct gpiod_line *line, struct line_fd_handle *handle)
{
	line->fd_handle = handle;
	line_fd_incref(line);
}

static int line_get_fd(struct gpiod_line *line)
{
	return line->fd_handle->fd;
}

struct gpiod_chip *gpiod_line_get_chip(struct gpiod_line *line)
{
	return line->chip;
}

unsigned int gpiod_line_offset(struct gpiod_line *line)
{
	return line->offset;
}

const char *gpiod_line_name(struct gpiod_line *line)
{
	return line->name[0] == '\0' ? NULL : line->name;
}

const char *gpiod_line_consumer(struct gpiod_line *line)
{
	return line->consumer[0] == '\0' ? NULL : line->consumer;
}

int gpiod_line_direction(struct gpiod_line *line)
{
	return line->direction;
}

int gpiod_line_active_state(struct gpiod_line *line)
{
	return line->active_state;
}

int gpiod_line_bias(struct gpiod_line *line)
{
	if (line->info_flags & GPIOLINE_FLAG_BIAS_DISABLE)
		return GPIOD_LINE_BIAS_DISABLE;
	if (line->info_flags & GPIOLINE_FLAG_BIAS_PULL_UP)
		return GPIOD_LINE_BIAS_PULL_UP;
	if (line->info_flags & GPIOLINE_FLAG_BIAS_PULL_DOWN)
		return GPIOD_LINE_BIAS_PULL_DOWN;

	return GPIOD_LINE_BIAS_AS_IS;
}

bool gpiod_line_is_used(struct gpiod_line *line)
{
	return line->info_flags & GPIOLINE_FLAG_KERNEL;
}

bool gpiod_line_is_open_drain(struct gpiod_line *line)
{
	return line->info_flags & GPIOLINE_FLAG_OPEN_DRAIN;
}

bool gpiod_line_is_open_source(struct gpiod_line *line)
{
	return line->info_flags & GPIOLINE_FLAG_OPEN_SOURCE;
}

bool gpiod_line_needs_update(struct gpiod_line *line GPIOD_UNUSED)
{
	return false;
}

static int line_info_v2_to_info_flags(struct gpio_v2_line_info *info)
{
	int iflags = 0;

	if (info->flags & GPIO_V2_LINE_FLAG_USED)
		iflags |= GPIOLINE_FLAG_KERNEL;

	if (info->flags & GPIO_V2_LINE_FLAG_OPEN_DRAIN)
		iflags |= GPIOLINE_FLAG_OPEN_DRAIN;
	if (info->flags & GPIO_V2_LINE_FLAG_OPEN_SOURCE)
		iflags |= GPIOLINE_FLAG_OPEN_SOURCE;

	if (info->flags & GPIO_V2_LINE_FLAG_BIAS_DISABLED)
		iflags |= GPIOLINE_FLAG_BIAS_DISABLE;
	if (info->flags & GPIO_V2_LINE_FLAG_BIAS_PULL_UP)
		iflags |= GPIOLINE_FLAG_BIAS_PULL_UP;
	if (info->flags & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN)
		iflags |= GPIOLINE_FLAG_BIAS_PULL_DOWN;

	return iflags;
}

int gpiod_line_update(struct gpiod_line *line)
{
	struct gpio_v2_line_info info;
	int rv;

	memset(&info, 0, sizeof(info));
	info.offset = line->offset;

	rv = ioctl(line->chip->fd, GPIO_V2_GET_LINEINFO_IOCTL, &info);
	if (rv < 0)
		return -1;

	line->direction = info.flags & GPIO_V2_LINE_FLAG_OUTPUT
						? GPIOD_LINE_DIRECTION_OUTPUT
						: GPIOD_LINE_DIRECTION_INPUT;

	line->active_state = info.flags & GPIO_V2_LINE_FLAG_ACTIVE_LOW
						? GPIOD_LINE_ACTIVE_STATE_LOW
						: GPIOD_LINE_ACTIVE_STATE_HIGH;

	line->info_flags = line_info_v2_to_info_flags(&info);

	strncpy(line->name, info.name, sizeof(line->name));
	strncpy(line->consumer, info.consumer, sizeof(line->consumer));

	return 0;
}

static bool line_bulk_same_chip(struct gpiod_line_bulk *bulk)
{
	struct gpiod_line *first_line, *line;
	struct gpiod_chip *first_chip, *chip;
	unsigned int i;

	if (bulk->num_lines == 1)
		return true;

	first_line = gpiod_line_bulk_get_line(bulk, 0);
	first_chip = gpiod_line_get_chip(first_line);

	for (i = 1; i < bulk->num_lines; i++) {
		line = bulk->lines[i];
		chip = gpiod_line_get_chip(line);

		if (first_chip != chip) {
			errno = EINVAL;
			return false;
		}
	}

	return true;
}

static bool line_bulk_all_requested(struct gpiod_line_bulk *bulk)
{
	struct gpiod_line *line, **lineptr;

	gpiod_line_bulk_foreach_line(bulk, line, lineptr) {
		if (!gpiod_line_is_requested(line)) {
			errno = EPERM;
			return false;
		}
	}

	return true;
}

static bool line_bulk_all_requested_values(struct gpiod_line_bulk *bulk)
{
	struct gpiod_line *line, **lineptr;

	gpiod_line_bulk_foreach_line(bulk, line, lineptr) {
		if (line->state != LINE_REQUESTED_VALUES) {
			errno = EPERM;
			return false;
		}
	}

	return true;
}

static bool line_bulk_all_free(struct gpiod_line_bulk *bulk)
{
	struct gpiod_line *line, **lineptr;

	gpiod_line_bulk_foreach_line(bulk, line, lineptr) {
		if (!gpiod_line_is_free(line)) {
			errno = EBUSY;
			return false;
		}
	}

	return true;
}

static bool line_request_direction_is_valid(int direction)
{
	if ((direction == GPIOD_LINE_REQUEST_DIRECTION_AS_IS) ||
	    (direction == GPIOD_LINE_REQUEST_DIRECTION_INPUT) ||
	    (direction == GPIOD_LINE_REQUEST_DIRECTION_OUTPUT))
		return true;

	errno = EINVAL;
	return false;
}

static void line_request_type_to_gpio_v2_line_config(int reqtype,
		struct gpio_v2_line_config *config)
{
	if (reqtype == GPIOD_LINE_REQUEST_DIRECTION_AS_IS)
		return;

	if (reqtype == GPIOD_LINE_REQUEST_DIRECTION_OUTPUT) {
		config->flags |= GPIO_V2_LINE_FLAG_OUTPUT;
		return;
	}
	config->flags |= GPIO_V2_LINE_FLAG_INPUT;

	if (reqtype == GPIOD_LINE_REQUEST_EVENT_RISING_EDGE)
		config->flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
	else if (reqtype == GPIOD_LINE_REQUEST_EVENT_FALLING_EDGE)
		config->flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
	else if (reqtype == GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES)
		config->flags |= (GPIO_V2_LINE_FLAG_EDGE_RISING |
				  GPIO_V2_LINE_FLAG_EDGE_FALLING);
}

static void line_request_flag_to_gpio_v2_line_config(int flags,
		struct gpio_v2_line_config *config)
{
	if (flags & GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW)
		config->flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;

	if (flags & GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN)
		config->flags |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
	else if (flags & GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE)
		config->flags |= GPIO_V2_LINE_FLAG_OPEN_SOURCE;

	if (flags & GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE)
		config->flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
	else if (flags & GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN)
		config->flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
	else if (flags & GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP)
		config->flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
}

static void line_request_config_to_gpio_v2_line_config(
		const struct gpiod_line_request_config *reqcfg,
		struct gpio_v2_line_config *lc)
{
	line_request_type_to_gpio_v2_line_config(reqcfg->request_type, lc);
	line_request_flag_to_gpio_v2_line_config(reqcfg->flags, lc);
}

static bool line_request_config_validate(
		const struct gpiod_line_request_config *config)
{
	int bias_flags = 0;

	if ((config->request_type != GPIOD_LINE_REQUEST_DIRECTION_OUTPUT) &&
	    (config->flags & (GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN |
			      GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE)))
		return false;


	if ((config->flags & GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN) &&
	    (config->flags & GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE)) {
		return false;
	}

	if (config->flags & GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE)
		bias_flags++;
	if (config->flags & GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP)
		bias_flags++;
	if (config->flags & GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN)
		bias_flags++;
	if (bias_flags > 1)
		return false;

	return true;
}

static void lines_bitmap_set_bit(__u64 *bits, int nr)
{
	*bits |= _BITULL(nr);
}

static void lines_bitmap_clear_bit(__u64 *bits, int nr)
{
	*bits &= ~_BITULL(nr);
}

static int lines_bitmap_test_bit(__u64 bits, int nr)
{
	return !!(bits & _BITULL(nr));
}

static void lines_bitmap_assign_bit(__u64 *bits, int nr, bool value)
{
	if (value)
		lines_bitmap_set_bit(bits, nr);
	else
		lines_bitmap_clear_bit(bits, nr);
}

static int line_request(struct gpiod_line_bulk *bulk,
			const struct gpiod_line_request_config *config,
			const int *vals)
{
	struct gpiod_line *line;
	struct line_fd_handle *line_fd;
	struct gpio_v2_line_request req;
	unsigned int i;
	int rv, fd, state;

	if (!line_request_config_validate(config)) {
		errno = EINVAL;
		return -1;
	}

	memset(&req, 0, sizeof(req));

	req.num_lines = gpiod_line_bulk_num_lines(bulk);
	line_request_config_to_gpio_v2_line_config(config, &req.config);
	if (req.config.flags & (GPIO_V2_LINE_FLAG_EDGE_RISING |
				GPIO_V2_LINE_FLAG_EDGE_FALLING))
		state = LINE_REQUESTED_EVENTS;
	else
		state = LINE_REQUESTED_VALUES;

	gpiod_line_bulk_foreach_line_off(bulk, line, i)
		req.offsets[i] = gpiod_line_offset(line);

	if (config->request_type == GPIOD_LINE_REQUEST_DIRECTION_OUTPUT &&
	    vals) {
		req.config.num_attrs = 1;
		req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
		gpiod_line_bulk_foreach_line_off(bulk, line, i) {
			lines_bitmap_assign_bit(
				&req.config.attrs[0].mask, i, 1);
			lines_bitmap_assign_bit(
				&req.config.attrs[0].attr.values,
				i, vals[i]);
		}
	}

	if (config->consumer)
		strncpy(req.consumer, config->consumer,
			sizeof(req.consumer) - 1);

	line = gpiod_line_bulk_get_line(bulk, 0);
	fd = line->chip->fd;

	rv = ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req);
	if (rv < 0)
		return -1;

	line_fd = line_make_fd_handle(req.fd);
	if (!line_fd)
		return -1;

	gpiod_line_bulk_foreach_line_off(bulk, line, i) {
		line->state = state;
		line->req_flags = config->flags;
		if (config->request_type == GPIOD_LINE_REQUEST_DIRECTION_OUTPUT)
			line->output_value = lines_bitmap_test_bit(
				req.config.attrs[0].attr.values, i);
		line_set_fd(line, line_fd);

		rv = gpiod_line_update(line);
		if (rv) {
			gpiod_line_release_bulk(bulk);
			return rv;
		}
	}

	return 0;
}


int gpiod_line_request(struct gpiod_line *line,
		       const struct gpiod_line_request_config *config,
		       int default_val)
{
	struct gpiod_line_bulk bulk;

	gpiod_line_bulk_init(&bulk);
	gpiod_line_bulk_add(&bulk, line);

	return gpiod_line_request_bulk(&bulk, config, &default_val);
}

int gpiod_line_request_bulk(struct gpiod_line_bulk *bulk,
			    const struct gpiod_line_request_config *config,
			    const int *vals)
{
	if (!line_bulk_same_chip(bulk) || !line_bulk_all_free(bulk))
		return -1;

	return line_request(bulk, config, vals);
}

void gpiod_line_release(struct gpiod_line *line)
{
	struct gpiod_line_bulk bulk;

	gpiod_line_bulk_init(&bulk);
	gpiod_line_bulk_add(&bulk, line);

	gpiod_line_release_bulk(&bulk);
}

void gpiod_line_release_bulk(struct gpiod_line_bulk *bulk)
{
	struct gpiod_line *line, **lineptr;

	gpiod_line_bulk_foreach_line(bulk, line, lineptr) {
		if (line->state != LINE_FREE) {
			line_fd_decref(line);
			line->state = LINE_FREE;
		}
	}
}

bool gpiod_line_is_requested(struct gpiod_line *line)
{
	return (line->state == LINE_REQUESTED_VALUES ||
		line->state == LINE_REQUESTED_EVENTS);
}

bool gpiod_line_is_free(struct gpiod_line *line)
{
	return line->state == LINE_FREE;
}

int gpiod_line_get_value(struct gpiod_line *line)
{
	struct gpiod_line_bulk bulk;
	int rv, value;

	gpiod_line_bulk_init(&bulk);
	gpiod_line_bulk_add(&bulk, line);

	rv = gpiod_line_get_value_bulk(&bulk, &value);
	if (rv < 0)
		return -1;

	return value;
}

int gpiod_line_get_value_bulk(struct gpiod_line_bulk *bulk, int *values)
{
	struct gpio_v2_line_values lv;
	struct gpiod_line *line;
	unsigned int i;
	int rv, fd;

	if (!line_bulk_same_chip(bulk) || !line_bulk_all_requested(bulk))
		return -1;

	line = gpiod_line_bulk_get_line(bulk, 0);

	memset(&lv, 0, sizeof(lv));

	for (i = 0; i < gpiod_line_bulk_num_lines(bulk); i++)
		lines_bitmap_set_bit(&lv.mask, i);

	fd = line_get_fd(line);

	rv = ioctl(fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &lv);
	if (rv < 0)
		return -1;

	for (i = 0; i < gpiod_line_bulk_num_lines(bulk); i++)
		values[i] = lines_bitmap_test_bit(lv.bits, i);

	return 0;
}

int gpiod_line_set_value(struct gpiod_line *line, int value)
{
	struct gpiod_line_bulk bulk;

	gpiod_line_bulk_init(&bulk);
	gpiod_line_bulk_add(&bulk, line);

	return gpiod_line_set_value_bulk(&bulk, &value);
}

int gpiod_line_set_value_bulk(struct gpiod_line_bulk *bulk, const int *values)
{
	struct gpio_v2_line_values lv;
	struct gpiod_line *line;
	unsigned int i;
	int rv, fd;

	if (!line_bulk_same_chip(bulk) || !line_bulk_all_requested(bulk))
		return -1;

	memset(&lv, 0, sizeof(lv));

	for (i = 0; i < gpiod_line_bulk_num_lines(bulk); i++) {
		lines_bitmap_set_bit(&lv.mask, i);
		lines_bitmap_assign_bit(&lv.bits, i, values && values[i]);
	}

	line = gpiod_line_bulk_get_line(bulk, 0);
	fd = line_get_fd(line);

	rv = ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &lv);
	if (rv < 0)
		return -1;

	gpiod_line_bulk_foreach_line_off(bulk, line, i)
		line->output_value = lines_bitmap_test_bit(lv.bits, i);

	return 0;
}

int gpiod_line_set_config(struct gpiod_line *line, int direction,
			  int flags, int value)
{
	struct gpiod_line_bulk bulk;

	gpiod_line_bulk_init(&bulk);
	gpiod_line_bulk_add(&bulk, line);

	return gpiod_line_set_config_bulk(&bulk, direction, flags, &value);
}

int gpiod_line_set_config_bulk(struct gpiod_line_bulk *bulk,
			       int direction, int flags,
			       const int *values)
{
	struct gpio_v2_line_config hcfg;
	struct gpiod_line *line;
	unsigned int i;
	int rv, fd;

	if (!line_bulk_same_chip(bulk) ||
	    !line_bulk_all_requested_values(bulk))
		return -1;

	if (!line_request_direction_is_valid(direction))
		return -1;

	memset(&hcfg, 0, sizeof(hcfg));

	line_request_flag_to_gpio_v2_line_config(flags, &hcfg);
	line_request_type_to_gpio_v2_line_config(direction, &hcfg);
	if (direction == GPIOD_LINE_REQUEST_DIRECTION_OUTPUT && values) {
		hcfg.num_attrs = 1;
		hcfg.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
		for (i = 0; i < gpiod_line_bulk_num_lines(bulk); i++) {
			lines_bitmap_assign_bit(&hcfg.attrs[0].mask, i, 1);
			lines_bitmap_assign_bit(
				&hcfg.attrs[0].attr.values, i, values[i]);
		}
	}

	line = gpiod_line_bulk_get_line(bulk, 0);
	fd = line_get_fd(line);

	rv = ioctl(fd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &hcfg);
	if (rv < 0)
		return -1;

	gpiod_line_bulk_foreach_line_off(bulk, line, i) {
		line->req_flags = flags;
		if (direction == GPIOD_LINE_REQUEST_DIRECTION_OUTPUT)
			line->output_value = lines_bitmap_test_bit(
				hcfg.attrs[0].attr.values, i);

		rv = gpiod_line_update(line);
		if (rv < 0)
			return rv;
	}
	return 0;
}

int gpiod_line_set_flags(struct gpiod_line *line, int flags)
{
	struct gpiod_line_bulk bulk;

	gpiod_line_bulk_init(&bulk);
	gpiod_line_bulk_add(&bulk, line);

	return gpiod_line_set_flags_bulk(&bulk, flags);
}

int gpiod_line_set_flags_bulk(struct gpiod_line_bulk *bulk, int flags)
{
	struct gpiod_line *line;
	int values[GPIOD_LINE_BULK_MAX_LINES];
	unsigned int i;
	int direction;

	line = gpiod_line_bulk_get_line(bulk, 0);
	if (line->direction == GPIOD_LINE_DIRECTION_OUTPUT) {
		gpiod_line_bulk_foreach_line_off(bulk, line, i)
			values[i] = line->output_value;

		direction = GPIOD_LINE_REQUEST_DIRECTION_OUTPUT;
	} else {
		direction = GPIOD_LINE_REQUEST_DIRECTION_INPUT;
	}

	return gpiod_line_set_config_bulk(bulk, direction,
					  flags, values);
}

int gpiod_line_set_direction_input(struct gpiod_line *line)
{
	return gpiod_line_set_config(line, GPIOD_LINE_REQUEST_DIRECTION_INPUT,
				     line->req_flags, 0);
}

int gpiod_line_set_direction_input_bulk(struct gpiod_line_bulk *bulk)
{
	struct gpiod_line *line;

	line = gpiod_line_bulk_get_line(bulk, 0);
	return gpiod_line_set_config_bulk(bulk,
					  GPIOD_LINE_REQUEST_DIRECTION_INPUT,
					  line->req_flags, NULL);
}

int gpiod_line_set_direction_output(struct gpiod_line *line, int value)
{
	return gpiod_line_set_config(line, GPIOD_LINE_REQUEST_DIRECTION_OUTPUT,
				     line->req_flags, value);
}

int gpiod_line_set_direction_output_bulk(struct gpiod_line_bulk *bulk,
					 const int *values)
{
	struct gpiod_line *line;

	line = gpiod_line_bulk_get_line(bulk, 0);
	return gpiod_line_set_config_bulk(bulk,
					  GPIOD_LINE_REQUEST_DIRECTION_OUTPUT,
					  line->req_flags, values);
}

int gpiod_line_event_wait(struct gpiod_line *line,
			  const struct timespec *timeout)
{
	struct gpiod_line_bulk bulk;

	gpiod_line_bulk_init(&bulk);
	gpiod_line_bulk_add(&bulk, line);

	return gpiod_line_event_wait_bulk(&bulk, timeout, NULL);
}

int gpiod_line_event_wait_bulk(struct gpiod_line_bulk *bulk,
			       const struct timespec *timeout,
			       struct gpiod_line_bulk *event_bulk)
{
	struct pollfd pfd;
	struct gpiod_line *line;
	int rv;

	if (!line_bulk_same_chip(bulk) || !line_bulk_all_requested(bulk))
		return -1;

	line = gpiod_line_bulk_get_line(bulk, 0);
	pfd.fd = line_get_fd(line);
	pfd.events = POLLIN | POLLPRI;

	rv = ppoll(&pfd, 1, timeout, NULL);
	if (rv < 0)
		return -1;
	else if (rv == 0)
		return 0;

	if (pfd.revents) {
		if (pfd.revents & POLLNVAL) {
			errno = EINVAL;
			return -1;
		}

		if (event_bulk) {
			gpiod_line_bulk_init(event_bulk);
			line = gpiod_line_bulk_get_line(bulk, 0);
			gpiod_line_bulk_add(event_bulk, line);
		}
	}

	return 1;
}

int gpiod_line_event_read(struct gpiod_line *line,
			  struct gpiod_line_event *event)
{
	int ret;

	ret = gpiod_line_event_read_multiple(line, event, 1);
	if (ret < 0)
		return -1;

	return 0;
}

int gpiod_line_event_read_multiple(struct gpiod_line *line,
				   struct gpiod_line_event *events,
				   unsigned int num_events)
{
	int fd;

	fd = gpiod_line_event_get_fd(line);
	if (fd < 0)
		return -1;

	return gpiod_line_event_read_fd_multiple(fd, events, num_events);
}

int gpiod_line_event_get_fd(struct gpiod_line *line)
{
	if (line->state != LINE_REQUESTED_EVENTS) {
		errno = EPERM;
		return -1;
	}

	return line_get_fd(line);
}

int gpiod_line_event_read_fd(int fd, struct gpiod_line_event *event)
{
	int ret;

	ret = gpiod_line_event_read_fd_multiple(fd, event, 1);
	if (ret < 0)
		return -1;

	return 0;
}

int gpiod_line_event_read_fd_multiple(int fd, struct gpiod_line_event *events,
				      unsigned int num_events)
{
	/*
	 * 16 is the maximum number of events the kernel can store in the FIFO
	 * so we can allocate the buffer on the stack.
	 *
	 * NOTE: This is no longer strictly true for uAPI v2.  While 16 is
	 * the default for single line, a request with multiple lines will
	 * have a larger buffer.  So need to rethink the allocation here,
	 * or at least the comment above...
	 */
	struct gpio_v2_line_event evdata[16], *curr;
	struct gpiod_line_event *event;
	unsigned int events_read, i;
	ssize_t rd;

	memset(evdata, 0, sizeof(evdata));

	if (num_events > 16)
		num_events = 16;

	rd = read(fd, evdata, num_events * sizeof(*evdata));
	if (rd < 0) {
		return -1;
	} else if ((unsigned int)rd < sizeof(*evdata)) {
		errno = EIO;
		return -1;
	}

	events_read = rd / sizeof(*evdata);
	if (events_read < num_events)
		num_events = events_read;

	for (i = 0; i < num_events; i++) {
		curr = &evdata[i];
		event = &events[i];

		event->offset = curr->offset;
		event->event_type = curr->id == GPIO_V2_LINE_EVENT_RISING_EDGE
					? GPIOD_LINE_EVENT_RISING_EDGE
					: GPIOD_LINE_EVENT_FALLING_EDGE;
		event->ts.tv_sec = curr->timestamp_ns / 1000000000ULL;
		event->ts.tv_nsec = curr->timestamp_ns % 1000000000ULL;
	}

	return i;
}

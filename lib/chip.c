// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2021 Bartosz Golaszewski <brgl@bgdev.pl>

/* Line attribute data structure and functions. */

#include <errno.h>
#include <fcntl.h>
#include <gpiod.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "internal.h"

struct gpiod_chip {
	int fd;
	char *path;
};

GPIOD_API struct gpiod_chip *gpiod_chip_open(const char *path)
{
	struct gpiod_chip *chip;
	int fd;

	if (!gpiod_is_gpiochip_device(path))
		return NULL;

	fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0)
		return NULL;

	chip = malloc(sizeof(*chip));
	if (!chip)
		goto err_close_fd;

	memset(chip, 0, sizeof(*chip));

	chip->path = strdup(path);
	if (!chip->path)
		goto err_free_chip;

	chip->fd = fd;

	return chip;

err_free_chip:
	free(chip);
err_close_fd:
	close(fd);

	return NULL;
}

GPIOD_API void gpiod_chip_close(struct gpiod_chip *chip)
{
	if (!chip)
		return;

	close(chip->fd);
	free(chip->path);
	free(chip);
}

static int chip_read_chip_info(int fd, struct gpiochip_info *info)
{
	int ret;

	memset(info, 0, sizeof(*info));

	ret = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, info);
	if (ret)
		return -1;

	return 0;
}

GPIOD_API struct gpiod_chip_info *gpiod_chip_get_info(struct gpiod_chip *chip)
{
	struct gpiochip_info info;
	int ret;

	ret = chip_read_chip_info(chip->fd, &info);
	if (ret < 0)
		return NULL;

	return gpiod_chip_info_from_kernel(&info);
}

GPIOD_API const char *gpiod_chip_get_path(struct gpiod_chip *chip)
{
	return chip->path;
}

static int chip_read_line_info(int fd, unsigned int offset,
			       struct gpio_v2_line_info *info, bool watch)
{
	int ret, cmd;

	memset(info, 0, sizeof(*info));
	info->offset = offset;

	cmd = watch ? GPIO_V2_GET_LINEINFO_WATCH_IOCTL :
		      GPIO_V2_GET_LINEINFO_IOCTL;

	ret = ioctl(fd, cmd, info);
	if (ret)
		return -1;

	return 0;
}

static struct gpiod_line_info *
chip_get_line_info(struct gpiod_chip *chip, unsigned int offset, bool watch)
{
	struct gpio_v2_line_info info;
	int ret;

	ret = chip_read_line_info(chip->fd, offset, &info, watch);
	if (ret)
		return NULL;

	return gpiod_line_info_from_kernel(&info);
}

GPIOD_API struct gpiod_line_info *
gpiod_chip_get_line_info(struct gpiod_chip *chip, unsigned int offset)
{
	return chip_get_line_info(chip, offset, false);
}

GPIOD_API struct gpiod_line_info *
gpiod_chip_watch_line_info(struct gpiod_chip *chip, unsigned int offset)
{
	return chip_get_line_info(chip, offset, true);
}

GPIOD_API int gpiod_chip_unwatch_line_info(struct gpiod_chip *chip,
					   unsigned int offset)
{
	return ioctl(chip->fd, GPIO_GET_LINEINFO_UNWATCH_IOCTL, &offset);
}

GPIOD_API int gpiod_chip_get_fd(struct gpiod_chip *chip)
{
	return chip->fd;
}

GPIOD_API int gpiod_chip_wait_info_event(struct gpiod_chip *chip,
					 uint64_t timeout_ns)
{
	return gpiod_poll_fd(chip->fd, timeout_ns);
}

GPIOD_API struct gpiod_info_event *
gpiod_chip_read_info_event(struct gpiod_chip *chip)
{
	return gpiod_info_event_read_fd(chip->fd);
}

GPIOD_API int gpiod_chip_find_line(struct gpiod_chip *chip, const char *name)
{
	struct gpio_v2_line_info linfo;
	struct gpiochip_info chinfo;
	unsigned int offset;
	int ret;

	ret = chip_read_chip_info(chip->fd, &chinfo);
	if (ret < 0)
		return -1;

	for (offset = 0; offset < chinfo.lines; offset++) {
		ret = chip_read_line_info(chip->fd, offset, &linfo, false);
		if (ret)
			return -1;

		if (strcmp(name, linfo.name) == 0)
			return offset;
	}

	errno = ENOENT;
	return -1;
}

static int set_fd_noblock(int fd)
{
	int ret, flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -1;

	flags |= O_NONBLOCK;

	ret = fcntl(fd, F_SETFL, flags);
	if (ret < 0)
		return -1;

	return 0;
}

GPIOD_API struct gpiod_line_request *
gpiod_chip_request_lines(struct gpiod_chip *chip,
			 struct gpiod_request_config *req_cfg,
			 struct gpiod_line_config *line_cfg)
{
	struct gpio_v2_line_request reqbuf;
	struct gpiod_line_request *request;
	int ret;

	memset(&reqbuf, 0, sizeof(reqbuf));

	ret = gpiod_request_config_to_kernel(req_cfg, &reqbuf);
	if (ret)
		return NULL;

	ret = gpiod_line_config_to_kernel(line_cfg, &reqbuf.config,
					  reqbuf.num_lines, reqbuf.offsets);
	if (ret)
		return NULL;

	ret = ioctl(chip->fd, GPIO_V2_GET_LINE_IOCTL, &reqbuf);
	if (ret < 0)
		return NULL;

	ret = set_fd_noblock(reqbuf.fd);
	if (ret) {
		close(reqbuf.fd);
		return NULL;
	}

	request = gpiod_line_request_from_kernel(&reqbuf);
	if (!request) {
		close(reqbuf.fd);
		return NULL;
	}

	return request;
}

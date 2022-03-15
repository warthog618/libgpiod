// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2022 Bartosz Golaszewski <brgl@bgdev.pl>

#include <gpiod.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

struct gpiod_chip_info {
	size_t num_lines;
	char name[32];
	char label[32];
};

GPIOD_API void gpiod_chip_info_free(struct gpiod_chip_info *info)
{
	if (!info)
		return;

	free(info);
}

GPIOD_API const char *gpiod_chip_info_get_name(struct gpiod_chip_info *info)
{
	return info->name;
}

GPIOD_API const char *gpiod_chip_info_get_label(struct gpiod_chip_info *info)
{
	return info->label;
}

GPIOD_API size_t gpiod_chip_info_get_num_lines(struct gpiod_chip_info *info)
{
	return info->num_lines;
}

struct gpiod_chip_info *
gpiod_chip_info_from_kernel(struct gpiochip_info *uinfo)
{
	struct gpiod_chip_info *info;

	info = malloc(sizeof(*info));
	if (!info)
		return NULL;

	memset(info, 0, sizeof(*info));

	info->num_lines = uinfo->lines;

	/*
	 * GPIO device must have a name - don't bother checking this field. In
	 * the worst case (would have to be a weird kernel bug) it'll be empty.
	 */
	strncpy(info->name, uinfo->name, sizeof(info->name));

	/*
	 * The kernel sets the label of a GPIO device to "unknown" if it
	 * hasn't been defined in DT, board file etc. On the off-chance that
	 * we got an empty string, do the same.
	 */
	if (uinfo->label[0] == '\0')
		strncpy(info->label, "unknown", sizeof(info->label));
	else
		strncpy(info->label, uinfo->label, sizeof(info->label));

	return info;
}

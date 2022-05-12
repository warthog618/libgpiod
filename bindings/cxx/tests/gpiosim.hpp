/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2022 Bartosz Golaszewski <brgl@bgdev.pl> */

#ifndef __GPIOD_CXX_GPIOSIM_HPP__
#define __GPIOD_CXX_GPIOSIM_HPP__

#include <any>
#include <filesystem>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace gpiosim {

class chip
{
public:
	enum class property {
		NUM_LINES = 1,
		LABEL,
		LINE_NAME,
		HOG
	};

	enum class pull {
		PULL_UP = 1,
		PULL_DOWN
	};

	enum class hog_direction {
		INPUT = 1,
		OUTPUT_HIGH,
		OUTPUT_LOW
	};

	enum class value {
		INACTIVE = 0,
		ACTIVE = 1
	};

	using line_name = ::std::tuple<unsigned int, ::std::string>;
	using line_hog = ::std::tuple<unsigned int, ::std::string, hog_direction>;
	using properties = ::std::vector<::std::pair<property, ::std::any>>;

	explicit chip(const properties& args = properties());
	chip(const chip& other) = delete;
	chip(chip&& other) = delete;
	~chip();

	chip& operator=(const chip& other) = delete;
	chip& operator=(chip&& other) = delete;

	::std::filesystem::path dev_path() const;
	::std::string name() const;

	value get_value(unsigned int offset);
	void set_pull(unsigned int offset, pull pull);

private:

	struct impl;

	::std::unique_ptr<impl> _m_priv;
};

} /* namespace gpiosim */

#endif /* __GPIOD_CXX_GPIOSIM_HPP__ */

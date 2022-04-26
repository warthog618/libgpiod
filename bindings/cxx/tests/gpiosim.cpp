/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2022 Bartosz Golaszewski <brgl@bgdev.pl> */

#include <functional>
#include <map>
#include <system_error>

#include "gpiosim.h"
#include "gpiosim.hpp"

#define NORETURN __attribute__((noreturn))

namespace gpiosim {

namespace {

const ::std::map<chip::pull, int> pull_mapping = {
	{ chip::pull::PULL_UP,		GPIOSIM_PULL_UP },
	{ chip::pull::PULL_DOWN,	GPIOSIM_PULL_DOWN }
};

const ::std::map<chip::hog_direction, int> hog_dir_mapping = {
	{ chip::hog_direction::INPUT,		GPIOSIM_HOG_DIR_INPUT },
	{ chip::hog_direction::OUTPUT_HIGH,	GPIOSIM_HOG_DIR_OUTPUT_HIGH },
	{ chip::hog_direction::OUTPUT_LOW,	GPIOSIM_HOG_DIR_OUTPUT_LOW }
};

const ::std::map<int, chip::value> value_mapping = {
	{ GPIOSIM_VALUE_INACTIVE,	chip::value::INACTIVE },
	{ GPIOSIM_VALUE_ACTIVE,		chip::value::ACTIVE }
};

template<class gpiosim_type, void free_func(gpiosim_type*)> struct deleter
{
	void operator()(gpiosim_type* ptr)
	{
		free_func(ptr);
	}
};

using ctx_deleter = deleter<::gpiosim_ctx, ::gpiosim_ctx_unref>;
using dev_deleter = deleter<::gpiosim_dev, ::gpiosim_dev_unref>;
using bank_deleter = deleter<::gpiosim_bank, ::gpiosim_bank_unref>;

using ctx_ptr = ::std::unique_ptr<::gpiosim_ctx, ctx_deleter>;
using dev_ptr = ::std::unique_ptr<::gpiosim_dev, dev_deleter>;
using bank_ptr = ::std::unique_ptr<::gpiosim_bank, bank_deleter>;

ctx_ptr sim_ctx;

class sim_ctx_initializer
{
public:
	sim_ctx_initializer(void)
	{
		sim_ctx.reset(gpiosim_ctx_new());
		if (!sim_ctx)
			throw ::std::system_error(errno, ::std::system_category(),
						  "unable to create the GPIO simulator context");
	}
};

dev_ptr make_sim_dev(void)
{
	static sim_ctx_initializer ctx_initializer;

	dev_ptr dev(::gpiosim_dev_new(sim_ctx.get()));
	if (!dev)
		throw ::std::system_error(errno, ::std::system_category(),
					  "failed to create a new GPIO simulator device");

	return dev;
}

bank_ptr make_sim_bank(const dev_ptr& dev)
{
	bank_ptr bank(::gpiosim_bank_new(dev.get()));
	if (!bank)
		throw ::std::system_error(errno, ::std::system_category(),
					  "failed to create a new GPIO simulator bank");

	return bank;
}

NORETURN void throw_invalid_type(void)
{
	throw ::std::logic_error("invalid type for property");
}

unsigned any_to_unsigned_int(const ::std::any& val)
{
	if (val.type() == typeid(int)) {
		auto num_lines = ::std::any_cast<int>(val);
		if (num_lines < 0)
			throw ::std::invalid_argument("negative value not accepted");

		   return static_cast<unsigned int>(num_lines);
	} else if (val.type() == typeid(unsigned int)) {
		return ::std::any_cast<unsigned int>(val);
	}

	throw_invalid_type();
}

::std::string any_to_string(const ::std::any& val)
{
	if (val.type() == typeid(::std::string))
		return ::std::any_cast<::std::string>(val);
	else if (val.type() == typeid(const char*))
		return ::std::any_cast<const char*>(val);

	throw_invalid_type();
}

} /* namespace */

struct chip::impl
{
	impl(void)
		: dev(make_sim_dev()),
		  bank(make_sim_bank(this->dev)),
		  has_num_lines(false),
		  has_label(false)
	{

	}

	impl(const impl& other) = delete;
	impl(impl&& other) = delete;
	~impl(void) = default;
	impl& operator=(const impl& other) = delete;
	impl& operator=(impl&& other) = delete;

	static const ::std::map<chip::property,
				::std::function<void (impl&,
						      const ::std::any&)>> setter_mapping;

	void set_property(chip::property prop, const ::std::any& val)
	{
		setter_mapping.at(prop)(*this, val);
	}

	void set_num_lines(const ::std::any& val)
	{
		if (this->has_num_lines)
			throw ::std::logic_error("number of lines can be set at most once");

		int ret = ::gpiosim_bank_set_num_lines(this->bank.get(), any_to_unsigned_int(val));
		if (ret)
			throw ::std::system_error(errno, ::std::system_category(),
						  "failed to set the number of lines");

		this->has_num_lines = true;
	}

	void set_label(const ::std::any& val)
	{
		if (this->has_label)
			throw ::std::logic_error("label can be set at most once");

		int ret = ::gpiosim_bank_set_label(this->bank.get(),
						   any_to_string(val).c_str());
		if (ret)
			throw ::std::system_error(errno, ::std::system_category(),
						  "failed to set the chip label");

		this->has_label = true;
	}

	void set_line_name(const ::std::any& val)
	{
		auto name = ::std::any_cast<line_name>(val);

		int ret = ::gpiosim_bank_set_line_name(this->bank.get(),
						       ::std::get<0>(name),
						       ::std::get<1>(name).c_str());
		if (ret)
			throw ::std::system_error(errno, ::std::system_category(),
						  "failed to set simulated line name");
	}

	void set_line_hog(const ::std::any& val)
	{
		auto hog = ::std::any_cast<line_hog>(val);

		int ret = ::gpiosim_bank_hog_line(this->bank.get(),
						  ::std::get<0>(hog),
						  ::std::get<1>(hog).c_str(),
						  hog_dir_mapping.at(::std::get<2>(hog)));
		if (ret)
			throw ::std::system_error(errno, ::std::system_category(),
						  "failed to hog a simulated line");
	}

	dev_ptr dev;
	bank_ptr bank;
	bool has_num_lines;
	bool has_label;
};

const ::std::map<chip::property,
		 ::std::function<void (chip::impl&,
				       const ::std::any&)>> chip::impl::setter_mapping = {
	{ chip::property::NUM_LINES,	&chip::impl::set_num_lines },
	{ chip::property::LABEL,	&chip::impl::set_label },
	{ chip::property::LINE_NAME,	&chip::impl::set_line_name },
	{ chip::property::HOG,		&chip::impl::set_line_hog }
};

chip::chip(const properties& args)
	: _m_priv(new impl)
{
	int ret;

	for (const auto& arg: args)
		this->_m_priv.get()->set_property(arg.first, arg.second);

	ret = ::gpiosim_dev_enable(this->_m_priv->dev.get());
	if (ret)
		throw ::std::system_error(errno, ::std::system_category(),
					  "failed to enable the simulated GPIO chip");
}

chip::~chip(void)
{
	this->_m_priv.reset(nullptr);
}

::std::filesystem::path chip::dev_path(void) const
{
	return ::gpiosim_bank_get_dev_path(this->_m_priv->bank.get());
}

::std::string chip::name(void) const
{
	return ::gpiosim_bank_get_chip_name(this->_m_priv->bank.get());
}

chip::value chip::get_value(unsigned int offset)
{
	int val = ::gpiosim_bank_get_value(this->_m_priv->bank.get(), offset);
	if (val < 0)
		throw ::std::system_error(errno, ::std::system_category(),
					  "failed to read the simulated GPIO line value");

	return value_mapping.at(val);
}

void chip::set_pull(unsigned int offset, pull pull)
{
	int ret = ::gpiosim_bank_set_pull(this->_m_priv->bank.get(),
					  offset, pull_mapping.at(pull));
	if (ret)
		throw ::std::system_error(errno, ::std::system_category(),
					  "failed to set the pull of simulated GPIO line");
}

} /* namespace gpiosim */

// SPDX-License-Identifier: LGPL-3.0-or-later
// SPDX-FileCopyrightText: 2021 Bartosz Golaszewski <brgl@bgdev.pl>

#include <utility>

#include "internal.hpp"

namespace gpiod {

namespace {

GPIOD_CXX_NORETURN void throw_bad_value_type(void)
{
	throw ::std::invalid_argument("bad value type for property");
}

request_config_ptr make_request_config(void)
{
	request_config_ptr config(::gpiod_request_config_new());
	if (!config)
		throw_from_errno("Unable to allocate the request config object");

	return config;
}

::std::string get_string_from_value(const ::std::any& val)
{
	if (val.type() == typeid(::std::string))
		return ::std::any_cast<::std::string>(val);
	else if (val.type() == typeid(const char*))
		return ::std::any_cast<const char*>(val);

	throw_bad_value_type();
}

unsigned int get_unsigned_int_from_value(const ::std::any& val)
{
	if (val.type() == typeid(unsigned int)) {
		return ::std::any_cast<unsigned int>(val);
	} else if (val.type() == typeid(int)) {
		int bufsize = ::std::any_cast<int>(val);
		if (bufsize < 0)
			bufsize = 0;

		return static_cast<unsigned int>(bufsize);
	}

	throw_bad_value_type();
}

} /* namespace */

request_config::impl::impl(void)
	: config(make_request_config())
{

}

GPIOD_CXX_API request_config::request_config(const properties& props)
	: _m_priv(new impl)
{
	for (const auto& prop: props)
		this->set_property(prop.first, prop.second);
}

GPIOD_CXX_API request_config::request_config(request_config&& other) noexcept
	: _m_priv(::std::move(other._m_priv))
{

}

GPIOD_CXX_API request_config::~request_config(void)
{

}

GPIOD_CXX_API request_config& request_config::operator=(request_config&& other) noexcept
{
	this->_m_priv = ::std::move(other._m_priv);

	return *this;
}

GPIOD_CXX_API void request_config::set_property(property prop, const ::std::any& val)
{
	switch (prop) {
	case property::OFFSETS:
		try {
			this->set_offsets(::std::any_cast<line::offsets>(val));
		} catch (const ::std::bad_any_cast& ex) {
			throw_bad_value_type();
		}
		break;
	case property::CONSUMER:
		this->set_consumer(get_string_from_value(val));
		break;
	case property::EVENT_BUFFER_SIZE:
		this->set_event_buffer_size(get_unsigned_int_from_value(val));
		break;
	default:
		throw ::std::invalid_argument("unknown property");
	}
}

GPIOD_CXX_API void request_config::set_offsets(const line::offsets& offsets) noexcept
{
	::std::vector<unsigned int> buf(offsets.size());

	for (unsigned int i = 0; i < offsets.size(); i++)
		buf[i] = offsets[i];

	::gpiod_request_config_set_offsets(this->_m_priv->config.get(),
					   buf.size(), buf.data());
}

GPIOD_CXX_API ::std::size_t request_config::num_offsets(void) const noexcept
{
	return ::gpiod_request_config_get_num_offsets(this->_m_priv->config.get());
}

GPIOD_CXX_API void
request_config::set_consumer(const ::std::string& consumer) noexcept
{
	::gpiod_request_config_set_consumer(this->_m_priv->config.get(), consumer.c_str());
}

GPIOD_CXX_API ::std::string request_config::consumer(void) const noexcept
{
	const char* consumer = ::gpiod_request_config_get_consumer(this->_m_priv->config.get());

	return consumer ?: "";
}

GPIOD_CXX_API line::offsets request_config::offsets(void) const
{
	line::offsets ret(this->num_offsets());
	::std::vector<unsigned int> buf(this->num_offsets());

	::gpiod_request_config_get_offsets(this->_m_priv->config.get(), buf.data());

	for (unsigned int i = 0; i < this->num_offsets(); i++)
		ret[i] = buf[i];

	return ret;
}

GPIOD_CXX_API void
request_config::set_event_buffer_size(::std::size_t event_buffer_size) noexcept
{
	::gpiod_request_config_set_event_buffer_size(this->_m_priv->config.get(),
						     event_buffer_size);
}

GPIOD_CXX_API ::std::size_t request_config::event_buffer_size(void) const noexcept
{
	return ::gpiod_request_config_get_event_buffer_size(this->_m_priv->config.get());
}

GPIOD_CXX_API ::std::ostream& operator<<(::std::ostream& out, const request_config& config)
{
	::std::string consumer;

	consumer = config.consumer().empty() ? "N/A" : ::std::string("'") + config.consumer() + "'";

	out << "gpiod::request_config(consumer=" << consumer <<
	       ", num_offsets=" << config.num_offsets() <<
	       ", offsets=(" << config.offsets() << ")" <<
	       ", event_buffer_size=" << config.event_buffer_size() <<
	       ")";

	return out;
}

} /* namespace gpiod */

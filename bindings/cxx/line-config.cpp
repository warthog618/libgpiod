// SPDX-License-Identifier: LGPL-3.0-or-later
// SPDX-FileCopyrightText: 2021-2022 Bartosz Golaszewski <brgl@bgdev.pl>

#include <iterator>
#include <map>
#include <sstream>

#include "internal.hpp"

namespace gpiod {

namespace {

template<class enum_type>
::std::map<int, enum_type> make_reverse_maping(const ::std::map<enum_type, int>& mapping)
{
	::std::map<int, enum_type> ret;

	for (const auto &item: mapping)
		ret[item.second] = item.first;

	return ret;
}

const ::std::map<line::direction, int> direction_mapping = {
	{ line::direction::AS_IS,	GPIOD_LINE_DIRECTION_AS_IS },
	{ line::direction::INPUT,	GPIOD_LINE_DIRECTION_INPUT },
	{ line::direction::OUTPUT,	GPIOD_LINE_DIRECTION_OUTPUT }
};

const ::std::map<int, line::direction> reverse_direction_mapping = make_reverse_maping(direction_mapping);

const ::std::map<line::edge, int> edge_mapping = {
	{ line::edge::NONE,		GPIOD_LINE_EDGE_NONE },
	{ line::edge::FALLING,		GPIOD_LINE_EDGE_FALLING },
	{ line::edge::RISING,		GPIOD_LINE_EDGE_RISING },
	{ line::edge::BOTH,		GPIOD_LINE_EDGE_BOTH }
};

const ::std::map<int, line::edge> reverse_edge_mapping = make_reverse_maping(edge_mapping);

const ::std::map<line::bias, int> bias_mapping = {
	{ line::bias::AS_IS,		GPIOD_LINE_BIAS_AS_IS },
	{ line::bias::DISABLED,		GPIOD_LINE_BIAS_DISABLED },
	{ line::bias::PULL_UP,		GPIOD_LINE_BIAS_PULL_UP },
	{ line::bias::PULL_DOWN,	GPIOD_LINE_BIAS_PULL_DOWN }
};

const ::std::map<int, line::bias> reverse_bias_mapping = make_reverse_maping(bias_mapping);

const ::std::map<line::drive, int> drive_mapping = {
	{ line::drive::PUSH_PULL,	GPIOD_LINE_DRIVE_PUSH_PULL },
	{ line::drive::OPEN_DRAIN,	GPIOD_LINE_DRIVE_OPEN_DRAIN },
	{ line::drive::OPEN_SOURCE,	GPIOD_LINE_DRIVE_OPEN_SOURCE }
};

const ::std::map<int, line::drive> reverse_drive_mapping = make_reverse_maping(drive_mapping);

const ::std::map<line::clock, int> clock_mapping = {
	{ line::clock::MONOTONIC,	GPIOD_LINE_EVENT_CLOCK_MONOTONIC },
	{ line::clock::REALTIME,	GPIOD_LINE_EVENT_CLOCK_REALTIME },
};

const ::std::map<int, line::clock> reverse_clock_mapping = make_reverse_maping(clock_mapping);

template<class key_type, class value_type, class exception_type>
value_type map_setting(const key_type& key, const ::std::map<key_type, value_type>& mapping)
{
	value_type ret;

	try {
		ret = mapping.at(key);
	} catch (const ::std::out_of_range& err) {
		throw exception_type(::std::string("invalid value for ") +
				     typeid(key_type).name());
	}

	return ret;
}

::gpiod_line_config* make_line_config()
{
	::gpiod_line_config *config = ::gpiod_line_config_new();
	if (!config)
		throw_from_errno("Unable to allocate the line config object");

	return config;
}

template<class enum_type>
int do_map_value(enum_type value, const ::std::map<enum_type, int>& mapping)
{
	return map_setting<enum_type, int, ::std::invalid_argument>(value, mapping);
}

template<class enum_type, void set_func(::gpiod_line_config*, int)>
void set_mapped_value_default(::gpiod_line_config* config, enum_type value,
			      const ::std::map<enum_type, int>& mapping)
{
	int mapped_val = do_map_value(value, mapping);

	set_func(config, mapped_val);
}

template<class enum_type, void set_func(::gpiod_line_config*, int, unsigned int)>
void set_mapped_value_override(::gpiod_line_config* config, enum_type value, line::offset offset,
			       const ::std::map<enum_type, int>& mapping)
{
	int mapped_val = do_map_value(value, mapping);

	set_func(config, mapped_val, offset);
}

template<class ret_type, int get_func(::gpiod_line_config*)>
ret_type get_mapped_value_default(::gpiod_line_config* config,
				  const ::std::map<int, ret_type>& mapping)
{
	int mapped_val = get_func(config);

	return map_int_to_enum(mapped_val, mapping);
}

template<class ret_type, int get_func(::gpiod_line_config*, unsigned int)>
ret_type get_mapped_value_offset(::gpiod_line_config* config, line::offset offset,
				 const ::std::map<int, ret_type>& mapping)
{
	int mapped_val = get_func(config, offset);

	return map_int_to_enum(mapped_val, mapping);
}

const ::std::map<int, line_config::property> property_mapping = {
	{ GPIOD_LINE_CONFIG_PROP_DIRECTION,		line_config::property::DIRECTION },
	{ GPIOD_LINE_CONFIG_PROP_EDGE_DETECTION,	line_config::property::EDGE_DETECTION },
	{ GPIOD_LINE_CONFIG_PROP_BIAS,			line_config::property::BIAS },
	{ GPIOD_LINE_CONFIG_PROP_DRIVE,			line_config::property::DRIVE },
	{ GPIOD_LINE_CONFIG_PROP_ACTIVE_LOW,		line_config::property::ACTIVE_LOW },
	{ GPIOD_LINE_CONFIG_PROP_DEBOUNCE_PERIOD_US,	line_config::property::DEBOUNCE_PERIOD },
	{ GPIOD_LINE_CONFIG_PROP_EVENT_CLOCK,		line_config::property::EVENT_CLOCK },
	{ GPIOD_LINE_CONFIG_PROP_OUTPUT_VALUE,		line_config::property::OUTPUT_VALUE }
};

} /* namespace */

line_config::impl::impl()
	: config(make_line_config())
{

}

GPIOD_CXX_API line_config::line_config(const properties& props)
	: _m_priv(new impl)
{
	for (const auto& prop: props) {
		if (prop.first == property::OUTPUT_VALUES)
			this->set_output_values(::std::any_cast<line::value_mappings>(prop.second));
		else
			this->set_property_default(prop.first, prop.second);
	}
}

GPIOD_CXX_API line_config::line_config(line_config&& other) noexcept
	: _m_priv(::std::move(other._m_priv))
{

}

GPIOD_CXX_API line_config::~line_config()
{

}

GPIOD_CXX_API void line_config::reset() noexcept
{
	::gpiod_line_config_reset(this->_m_priv->config.get());
}

GPIOD_CXX_API line_config& line_config::operator=(line_config&& other) noexcept
{
	this->_m_priv = ::std::move(other._m_priv);

	return *this;
}

GPIOD_CXX_API void line_config::set_property_default(property prop, const ::std::any& val)
{
	switch(prop) {
	case property::DIRECTION:
		this->set_direction_default(::std::any_cast<line::direction>(val));
		break;
	case property::EDGE_DETECTION:
		this->set_edge_detection_default(::std::any_cast<line::edge>(val));
		break;
	case property::BIAS:
		this->set_bias_default(::std::any_cast<line::bias>(val));
		break;
	case property::DRIVE:
		this->set_drive_default(::std::any_cast<line::drive>(val));
		break;
	case property::ACTIVE_LOW:
		this->set_active_low_default(::std::any_cast<bool>(val));
		break;
	case property::DEBOUNCE_PERIOD:
		this->set_debounce_period_default(::std::any_cast<::std::chrono::microseconds>(val));
		break;
	case property::EVENT_CLOCK:
		this->set_event_clock_default(::std::any_cast<line::clock>(val));
		break;
	case property::OUTPUT_VALUE:
		this->set_output_value_default(::std::any_cast<line::value>(val));
		break;
	default:
		throw ::std::invalid_argument("invalid property type");
	}
}

GPIOD_CXX_API void line_config::set_property_offset(property prop, line::offset offset,
						    const ::std::any& val)
{
	switch(prop) {
	case property::DIRECTION:
		this->set_direction_override(::std::any_cast<line::direction>(val), offset);
		break;
	case property::EDGE_DETECTION:
		this->set_edge_detection_override(::std::any_cast<line::edge>(val), offset);
		break;
	case property::BIAS:
		this->set_bias_override(::std::any_cast<line::bias>(val), offset);
		break;
	case property::DRIVE:
		this->set_drive_override(::std::any_cast<line::drive>(val), offset);
		break;
	case property::ACTIVE_LOW:
		this->set_active_low_override(::std::any_cast<bool>(val), offset);
		break;
	case property::DEBOUNCE_PERIOD:
		this->set_debounce_period_override(::std::any_cast<::std::chrono::microseconds>(val),
						      offset);
		break;
	case property::EVENT_CLOCK:
		this->set_event_clock_override(::std::any_cast<line::clock>(val), offset);
		break;
	case property::OUTPUT_VALUE:
		this->set_output_value_override(::std::any_cast<line::value>(val), offset);
		break;
	default:
		throw ::std::invalid_argument("invalid property type");
	}
}

GPIOD_CXX_API void line_config::set_direction_default(line::direction direction)
{
	set_mapped_value_default<line::direction,
				 ::gpiod_line_config_set_direction_default>(this->_m_priv->config.get(),
									    direction, direction_mapping);
}

GPIOD_CXX_API void line_config::set_direction_override(line::direction direction, line::offset offset)
{
	set_mapped_value_override<line::direction,
				  ::gpiod_line_config_set_direction_override>(this->_m_priv->config.get(),
									      direction, offset,
									      direction_mapping);
}

GPIOD_CXX_API line::direction line_config::direction_default() const
{
	return get_mapped_value_default<line::direction,
					::gpiod_line_config_get_direction_default>(
							this->_m_priv->config.get(),
							reverse_direction_mapping);
}

GPIOD_CXX_API line::direction line_config::direction_offset(line::offset offset) const
{
	return get_mapped_value_offset<line::direction,
				       ::gpiod_line_config_get_direction_offset>(
						       this->_m_priv->config.get(),
						       offset, reverse_direction_mapping);
}

GPIOD_CXX_API void line_config::clear_direction_override(line::offset offset) noexcept
{
	::gpiod_line_config_clear_direction_override(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API bool line_config::direction_is_overridden(line::offset offset) const noexcept
{
	return ::gpiod_line_config_direction_is_overridden(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API void line_config::set_edge_detection_default(line::edge edge)
{
	set_mapped_value_default<line::edge,
				 ::gpiod_line_config_set_edge_detection_default>(
						 this->_m_priv->config.get(),
						 edge, edge_mapping);
}

GPIOD_CXX_API void line_config::set_edge_detection_override(line::edge edge, line::offset offset)
{
	set_mapped_value_override<line::edge,
				  ::gpiod_line_config_set_edge_detection_override>(
						this->_m_priv->config.get(),
						edge, offset, edge_mapping);
}

GPIOD_CXX_API line::edge line_config::edge_detection_default() const
{
	return get_mapped_value_default<line::edge,
					::gpiod_line_config_get_edge_detection_default>(
							this->_m_priv->config.get(),
							reverse_edge_mapping);
}

GPIOD_CXX_API line::edge line_config::edge_detection_offset(line::offset offset) const
{
	return get_mapped_value_offset<line::edge,
				       ::gpiod_line_config_get_edge_detection_offset>(
						       this->_m_priv->config.get(),
						       offset, reverse_edge_mapping);
}

GPIOD_CXX_API void line_config::clear_edge_detection_override(line::offset offset) noexcept
{
	::gpiod_line_config_clear_edge_detection_override(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API bool line_config::edge_detection_is_overridden(line::offset offset) const noexcept
{
	return ::gpiod_line_config_edge_detection_is_overridden(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API void line_config::set_bias_default(line::bias bias)
{
	set_mapped_value_default<line::bias,
				 ::gpiod_line_config_set_bias_default>(this->_m_priv->config.get(),
								       bias, bias_mapping);
}

GPIOD_CXX_API void line_config::set_bias_override(line::bias bias, line::offset offset)
{
	set_mapped_value_override<line::bias,
				 ::gpiod_line_config_set_bias_override>(this->_m_priv->config.get(),
									bias, offset, bias_mapping);
}

GPIOD_CXX_API line::bias line_config::bias_default() const
{
	return get_mapped_value_default<line::bias,
					::gpiod_line_config_get_bias_default>(this->_m_priv->config.get(),
									      reverse_bias_mapping);
}

GPIOD_CXX_API line::bias line_config::bias_offset(line::offset offset) const
{
	return get_mapped_value_offset<line::bias,
				       ::gpiod_line_config_get_bias_offset>(this->_m_priv->config.get(),
									    offset, reverse_bias_mapping);
}

GPIOD_CXX_API void line_config::clear_bias_override(line::offset offset) noexcept
{
	::gpiod_line_config_clear_bias_override(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API bool line_config::bias_is_overridden(line::offset offset) const noexcept
{
	return ::gpiod_line_config_bias_is_overridden(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API void line_config::set_drive_default(line::drive drive)
{
	set_mapped_value_default<line::drive,
				 ::gpiod_line_config_set_drive_default>(this->_m_priv->config.get(),
									drive, drive_mapping);
}

GPIOD_CXX_API void line_config::set_drive_override(line::drive drive, line::offset offset)
{
	set_mapped_value_override<line::drive,
				  ::gpiod_line_config_set_drive_override>(this->_m_priv->config.get(),
									  drive, offset, drive_mapping);
}

GPIOD_CXX_API line::drive line_config::drive_default() const
{
	return get_mapped_value_default<line::drive,
					::gpiod_line_config_get_drive_default>(this->_m_priv->config.get(),
									       reverse_drive_mapping);
}

GPIOD_CXX_API line::drive line_config::drive_offset(line::offset offset) const
{
	return get_mapped_value_offset<line::drive,
				       ::gpiod_line_config_get_drive_offset>(this->_m_priv->config.get(),
									     offset, reverse_drive_mapping);
}

GPIOD_CXX_API void line_config::clear_drive_override(line::offset offset) noexcept
{
	::gpiod_line_config_clear_drive_override(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API bool line_config::drive_is_overridden(line::offset offset) const noexcept
{
	return ::gpiod_line_config_drive_is_overridden(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API void line_config::set_active_low_default(bool active_low) noexcept
{
	::gpiod_line_config_set_active_low_default(this->_m_priv->config.get(), active_low);
}

GPIOD_CXX_API void line_config::set_active_low_override(bool active_low, line::offset offset) noexcept
{
	::gpiod_line_config_set_active_low_override(this->_m_priv->config.get(), active_low, offset);
}

GPIOD_CXX_API bool line_config::active_low_default() const noexcept
{
	return ::gpiod_line_config_get_active_low_default(this->_m_priv->config.get());
}

GPIOD_CXX_API bool line_config::active_low_offset(line::offset offset) const noexcept
{
	return ::gpiod_line_config_get_active_low_offset(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API void line_config::clear_active_low_override(line::offset offset) noexcept
{
	::gpiod_line_config_clear_active_low_override(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API bool line_config::active_low_is_overridden(line::offset offset) const noexcept
{
	return ::gpiod_line_config_active_low_is_overridden(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API void
line_config::set_debounce_period_default(const ::std::chrono::microseconds& period) noexcept
{
	::gpiod_line_config_set_debounce_period_us_default(this->_m_priv->config.get(), period.count());
}

GPIOD_CXX_API void
line_config::set_debounce_period_override(const ::std::chrono::microseconds& period,
					     line::offset offset) noexcept
{
	::gpiod_line_config_set_debounce_period_us_override(this->_m_priv->config.get(),
							    period.count(), offset);
}

GPIOD_CXX_API ::std::chrono::microseconds line_config::debounce_period_default() const noexcept
{
	return ::std::chrono::microseconds(
			::gpiod_line_config_get_debounce_period_us_default(this->_m_priv->config.get()));
}

GPIOD_CXX_API ::std::chrono::microseconds
line_config::debounce_period_offset(line::offset offset) const noexcept
{
	return ::std::chrono::microseconds(
			::gpiod_line_config_get_debounce_period_us_offset(this->_m_priv->config.get(),
									  offset));
}

GPIOD_CXX_API void line_config::clear_debounce_period_override(line::offset offset) noexcept
{
	::gpiod_line_config_clear_debounce_period_us_override(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API bool line_config::debounce_period_is_overridden(line::offset offset) const noexcept
{
	return ::gpiod_line_config_debounce_period_us_is_overridden(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API void line_config::set_event_clock_default(line::clock clock)
{
	set_mapped_value_default<line::clock,
				 ::gpiod_line_config_set_event_clock_default>(this->_m_priv->config.get(),
									      clock, clock_mapping);
}

GPIOD_CXX_API void line_config::set_event_clock_override(line::clock clock, line::offset offset)
{
	set_mapped_value_override<line::clock,
				  ::gpiod_line_config_set_event_clock_override>(this->_m_priv->config.get(),
										clock, offset,
										clock_mapping);
}

GPIOD_CXX_API line::clock line_config::event_clock_default() const
{
	return get_mapped_value_default<line::clock,
					::gpiod_line_config_get_event_clock_default>(
							this->_m_priv->config.get(),
							reverse_clock_mapping);
}

GPIOD_CXX_API line::clock line_config::event_clock_offset(line::offset offset) const
{
	return get_mapped_value_offset<line::clock,
					::gpiod_line_config_get_event_clock_offset>(
							this->_m_priv->config.get(),
							offset, reverse_clock_mapping);
}

GPIOD_CXX_API void line_config::clear_event_clock_override(line::offset offset) noexcept
{
	::gpiod_line_config_clear_event_clock_override(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API bool line_config::event_clock_is_overridden(line::offset offset) const noexcept
{
	return ::gpiod_line_config_event_clock_is_overridden(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API void line_config::set_output_value_default(line::value value) noexcept
{
	::gpiod_line_config_set_output_value_default(this->_m_priv->config.get(), static_cast<int>(value));
}

GPIOD_CXX_API void line_config::set_output_value_override(line::value value, line::offset offset) noexcept
{
	::gpiod_line_config_set_output_value_override(this->_m_priv->config.get(),
						      offset, static_cast<int>(value));
}

GPIOD_CXX_API void line_config::set_output_values(const line::value_mappings& values)
{
	line::offsets offsets;
	line::values vals;

	if (values.empty())
		return;

	offsets.reserve(values.size());
	vals.reserve(values.size());

	for (auto& val: values) {
		offsets.push_back(val.first);
		vals.push_back(val.second);
	}

	this->set_output_values(offsets, vals);
}

GPIOD_CXX_API void line_config::set_output_values(const line::offsets& offsets,
						  const line::values& values)
{
	if (offsets.size() != values.size())
		throw ::std::invalid_argument("values must have the same size as the offsets");

	if (offsets.empty())
		return;

	::std::vector<unsigned int> buf(offsets.size());

	for (unsigned int i = 0; i < offsets.size(); i++)
		buf[i] = offsets[i];

	::gpiod_line_config_set_output_values(this->_m_priv->config.get(),
					      offsets.size(), buf.data(),
					      reinterpret_cast<const int*>(values.data()));
}

GPIOD_CXX_API line::value line_config::output_value_default() const noexcept
{
	return static_cast<line::value>(::gpiod_line_config_get_output_value_default(
								this->_m_priv->config.get()));
}

GPIOD_CXX_API line::value line_config::output_value_offset(line::offset offset) const noexcept
{
	return static_cast<line::value>(
			::gpiod_line_config_get_output_value_offset(this->_m_priv->config.get(),
								    offset));
}

GPIOD_CXX_API void line_config::clear_output_value_override(line::offset offset) noexcept
{
	::gpiod_line_config_clear_output_value_override(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API bool line_config::output_value_is_overridden(line::offset offset) const noexcept
{
	return ::gpiod_line_config_output_value_is_overridden(this->_m_priv->config.get(), offset);
}

GPIOD_CXX_API ::std::size_t line_config::num_overrides() const noexcept
{
	return ::gpiod_line_config_get_num_overrides(this->_m_priv->config.get());
}

GPIOD_CXX_API line_config::override_list line_config::overrides() const
{
	unsigned int num_overrides = this->num_overrides();
	override_list ret(num_overrides);
	::std::vector<unsigned int> offsets(num_overrides);
	::std::vector<int> props(num_overrides);

	::gpiod_line_config_get_overrides(this->_m_priv->config.get(), offsets.data(), props.data());

	for (unsigned int i = 0; i < num_overrides; i++)
		ret[i] = { offsets[i], property_mapping.at(props[i]) };

	return ret;
}

GPIOD_CXX_API ::std::ostream& operator<<(::std::ostream& out, const line_config& config)
{
	out << "gpiod::line_config(defaults=(direction=" << config.direction_default() <<
	       ", edge_detection=" << config.edge_detection_default() <<
	       ", bias=" << config.bias_default() <<
	       ", drive=" << config.drive_default() << ", " <<
	       (config.active_low_default() ? "active-low" : "active-high") <<
	       ", debounce_period=" << config.debounce_period_default().count() << "us" <<
	       ", event_clock=" << config.event_clock_default() <<
	       ", default_output_value=" << config.output_value_default() <<
	       "), ";

	if (config.num_overrides()) {
		::std::vector<::std::string> overrides(config.num_overrides());
		::std::vector<::std::string>::iterator it = overrides.begin();

		out << "overrides=[";

		for (const auto& override: config.overrides()) {
			line::offset offset = override.first;
			line_config::property prop = override.second;
			::std::stringstream out;

			out << "(offset=" << offset << " -> ";

			switch (prop) {
			case line_config::property::DIRECTION:
				out << "direction=" << config.direction_offset(offset);
				break;
			case line_config::property::EDGE_DETECTION:
				out << "edge_detection=" << config.edge_detection_offset(offset);
				break;
			case line_config::property::BIAS:
				out << "bias=" << config.bias_offset(offset);
				break;
			case line_config::property::DRIVE:
				out << "drive=" << config.drive_offset(offset);
				break;
			case line_config::property::ACTIVE_LOW:
				out << (config.active_low_offset(offset) ? "active-low" : "active-high");
				break;
			case line_config::property::DEBOUNCE_PERIOD:
				out << "debounce_period=" <<
				       config.debounce_period_offset(offset).count() << "us";
				break;
			case line_config::property::EVENT_CLOCK:
				out << "event_clock=" << config.event_clock_offset(offset);
				break;
			case line_config::property::OUTPUT_VALUE:
				out << "output_value=" << config.output_value_offset(offset);
				break;
			default:
				/* OUTPUT_VALUES is ignored. */
				break;
			}

			out << ")";

			*it = out.str();
			it++;
		}

		::std::copy(overrides.begin(), ::std::prev(overrides.end()),
			    ::std::ostream_iterator<::std::string>(out, ", "));
		out << overrides.back();

		out << "]";
	}

	out << ")";

	return out;
}

} /* namespace gpiod */

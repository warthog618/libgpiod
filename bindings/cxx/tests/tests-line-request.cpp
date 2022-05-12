// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2022 Bartosz Golaszewski <brgl@bgdev.pl>

#include <catch2/catch.hpp>
#include <gpiod.hpp>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "gpiosim.hpp"
#include "helpers.hpp"

using simprop = ::gpiosim::chip::property;
using reqprop = ::gpiod::request_config::property;
using lineprop = ::gpiod::line_config::property;
using offsets = ::gpiod::line::offsets;
using values = ::gpiod::line::values;
using direction = ::gpiod::line::direction;
using value = ::gpiod::line::value;
using simval = ::gpiosim::chip::value;
using pull = ::gpiosim::chip::pull;

namespace {

class value_matcher : public Catch::MatcherBase<value>
{
public:
	value_matcher(pull pull, bool active_low = false)
		: _m_pull(pull),
		  _m_active_low(active_low)
	{

	}

	::std::string describe() const override
	{
		::std::string repr(this->_m_pull == pull::PULL_UP ? "PULL_UP" : "PULL_DOWN");
		::std::string active_low = this->_m_active_low ? "(active-low) " : "";

		return active_low + "corresponds with " + repr;
	}

	bool match(const value& val) const override
	{
		if (this->_m_active_low) {
			if ((val == value::ACTIVE && this->_m_pull == pull::PULL_DOWN) ||
			    (val == value::INACTIVE && this->_m_pull == pull::PULL_UP))
				return true;
		} else {
			if ((val == value::ACTIVE && this->_m_pull == pull::PULL_UP) ||
			    (val == value::INACTIVE && this->_m_pull == pull::PULL_DOWN))
				return true;
		}

		return false;
	}

private:
	pull _m_pull;
	bool _m_active_low;
};

TEST_CASE("requesting lines fails with invalid arguments", "[line-request][chip]")
{
	::gpiosim::chip sim({{ simprop::NUM_LINES, 8 }});
	::gpiod::chip chip(sim.dev_path());

	SECTION("no offsets")
	{
		REQUIRE_THROWS_AS(chip.request_lines(::gpiod::request_config(),
						     ::gpiod::line_config()),
				  ::std::invalid_argument);
	}

	SECTION("duplicate offsets")
	{
		REQUIRE_THROWS_MATCHES(chip.request_lines(
			::gpiod::request_config({
				{ reqprop::OFFSETS, offsets({ 2, 0, 0, 4 }) }
			}),
			::gpiod::line_config()),
			::std::system_error,
			 system_error_matcher(EBUSY)
		);
	}

	SECTION("offset out of bounds")
	{
		REQUIRE_THROWS_AS(chip.request_lines(
			::gpiod::request_config({
				{ reqprop::OFFSETS, offsets({ 2, 0, 8, 4 }) }
			}),
			::gpiod::line_config()),
			::std::invalid_argument
		);
	}
}

TEST_CASE("consumer string is set correctly", "[line-request]")
{
	::gpiosim::chip sim({{ simprop::NUM_LINES, 4 }});
	::gpiod::chip chip(sim.dev_path());
	offsets offs({ 3, 0, 2 });

	SECTION("set custom consumer")
	{
		auto request = chip.request_lines(
			::gpiod::request_config({
				{ reqprop::OFFSETS, offsets({ 2 }) },
				{ reqprop::CONSUMER, "foobar" }
			}),
			::gpiod::line_config()
		);

		auto info = chip.get_line_info(2);

		REQUIRE(info.used());
		REQUIRE_THAT(info.consumer(), Catch::Equals("foobar"));
	}

	SECTION("empty consumer")
	{
		auto request = chip.request_lines(
			::gpiod::request_config({
				{ reqprop::OFFSETS, offsets({ 2 }) },
			}),
			::gpiod::line_config()
		);

		auto info = chip.get_line_info(2);

		REQUIRE(info.used());
		REQUIRE_THAT(info.consumer(), Catch::Equals("?"));
	}
}

TEST_CASE("values can be read", "[line-request]")
{
	::gpiosim::chip sim({{ simprop::NUM_LINES, 8 }});
	const offsets offs({ 7, 1, 0, 6, 2 });

	const ::std::vector<pull> pulls({
		pull::PULL_UP,
		pull::PULL_UP,
		pull::PULL_DOWN,
		pull::PULL_UP,
		pull::PULL_DOWN
	});

	for (unsigned int i = 0; i < offs.size(); i++)
		sim.set_pull(offs[i], pulls[i]);

	auto request = ::gpiod::chip(sim.dev_path()).request_lines(
		::gpiod::request_config({
			{ reqprop::OFFSETS, offs }
		}),
		::gpiod::line_config({
			{ lineprop::DIRECTION, direction::INPUT }
		})
	);

	SECTION("get all values (returning variant)")
	{
		auto vals = request.get_values();

		REQUIRE_THAT(vals[0], value_matcher(pull::PULL_UP));
		REQUIRE_THAT(vals[1], value_matcher(pull::PULL_UP));
		REQUIRE_THAT(vals[2], value_matcher(pull::PULL_DOWN));
		REQUIRE_THAT(vals[3], value_matcher(pull::PULL_UP));
		REQUIRE_THAT(vals[4], value_matcher(pull::PULL_DOWN));
	}

	SECTION("get all values (passed buffer variant)")
	{
		values vals(5);

		request.get_values(vals);

		REQUIRE_THAT(vals[0], value_matcher(pull::PULL_UP));
		REQUIRE_THAT(vals[1], value_matcher(pull::PULL_UP));
		REQUIRE_THAT(vals[2], value_matcher(pull::PULL_DOWN));
		REQUIRE_THAT(vals[3], value_matcher(pull::PULL_UP));
		REQUIRE_THAT(vals[4], value_matcher(pull::PULL_DOWN));
	}

	SECTION("get_values(buffer) throws for invalid buffer size")
	{
		values vals(4);
		REQUIRE_THROWS_AS(request.get_values(vals), ::std::invalid_argument);
		vals.resize(6);
		REQUIRE_THROWS_AS(request.get_values(vals), ::std::invalid_argument);
	}

	SECTION("get a single value")
	{
		auto val = request.get_value(7);

		REQUIRE_THAT(val, value_matcher(pull::PULL_UP));
	}

	SECTION("get a single value (active-low)")
	{
		request.reconfigure_lines(
			::gpiod::line_config({
				{ lineprop::ACTIVE_LOW, true }
			})
		);

		auto val = request.get_value(7);

		REQUIRE_THAT(val, value_matcher(pull::PULL_UP, true));
	}

	SECTION("get a subset of values (returning variant)")
	{
		auto vals = request.get_values(offsets({ 2, 0, 6 }));

		REQUIRE_THAT(vals[0], value_matcher(pull::PULL_DOWN));
		REQUIRE_THAT(vals[1], value_matcher(pull::PULL_DOWN));
		REQUIRE_THAT(vals[2], value_matcher(pull::PULL_UP));
	}

	SECTION("get a subset of values (passed buffer variant)")
	{
		values vals(3);

		request.get_values(offsets({ 2, 0, 6 }), vals);

		REQUIRE_THAT(vals[0], value_matcher(pull::PULL_DOWN));
		REQUIRE_THAT(vals[1], value_matcher(pull::PULL_DOWN));
		REQUIRE_THAT(vals[2], value_matcher(pull::PULL_UP));
	}
}

TEST_CASE("output values can be set at request time", "[line-request]")
{
	::gpiosim::chip sim({{ simprop::NUM_LINES, 8 }});
	::gpiod::chip chip(sim.dev_path());
	const offsets offs({ 0, 1, 3, 4 });

	::gpiod::request_config req_cfg({
		{ reqprop::OFFSETS, offs }
	});

	::gpiod::line_config line_cfg({
		{ lineprop::DIRECTION, direction::OUTPUT },
		{ lineprop::OUTPUT_VALUE, value::ACTIVE }
	});

	SECTION("default output value")
	{
		auto request = chip.request_lines(req_cfg, line_cfg);

		for (const auto& off: offs)
			REQUIRE(sim.get_value(off) == simval::ACTIVE);

		REQUIRE(sim.get_value(2) == simval::INACTIVE);
	}

	SECTION("overridden output value")
	{
		line_cfg.set_output_value_override(value::INACTIVE, 1);

		auto request = chip.request_lines(req_cfg, line_cfg);

		REQUIRE(sim.get_value(0) == simval::ACTIVE);
		REQUIRE(sim.get_value(1) == simval::INACTIVE);
		REQUIRE(sim.get_value(2) == simval::INACTIVE);
		REQUIRE(sim.get_value(3) == simval::ACTIVE);
		REQUIRE(sim.get_value(4) == simval::ACTIVE);
	}
}

TEST_CASE("values can be set after requesting lines", "[line-request]")
{
	::gpiosim::chip sim({{ simprop::NUM_LINES, 8 }});
	const offsets offs({ 0, 1, 3, 4 });

	::gpiod::request_config req_cfg({
		{ reqprop::OFFSETS, offs }
	});

	::gpiod::line_config line_cfg({
		{ lineprop::DIRECTION, direction::OUTPUT },
		{ lineprop::OUTPUT_VALUE, value::INACTIVE }
	});

	auto request = ::gpiod::chip(sim.dev_path()).request_lines(req_cfg, line_cfg);

	SECTION("set single value")
	{
		request.set_value(1, value::ACTIVE);

		REQUIRE(sim.get_value(0) == simval::INACTIVE);
		REQUIRE(sim.get_value(1) == simval::ACTIVE);
		REQUIRE(sim.get_value(3) == simval::INACTIVE);
		REQUIRE(sim.get_value(4) == simval::INACTIVE);
	}

	SECTION("set all values")
	{
		request.set_values({
			value::ACTIVE,
			value::INACTIVE,
			value::ACTIVE,
			value::INACTIVE
		});

		REQUIRE(sim.get_value(0) == simval::ACTIVE);
		REQUIRE(sim.get_value(1) == simval::INACTIVE);
		REQUIRE(sim.get_value(3) == simval::ACTIVE);
		REQUIRE(sim.get_value(4) == simval::INACTIVE);
	}

	SECTION("set a subset of values")
	{
		request.set_values({ 4, 3 }, { value::ACTIVE, value::INACTIVE });

		REQUIRE(sim.get_value(0) == simval::INACTIVE);
		REQUIRE(sim.get_value(1) == simval::INACTIVE);
		REQUIRE(sim.get_value(3) == simval::INACTIVE);
		REQUIRE(sim.get_value(4) == simval::ACTIVE);
	}

	SECTION("set a subset of values with mappings")
	{
		request.set_values({
			{ 0, value::ACTIVE },
			{ 4, value::INACTIVE },
			{ 1, value::ACTIVE}
		});

		REQUIRE(sim.get_value(0) == simval::ACTIVE);
		REQUIRE(sim.get_value(1) == simval::ACTIVE);
		REQUIRE(sim.get_value(3) == simval::INACTIVE);
		REQUIRE(sim.get_value(4) == simval::INACTIVE);
	}
}

TEST_CASE("line_request can be moved", "[line-request]")
{
	::gpiosim::chip sim({{ simprop::NUM_LINES, 8 }});
	::gpiod::chip chip(sim.dev_path());
	const offsets offs({ 3, 1, 0, 2 });

	auto request = chip.request_lines(
		::gpiod::request_config({
			{ reqprop::OFFSETS, offs }
		}),
		::gpiod::line_config()
	);

	auto fd = request.fd();

	auto another = chip.request_lines(
		::gpiod::request_config({
			{ reqprop::OFFSETS, offsets({ 6 }) }
		}),
		::gpiod::line_config()
	);

	SECTION("move constructor works")
	{
		auto moved(::std::move(request));

		REQUIRE(moved.fd() == fd);
		REQUIRE_THAT(moved.offsets(), Catch::Equals(offs));
	}

	SECTION("move assignment operator works")
	{
		another = ::std::move(request);

		REQUIRE(another.fd() == fd);
		REQUIRE_THAT(another.offsets(), Catch::Equals(offs));
	}
}

TEST_CASE("released request can no longer be used", "[line-request]")
{
	::gpiosim::chip sim;

	auto request = ::gpiod::chip(sim.dev_path()).request_lines(
		::gpiod::request_config({
			{ reqprop::OFFSETS, offsets({ 0 }) }
		}),
		::gpiod::line_config()
	);

	request.release();

	REQUIRE_THROWS_AS(request.offsets(), ::gpiod::request_released);
}

TEST_CASE("line_request survives parent chip", "[line-request][chip]")
{
	::gpiosim::chip sim;

	sim.set_pull(0, pull::PULL_UP);

	SECTION("chip is released")
	{
		::gpiod::chip chip(sim.dev_path());

		auto request = chip.request_lines(
			::gpiod::request_config({
				{ reqprop::OFFSETS, offsets({ 0 }) }
			}),
			::gpiod::line_config({
				{ lineprop::DIRECTION, direction::INPUT }
			})
		);

		REQUIRE_THAT(request.get_value(0), value_matcher(pull::PULL_UP));

		chip.close();

		REQUIRE_THAT(request.get_value(0), value_matcher(pull::PULL_UP));
	}

	SECTION("chip goes out of scope")
	{
		/* Need to get the request object somehow. */
		::gpiod::chip dummy(sim.dev_path());

		auto request = dummy.request_lines(
			::gpiod::request_config({
				{ reqprop::OFFSETS, offsets({ 0 }) }
			}),
			::gpiod::line_config({
				{ lineprop::DIRECTION, direction::INPUT }
			})
		);

		request.release();
		dummy.close();

		{
			::gpiod::chip chip(sim.dev_path());

			request = chip.request_lines(
				::gpiod::request_config({
					{ reqprop::OFFSETS, offsets({ 0 }) }
				}),
				::gpiod::line_config({
					{ lineprop::DIRECTION, direction::INPUT }
				})
			);

			REQUIRE_THAT(request.get_value(0), value_matcher(pull::PULL_UP));
		}

		REQUIRE_THAT(request.get_value(0), value_matcher(pull::PULL_UP));
	}
}

TEST_CASE("line_request stream insertion operator works", "[line-request]")
{
	::gpiosim::chip sim({{ simprop::NUM_LINES, 4 }});

	auto request = ::gpiod::chip(sim.dev_path()).request_lines(
		::gpiod::request_config({
			{ reqprop::OFFSETS, offsets({ 3, 1, 0, 2 }) }
		}),
		::gpiod::line_config()
	);

	::std::stringstream buf, expected;

	expected << "gpiod::line_request(num_lines=4, line_offsets=gpiod::offsets(3, 1, 0, 2), fd=" <<
		    request.fd() << ")";

	SECTION("active request")
	{
		buf << request;

		REQUIRE_THAT(buf.str(), Catch::Equals(expected.str()));
	}

	SECTION("request released")
	{
		request.release();

		buf << request;

		REQUIRE_THAT(buf.str(), Catch::Equals("gpiod::line_request(released)"));
	}
}

} /* namespace */

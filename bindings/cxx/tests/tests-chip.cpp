// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2021-2022 Bartosz Golaszewski <brgl@bgdev.pl>

#include <catch2/catch.hpp>
#include <gpiod.hpp>
#include <sstream>
#include <system_error>
#include <utility>

#include "gpiosim.hpp"
#include "helpers.hpp"

using property = ::gpiosim::chip::property;
using line_name = ::gpiosim::chip::line_name;

namespace {

TEST_CASE("chip constructor works", "[chip]")
{
	SECTION("open an existing GPIO chip")
	{
		::gpiosim::chip sim;

		REQUIRE_NOTHROW(::gpiod::chip(sim.dev_path()));
	}

	SECTION("opening a nonexistent file fails with ENOENT")
	{
		REQUIRE_THROWS_MATCHES(::gpiod::chip("/dev/nonexistent"),
				       ::std::system_error, system_error_matcher(ENOENT));
	}

	SECTION("opening a file that is not a device fails with ENOTTY")
	{
		REQUIRE_THROWS_MATCHES(::gpiod::chip("/tmp"),
				       ::std::system_error, system_error_matcher(ENOTTY));
	}

	SECTION("opening a non-GPIO character device fails with ENODEV")
	{
		REQUIRE_THROWS_MATCHES(::gpiod::chip("/dev/null"),
				       ::std::system_error, system_error_matcher(ENODEV));
	}

	SECTION("move constructor")
	{
		::gpiosim::chip sim({{ property::LABEL, "foobar" }});

		::gpiod::chip first(sim.dev_path());
		REQUIRE_THAT(first.get_info().label(), Catch::Equals("foobar"));
		::gpiod::chip second(::std::move(first));
		REQUIRE_THAT(second.get_info().label(), Catch::Equals("foobar"));
	}
}

TEST_CASE("chip operators work", "[chip]")
{
	::gpiosim::chip sim({{ property::LABEL, "foobar" }});
	::gpiod::chip chip(sim.dev_path());

	SECTION("assignment operator")
	{
		::gpiosim::chip moved_sim({{ property::LABEL, "moved" }});
		::gpiod::chip moved_chip(moved_sim.dev_path());

		REQUIRE_THAT(chip.get_info().label(), Catch::Equals("foobar"));
		chip = ::std::move(moved_chip);
		REQUIRE_THAT(chip.get_info().label(), Catch::Equals("moved"));
	}

	SECTION("boolean operator")
	{
		REQUIRE(chip);
		chip.close();
		REQUIRE_FALSE(chip);
	}
}

TEST_CASE("chip properties can be read", "[chip]")
{
	::gpiosim::chip sim({{ property::NUM_LINES, 8 }, { property::LABEL, "foobar" }});
	::gpiod::chip chip(sim.dev_path());

	SECTION("get device path")
	{
		REQUIRE_THAT(chip.path(), Catch::Equals(sim.dev_path()));
	}

	SECTION("get file descriptor")
	{
		REQUIRE(chip.fd() >= 0);
	}
}

TEST_CASE("line lookup by name works", "[chip]")
{
	::gpiosim::chip sim({
		{ property::NUM_LINES, 8 },
		{ property::LINE_NAME, line_name(0, "foo") },
		{ property::LINE_NAME, line_name(2, "bar") },
		{ property::LINE_NAME, line_name(3, "baz") },
		{ property::LINE_NAME, line_name(5, "xyz") }
	});

	::gpiod::chip chip(sim.dev_path());

	SECTION("lookup successful")
	{
		REQUIRE(chip.get_line_offset_from_name("baz") == 3);
	}

	SECTION("lookup failed")
	{
		REQUIRE(chip.get_line_offset_from_name("nonexistent") < 0);
	}
}

TEST_CASE("line lookup: behavior for duplicate names", "[chip]")
{
	::gpiosim::chip sim({
		{ property::NUM_LINES, 8 },
		{ property::LINE_NAME, line_name(0, "foo") },
		{ property::LINE_NAME, line_name(2, "bar") },
		{ property::LINE_NAME, line_name(3, "baz") },
		{ property::LINE_NAME, line_name(5, "bar") }
	});

	::gpiod::chip chip(sim.dev_path());

	REQUIRE(chip.get_line_offset_from_name("bar") == 2);
}

TEST_CASE("closed chip can no longer be used", "[chip]")
{
	::gpiosim::chip sim;

	::gpiod::chip chip(sim.dev_path());
	chip.close();
	REQUIRE_THROWS_AS(chip.path(), ::gpiod::chip_closed);
}

TEST_CASE("stream insertion operator works for chip", "[chip]")
{
	::gpiosim::chip sim({
		{ property::NUM_LINES, 4 },
		{ property::LABEL, "foobar" }
	});

	::gpiod::chip chip(sim.dev_path());
	::std::stringstream buf;

	SECTION("open chip")
	{
		::std::stringstream expected;

		expected << "gpiod::chip(path=" << sim.dev_path() <<
			    ", info=gpiod::chip_info(name=\"" << sim.name() <<
			    "\", label=\"foobar\", num_lines=4))";

		buf << chip;
		REQUIRE_THAT(buf.str(), Catch::Equals(expected.str()));
	}

	SECTION("closed chip")
	{
		chip.close();
		REQUIRE_THAT(chip, stringify_matcher<::gpiod::chip>("gpiod::chip(closed)"));
	}
}

} /* namespace */

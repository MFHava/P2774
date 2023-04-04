
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <future>
#include <catch.hpp>
#include <tls.hpp>

namespace {
	struct no_default_ctor {
		int val;

		no_default_ctor(int val) : val{val} {}
	};

	struct move_only {
		move_only() =default;
		move_only(const move_only &) =delete;
		move_only(move_only &&) noexcept =default;
		auto operator=(const move_only &) -> move_only & =delete;
		auto operator=(move_only &&) noexcept -> move_only & =default;
		~move_only() noexcept =default;
	};
}

TEST_CASE("tls default ctor", "[tls] [ctor] [default]") {
	p2774::tls<int> tls0;
	p2774::tls<move_only> tls1;
	static_assert(!std::is_constructible_v<p2774::tls<no_default_ctor>>);
}

TEST_CASE("tls custom ctor copy", "[tls] [ctor] [custom] [copy]") {
	int i{10};
	p2774::tls<int> tls0{i};
	REQUIRE(std::get<0>(tls0.local()) == i);

	no_default_ctor nd{1};
	p2774::tls<no_default_ctor> tls1{nd};
	REQUIRE(std::get<0>(tls1.local()).val == nd.val);

	static_assert(!std::is_constructible_v<p2774::tls<move_only>, const move_only &>);
}

TEST_CASE("tls custom ctor move", "[tls] [ctor] [custom] [move]") {
	p2774::tls<int> tls0{10};
	REQUIRE(std::get<0>(tls0.local()) == 10);

	p2774::tls<no_default_ctor> tls1{no_default_ctor{1}};
	REQUIRE(std::get<0>(tls1.local()).val == 1);

	static_assert(!std::is_constructible_v<p2774::tls<move_only>, move_only &&>);
}

TEST_CASE("tls custom ctor functor", "[tls] [ctor] [custom] [functor]") {
	p2774::tls<int> tls0{[] { return 10; }};
	REQUIRE(std::get<0>(tls0.local()) == 10);

	p2774::tls<no_default_ctor> tls1{[] { return no_default_ctor{1}; }};
	REQUIRE(std::get<0>(tls1.local()).val == 1);

	p2774::tls<move_only> tls2{[] { return move_only{}; }};
	(void)tls2.local();
}

TEST_CASE("tls clear", "[tls]") {
	p2774::tls<int> tls;
	REQUIRE(std::get<1>(tls.local()));
	REQUIRE(!std::get<1>(tls.local()));

	tls.clear();
	REQUIRE(std::get<1>(tls.local()));
	REQUIRE(!std::get<1>(tls.local()));
}

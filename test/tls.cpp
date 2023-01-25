
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <catch.hpp>
#include <tls.hpp>

//TODO
TEST_CASE("WIP") {
	p2774::tls<int> tls{[] { return 10; }};
	auto [storage, flag]{tls.local()};
	REQUIRE(storage == 10);
	REQUIRE(flag);
	REQUIRE(std::get<1>(tls.local()) == false);
}

namespace {
	struct noncopyable {
		noncopyable() =default;
		noncopyable(const noncopyable &) =delete;
		noncopyable(noncopyable &&) noexcept =default;
		auto operator=(const noncopyable &) -> noncopyable & =delete;
		auto operator=(noncopyable &&) noexcept -> noncopyable & =default;
		~noncopyable() noexcept =default;
	};
}

TEST_CASE("noncopyable", "[tls]") {
	noncopyable n;
	p2774::tls<noncopyable> tls{n};//{[] { return noncopyable{}; }};
	auto [storage, flag]{tls.local()};
	REQUIRE(flag);
	REQUIRE(std::get<1>(tls.local()) == false);
}

//TODO: noncopyable


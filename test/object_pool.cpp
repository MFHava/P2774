
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <vector>
#include <iostream>
#include <algorithm>
#include <execution>
#include <catch.hpp>
#include <object_pool.hpp>

TEST_CASE("object_pool", "[object_pool]") {
	std::vector<std::size_t> values(1'000'000);
	std::iota(std::begin(values), std::end(values), 0);

	const auto reference{std::accumulate(std::begin(values), std::end(values), std::size_t{0})};

	p2774::object_pool<std::size_t> tls;
	std::for_each(std::execution::par, std::begin(values), std::end(values), [&](auto val) {
		*tls.lease() += val;
	});

	const auto value{std::accumulate(tls.begin(), tls.end(), std::size_t{0})};
	REQUIRE(value == reference);

	std::cout << "block count: " << tls.block_count() << "\n";
	std::cout << "node count: " << tls.node_count() << "\n";
}

//TODO: further tests

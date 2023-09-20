
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "race_free.hpp"

namespace p2774::internal {
	auto lockfree_stack::load() const -> tagged_ptr {
#ifdef _WIN32
		tagged_ptr result{nullptr, 0};
		(void)_InterlockedCompareExchange128(std::bit_cast<long long *>(&top_), 0, 0, std::bit_cast<long long *>(&result));
		return result;
#else
		return std::bit_cast<tagged_ptr>(__sync_val_compare_and_swap(std::bit_cast<__uint128_t *>(&top_), 0, 0));
#endif
	}

	auto lockfree_stack::compare_exchange(tagged_ptr & expected, tagged_ptr desired) noexcept -> bool {
#ifdef _WIN32
		return _InterlockedCompareExchange128(std::bit_cast<long long *>(&top_), std::bit_cast<long long>(desired.tag), std::bit_cast<long long>(desired.head), std::bit_cast<long long *>(&expected)) == 1;
#else
		const auto old{expected};
		expected = std::bit_cast<tagged_ptr>(__sync_val_compare_and_swap(std::bit_cast<__uint128_t *>(&top_), std::bit_cast<__uint128_t>(expected), std::bit_cast<__uint128_t>(desired)));
		return expected == old;
#endif
	}
}

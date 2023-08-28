
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <bit>
#include <memory>
#include <cstdint>
#include <utility>
#include <optional>
#include <concepts>
#include <type_traits>

namespace p2774 {
	//! @attention @c Allocator must be thread-safe!
	template<typename T, typename Allocator = std::allocator<T>>
	class race_free final {
		struct node final {
			std::optional<T> value; //!TODO: would this need to be aware of @c Allocator?
			node * next{nullptr};
		};

		using allocator_traits = std::allocator_traits<Allocator>::template rebind_traits<node>;
		using allocator_type = typename allocator_traits::allocator_type;

		class lockfree_stack final {
			//TODO: 32bit support?
			static_assert(sizeof(void *) == 8);
			static_assert(sizeof(void *) == sizeof(long long));

			mutable struct alignas(16) tagged_ptr final {
				node * head{nullptr};
				std::uintptr_t tag{0};

				friend
				auto operator==(const tagged_ptr &, const tagged_ptr &) noexcept -> bool =default;
			} top_;
			static_assert(sizeof(tagged_ptr) == 16);
#ifndef _WIN32
			static_assert(sizeof(__uint128_t) == sizeof(tagged_ptr));
#endif
		public:
			lockfree_stack() noexcept =default;
			lockfree_stack(const lockfree_stack &) =delete;
			auto operator=(const lockfree_stack &) -> lockfree_stack & =delete;
			~lockfree_stack() noexcept =default;

			auto unsafe_top() const -> node * { return top_.head; }

			auto load() const -> tagged_ptr {
#ifdef _WIN32
				tagged_ptr result{nullptr, 0};
				(void)_InterlockedCompareExchange128(std::bit_cast<long long *>(&top_), 0, 0, std::bit_cast<long long *>(&result));
				return result;
#else
				return std::bit_cast<tagged_ptr>(__sync_val_compare_and_swap(std::bit_cast<__uint128_t *>(&top_), 0, 0));
#endif
			}

			auto compare_exchange(tagged_ptr & expected, tagged_ptr desired) noexcept -> bool {
#ifdef _WIN32
				return _InterlockedCompareExchange128(std::bit_cast<long long *>(&top_), std::bit_cast<long long>(desired.tag), std::bit_cast<long long>(desired.head), std::bit_cast<long long *>(&expected)) == 1;
#else
				const auto old{expected};
				expected = std::bit_cast<tagged_ptr>(__sync_val_compare_and_swap(std::bit_cast<__uint128_t *>(&top_), std::bit_cast<__uint128_t>(expected), std::bit_cast<__uint128_t>(desired)));
				return expected == old;
#endif
			}
		};

		mutable lockfree_stack stack;
		[[no_unique_address]] mutable allocator_type allocator;

		template<bool IsConst>
		class iterator_t final {
			node * ptr{nullptr};

			friend race_free;

			iterator_t(node * head) noexcept : ptr{head} { for(; ptr && !ptr->value; ptr = ptr->next); }
		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type        = T;
			using difference_type   = std::ptrdiff_t;
			using pointer           = std::conditional_t<IsConst, const T, T> *;
			using reference         = std::conditional_t<IsConst, const T, T> &;

			iterator_t() noexcept =default;

			auto operator++() noexcept -> iterator_t & {
				assert(ptr);
				for(ptr = ptr->next; ptr && !ptr->value; ptr = ptr->next);
				return *this;
			}
			auto operator++(int) noexcept -> iterator_t {
				auto tmp{*this};
				++*this;
				return tmp;
			}

			auto operator*() const noexcept -> reference {
				assert(ptr);
				assert(ptr->value);
				return *ptr->value;
			}
			auto operator->() const noexcept -> pointer { return &**this; }

			friend
			auto operator==(const iterator_t &, const iterator_t &) noexcept -> bool =default;
		};
	public:
		class handle final {
			friend race_free;

			lockfree_stack * owner{nullptr};
			node * ptr;

			handle(lockfree_stack * owner, node * ptr) noexcept : owner{owner}, ptr{ptr} {}
		public:
			handle(const handle &) =delete;
			handle(handle && other) noexcept : owner{std::exchange(other.owner, nullptr)}, ptr{other.ptr} {} //moved-from state is valid but unspecified!
			auto operator=(const handle &) -> handle & =delete;
			auto operator=(handle && other) noexcept -> handle & { //moved-from state is valid but unspecified!
				using std::swap;
				swap(owner, other.owner);
				swap(ptr, other.ptr);
			}

			~handle() noexcept {
				if(!owner) return;

				//push to stack
				for(auto old{owner->load()};;) {
					ptr->next = old.head;
					if(owner->compare_exchange(old, {ptr, old.tag + 1}))
						break; //inserted
				}
			}

			explicit
			operator bool() const noexcept {
				assert(owner);
				return ptr->value.has_value();
			}

			auto operator*() noexcept -> T & {
				assert(owner);
				return *ptr->value;
			}
			auto operator->() noexcept -> T * { return &**this; }

			template<typename... Args>
			requires std::is_constructible_v<T, Args...>
			auto emplace(Args &&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) -> T & {
				assert(owner);
				return ptr->value.emplace(std::forward<Args>(args)...);
			}
			template<typename U, typename... Args>
			requires std::is_constructible_v<T, std::initializer_list<U>, Args...>
			auto emplace(std::initializer_list<U> ilist, Args &&... args) noexcept(std::is_nothrow_constructible_v<T, std::initializer_list<U>, Args...>) -> T & {
				assert(owner);
				return ptr->value.emplace(ilist, std::forward<Args>(args)...);
			}

			void reset() noexcept { //TODO: needed?!
				assert(owner);
				ptr->value.reset();
			}
		};

		race_free(Allocator alloc = Allocator{}) noexcept : allocator{alloc} {}
		race_free(const race_free &) =delete;
		auto operator=(const race_free &) -> race_free & =delete;
		~race_free() noexcept {
			for(auto ptr{stack.unsafe_top()}; ptr;) {
				auto next{ptr->next};
				allocator_traits::destroy(allocator, ptr);
				allocator_traits::deallocate(allocator, ptr, 1);
				ptr = next;
			}
		}

		[[nodiscard]]
		auto get() const -> handle {
			//pop from stack or allocate new node if stack is empty
			for(auto old{stack.load()};;) {
				const auto & [ptr, tag]{old};
				if(!ptr) { //need new node
					auto node{allocator_traits::allocate(allocator, 1)};
					try {
						allocator_traits::construct(allocator, node);
						return {&stack, node};
					} catch(...) {
						allocator_traits::deallocate(allocator, node, 1);
						throw;
					}
				}
				if(stack.compare_exchange(old, {ptr->next, tag + 1}))
					return {&stack, old.head}; //hand ownership to handle
			}
		}

		auto reset() noexcept {
			//resets all nodes, does not release memory!
			for(auto ptr{stack.unsafe_top()}; ptr; ptr = ptr->next)
				ptr->value.reset();
		}

		//! @name Iteration
		//! @note only yields iterators that actually contain a value!
		//! @{
		using iterator       = iterator_t<false>;
		static_assert(std::forward_iterator<iterator>);
		using const_iterator = iterator_t<true>;
		static_assert(std::forward_iterator<const_iterator>);

		auto begin() const noexcept -> const_iterator { return stack.unsafe_top(); }
		auto begin()       noexcept -> iterator { return stack.unsafe_top(); }
		auto end() const noexcept -> const_iterator { return {}; }
		auto end()       noexcept -> iterator { return {}; }

		auto cbegin() const noexcept -> const_iterator { return begin(); }
		auto cend() const noexcept -> const_iterator { return end(); }
		//! @}
	};
}

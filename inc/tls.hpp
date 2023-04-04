
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <tuple>
#include <atomic>
#include <thread>
#include <functional>
#include <type_traits>

namespace p2774 {
	namespace internal {
		template<bool IsConst, typename T>
		using add_const_t = std::conditional_t<IsConst, const T, T>;

		inline
		const
		std::size_t bucket_count{std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1};

		template<typename T>
		using init_func = std::move_only_function<T() const>;

		template<typename T>
		class atomic_unordered_map final {
			class atomic_forward_list final {
				struct node final {
					node(const init_func<T> & init) : value{init()} {}

					T value;
					const std::thread::id owner{std::this_thread::get_id()};
					node * next{nullptr};
				};

				std::atomic<node *> head{nullptr};
			public:
				template<bool IsConst>
				class iterator_t final {
					node * ptr{nullptr};

					friend atomic_forward_list;

					iterator_t(add_const_t<IsConst, std::atomic<node *>> & head) noexcept : ptr{head.load()} {}
				public:
					iterator_t() noexcept =default;

					auto operator++() noexcept -> iterator_t & {
						assert(ptr);
						ptr = ptr->next;
						return *this;
					}

					auto operator*() const noexcept -> add_const_t<IsConst, T> & {
						assert(ptr);
						return ptr->value;
					}

					friend
					auto operator==(const iterator_t & lhs, const iterator_t & rhs) noexcept -> bool { return lhs.ptr == rhs.ptr; }
				};

				atomic_forward_list() noexcept =default;
				atomic_forward_list(const atomic_forward_list &) =delete;
				auto operator=(const atomic_forward_list &) -> atomic_forward_list & =delete;
				~atomic_forward_list() noexcept { clear(); }

				auto local(const init_func<T> & init) -> std::tuple<T &, bool> {
					for(auto ptr{head.load()}; ptr; ptr = ptr->next)
						if(ptr->owner == std::this_thread::get_id())
							return {ptr->value, false};

					auto ptr{new node(init)};
					ptr->next = head.load();
					while(!head.compare_exchange_weak(ptr->next, ptr));
					return {ptr->value, true};
				}

				void clear() noexcept {
					for(auto ptr{head.exchange(nullptr)}; ptr;) {
						const auto old{ptr};
						ptr = ptr->next;
						delete old;
					}
				}

				auto begin() const noexcept -> iterator_t<true> { return head; }
				auto begin()       noexcept -> iterator_t<false> { return head; }
				auto end() const noexcept -> iterator_t<true> { return {}; }
				auto end()       noexcept -> iterator_t<false> { return {}; }
			};

			std::atomic<atomic_forward_list *> buckets{new atomic_forward_list[bucket_count]};

			template<bool IsConst>
			class iterator_t final {
				using list_t = add_const_t<IsConst, atomic_forward_list>;
				list_t * buckets{nullptr};
				std::size_t index{0};
				typename atomic_forward_list::template iterator_t<IsConst> it;

				friend atomic_unordered_map;

				iterator_t(list_t * buckets) noexcept : buckets{buckets} {
					if(buckets)
						for(; index < bucket_count;) {
							auto & bucket{this->buckets[index]};
							it = bucket.begin();
							if(it != bucket.end()) break;
							++index;
						}
				}
			public:
				using iterator_category = std::forward_iterator_tag;
				using value_type        = T;
				using difference_type   = std::ptrdiff_t;
				using pointer           = add_const_t<IsConst, T> *;
				using reference         = add_const_t<IsConst, T> &;

				iterator_t() noexcept =default;

				auto operator++() noexcept -> iterator_t & {
					if(++it == buckets[index].end()) {
						for(++index; index < bucket_count;) {
							auto & bucket{buckets[index]};
							it = bucket.begin();
							if(it != bucket.end()) break;
							++index;
						}
					}
					return *this;
				}
				auto operator++(int) noexcept -> iterator_t {
					auto tmp{*this};
					++*this;
					return tmp;
				}

				auto operator*() const noexcept -> reference { return *it; }
				auto operator->() const noexcept -> pointer { return &**this; }

				friend
				auto operator==(const iterator_t & lhs, const iterator_t & rhs) noexcept -> bool { return lhs.it == rhs.it; }
			};
		public:
			using iterator       = iterator_t<false>;
			static_assert(std::forward_iterator<iterator>);
			using const_iterator = iterator_t<true>;
			static_assert(std::forward_iterator<const_iterator>);

			atomic_unordered_map() =default;
			atomic_unordered_map(const atomic_unordered_map &) =delete;
			auto operator=(const atomic_unordered_map &) -> atomic_unordered_map & =delete;
			~atomic_unordered_map() noexcept {
				clear();
				delete[] buckets;
			}

			auto local(const init_func<T> & init) -> std::tuple<T &, bool> {
				const auto tid{std::this_thread::get_id()};
				const auto hash{std::hash<std::thread::id>{}(tid)};
				const auto ind{hash % bucket_count};
				return buckets[ind].local(init);
			}

			void clear() noexcept {
				auto ptr{buckets.load()};
				for(std::size_t i{0}; i < bucket_count; ++i) ptr[i].clear();
			}

			auto begin() const noexcept -> const_iterator { return buckets.load(); }
			auto begin()       noexcept -> iterator { return buckets.load(); }
			auto end() const noexcept -> const_iterator { return {}; }
			auto end()       noexcept -> iterator { return {}; }
		};
	}

	//! @brief scoped thread-local storage
	//! @tparam Type type of thread-local storage
	template<typename Type>
	requires (std::is_same_v<Type, std::remove_cvref_t<Type>>)
	class tls final {
		using storage_t = internal::atomic_unordered_map<Type>;
		storage_t storage;
		internal::init_func<Type> init;
	public:
		using iterator       = typename storage_t::iterator;
		using const_iterator = typename storage_t::const_iterator;

		template<typename Func>
		requires std::is_convertible_v<Type, std::invoke_result_t<Func>>
		tls(Func f) : init{std::move(f)} {}

		template<typename... Args>
		requires (std::is_constructible_v<Type, Args...> && (std::is_copy_constructible_v<Args> && ...))
		tls(Args &&... args) : tls{[=] { return Type(args...); }} {}

		tls(const tls &) =delete;
		auto operator=(const tls &) -> tls & =delete;

		~tls() noexcept =default;

		//! @brief get access to thread-local storage
		//! @returns tuple with reference to thread-local storage and a bool flag specifying whether the element was newly allocated (=true)
		//! @throws any exception thrown by the respective constructor of Type
		//! @note allocates thread-local storage on first call from thread
		[[nodiscard]]
		auto local() -> std::tuple<Type &, bool> { return storage.local(init); }

		//! @brief clears all thread-local storage
		//! @attention never invoke from concurrent context!
		void clear() noexcept { storage.clear(); }

		//! @name Iteration
		//! @brief forward iteration support for thread-local storage
		//! @{
		auto begin() const noexcept -> const_iterator { return storage.begin(); }
		auto begin()       noexcept -> iterator { return storage.begin(); }
		auto end() const noexcept -> const_iterator { return storage.end(); }
		auto end()       noexcept -> iterator { return storage.end(); }

		auto cbegin() const noexcept -> const_iterator { return begin(); }
		auto cend() const noexcept -> const_iterator { return end(); }
		//! @}
	};
}

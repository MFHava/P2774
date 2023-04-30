
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <tuple>
#include <atomic>
#include <thread>
#include <type_traits>
#include <memory_resource>

namespace p2774 {
	namespace internal {
		template<bool IsConst, typename T>
		using add_const_t = std::conditional_t<IsConst, const T, T>;

		inline
		const
		std::size_t bucket_count{std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1};

		template<typename T, typename Allocator>
		class atomic_unordered_map final {
			struct node final {
				node(const auto & init) : value{init()} {}

				T value;
				const std::thread::id owner{std::this_thread::get_id()};
				node * bucket_next{nullptr}, * list_next{nullptr};
			};

			using bucket = std::atomic<node *>;

			using node_allocator_traits = std::allocator_traits<Allocator>::template rebind_traits<node>;
			using node_allocator = typename node_allocator_traits::allocator_type;
			using bucket_allocator_traits = std::allocator_traits<Allocator>::template rebind_traits<bucket>;
			using bucket_allocator = typename bucket_allocator_traits::allocator_type;

			bucket * buckets; //non-owning, for fast lookup
			std::atomic<node *> root; //owning, for fast traversal
			[[no_unique_address]] Allocator allocator;

			template<bool IsConst>
			class iterator_t final {
				node * ptr{nullptr};

				friend atomic_unordered_map;

				iterator_t(node * root) noexcept : ptr{root} {}
			public:
				using iterator_category = std::forward_iterator_tag;
				using value_type        = T;
				using difference_type   = std::ptrdiff_t;
				using pointer           = add_const_t<IsConst, T> *;
				using reference         = add_const_t<IsConst, T> &;

				iterator_t() noexcept =default;

				auto operator++() noexcept -> iterator_t & {
					ptr = ptr->list_next;
					return *this;
				}
				auto operator++(int) noexcept -> iterator_t {
					auto tmp{*this};
					++*this;
					return tmp;
				}

				auto operator*() const noexcept -> reference { return ptr->value; }
				auto operator->() const noexcept -> pointer { return &**this; }

				friend
				auto operator==(const iterator_t & lhs, const iterator_t & rhs) noexcept -> bool { return lhs.ptr == rhs.ptr; }
			};
		public:
			using iterator       = iterator_t<false>;
			static_assert(std::forward_iterator<iterator>);
			using const_iterator = iterator_t<true>;
			static_assert(std::forward_iterator<const_iterator>);

			atomic_unordered_map(const Allocator & alloc) : allocator{alloc} {
				bucket_allocator a{allocator};
				auto ptr{bucket_allocator_traits::allocate(a, bucket_count)};
				for(std::size_t i{0}; i < bucket_count; ++i) bucket_allocator_traits::construct(a, ptr + i);
				buckets = ptr;
			}
			atomic_unordered_map(const atomic_unordered_map &) =delete;
			auto operator=(const atomic_unordered_map &) -> atomic_unordered_map & =delete;
			~atomic_unordered_map() noexcept {
				clear();
				bucket_allocator a{allocator};
				for(std::size_t i{0}; i < bucket_count; ++i) bucket_allocator_traits::destroy(a, buckets + i);
				bucket_allocator_traits::deallocate(a, buckets, bucket_count);
			}

			auto local(const auto & init) -> std::tuple<T &, bool> {
				const auto tid{std::this_thread::get_id()};
				const auto hash{std::hash<std::thread::id>{}(tid)};
				const auto ind{hash % bucket_count};
				auto & bucket{buckets[ind]};

				//trying to find existing node
				for(auto ptr{bucket.load()}; ptr; ptr = ptr->bucket_next)
					if(ptr->owner == std::this_thread::get_id())
						return {ptr->value, false};

				//adding new node
				node_allocator a{allocator};
				auto node{node_allocator_traits::allocate(a, 1)};
				try {
					node_allocator_traits::construct(a, node, init);
				} catch(...) { 
					node_allocator_traits::deallocate(a, node, 1);
					throw;
				}
				for(node->bucket_next = bucket.load(); !bucket.compare_exchange_weak(node->bucket_next, node););
				for(node->list_next = root.load(); !root.compare_exchange_weak(node->list_next, node););
				return {node->value, true};
			}

			void clear() noexcept {
				node_allocator a{allocator};
				for(auto ptr{root.exchange(nullptr)}; ptr;) {
					const auto old{ptr};
					ptr = ptr->list_next;
					node_allocator_traits::destroy(a, old);
					node_allocator_traits::deallocate(a, old, 1);
				}
				for(std::size_t i{0}; i < bucket_count; ++i) buckets[i] = nullptr;
			}

			auto begin() const noexcept -> const_iterator { return root.load(); }
			auto begin()       noexcept -> iterator { return root.load(); }
			auto end() const noexcept -> const_iterator { return {}; }
			auto end()       noexcept -> iterator { return {}; }
		};

		template<typename T, typename Allocator>
		class init_func final {
			struct vtable final {
				void(*dtor)(void *, const Allocator &) noexcept;
				T(*call)(const void *);
			};
			void * functor;
			const vtable * vptr;
			[[no_unique_address]] Allocator allocator;
		public:
			template<typename F>
			requires std::is_invocable_v<F>
			init_func(F func, const Allocator & alloc) : allocator{alloc} {
				using VT = std::decay_t<F>;
				static_assert(std::is_constructible_v<VT, F>);
				if constexpr(std::is_function_v<std::remove_pointer_t<F>>) assert(func);

				using allocator_traits = std::allocator_traits<Allocator>::template rebind_traits<VT>;
				using allocator_type = typename allocator_traits::allocator_type;

				static constexpr vtable vtable{
					+[](void * functor, const Allocator & alloc) noexcept {
						allocator_type a{alloc};
						auto ptr{reinterpret_cast<VT *>(functor)};
						allocator_traits::destroy(a, ptr);
						allocator_traits::deallocate(a, ptr, 1);
					},
					+[](const void * functor) { return std::invoke_r<T>(*reinterpret_cast<const VT *>(functor)); }
				};
				vptr = &vtable;
				allocator_type a{alloc};
				auto ptr{allocator_traits::allocate(a, 1)};
				allocator_traits::construct(a, ptr, std::move(func));
				functor = ptr;
			}

			init_func(const init_func &) =delete;
			auto operator=(const init_func &) -> init_func & =delete;

			~init_func() noexcept { vptr->dtor(functor, allocator); }

			auto operator()() const -> T { return vptr->call(functor); }
		};
	}

	//! @brief scoped thread-local storage
	//! @tparam Type type of thread-local storage
	//! @tparam Allocator allocator to use, must be concurrency-safe!
	template<typename Type, typename Allocator = std::allocator<Type>>
	requires (std::is_same_v<Type, std::remove_cvref_t<Type>>)
	class tls final {
		using storage_t = internal::atomic_unordered_map<Type, Allocator>;
		storage_t storage;
		internal::init_func<Type, Allocator> init;
	public:
		using iterator       = typename storage_t::iterator;
		using const_iterator = typename storage_t::const_iterator;

		template<typename Func>
		requires std::is_convertible_v<Type, std::invoke_result_t<Func>>
		explicit
		tls(Func func, const Allocator & allocator = Allocator{}) : storage{allocator}, init{std::move(func), allocator} {}
		explicit
		tls(const Allocator & allocator = Allocator{}) requires std::is_default_constructible_v<Type> : tls{[] { return Type{}; }, allocator} {}
		explicit
		tls(const Type & val, const Allocator & allocator = Allocator{}) requires std::is_copy_constructible_v<Type> : tls{[=] { return val; }, allocator} {}
		explicit
		tls(Type && val, const Allocator & allocator = Allocator{}) requires std::is_copy_constructible_v<Type> : tls{[val{std::move(val)}] { return val; }, allocator} {}

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

	namespace pmr {
		template<typename Type>
		using tls = p2774::tls<Type, std::pmr::polymorphic_allocator<Type>>;
	}
}

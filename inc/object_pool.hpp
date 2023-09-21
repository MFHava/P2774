
//          Copyright Michael Florian Hava.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file ../LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <bit>
#include <memory>
#include <cassert>
#include <cstdint>
#include <utility>
#include <concepts>
#include <semaphore>
#include <type_traits>

namespace p2774 {
	template<std::default_initializable T, typename Allocator>
	class object_pool;

	namespace internal {
		class lockfree_stack final {
			//! @todo 32bit support?
			static_assert(sizeof(void *) == 8);

			mutable struct alignas(16) tagged_ptr final {
				void * head{nullptr};
				std::uintptr_t tag{0};

				friend
				auto operator==(const tagged_ptr &, const tagged_ptr &) noexcept -> bool =default;
			} top_;
			static_assert(sizeof(tagged_ptr) == 16);
		public:
			lockfree_stack() noexcept =default;
			lockfree_stack(const lockfree_stack &) =delete;
			auto operator=(const lockfree_stack &) -> lockfree_stack & =delete;
			~lockfree_stack() noexcept =default;

			auto load() const -> tagged_ptr;
			auto compare_exchange(tagged_ptr & expected, tagged_ptr desired) noexcept -> bool;
		};


		inline
		constexpr
		std::size_t max_block_size{512}; //! @todo optimal size?

		template<typename T>
		struct node final {
			T value{};
			node * next{nullptr};
		};

		template<typename T>
		constexpr
		std::size_t nodes_per_block{(internal::max_block_size - sizeof(void *)) / sizeof(node<T>)};

		template<typename T>
		struct block final {
			block * next{nullptr};
			static_assert(nodes_per_block<T> > 1);
			node<T> nodes[nodes_per_block<T>];
		};


		template<typename T>
		struct iterator final {
			using iterator_category = std::forward_iterator_tag;
			using value_type        = std::remove_const_t<T>;
			using difference_type   = std::ptrdiff_t;
			using pointer           = T *;
			using reference         = T &;

			iterator() noexcept =default;

			auto operator++() noexcept -> iterator & {
				assert(ptr);
				++index;
				if(index == internal::nodes_per_block<value_type>) {
					ptr = ptr->next;
					index = 0;
				}
				return *this;
			}
			auto operator++(int) noexcept -> iterator {
				auto tmp{*this};
				++*this;
				return tmp;
			}

			auto operator*() const noexcept -> reference {
				assert(ptr);
				return ptr->nodes[index].value;
			}
			auto operator->() const noexcept -> pointer { return std::addressof(**this); }

			friend
			auto operator==(const iterator &, const iterator &) noexcept -> bool =default;
		private:
			template<std::default_initializable U, typename Allocator>
			friend
			class p2774::object_pool;

			iterator(block<value_type> * ptr) noexcept : ptr{ptr} {}

			block<value_type> * ptr{nullptr};
			std::size_t index{0};
		};


		template<typename T>
		class handle final {
			template<std::default_initializable U, typename Allocator>
			friend
			class p2774::object_pool;

			internal::lockfree_stack & owner;
			node<T> * ptr;

			handle(internal::lockfree_stack & owner, node<T> * ptr) noexcept : owner{owner}, ptr{ptr} {}
		public:
			handle(const handle &) =delete;
			handle(handle && other) noexcept =delete;
			auto operator=(const handle &) -> handle & =delete;
			auto operator=(handle &&) noexcept -> handle & =delete;

			~handle() noexcept {
				//push to stack
				for(auto old{owner.load()};;) {
					ptr->next = static_cast<node<T> *>(old.head);
					if(owner.compare_exchange(old, {ptr, old.tag + 1}))
						break; //inserted
				}
			}

			auto operator*() const noexcept -> T & { return ptr->value; }
			auto operator->() const noexcept -> T * { return get(); }
			auto get() const noexcept -> T *{ return std::addressof(**this); }
		};
	}

	template<std::default_initializable T, typename Allocator = std::allocator<T>>
	class object_pool final {
		using node = internal::node<T>;
		using block = internal::block<T>;
		using allocator_traits = std::allocator_traits<Allocator>::template rebind_traits<block>;
		using allocator_type = typename allocator_traits::allocator_type;

		mutable internal::lockfree_stack stack;
		mutable block * blocks{nullptr};
		mutable std::binary_semaphore lock{1};
		[[no_unique_address]] mutable allocator_type allocator;
	public:
		using handle = internal::handle<T>;

		object_pool(const Allocator & alloc = Allocator{}) noexcept : allocator{alloc} {}
		object_pool(const object_pool &) =delete;
		auto operator=(const object_pool &) -> object_pool & =delete;
		~object_pool() noexcept {
			for(auto ptr{blocks}; ptr;) {
				auto next{ptr->next};
				allocator_traits::destroy(allocator, ptr);
				allocator_traits::deallocate(allocator, ptr, 1);
				ptr = next;
			}
		}

		[[nodiscard]]
		auto lease() const -> handle {
			//pop from stack or allocate new node if stack is empty
			auto old{stack.load()};
			for(; old.head;) {
retry: //jump here for retry as we already know that head is valid...
				if(stack.compare_exchange(old, {static_cast<node *>(old.head)->next, old.tag + 1}))
					return {stack, static_cast<node *>(old.head)}; //hand ownership to handle
			}

			//may need new node
			const class guard final {
				std::binary_semaphore & lock;
			public:
				guard(std::binary_semaphore & lock) noexcept : lock{lock} { lock.acquire(); }
				~guard() noexcept { lock.release(); }
			} guard{lock};
			if((old = stack.load()).head) goto retry; //since trying to acquire lock, memory has become available

			//actually need new node
			auto block{allocator_traits::allocate(allocator, 1)};
			try {
				allocator_traits::construct(allocator, block);

				//register block & link new nodes
				block->next = blocks;
				blocks = block;
				for(std::size_t i{1}; i < internal::nodes_per_block<T>; ++i) block->nodes[i].next = block->nodes + i + 1;

				//insert new nodes into stack
				do { block->nodes[internal::nodes_per_block<T> - 1].next = static_cast<node *>(old.head); }
				while(!stack.compare_exchange(old, {block->nodes + 1, old.tag + 1}));

				return {stack, block->nodes}; //we kept the first node for ourselves
			} catch(...) {
				allocator_traits::deallocate(allocator, block, 1);
				throw;
			}
		}

		//! @name Iteration
		//! @note only yields iterators that actually contain a value!
		//! @todo can we statically prevent users from calling this whilst there are active handles?!
		//! @{
		using iterator       = internal::iterator<T>;
		static_assert(std::forward_iterator<iterator>);
		using const_iterator = internal::iterator<const T>;
		static_assert(std::forward_iterator<const_iterator>);

		auto begin() const noexcept -> const_iterator { return blocks; }
		auto begin()       noexcept -> iterator { return blocks; }
		auto end() const noexcept -> const_iterator { return {}; }
		auto end()       noexcept -> iterator { return {}; }

		auto cbegin() const noexcept -> const_iterator { return begin(); }
		auto cend() const noexcept -> const_iterator { return end(); }
		//! @}


		//! @name Debugging
		//! @{
		auto block_count() const noexcept -> std::size_t {
			std::size_t count{0};
			for(auto ptr{blocks}; ptr; ptr = ptr->next) ++count;
			return count;
		}

		auto node_count() const noexcept -> std::size_t {
			return block_count() * internal::nodes_per_block<T>;
		}
		//! @}
	};
}

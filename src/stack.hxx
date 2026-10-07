#pragma once

#include <cstdlib>
#include <type_traits>

template <class T> class Stack {
	static_assert(std::is_trivially_copyable_v<T>);

  public:
	Stack() : items(static_cast<T *>(malloc(capacity * sizeof(T)))) {
	}

	Stack(const Stack &) = delete;
	Stack &operator=(const Stack &) = delete;

	~Stack() {
		free(items);
	}

	void push(T item) {
		if (count >= capacity) {
			capacity *= 2;
			items = static_cast<T *>(realloc(items, capacity * sizeof(T)));
		}

		items[count++] = item;
	}

	/* T{} when the stack is empty */
	T pop() {
		return count > 0 ? items[--count] : T{};
	}

	/* T{} when the stack is empty */
	T peek() const {
		return count > 0 ? items[count - 1] : T{};
	}

	unsigned size() const {
		return count;
	}

	bool empty() const {
		return count == 0;
	}

	void clear() {
		count = 0;
	}

  private:
	unsigned capacity = 10;
	unsigned count = 0;
	T *items;
};

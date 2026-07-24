export module util;
import std;

export template<class... Ts>
struct overload : Ts... {
	using Ts::operator()...;
};
template<class... Ts>
overload(Ts...) -> overload<Ts...>;

export template<typename T>
class Buffered {
  private:
	bool dirty = false;
	T current;
	T pending;

  public:
	Buffered(T&& initial) : current(std::move(initial)) {}

	void buffer(T&& recent) {
		pending = std::move(recent);
		dirty = true;
	}

	void commit() {
		if (dirty == true) {
			current = std::move(pending);
			// Pending doesn't need to be changed, we won't read from it again
			dirty = false;
		}
	}
};

#include <mayquill/logger.h>
import mayday;
import mayday.epoll;
import std;

int main() {
#ifndef MAYQUILL_ICE
	std::println(" This is incorrect! Exiting!");
	std::exit(1);
#endif

	Mayday mayday;
	Epoll epoll;

	epoll.add_fd(mayday.seat.seat_fd, 0, Epoll::Interest::Readable);
	auto events = epoll.yield();

	for (auto& event : events) {
		switch (event.data.u32) {
		// seat fd
        case 0: {
			break;
		}
		}
	}
}

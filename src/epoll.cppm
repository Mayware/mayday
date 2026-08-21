module;
#include <mayquill/logger.h>
#include <sys/epoll.h>
export module mayday.epoll;
import mayquill;
import std;

export class Epoll {
  private:
	int epoll_fd;
	std::array<epoll_event, 64> events; // Max of 64 events

  public:
	enum class Interest {
		Readable,
		Writable,
	};

	Epoll() {
		epoll_fd = epoll_create1(EPOLL_CLOEXEC);
		if (epoll_fd == -1) {
			MQ_XERROR("Failed to create epoll_fd");
		};
	}

	void add_fd(int fd, std::uint32_t id, Interest interest) {
		auto event = epoll_event {
			.events = get_interest(interest),
			.data = epoll_data_t {
				.u32 = id,
			}};
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) // will copy event
			MQ_XERRNO("Failed to add fd {}, id {}, to epoll", fd, id);
	}

	std::span<epoll_event> yield() {
		int event_count = epoll_wait(epoll_fd, events.data(), events.size(), -1); // -1 means no timeout
		if (event_count < 0)
			MQ_XERRNO("Failed to epoll_wait");
		return std::span(events).first(event_count);
	};

  private:
	EPOLL_EVENTS get_interest(Interest interest, std::source_location source = std::source_location::current()) {
		switch (interest) {
		case Interest::Readable:
			return EPOLLIN;
		case Interest::Writable:
			return EPOLLOUT;
		default:
			MQ_SXERROR(source, "Invalid interest provided");
		}
	}
};

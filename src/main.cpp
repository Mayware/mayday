#include <libudev.h>
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

	// epoll.add_fd(mayday.seat.seat_fd, 0, Epoll::Interest::Readable);
	epoll.add_fd(mayday.udevd.watch_fd, 1, Epoll::Interest::Readable);
	while (true) {
		auto events = epoll.yield();
		for (auto& event : events) {
			switch (event.data.u32) {
			// seat fd
			case 0: {
				break;
			}
			// Udev watch fd
			case 1: {
				// We'll probably receive multiple events
				// Essentially, recv
				while (udev_device* event = udev_monitor_receive_device(mayday.udevd.watch)) {
					if (!event)
						continue;
					auto action = udev_device_get_action(event);
					// Final component of the /sys path
					auto sysname = udev_device_get_sysname(event);
					auto hotplug = udev_device_get_property_value(event, "HOTPLUG");

					if (hotplug && std::string_view(action) == "change" && std::string_view(hotplug) == "1") {
					}
				}
				break;
			}
			}
		}
	}
}

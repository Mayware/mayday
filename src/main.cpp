#include <libudev.h>
#include <xf86drm.h>
#include <mayquill/logger.h>
import mayday;
import mayday.epoll;
import std;

void handle_vsync_wrapper(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, unsigned int crtc_handle, void* user_data) {
	// tv_usec means time value microseconds, where 1000 microseconds = 1 millisecond and 1000 milliseconds = 1 second
	static_cast<Mayday*>(user_data)->handle_vsync(fd, sequence, tv_sec, tv_usec, crtc_handle);
}

int main() {
#ifndef MAYQUILL_ICE
	std::println(" This is incorrect! Exiting!");
	std::exit(1);
#endif
	Mayday mayday;
	Epoll epoll;

	epoll.add_fd(mayday.seat.seat_fd, 0, Epoll::Interest::Readable);
	epoll.add_fd(mayday.udevd.watch_fd, 1, Epoll::Interest::Readable);
	epoll.add_fd(mayday.seat.device_fd, 2, Epoll::Interest::Readable);

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
				bool needs_regen = false;
				// Essentially, recv
				while (udev_device* event = udev_monitor_receive_device(mayday.udevd.watch)) {
					if (!event)
						continue;
					auto hotplug = udev_device_get_property_value(event, "HOTPLUG");

					if (hotplug && std::string_view(hotplug) == "1") {
						needs_regen = true;
					}
				}
				if (needs_regen)
					mayday.regenerate_monitors();
				break;
			}
            // device fd
			case 2: {
                drmEventContext handler = {
                    .version = DRM_EVENT_CONTEXT_VERSION,
                    .page_flip_handler2 = &handle_vsync_wrapper,
                };
                drmHandleEvent(mayday.seat.device_fd, &handler);
			}
			}
		}
	}
}

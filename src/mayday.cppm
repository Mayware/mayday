module;
#include <libudev.h>
#include <mayday/libseat.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
export module mayday;
import logger;
export import mayday.reality;
import mayquill;
import vulkan;

void enable_seat(libseat* libseat, void* user_data) {
	auto seat = static_cast<Seat*>(user_data);
	seat->active = true;
	log<If>([] { return "Seat was enabled"; });
}

void disable_seat(libseat* libseat, void* user_data) {
	auto seat = static_cast<Seat*>(user_data);
	seat->active = false;
	log<If>([] { return "Seat was disabled"; });
	libseat_disable_seat(libseat);
	// No need to re-open devices, the fd is not invalidated, it isn't useable until re-enabling
}

// Reality contains the non-wayland (ie. vulkan, drm, seat etc) fields
export class Mayday : public Reality {
  private:
	/* Vulkan shit */
	// Gets the vulkan "objects"
	static Render get_shit(dev_t device_rdev);
	VkMonitor get_vk_monitor(std::uint32_t width, std::uint32_t height, std::uint32_t frame_count);
	void render_monitor(std::uint32_t monitor_index, std::uint32_t frame_index);
	void regenerate_heaps(); // Regenerate the resource + sampler heaps (and heap_properties)

  public:
	/* Mainly drm shit */
	void regenerate_monitors();
	void handle_vsync(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, unsigned int crtc_handle);

	mayquill::Server server;

	Mayday(std::string device_path, dev_t device_rdev) : Reality {.render = get_shit(device_rdev)} {
		//* MISC *//
		misc = Misc {
			.device_path = device_path,
			.device_rdev = device_rdev,
		};

		//* LIBSEAT *//
		libseat_seat_listener listener = {
			.enable_seat = enable_seat,
			.disable_seat = disable_seat,
		};
		seat.seat = libseat_open_seat(&listener, &seat);
		if (!seat.seat)
			fail<Er, No>([] { return "Failed to open seat"; });

		seat.seat_fd = libseat_get_fd(seat.seat);
		if (seat.seat_fd < 0) {
			fail<Er, No>([] { return "Failed to get seat fd"; });
		}

		// Yield until we're given control of the seat
		while (!seat.active) {
			if (libseat_dispatch(seat.seat, -1) == -1)
				fail<Er, No>([] { return "Failed to dispatch libseat"; });
		}

		// Device id is libseats internal handle to the device, it takes it again when closing the device
		seat.device_id = libseat_open_device(seat.seat, misc.device_path.c_str(), &seat.device_fd);
		if (seat.device_id == -1)
			fail<Er, No>([] { return "Failed to open device"; });

		//* UDEV *//
		// Udev 'context', essentially, the handle to udev
		this->udevd.context = udev_new();
		if (!udevd.context)
			fail<Er, No>([] { return "Failed to create udev context"; });

		// Netlink is how userspace programs communicate with the kernel (via socket shit)
		// Netlink is has a 'multicast' system meaning you subscribe, then all subscribers get notifs
		// THis means we subscribe to udev's "udev" broadcast group, which rebroadcasts kernel kobject events (after udev processes them)
		// https://www.kernel.org/doc/html/next/userspace-api/netlink/intro.html
		udevd.watch = udev_monitor_new_from_netlink(udevd.context, "udev");
		if (!udevd.watch)
			fail<Er>([] { return "Failed to create udev monitor"; });

		// A good article, albeit, looks ai genned
		// https://linuxvox.com/blog/uevent-sent-from-kernel-to-user-space-udev/#what-are-uevents
		// Matches changes to /sys/class/drm (that is where the drm subsystem reflects its state)
		if (udev_monitor_filter_add_match_subsystem_devtype(udevd.watch, "drm", nullptr) < 0)
			fail<Er>([] { return "Failed to add watch"; });
		if (udev_monitor_enable_receiving(udevd.watch) < 0)
			fail<Er>([] { return "Failed to enable receiving the watch"; });
		udevd.watch_fd = udev_monitor_get_fd(udevd.watch);
		if (udevd.watch_fd < 0)
			fail<Er>([] { return "Failed to get watch fd"; });

		//* DRM *//
		if (drmSetClientCap(seat.device_fd, DRM_CLIENT_CAP_ATOMIC, 1))
			fail<Er, No>([] { return "Failed to enable atomic commits"; });

		// Gives us access to the real planes, i.e. the primary plane.
		// This isn't enabled by default for legacy programs not using the API
		if (drmSetClientCap(seat.device_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1))
			fail<Er, No>([] { return "Failed to enable universal planes"; });

		regenerate_monitors();

		//* SERVER *//
		// Upcasting, the base class (i.e. reality), sits inside of mayday (the derived class)
        // And will be initialised as a whole unti (i.e. it will be contiguous in memory, like a standalone object would be)
        // The compiler knows the memory offset reality sits within mayday, and hence can static_cast
        // the mayday address, returning the address of the reality object within mayday.
        // Given base classes are initialised before mayday's members, I would imagine the address of reality to be the same
        // as maydays, as mayday's size would just extend further to cover its sole unique field of mayquill::server.
        // (and hence, it would be funny and perhaps possible to just shove mayday's address into there, since they're probably the same)
        // We can later just use this reference, to get reality back, completely safely
		server.reference = static_cast<Reality*>(this);
		server.bind_socket();
	}

	~Mayday() {

		// libseat_close_device(seat.seat, seat.device_id);
		// close(seat.device_fd);
		// libseat_close_seat(seat.seat);
	}
};

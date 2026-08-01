module;
#include <libudev.h>
#include <mayday/libseat.h>
#include <mayquill/logger.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
export module mayday;
import mayquill;
import vulkan;

// using namespace mayquill;
struct Seat {
	bool active;
	libseat* seat;
	int seat_fd;
	int device_id;
	int device_fd;
};

struct Udev {
	udev* context;
	udev_monitor* watch;
	int watch_fd;
};

struct VkFrame {
	vk::raii::CommandBuffer command_buffer;
	vk::raii::DeviceMemory memory;
	vk::raii::Image image;
	vk::raii::ImageView image_view;
	vk::DrmFormatModifierProperties2EXT drm_modifier;
	std::vector<vk::SubresourceLayout> memory_planes_layouts;
	int dmabuf_fd; // We create the image in vulkan, but get an FD to it, so DRM can import that image as a GEM buffer
};

struct Frame {
	vk::raii::CommandBuffer command_buffer;
	std::uint32_t semaphore_value;
	vk::raii::DeviceMemory memory;
	vk::raii::Image image;
	vk::raii::ImageView image_view;
	vk::DrmFormatModifierProperties2EXT drm_modifier;
	std::vector<vk::SubresourceLayout> memory_planes_layouts;

	std::uint32_t framebuffer_handle;
};

struct VkMonitor {
	vk::raii::CommandPool command_pool;
	std::vector<VkFrame> frames;
};

struct Monitor {
	std::uint32_t connector_handle;
    std::uint32_t encoder_handle;
	std::uint32_t crtc_handle;
	std::uint32_t plane_handle;
	drmModeModeInfo mode;
	vk::raii::CommandPool command_pool;
	std::vector<Frame> frames;
};

struct Render {
	vk::raii::Context context;
	vk::raii::Instance instance;
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device device;
	std::uint32_t queue_family_index;
	vk::raii::Queue queue;
	vk::raii::Pipeline graphics_pipeline;
	vk::raii::Semaphore semaphore;
	std::vector<vk::DrmFormatModifierProperties2EXT> supported_drm_modifiers;
};

void enable_seat(libseat* libseat, void* user_data) {
	auto seat = static_cast<Seat*>(user_data);
	seat->active = true;
	MQ_INFO("Seat was enabled");
}

void disable_seat(libseat* libseat, void* user_data) {
	auto seat = static_cast<Seat*>(user_data);
	seat->active = false;
	MQ_INFO("Seat was disabled");
	libseat_disable_seat(libseat);
	// No need to re-open devices, the fd is not invalidated, it isn't useable until re-enabling
}

export class Mayday {
  private:
	/* Vulkan shit */
	// Gets the vulkan "objects"
	Render get_shit();
	VkMonitor get_vk_monitor(std::uint32_t width, std::uint32_t height, std::uint32_t frame_count);

  public:
	/* Mainly drm shit */
	void regenerate_monitors();

	Seat seat;
	Udev udevd;
	mayquill::Server server;
	Render render;
	std::vector<Monitor> monitors;

	Mayday() : render(get_shit()) {

		//* LIBSEAT *//
		libseat_seat_listener listener = {
			.enable_seat = enable_seat,
			.disable_seat = disable_seat,
		};
		seat.seat = libseat_open_seat(&listener, &seat);
		if (!seat.seat)
			MQ_XERRNO("Failed to open seat");

		seat.seat_fd = libseat_get_fd(seat.seat);
		if (seat.seat_fd < 0) {
			MQ_XERRNO("Failed to get seat fd");
		}

		// Yield until we're given control of the seat
		while (!seat.active) {
			if (libseat_dispatch(seat.seat, -1) == -1)
				MQ_XERRNO("Failed to dispatch libseat");
		}

		// Device id is libseats internal handle to the device, it takes it again when closing the device
		seat.device_id = libseat_open_device(seat.seat, "/dev/dri/card0", &seat.device_fd);
		if (seat.device_id == -1)
			MQ_XERRNO("Failed to open device");

		//* UDEV *//
		// Udev 'context', essentially, the handle to udev
		this->udevd.context = udev_new();
		if (!udevd.context)
			MQ_XERRNO("Failed to create udev context");

		// Netlink is how userspace programs communicate with the kernel (via socket shit)
		// Netlink is has a 'multicast' system meaning you subscribe, then all subscribers get notifs
		// THis means we subscribe to udev's "udev" broadcast group, which rebroadcasts kernel kobject events (after udev processes them)
		// https://www.kernel.org/doc/html/next/userspace-api/netlink/intro.html
		udevd.watch = udev_monitor_new_from_netlink(udevd.context, "udev");
		if (!udevd.watch)
			MQ_XERROR("Failed to create udev monitor");

		// A good article, albeit, looks ai genned
		// https://linuxvox.com/blog/uevent-sent-from-kernel-to-user-space-udev/#what-are-uevents
		// Matches changes to /sys/class/drm (that is where the drm subsystem reflects its state)
		if (udev_monitor_filter_add_match_subsystem_devtype(udevd.watch, "drm", nullptr) < 0)
			MQ_XERROR("Failed to add watch");
		if (udev_monitor_enable_receiving(udevd.watch) < 0)
			MQ_XERROR("Failed to enable receiving the watch");
		udevd.watch_fd = udev_monitor_get_fd(udevd.watch);
		if (udevd.watch_fd < 0)
			MQ_XERROR("Failed to get watch fd");

		//* DRM *//
		if (drmSetClientCap(seat.device_fd, DRM_CLIENT_CAP_ATOMIC, 1))
			MQ_XERRNO("Failed to enable atomic commits");

		// Gives us access to the real planes, i.e. the primary plane.
		// This isn't enabled by default for legacy programs not using the API
		if (drmSetClientCap(seat.device_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1))
			MQ_XERRNO("Failed to enable universal planes");

		regenerate_monitors();

		server.bind_socket();
	}

	~Mayday() {

		// libseat_close_device(seat.seat, seat.device_id);
		// close(seat.device_fd);
		// libseat_close_seat(seat.seat);
	}
};

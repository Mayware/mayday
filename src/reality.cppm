module;
#include <libudev.h>
#include <mayday/libseat.h>
#include <xf86drmMode.h>
export module mayday.reality;
import std;
import vulkan;

// This is reality, with the ear to the ground.
// Essentially, this is the non-wayland state of the Mayday class, hence "Reality".
// Mayday itself depends on Mayquill (so it can have the Server). Mayday reality does not depend on mayquill.
// Mayday inherits Reality, to get the same fields. However, Mayquill can also import Reality
// and then co-erce its mayday object reference to Reality, hence getting access to all the other fields
// other than the wayland shit (which it already has). Therefore, it just solves the cyclical dep issue.

export struct Seat {
	bool active;
	libseat* seat;
	int seat_fd;
	int device_id;
	int device_fd;
	dev_t rdev;
};

export struct Udev {
	udev* context;
	udev_monitor* watch;
	int watch_fd;
};

export struct VkFrame {
	vk::raii::DeviceMemory memory;
	vk::raii::Image image;
	vk::raii::ImageView image_view;
	vk::DrmFormatModifierProperties2EXT drm_modifier;
	std::vector<vk::SubresourceLayout> memory_planes_layouts;
	int dmabuf_fd; // We create the image in vulkan, but get an FD to it, so DRM can import that image as a GEM buffer
};

export struct Frame {
	std::uint32_t semaphore_value;
	vk::raii::DeviceMemory memory;
	vk::raii::Image image;
	vk::raii::ImageView image_view;
	vk::DrmFormatModifierProperties2EXT drm_modifier;
	std::vector<vk::SubresourceLayout> memory_planes_layouts;

	std::uint32_t framebuffer_handle;
};

export struct Command {
	vk::raii::CommandPool pool;
	std::vector<vk::raii::CommandBuffer> buffers;
};

export struct VkMonitor {
	Command command;
	std::vector<VkFrame> frames;
};

export struct Monitor {
	std::uint32_t connector_handle;
	std::uint32_t encoder_handle;
	std::uint32_t crtc_handle;
	std::uint32_t plane_handle;
	drmModeModeInfo mode;
	Command command;
	std::vector<Frame> frames;
};

export struct UltraFormat {
	std::uint32_t drm_format;
	std::vector<vk::DrmFormatModifierProperties2EXT> drm_modifiers;
	vk::Format vk_format;
	vk::ComponentMapping vk_swizzed;
};

export struct Render {
	vk::raii::Context context;
	vk::raii::Instance instance;
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device device;
	std::uint32_t queue_family_index;
	vk::raii::Queue queue;
	std::mutex queue_mutex;
	vk::raii::Pipeline graphics_pipeline;
	vk::raii::Semaphore semaphore;
	std::vector<UltraFormat> ultra_formats;

	std::vector<Command> free_pools;
};

export class Reality {
  public:
	Seat seat;
	Udev udevd;
	Render render;
	std::vector<Monitor> monitors;

	// To operate on free_pools, basically a pool of pools
	Command beg_pool(std::uint32_t buffer_count);
	void donate_pool(Command&& command);

    // General memory helper
	std::optional<std::uint32_t> get_memory_type_index(vk::PhysicalDevice physical_device, vk::MemoryRequirements memory_requirements, vk::MemoryPropertyFlagBits property_flags);
};

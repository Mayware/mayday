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
	std::uint64_t semaphore_value;
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
	std::uint32_t current_frame = 0;
};

export struct UltraFormat {
	std::uint32_t drm_format;
	std::vector<vk::DrmFormatModifierProperties2EXT> drm_modifiers;
	vk::Format vk_format;
	vk::ComponentMapping vk_swizzed;
};

export struct HeapBuffer {
	vk::raii::DeviceMemory memory = nullptr;
	vk::DeviceAddress gpu_address;
	vk::DeviceSize size;
	std::byte* cpu_address;
	vk::raii::Buffer buffer = nullptr;
};

export struct ArbitraryDescriptor {
	float x;
	float y;
	float width;
	float height;
	std::uint32_t sampler_index;
};

export struct HeapProperties {
	static constexpr std::uint32_t resources_per_monitor = 1024;

	vk::DeviceSize driver_reserved_resource_heap_size;
	vk::DeviceSize driver_reserved_sampler_heap_size;
	vk::DeviceSize image_descriptor_size;
	vk::DeviceSize sampler_descriptor_size;
	vk::DeviceSize image_alignment;
	vk::DeviceSize sampler_alignment;
	// So if the arbitrary descriptor is 20 bytes, arbitrary descriptor [0-20], padding [value], image descriptor [eg. value-32]
	// Padding added to match the required alignment
	vk::DeviceSize image_descriptor_in_arbitrary_descriptor_offset;
	// From one resource (as in the "Resource" struct in the shader) to the next
	vk::DeviceSize resource_stride;
	// Sent to the shader, only goes upto u32's. Essentially just the driver reserved size + alignment to match the descriptors
	std::uint32_t resource_heap_start_offset;
	std::uint32_t sampler_heap_start_offset;
};

export struct Render {
	std::vector<Command> free_pools;

	vk::raii::Context context;
	vk::raii::Instance instance;
    std::optional<vk::raii::DebugUtilsMessengerEXT> debug_messenger;
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device device;
	std::uint32_t queue_family_index;
	vk::raii::Queue queue;
	vk::raii::Pipeline graphics_pipeline;
	vk::raii::Semaphore semaphore;
	std::uint64_t semaphore_value = 0; // This is the current value, ie. something will be yielding on this, it's not the one free after
	std::vector<UltraFormat> ultra_formats;
	HeapBuffer resource_heap;
	HeapBuffer sampler_heap;
	HeapProperties heap_properties;
};

export struct Misc {
	std::string device_path;
	dev_t device_rdev;
};

export class Reality {
  public:
	Misc misc;
	Seat seat;
	Udev udevd;
	Render render;
	std::vector<Monitor> monitors;

	// To operate on free_pools, basically a pool of pools
	Command beg_pool(std::uint32_t buffer_count);
	void donate_pool(Command&& command);

	// General memory helper
	static std::optional<std::uint32_t> get_memory_type_index(vk::raii::PhysicalDevice& physical_device, std::uint32_t base_requirements, vk::MemoryPropertyFlags extended_requirements);
	std::uint64_t increment_semaphore() { return ++render.semaphore_value; }
};

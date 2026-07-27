module;
#include <mayquill/logger.h>
export module render;
import vulkan;
import mayquill;

/*
 *  The function2() functions exist as modern alternatives to the older non-2 functions.
 *  They include an additional pnext pointer, which can point to other additional fields they add over time
 *  For example, physical.getQueueFamilyProperties2<vk::StructureChain<vk::QueueFamilyProperties2, vk::QueueFamilyGlobalPriorityProperties>>();
 *  would give you the additional QueueFamilyGlobalPriorityProperties struct in the pnext pointer.
 */
constexpr auto vk_version = vk::ApiVersion14;
constexpr vk::Format image_format = vk::Format::eB8G8R8A8Unorm;

void start(int fd) {
	// Loads libvulkan (manually, via dlopen), and caches the function pointers to be used for instance creation
	// and other gloval entry points
	vk::raii::Context context = {};

	static constexpr vk::ApplicationInfo app_info = {
		.pApplicationName = "Mayday",
		.applicationVersion = vk::makeApiVersion(0, 1, 0, 0),
		.pEngineName = "The dark fountain",
		.engineVersion = vk::makeApiVersion(0, 1, 0, 0),
		.apiVersion = vk_version,
	};

	static constexpr vk::InstanceCreateInfo create_info = {
		.pApplicationInfo = &app_info,
		.enabledLayerCount = 0,
		.ppEnabledLayerNames = nullptr,
		.enabledExtensionCount = 0,
		.ppEnabledExtensionNames = nullptr,
	};

	auto instance = context.createInstance(create_info);

	vk::raii::PhysicalDevice physical_device = nullptr;
	std::uint32_t queue_family_index;
	// A physical device is an actual GPU / vulkan-capable device, that vulkan has access to
	// It becomes a logical device when we actually connect to it
	for (auto& physical : instance.enumeratePhysicalDevices()) {
		if (physical.getProperties().apiVersion < vk_version)
			continue;

		// Vulkan has the concept of `Queue Families`. Queue families is a group of queues that all have the same capabilites.
		// Queue families will advertise capbilities such as `graphics`, `compute`, `transfer, `video decode` etc, implying their supported operations.
		// Queue families will then have multiple queues within them, all supporting these capabilities.
		// In our case we'll just take one queue, that supports graphics, and transfer capabilities (transfer allowing for staging buffers and the like)
		std::uint32_t acceptable_queue_family_index =  std::numeric_limits<std::uint32_t>::max();
		auto queue_families = physical.getQueueFamilyProperties2();
		for (int i = 0; i < queue_families.size(); ++i) {
			auto flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
			if ((queue_families[i].queueFamilyProperties.queueFlags & flags) == flags) {
				acceptable_queue_family_index = i;
				break;
			}
		}
        if (acceptable_queue_family_index ==  std::numeric_limits<std::uint32_t>::max())
            continue;

		// Get the supported features. Supported allows us to get the requested features values, as it fills it out
		auto supported = physical.getFeatures2<
			vk::PhysicalDeviceFeatures2,
			vk::PhysicalDeviceVulkan12Features,
			vk::PhysicalDeviceVulkan13Features>();

		auto& supported_12 = supported.get<vk::PhysicalDeviceVulkan12Features>();
		auto& supported_13 = supported.get<vk::PhysicalDeviceVulkan13Features>();

		// Allows for ImageMemoryBarrier2, SubmitInfo2, etc, just the 2ver for synchronisation stuff
		if (supported_13.synchronization2 == false ||
			// Allows for cmd_buffer.begin_rendering() and cmd_buffer.end_rendering() rather than vk::RenderPass
			supported_13.dynamicRendering == false ||
			// Timeline semaphores are submitted with an integer, and then go up to that integer once the work is done
			// You can submit multiple things with the same semaphore, and since queues are linear, you know if the integer
			// is the highest one you submitted, then the rest are also done. It's an alternative to fences
			supported_12.timelineSemaphore == false) {
			continue;
		}

		physical_device = std::move(physical);
		queue_family_index = acceptable_queue_family_index;
		break;
	}

	if (*physical_device == nullptr)
		MQ_XERROR("No suitable physical GPU device was found");

	// Create the logical device, with our specified features
	vk::PhysicalDeviceVulkan13Features features_13 = {
		.synchronization2 = true,
		.dynamicRendering = true,
	};
	vk::PhysicalDeviceVulkan12Features features_12 = {
		.pNext = &features_13,
		.timelineSemaphore = true,
	};
	// Get the actual queue
	// queue_prorities is a hint to the GPU of which queues should be favoured, if it becomes overloaded
	// If we were using multiple queues, we could assign one 1.0f, and the other 0.2f. In the event that
	// the GPU has to ration workpower, it will favour the workload of the queues with higher priorities.
	float queue_priorities[1] = {1.0f};
	vk::DeviceQueueCreateInfo queue_info = {
		.queueFamilyIndex = queue_family_index,
		.queueCount = 1,
		.pQueuePriorities = queue_priorities,
	};

	vk::DeviceCreateInfo device_info = {
		.pNext = &features_12,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue_info,
	};
	auto device = physical_device.createDevice(device_info);
	auto queue = device.getQueue(queue_family_index, 0); // If we requested 5 to be made, we could get 0 through to 4

	vk::ImageCreateInfo image_info = {
		.imageType = vk::ImageType::e2D, // e just means enum
		.format = image_format,
		.extent = {
			.width = 1920,
			.height = 1080,
			// We can specify a 3d image by setting depth to other than 1. What this means is that the image has a z axis,
			// interpreted exactly the same as X and Y. So in multi-sampling, it'll also apply across the Z-axis. Mip-maps
			// will also shrink the Z axis
			.depth = 1,
		},
		// How many mipmaps we will have in the future, stored in this image (they aren't autogenerated, we'll need to add them if we want them)
		// 2 would mean we will add a half sized image too, 3 would mean that, and a quarter sized one, etc.
		.mipLevels = 1,
		// How many of these images do we want. Eg. 3 would mean we have 3 of these 2d images stored in here. This is different to depth
		// because these images are separate from eachother, the z co-ordinate would be an integer not float, sampling wouldn't go across the z, etc.
		.arrayLayers = 1,
		// Only 1 sampling. A triangle could only go partially across a pixel, for example, through a pixel diagonal. With 1 sample, we'd only test the
		// middle and that would determine if it is fully coloured as that or not. If we had 4 samples, we'd test every quarter middle, and then only add
		// some colour depending on how many hits. Essentially, adds transparency, based on how many samples we miss.
		.samples = vk::SampleCountFlagBits::e1,
		// We know longer know what format the GPU is choosing to store this image in memory with. Eg. with RGBA we'd normally assume from the CPU that
		// we could traverse it literally byte by byte to get pixels, but with this the GPU can choose whatever way it prefers to store it (eg. interlaced,
		// or not consecutively), meaning we can't really assume the memory locations from the CPU any longer.
		.tiling = vk::ImageTiling::eOptimal,
		// We can use this as a colour attachment (for fragment shaders to render into) and as a transfer source into another image
		.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
		// Only one queue family will operate on this image, because we only have one queue lmao. We could set to concurrent if more than one queue
		// could use this image
		.sharingMode = vk::SharingMode::eExclusive,
		// Specifies what content is in this image memory, when we are going to transition
		.initialLayout = vk::ImageLayout::eUndefined,
	};
	vk::raii::DeviceMemory image_memory = nullptr; // Memory declared early, so it is dropped after the image
	vk::raii::Image image = device.createImage(image_info);

	auto memory_requirements = image.getMemoryRequirements();
	auto memory_properties = physical_device.getMemoryProperties();
	std::uint32_t memory_type_index = std::numeric_limits<std::uint32_t>::max();
	for (int i = 0; i < memory_properties.memoryTypeCount; ++i) {
		auto property_flag = vk::MemoryPropertyFlagBits::eDeviceLocal;

		// memoryTypeBits is a bitmask representing memory_properties.memoryTypes indices
		// eg 1011 means the 1st, 3rd, and 4th memoryTypes are compatible.
		if ((memory_requirements.memoryTypeBits & (1u << i)) != 0 &&
			// Ensure the memory is on the GPU (device local), by checking the property_flags on the memoryType
			(memory_properties.memoryTypes[i].propertyFlags & property_flag) == property_flag) {
			memory_type_index = i;
			break;
		}
	}

	if (memory_type_index == std::numeric_limits<std::uint32_t>::max())
		MQ_XERROR("No appropriate image memory found");

	vk::MemoryAllocateInfo allocation_info = {
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = memory_type_index,
	};
	image_memory = device.allocateMemory(allocation_info);
	image.bindMemory(*image_memory, 0);

	// Essentially a 'span' into our existing image, that lets us re-interpret it, and filter what we want from it
	vk::ImageViewCreateInfo image_view_info = {
		.image = image,
		// You could interpret a 3d image as a 2d image with array layers
		.viewType = vk::ImageViewType::e2D,
		// The format is allowed to differ, (ie. reading R8G8B8A8Unorm as R8G8B8A8Srgb), if the format is compatible
		.format = image_format,

		.subresourceRange = {
			// If the image stores multiple data in one (eg.interleaved depth/stencil), we can pick just the data we want
			// In our case, all our data is colour
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			// MIP level 0 is the regular full-res image, so we choose mip level range from 0 to 1 (exclusive), so just 0
			.baseMipLevel = 0,
			.levelCount = 1,
			// Same with array layers
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};
	auto image_view = device.createImageView(image_view_info);
}

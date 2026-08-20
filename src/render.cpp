module;
#include <drm_fourcc.h>
#include <mayquill/logger.h>
module mayday;
import vulkan;
import mayquill;
import mayday.buffers;
import mayday.surfaces;
import mayday.util;

/*
 *  The function2() functions exist as modern alternatives to the older non-2 functions.
 *  They include an additional pnext pointer, which can point to other additional fields they add over time
 *  For example, physical.getQueueFamilyProperties2<vk::StructureChain<vk::QueueFamilyProperties2, vk::QueueFamilyGlobalPriorityProperties>>();
 *  would give you the additional QueueFamilyGlobalPriorityProperties struct in the pnext pointer.
 */

// There are two types of vulkan planes, format planes, and memory planes in vulkan. Format planes are like the abstract planes concepts formats specify
// like how NV12 specifies 2 format planes. Memory planes are the actual underlying memory representation the GPU has so although NV12 has 2 format
// planes, the GPU may store it in one "plane" (a single buffer allocation). Or it may do it in 62 of them. When the linear modifier is used, it'll
// do memory planes = format planes, so you can interpret it CPU side.
// DRM/KMS also has the idea of "planes", but for them it means the output planes the GPU supports, as in primary plane, overlay plane, ie. layers that the CRTC can blend

constexpr auto vk_version = vk::ApiVersion14;

// To get the equivalent enum, take the string name, PascalCase it, and append ExtensionName
// The enum points to a macro, which just defines the string
constexpr std::array required_extensions = {
	vk::EXTImageDrmFormatModifierExtensionName, // Allows us to use DRM format modifiers with images
	vk::KHRExternalMemoryFdExtensionName,		// Ability to export device memory as POSIX FD's (generic)
	vk::EXTExternalMemoryDmaBufExtensionName,	// As a DMABUF Fd, requires the one above
};

// Our internal image format is the 0 index, which is equivalent to vk::Format::eB8G8R8A8Unorm
// You'll see we use ultra_formats[0].vk_format / ultra_formats[0].drm_format just hardcoded
constexpr std::array supported_drm_formats = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_BGR161616,
	DRM_FORMAT_RGB161616,
	DRM_FORMAT_XBGR16161616,
	DRM_FORMAT_ABGR16161616,
	DRM_FORMAT_XRGB16161616,
	DRM_FORMAT_ARGB16161616,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_ABGR16161616F,
	DRM_FORMAT_XRGB16161616F,
	DRM_FORMAT_ARGB16161616F,
};

// DRM formats are little endian, so RGB would be stored as BGR. Vulkan just reads in normal order, hence it being inversed.
// Vulkan doesn't say if some bits are unused, nor does it have all the unique RGB, BGR, GBR combos etc, so the swizzling is to get that (and to blank out alpha to just padding as required)
std::pair<vk::Format, vk::ComponentMapping> fourcc_to_vk(std::uint32_t format) {
	switch (format) {
	case DRM_FORMAT_XRGB8888:
		return {vk::Format::eB8G8R8A8Unorm, {.a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_ARGB8888:
		return {vk::Format::eB8G8R8A8Unorm, {}};
	case DRM_FORMAT_XBGR8888:
		return {vk::Format::eR8G8B8A8Unorm, {.a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_ABGR8888:
		return {vk::Format::eR8G8B8A8Unorm, {}};
	case DRM_FORMAT_RGBX8888:
		return {vk::Format::eR8G8B8A8Unorm, {.r = vk::ComponentSwizzle::eA, .g = vk::ComponentSwizzle::eB, .b = vk::ComponentSwizzle::eG, .a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_RGBA8888:
		return {vk::Format::eR8G8B8A8Unorm, {.r = vk::ComponentSwizzle::eA, .g = vk::ComponentSwizzle::eB, .b = vk::ComponentSwizzle::eG, .a = vk::ComponentSwizzle::eR}};
	case DRM_FORMAT_BGRX8888:
		return {vk::Format::eR8G8B8A8Unorm, {.r = vk::ComponentSwizzle::eG, .g = vk::ComponentSwizzle::eB, .b = vk::ComponentSwizzle::eA, .a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_BGRA8888:
		return {vk::Format::eR8G8B8A8Unorm, {.r = vk::ComponentSwizzle::eG, .g = vk::ComponentSwizzle::eB, .b = vk::ComponentSwizzle::eA, .a = vk::ComponentSwizzle::eR}};
	case DRM_FORMAT_XRGB2101010:
		return {vk::Format::eA2R10G10B10UnormPack32, {.a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_ARGB2101010:
		return {vk::Format::eA2R10G10B10UnormPack32, {}};
	case DRM_FORMAT_XBGR2101010:
		return {vk::Format::eA2B10G10R10UnormPack32, {.a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_ABGR2101010:
		return {vk::Format::eA2B10G10R10UnormPack32, {}};
	case DRM_FORMAT_BGR161616:
		return {vk::Format::eR16G16B16Unorm, {}};
	case DRM_FORMAT_RGB161616:
		return {vk::Format::eR16G16B16Unorm, {.r = vk::ComponentSwizzle::eB, .g = vk::ComponentSwizzle::eG, .b = vk::ComponentSwizzle::eR, .a = vk::ComponentSwizzle::eA}};
	case DRM_FORMAT_XBGR16161616:
		return {vk::Format::eR16G16B16A16Unorm, {.a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_ABGR16161616:
		return {vk::Format::eR16G16B16A16Unorm, {}};
	case DRM_FORMAT_XRGB16161616:
		return {vk::Format::eR16G16B16A16Unorm, {.r = vk::ComponentSwizzle::eB, .g = vk::ComponentSwizzle::eG, .b = vk::ComponentSwizzle::eR, .a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_ARGB16161616:
		return {vk::Format::eR16G16B16A16Unorm, {.r = vk::ComponentSwizzle::eB, .g = vk::ComponentSwizzle::eG, .b = vk::ComponentSwizzle::eR, .a = vk::ComponentSwizzle::eA}};
	case DRM_FORMAT_XBGR16161616F:
		return {vk::Format::eR16G16B16A16Sfloat, {.a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_ABGR16161616F:
		return {vk::Format::eR16G16B16A16Sfloat, {}};
	case DRM_FORMAT_XRGB16161616F:
		return {vk::Format::eR16G16B16A16Sfloat, {.r = vk::ComponentSwizzle::eB, .g = vk::ComponentSwizzle::eG, .b = vk::ComponentSwizzle::eR, .a = vk::ComponentSwizzle::eOne}};
	case DRM_FORMAT_ARGB16161616F:
		return {vk::Format::eR16G16B16A16Sfloat, {.r = vk::ComponentSwizzle::eB, .g = vk::ComponentSwizzle::eG, .b = vk::ComponentSwizzle::eR, .a = vk::ComponentSwizzle::eA}};
	default:
		MQ_XERROR("Invalid drm format");
	}
}

// spirv wordsd are uint32's
std::vector<std::uint32_t> read_spirv(std::string name) {
	std::ifstream stream("build/shaders/" + name + ".spv", std::ios::binary | std::ios::ate); // (ate means open file, at the end)
	if (!stream)
		MQ_XERROR("Failed to open shaderfile");
	std::vector<std::uint32_t> spirv(stream.tellg() / sizeof(std::uint32_t));
	stream.seekg(0);
	stream.read(reinterpret_cast<char*>(spirv.data()), spirv.size() * sizeof(std::uint32_t));
	return spirv;
}

VkMonitor Mayday::get_vk_monitor(std::uint32_t width, std::uint32_t height, std::uint32_t frame_count) {
	// Command buffers
	vk::CommandPoolCreateInfo command_pool_info = {
		// the buffers won't live for a long time, hint to the driver
		.flags = vk::CommandPoolCreateFlagBits::eTransient,
		// Buffers can be submitted to any queue in the same family
		.queueFamilyIndex = render.queue_family_index,
	};

	auto command = beg_pool(1);

	// Collate the valid drm modifiers, into just a vector of their IDs
	auto& base_format = render.ultra_formats[0];
	std::vector<std::uint64_t> drm_modifiers_ids;
	drm_modifiers_ids.reserve(base_format.drm_modifiers.size());
	for (auto modifier : base_format.drm_modifiers) {
		drm_modifiers_ids.push_back(modifier.drmFormatModifier);
	}
	// https://docs.vulkan.org/refpages/latest/refpages/source/VkImageDrmFormatModifierListCreateInfoEXT.html (allocation path)
	// Vulkan picks the plane layouts & modifiers, from the options we provide here (layouts are implicit), ie. we are negotiating and vulkan will tell us after what it picked
	vk::ImageDrmFormatModifierListCreateInfoEXT potential_modifiers_info = {
		.drmFormatModifierCount = static_cast<std::uint32_t>(drm_modifiers_ids.size()),
		.pDrmFormatModifiers = drm_modifiers_ids.data(),
	};

	std::vector<VkFrame> frames;

	for (int i = 0; i < frame_count; ++i) {
		vk::ImageCreateInfo image_info = {
			.pNext = &potential_modifiers_info, // The driver knows best. (It will pick its preferred one)
			.imageType = vk::ImageType::e2D,	// e just means enum
			.format = render.ultra_formats[0].vk_format,
			.extent = {
				.width = width,
				.height = height,
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
			.tiling = vk::ImageTiling::eDrmFormatModifierEXT, // We provided the potential options in pnext
			// We can use this as a colour attachment (for fragment shaders to render into)
			.usage = vk::ImageUsageFlagBits::eColorAttachment,
			// Only one queue family will operate on this image, because we only have one queue lmao. We could set to concurrent if more than one queue
			// could use this image
			.sharingMode = vk::SharingMode::eExclusive,
			// Specifies what content is in this image memory, when we are going to transition
			.initialLayout = vk::ImageLayout::eUndefined,
		};
		// Get whatever drm modifier it chose (the full definition)
		vk::raii::Image image = render.device.createImage(image_info);
		auto image_drm_modifier_id = image.getDrmFormatModifierPropertiesEXT().drmFormatModifier;
		// get DrmFOrmatModifierPropertiesEXT returns a VKImageDrmFormatModifierProprertiesEXT structure, not a DrmFormatModifierProperties2EXT structure.
		// The former only contains the drm id, the latter (which we use in the supported_drm_modifiers field), contains additional fields such as the plane count.
		// Therefore, we need to lookup the id in the supported_drm_modifiers to get the full deets
		vk::DrmFormatModifierProperties2EXT image_drm_modifier;
		// This should always land
		for (auto& possible_modifier : base_format.drm_modifiers) {
			if (possible_modifier.drmFormatModifier == image_drm_modifier_id) {
				image_drm_modifier = possible_modifier;
				break;
			}
		}
		// Although we only rock classic RGBA which specifies 1 format planes, it may possibly have multiple memory planes
		// each with their own pitch and offset. Drm only supports upto 4 memory planes, hence we'll retrieve their infos, if they do exist
		static constexpr vk::ImageAspectFlagBits memory_planes[4] = {
			vk::ImageAspectFlagBits::eMemoryPlane0EXT,
			vk::ImageAspectFlagBits::eMemoryPlane1EXT,
			vk::ImageAspectFlagBits::eMemoryPlane2EXT,
			vk::ImageAspectFlagBits::eMemoryPlane3EXT,
		};
		// https://docs.vulkan.org/refpages/latest/refpages/source/VkSubresourceLayout.html
		std::vector<vk::SubresourceLayout> memory_planes_layouts;
		for (int i = 0; i < image_drm_modifier.drmFormatModifierPlaneCount; ++i) {
			memory_planes_layouts.push_back(image.getSubresourceLayout(vk::ImageSubresource {
				.aspectMask = memory_planes[i],
				// Mips / array layers would give different offsets, of course (ie. if we had miplevels, we could choose which we wanted)
				.mipLevel = 0,
				.arrayLayer = 0,
			}));
		}
		// The subresource then gives us the offset, size, rowPitch of that plane in memory

		// Allocate the memory
		auto requirements = image.getMemoryRequirements();
		auto memory_type_index = get_memory_type_index(*render.physical_device, requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		if (!memory_type_index.has_value())
			MQ_XERROR("No appropriate image memory found");

		// Make the memory exportable, as DRM will import it
		vk::ExportMemoryAllocateInfo export_info = {
			.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
		};
		vk::MemoryAllocateInfo allocation_info = {
			.pNext = &export_info,
			.allocationSize = requirements.size,
			.memoryTypeIndex = *memory_type_index,
		};
		auto image_memory = render.device.allocateMemory(allocation_info);
		image.bindMemory(image_memory, 0); // Last param is offset
		// Get an fd to the memory
		int dmabuf_fd = render.device.getMemoryFdKHR({
			.memory = image_memory,
			// https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalMemoryHandleTypeFlagBits.html
			// Gimme an FD pointing to a DMA-BUF object
			.handleType = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
		});

		// Essentially a 'span' into our existing image, that lets us re-interpret it, and filter what we want from it
		vk::ImageViewCreateInfo image_view_info = {
			.image = image,
			// You could interpret a 3d image as a 2d image with array layers
			.viewType = vk::ImageViewType::e2D,
			// The format is allowed to differ, (ie. reading R8G8B8A8Unorm as R8G8B8A8Srgb), if the format is compatible
			.format = render.ultra_formats[0].vk_format,
			// The swizzle, ie. swapping colour channels
			.components = render.ultra_formats[0].vk_swizzed,
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
		auto image_view = render.device.createImageView(image_view_info);

		frames.push_back(VkFrame {
			.memory = std::move(image_memory),
			.image = std::move(image),
			.image_view = std::move(image_view),
			.drm_modifier = std::move(image_drm_modifier),
			.memory_planes_layouts = std::move(memory_planes_layouts),
			.dmabuf_fd = std::move(dmabuf_fd),
		});
	}

	return VkMonitor {
		.command = std::move(command),
		.frames = std::move(frames),
	};
}

Render Mayday::get_shit() {
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
		std::uint32_t acceptable_queue_family_index = std::numeric_limits<std::uint32_t>::max();
		auto queue_families = physical.getQueueFamilyProperties2();
		for (int i = 0; i < queue_families.size(); ++i) {
			auto flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
			if ((queue_families[i].queueFamilyProperties.queueFlags & flags) == flags) {
				acceptable_queue_family_index = i;
				break;
			}
		}
		if (acceptable_queue_family_index == std::numeric_limits<std::uint32_t>::max())
			continue;

		// Get the supported extensions. Extensions define new additions to the API surface
		auto available_extensions = physical.enumerateDeviceExtensionProperties();
		std::vector<const char*> required_extensions_check(std::begin(required_extensions), std::end(required_extensions));
		for (auto available_extension : available_extensions) {
			for (auto it = required_extensions_check.begin(); it < required_extensions_check.end(); ++it) {
				if (std::string_view(available_extension.extensionName) == *it) {
					required_extensions_check.erase(it);
					break;
				}
			}
			if (required_extensions_check.empty())
				break;
		}
		// The required extensions wasn't entirely drained, so not every ext is supported
		if (!required_extensions_check.empty())
			continue;

		// Get the supported features. Supported allows us to get the requested features values, as it fills it out
		// Features define the functionality of the existing API
		auto supported = physical.getFeatures2<
			vk::PhysicalDeviceFeatures2,
			vk::PhysicalDeviceVulkan12Features,
			vk::PhysicalDeviceVulkan13Features,
			vk::PhysicalDeviceVulkan14Features>();

		auto& supported_12 = supported.get<vk::PhysicalDeviceVulkan12Features>();
		auto& supported_13 = supported.get<vk::PhysicalDeviceVulkan13Features>();
		auto& supported_14 = supported.get<vk::PhysicalDeviceVulkan14Features>();

		// Allows for ImageMemoryBarrier2, SubmitInfo2, etc, just the 2ver for synchronisation stuff
		if (supported_13.synchronization2 == false ||
			// Allows for cmd_buffer.begin_rendering() and cmd_buffer.end_rendering() rather than vk::RenderPass
			supported_13.dynamicRendering == false ||
			// Timeline semaphores are submitted with an integer, and then go up to that integer once the work is done
			// You can submit multiple things with the same semaphore, and since queues are linear, you know if the integer
			// is the highest one you submitted, then the rest are also done. It's an alternative to fences
			supported_12.timelineSemaphore == false ||
			// QOL features, the main one we use is that we can pass the shader module info directly to the pipeline now,
			// and the pipeline will create the module. Without this, we would need to do
			// auto vert_shader_module = device.createShaderModule(vert_shader_info); and then pass that to the pipeline.
			// It's just a bit cleaner
			supported_14.maintenance5 == false ||
			// https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_host_image_copy.html#_promotion_to_vulkan_1_4
			// Lets us skip staging buffers, and upload images directly from CPU to GPU
			supported_14.hostImageCopy == false) {
			continue;
		}

		physical_device = std::move(physical);
		queue_family_index = acceptable_queue_family_index;
		break;
	}

	if (*physical_device == nullptr)
		MQ_XERROR("No suitable physical GPU device was found");

	// Create the logical device
	// Enable specified features
	vk::PhysicalDeviceVulkan14Features features_14 = {
		.maintenance5 = true,
		.hostImageCopy = true,
	};
	vk::PhysicalDeviceVulkan13Features features_13 = {
		.pNext = &features_14,
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
		.enabledExtensionCount = required_extensions.size(),
		.ppEnabledExtensionNames = required_extensions.data(),
	};
	auto device = physical_device.createDevice(device_info);
	auto queue = device.getQueue(queue_family_index, 0); // If we requested 5 to be made, we could get 0 through to 4

	// Get what DRM modifiers our GPU supports for each DRM format
	// https://docs.vulkan.org/refpages/latest/refpages/source/VkDrmFormatModifierPropertiesList2EXT.html
	// If DrmFormatModifierPropertiesList2's properties field is null, it fills in its count field for us when we call getFormatProperties2
	// We then allocate a vector based on that count, then give it to the properties data field, then call getFormatProperties2 again
	// and then it will fill in the data. I believe this pattern is done so vulkan never allocates for us, we must do allocations ourself
	std::vector<UltraFormat> ultra_formats;
	for (auto drm_format : supported_drm_formats) {
		auto vk_format = fourcc_to_vk(drm_format);
		auto format_properties = physical_device.getFormatProperties2<vk::FormatProperties2, vk::DrmFormatModifierPropertiesList2EXT>(vk_format.first);
		auto& drm_modifiers_wrapper = format_properties.get<vk::DrmFormatModifierPropertiesList2EXT>(); // A pointer to the storage, and the number of entries
		std::vector<vk::DrmFormatModifierProperties2EXT> drm_modifiers(drm_modifiers_wrapper.drmFormatModifierCount);
		drm_modifiers_wrapper.pDrmFormatModifierProperties = drm_modifiers.data(); // Actually give it the pointer
		physical_device.getFormatProperties2(vk_format.first, &format_properties.get<vk::FormatProperties2>());

		ultra_formats.push_back(UltraFormat {
			.drm_format = drm_format,
			.drm_modifiers = std::move(drm_modifiers),
			.vk_format = vk_format.first,
			.vk_swizzed = vk_format.second,
		});
	}

	// Create shaders
	auto vert_spirv = read_spirv("shader.vert");
	auto frag_spirv = read_spirv("shader.frag");

	vk::ShaderModuleCreateInfo vert_shader_info = {
		.codeSize = vert_spirv.size() * sizeof(std::uint32_t),
		.pCode = vert_spirv.data()};
	vk::ShaderModuleCreateInfo frag_shader_info = {
		.codeSize = frag_spirv.size() * sizeof(std::uint32_t),
		.pCode = frag_spirv.data()};

	// Generally just need vertex + fragment shader here, but we could have stuff like tesselation too (subdividing triangles)
	std::array shader_stages = {
		vk::PipelineShaderStageCreateInfo {
			// We enabled the maintainence5 feature, so we can put info in pNext here, and the pipeline
			// will create the module for us (hence why we *dont* set the module field here)
			.pNext = &vert_shader_info,
			.stage = vk::ShaderStageFlagBits::eVertex,
			.pName = "main", // name of the primary function entry point, so main()
		},
		vk::PipelineShaderStageCreateInfo {
			.pNext = &frag_shader_info,
			.stage = vk::ShaderStageFlagBits::eFragment,
			.pName = "main",
		},
	};

	// We have no vertex input, it's built into the shader
	vk::PipelineVertexInputStateCreateInfo vertex_input_info = {};
	// How to interpret the vertices
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_info = {
		// Every 3 verts is a separate triangle
		.topology = vk::PrimitiveTopology::eTriangleList,
	};

	// A viewport is essentially another "image view" into the image view, where we can specify what section of the image_view we want
	// We can have multiple viewports for one image view, and the shader can choose what image_view it renders into. That viewport will be
	// normalised to the regular -1 to 1 co-ordinate range. Scissors control the max bounds on the fragments to be rendered out, essentially,
	// on the x and y
	vk::PipelineViewportStateCreateInfo viewport_state_info = {
		.viewportCount = 1,
		.scissorCount = 1,
	};

	// We don't set  the exact scissor or viewport dimensions now, we do that at cmd buffer time
	constexpr std::array dynamic_states = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
	};

	// NOTE: The count is just the number of dynamic_states, so it can look through the array and
	// see we're using dynamic_x (ie. not setting it during pipeline creation). This is NOT the same
	// as viewport_state_info, like if we had another dynamic state, we'd add that to dynamic_states
	// For example, we could add if we want to cull back-faces (ie. inverted normals) or not as a dynamic property
	vk::PipelineDynamicStateCreateInfo dynamic_state_info = {
		.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size()),
		.pDynamicStates = dynamic_states.data(),
	};

	vk::PipelineMultisampleStateCreateInfo multisample_info = {
		// Only one sample (no msaa)
		.rasterizationSamples = vk::SampleCountFlagBits::e1,
	};

	// Runs on every fragment shader, when it outputs to the colour attachment
	vk::PipelineColorBlendAttachmentState colour_blend_attachment = {
		// Specify that it can write these colour channels (by default, it can't write any channels,
		// so it will do nothing)
		.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
	};

	// We may have multiple blend attachments, in the case we have multiple colour (or image generally) attachments
	// They will match up by index (eg blend[1] matches colour[1]). We only have one.
	vk::PipelineColorBlendStateCreateInfo colour_blend_info = {
		.attachmentCount = 1,
		.pAttachments = &colour_blend_attachment,
	};

	// This would usually specify what push constants / descriptor sets ranges, but we have none
	auto pipeline_layout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo {});

	// Specify the formats of the colour attachments we will add
	vk::PipelineRenderingCreateInfo pipeline_rendering_info = {
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &ultra_formats[0].vk_format,
	};

	// How to rasterize the traingles
	vk::PipelineRasterizationStateCreateInfo rasterisation_info = {
		// It defaults to filling the triangle (we could set it to draw the lines, or draw the vertices instead)
		// I've explicitly put it here to be obvious
		.polygonMode = vk::PolygonMode::eFill,
		// We don't draw lines, but the spec mandates it to be set to 1.0 if we don't enable the widelines feature
		// which contrary to its name, just means setting linewidth to a size other than 1.0
		// "wideLines specifies whether lines with width other than 1.0 are supported. If this feature is not enabled,
		// the lineWidth member of the VkPipelineRasterizationStateCreateInfo structure must be 1."
		.lineWidth = 1.0f,
	};

	vk::SemaphoreTypeCreateInfo semaphore_info = {
		.semaphoreType = vk::SemaphoreType::eTimeline,
		.initialValue = 0,
	};

	auto semaphore = device.createSemaphore(vk::SemaphoreCreateInfo {.pNext = &semaphore_info});

	// Create sampler + resource heaps
	auto create_heap_buffer = [&device, &physical_device](std::uint64_t size) -> HeapBuffer {
		// Buffer for descriptor heap
		// https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferCreateInfo.html
		// https://docs.vulkan.org/spec/latest/chapters/resources.html - What counts as a resource (basically, buffers, images)
		// basically, a resource is the underlying data. Samplers are not resources, hence their own heap since they aren't actually data in the same sense
		// of an image, but rather, just configuration more like what an image view is.
		auto buffer = device.createBuffer(vk::BufferCreateInfo {
			.size = size,
			.usage = vk::BufferUsageFlagBits::eDescriptorHeapEXT | vk::BufferUsageFlagBits::eShaderDeviceAddress, // 2nd one lets us get the address of the buffer, which we can then use directly in shaders
		});
		auto requirements = buffer.getMemoryRequirements();
		auto memory_type_index = get_memory_type_index(*physical_device, requirements.memoryTypeBits,
			// Host coherent means that it automatically flushes after we write on the CPU side
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eDeviceLocal);
		if (!memory_type_index.has_value())
			MQ_XERROR("No appropriate image memory found");

		vk::StructureChain<vk::MemoryAllocateInfo, vk::MemoryAllocateFlagsInfo> chain = {
			{
				.allocationSize = requirements.size,
				.memoryTypeIndex = *memory_type_index,
			},
			{
				// eDeviceAddress specifies this memory can be attached to a buffer created with the eShaderDeviceAddress usage bit
				.flags = vk::MemoryAllocateFlagBits::eDeviceAddress,
			},
		};

		auto memory = device.allocateMemory(chain.get<vk::MemoryAllocateInfo>());
		buffer.bindMemory(memory, 0);
		auto buffer_address = device.getBufferAddress(vk::BufferDeviceAddressInfo {.buffer = buffer});
		std::byte* cpu_address = static_cast<std::byte*>(memory.mapMemory(0, requirements.size)); // Map it into our process memory
																								  // Remember, when using this buffer, the lowest address is actually mapped + reserved_size, not just mapped

		return HeapBuffer {
			.memory = std::move(memory),
			.gpu_address = std::move(buffer_address),
			.cpu_address = std::move(cpu_address),
			.buffer = std::move(buffer),
		};
	};

	vk::PhysicalDeviceDescriptorHeapPropertiesEXT heap_properties;
	vk::PhysicalDeviceProperties2 physical_device_info = {.pNext = &heap_properties};
	physical_device.getProperties2(&physical_device_info);

	constexpr std::uint64_t max_images = 1024;
	// The driver requires a reserved memory range in each heap, for its own tracking of stuff. Hence, for whatever size we request, we just add that ReservedRange (i.e. the size it will steal from us)
	// 32 bytes is because each image is 4 vertices, and each vertex is 8 bytes (2 float32s), hence 32 bytes.
	// Also fyi, obviously we aren't actually writing the images into the heap, just a view of it into it (like a pointer, but image view).
	// A sampler is a descriptor and lives entirely in the heap. But an image is not a descriptor, a descriptor for an image tells the gpu how to access the image, hence only its descriptor lives in the heap
	vk::DeviceSize resource_heap_size = heap_properties.imageDescriptorSize * max_images + (32 * max_images) + heap_properties.minResourceHeapReservedRange;
	auto resource_heap = create_heap_buffer(resource_heap_size);
	resource_heap.size = resource_heap_size;
	vk::DeviceSize sampler_heap_size = heap_properties.samplerDescriptorSize * max_images + heap_properties.minSamplerHeapReservedRange;
	auto sampler_heap = create_heap_buffer(sampler_heap_size);
	sampler_heap.size = sampler_heap_size;

	vk::PipelineCreateFlags2CreateInfo flags_create_info = {
		.pNext = &pipeline_rendering_info,
		.flags = vk::PipelineCreateFlagBits2::eDescriptorHeapEXT,
	};

	/*  After running vkCmdDraw(image to render to), this is what the pipeline does (i.e. the pipeline stages), in order:
	 *
	 *  Input Assembler: Combines vertices inputted into primitives, such as triangles (or, strip triangles, etc)
	 *  Vertex Shader: Runs on every vertex, transforming its position.
	 *  (Optionally, tesselation shaders, etc run here)
	 *  Clip Volume: Clips primitives which fall outside of the region (new vertices can be generated, to 'cut' in half / clip it)
	 *  Viewport transformation: Map clip-space co-ordinates to the image space, ie. converts to actual pixel space (so -1, 1, becomes 1920x1080)
	 *  Rasterisation: Fragments are now produced (a fragment is basically a "potential pixel"). It also calculates depth of each fragment
	 *  Scissor test: Reject fragments outside of the scissor
	 *  Depth test: Reject fragments which cannot be seen
	 *  Fragment Shader: The fragment shader runs, and outputs a single vec4 per attached colour attachment
	 *  Colour Attachment Output stage: Applies blending of the fragments, and writes the result to the image.
	 */
	auto graphics_pipeline = device.createGraphicsPipeline(
		nullptr, // Optional cache to specify, otherwise shaders will need to be re-compiled, etc
		vk::GraphicsPipelineCreateInfo {
			.pNext = &flags_create_info,
			.stageCount = shader_stages.size(),
			.pStages = shader_stages.data(),
			.pVertexInputState = &vertex_input_info,
			.pInputAssemblyState = &input_assembly_info,
			.pViewportState = &viewport_state_info,
			.pRasterizationState = &rasterisation_info,
			.pMultisampleState = &multisample_info,
			.pColorBlendState = &colour_blend_info,
			.pDynamicState = &dynamic_state_info,
			.layout = pipeline_layout,
		});

	return {
		.context = std::move(context),
		.instance = std::move(instance),
		.physical_device = std::move(physical_device),
		.device = std::move(device),
		.queue_family_index = queue_family_index,
		.queue = std::move(queue),
		.graphics_pipeline = std::move(graphics_pipeline),
		.semaphore = std::move(semaphore),
		.ultra_formats = std::move(ultra_formats),
		.resource_heap = std::move(resource_heap),
		.sampler_heap = std::move(sampler_heap),
		.heap_properties = std::move(heap_properties),
	};
}

void Mayday::render_monitor(Monitor& monitor) {
	auto& frame = monitor.frames[monitor.current_frame];
	monitor.current_frame = (monitor.current_frame + 1) % monitor.frames.size();
	auto& command_buffer = monitor.command.buffers[0];

	// Write the images views
	std::uint32_t i = 0;
	for (auto& client : server.clients) {
		for (auto& object : client.get()->objects) {
			auto& interface = std::get<1>(object.second);
			using namespace mayquill;
			if (std::holds_alternative<WlSurface>(interface)) {
				auto& surface = std::get<WlSurface>(interface);
				auto& surface_data = gimme_data<WlSurfaceData>(surface);
				if (surface_data.buffer_friends.buffer && *surface_data.buffer_friends.buffer) {
					auto key = **surface_data.buffer_friends.buffer;
					auto& buffer_data = gimme_data<WlBufferData>(client->get_object<WlBuffer>(key));
					auto& inner = (*buffer_data.inner);

                    // TODO - Only allow surfaces that are on this monitor and buffer scale
                    float scale_width, scale_height, scale_x, scale_y;
                    auto geometry = *surface_data.geometry;
                    if (geometry.width == 0 && geometry.height == 0) {
                        scale_width = static_cast<float>(buffer_data.inner.width) / monitor.mode.hdisplay;
                        scale_height = static_cast<float>(buffer_data.inner.height) / monitor.mode.vdisplay;
                    } else {
                        scale_width = static_cast<float>(geometry.width) / monitor.mode.hdisplay;
                        scale_height = static_cast<float>(geometry.height) / monitor.mode.vdisplay;
                    }
                    scale_x = static_cast<float>(geometry.x) / monitor.mode.hdisplay;
                    scale_y = static_cast<float>(geometry.y) / monitor.mode.vdisplay;

                    auto* arbitrary = reinterpret_cast<ArbitraryDescriptor*>(render.resource_heap.cpu_address + (i * sizeof(ArbitraryDescriptor)));
                    *arbitrary = ArbitraryDescriptor {
                        .x = 0,
                        .y = 0,
                        .width = 1,
                        .height = 1,
                        .sampler_index = 1,
                    };

					vk::ImageViewCreateInfo view_info = {
						.image = inner.image,
						.viewType = vk::ImageViewType::e2D,
						.format = inner.ultra_format.vk_format,
						.components = inner.ultra_format.vk_swizzed,
						.subresourceRange = {
							.aspectMask = vk::ImageAspectFlagBits::eColor,
							.baseMipLevel = 0,
							.levelCount = 0,
							.baseArrayLayer = 0,
							.layerCount = 1,
						}};

					// https://vkdoc.net/man/VkImageDescriptorInfoEXT
					vk::ImageDescriptorInfoEXT image_descriptor_info = {
						.pView = &view_info,
						.layout = vk::ImageLayout::eShaderReadOnlyOptimal,
					};

					// https://vkdoc.net/man/VkResourceDescriptorInfoEXT
					vk::ResourceDescriptorInfoEXT resource_descriptor_info = {
						.type = vk::DescriptorType::eSampledImage,
						// https://vkdoc.net/man/VkResourceDescriptorDataEXT
						.data = vk::ResourceDescriptorDataEXT(&image_descriptor_info), // This is a union type
					};

					vk::HostAddressRangeEXT target = {
						.address = render.resource_heap.cpu_address + ((i + 1) * sizeof(ArbitraryDescriptor)) + (i * render.heap_properties.imageDescriptorSize),
						.size = render.heap_properties.imageDescriptorSize,
					};

					render.device.writeResourceDescriptorsEXT(std::array {resource_descriptor_info}, std::array {target});
					++i;
				}
			}
		}
	}

	// Command buffers are the 'unit of work' on the gpu. When we are recording stuff into it, we're mainly setting metadata,
	// only command buffers start in queue order between themselves, not stuff that was recorded within them.
	command_buffer.begin(vk::CommandBufferBeginInfo {
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
	});

	// https://youtu.be/GiKbGWI4M-Y?t=2046 https://www.khronos.org/blog/understanding-vulkan-synchronization. Essentially, memory barriers ensrue caches are flushed when relevant
	// the src stage mask says that for every buffer / command before this barrier, yield until it passes that stage. dst access mask means that for every buffer / command after this barrier
	// make it yield just before it starts the dst stage, then when the src stages are complete, let it continue.
	// Eg. src mask: fragment shader, dst mask: colour attachment; do not allow commands after this barrier to pass colour attachment stage, until commands before it have passed their fragment shader stages
	// It applies to all in flight buffers, before and after
	vk::ImageMemoryBarrier2 image_barrier = {
		.srcStageMask = vk::PipelineStageFlagBits2::eNone,
		.srcAccessMask = vk::AccessFlagBits2::eNone,
		.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
		.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
		.oldLayout = vk::ImageLayout::eUndefined,
		.newLayout = vk::ImageLayout::eAttachmentOptimal,
		// We aren't changing what queue family we're using
		.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
		.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
		.image = *frame.image,
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	// We didn't record anything before this, so the barrier is only ensuring we can write during the colour attachment output stage
	command_buffer.pipelineBarrier2(vk::DependencyInfo {
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &image_barrier,
	});

	// Colour output attachment info
	vk::RenderingAttachmentInfo colour_attachment_info = {
		.imageView = *frame.image_view,
		// What layout the image is in, at the colour attachment stage
		// It does not do the transition, the memory barrier did that
		.imageLayout = vk::ImageLayout::eAttachmentOptimal,
		// Clear the image (within the render area, we set just below) with the clear colour on load, before draw
		.loadOp = vk::AttachmentLoadOp::eClear,
		// Keep the contents after rendering is done (we could set it to eDontCare, if it doesn't need to be valid)
		.storeOp = vk::AttachmentStoreOp::eStore,
		.clearValue = vk::ClearColorValue {
			std::array {
				0.5f,
				0.5f,
				0.5f,
				0.1f,
			},
		},
	};

	vk::Extent2D extent = {
		.width = monitor.mode.hdisplay,
		.height = monitor.mode.vdisplay,
	};

	// What region of the image we'll be drawing to - it doesn't actually clip it for us, but it's a promise we
	// make, to not render outside of that area
	vk::RenderingInfo rendering_info = {
		.renderArea = {
			.offset = {
				.x = 0,
				.y = 0,
			},
			.extent = extent,
		},
		// What layers the shader has access to (shaders default to the first layer)
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colour_attachment_info,
	};
	// NDC conversion to framebuffer co-ordinates, ie. 0,0 NDC would be the centre, and 1,1 would be 1920x1080 on a 1080p monitor
	//  -1,-1           1,-1        0,0         1920,0
	//          0,0                      960,540
	//  -1, 1           1, 1        0,1080   1920,1080
	// Any vertices outside of NDC are clipped after we give gl_Position ((x, y, z, w), we clip to -w <= x <= w etc, then we divide through by w. Useful for perspective
	// projection, but we're doing orthographic so the division doesn't mean anything to us, hence our w is one. If we set it to like 0.5, and x was 0.5, it woul clip x to 0.5,
	// then 0.5/0.5 = 1, so scaling it back up to 1, it's a nice property of the division, if only partially clipped new vertices are made so it fits in NDC)
	// hence this satisfies the above render info promise (since our viewport size = render size, and NDC is just scaled up to viewport size)
	// This operates on primitives. Good video on homogenous coordinates: https://www.youtube.com/watch?v=o-xwmTODTUI
	command_buffer.setViewport(0,
		vk::Viewport {
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(monitor.mode.hdisplay),
			.height = static_cast<float>(monitor.mode.vdisplay),
			.minDepth = 1.0f,
			.maxDepth = 1.0f,
		});

	// scissors can allow us to discard fragments from the viewport, but we don't want to (eg. if we only wanted half of the viewport)
	// but vulkan requires a scissor to be set (atleast matching viewport count). This operates on fragments
	// (fragments are the size of one pixel, but multiple fragments can contribute to the same pixel, hence they aren't the same)
	command_buffer.setScissor(0,
		vk::Rect2D {
			.offset = {
				.x = 0,
				.y = 0,
			},
			.extent = extent,
		});

	// Unlike the name suggests, it just specifies the colour attachment - it doesn't actually 'render' into it here
	command_buffer.beginRendering(rendering_info);
	// This is a graphics pipeline, we could have stuff like compute pipelines instead
	command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, render.graphics_pipeline);

	// Bind heaps: https://docs.vulkan.org/refpages/latest/refpages/source/VkBindHeapInfoEXT.html
	command_buffer.bindResourceHeapEXT(vk::BindHeapInfoEXT {
		.heapRange = vk::DeviceAddressRangeEXT {
			.address = render.resource_heap.gpu_address,
			.size = render.resource_heap.size,
		},
		.reservedRangeOffset = 0,
		.reservedRangeSize = render.heap_properties.minResourceHeapReservedRange,
	});
	command_buffer.bindSamplerHeapEXT(vk::BindHeapInfoEXT {
		.heapRange = vk::DeviceAddressRangeEXT {
			.address = render.sampler_heap.gpu_address,
			.size = render.sampler_heap.size,
		},
		.reservedRangeOffset = 0,
		.reservedRangeSize = render.heap_properties.minSamplerHeapReservedRange,
	});

	command_buffer.draw(3, 1, 0, 0);

	command_buffer.endRendering();
	command_buffer.end();

	vk::CommandBufferSubmitInfo submit_info = {
		.commandBuffer = command_buffer,
	};

	render.queue.submit2(vk::SubmitInfo2 {
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &submit_info,
	});
}

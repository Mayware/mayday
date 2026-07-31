module;
#include <mayquill/logger.h>
module mayday;
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

// export void create_images(std::uint32_t width, std::uint32_t height) {
// }

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

		// Get the supported features. Supported allows us to get the requested features values, as it fills it out
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
			supported_14.maintenance5 == false) {
			continue;
		}

		physical_device = std::move(physical);
		queue_family_index = acceptable_queue_family_index;
		break;
	}

	if (*physical_device == nullptr)
		MQ_XERROR("No suitable physical GPU device was found");

	// Create the logical device, with our specified features
	vk::PhysicalDeviceVulkan14Features features_14 = {
		.maintenance5 = true,
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
	};
	auto device = physical_device.createDevice(device_info);
	auto queue = device.getQueue(queue_family_index, 0); // If we requested 5 to be made, we could get 0 through to 4

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
		.pColorAttachmentFormats = &image_format,
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
			.pNext = &pipeline_rendering_info, // It's in pnext, because this is a dynamic rendering extension
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

    vk::SemaphoreTypeCreateInfo semaphore_info = {
        .semaphoreType = vk::SemaphoreType::eTimeline,
        .initialValue = 0,
    };

    auto semaphore = device.createSemaphore(vk::SemaphoreCreateInfo { .pNext = &semaphore_info });

	return {
		.context = std::move(context),
		.instance = std::move(instance),
		.physical_device = std::move(physical_device),
		.device = std::move(device),
		.queue = std::move(queue),
		.graphics_pipeline = std::move(graphics_pipeline),
		.semaphore = std::move(semaphore),
	};
}



// export void start() {
// 	// Loads libvulkan (manually, via dlopen), and caches the function pointers to be used for instance creation
// 	// and other gloval entry points
// 	vk::raii::Context context = {};

// 	static constexpr vk::ApplicationInfo app_info = {
// 		.pApplicationName = "Mayday",
// 		.applicationVersion = vk::makeApiVersion(0, 1, 0, 0),
// 		.pEngineName = "The dark fountain",
// 		.engineVersion = vk::makeApiVersion(0, 1, 0, 0),
// 		.apiVersion = vk_version,
// 	};

// 	static constexpr vk::InstanceCreateInfo create_info = {
// 		.pApplicationInfo = &app_info,
// 		.enabledLayerCount = 0,
// 		.ppEnabledLayerNames = nullptr,
// 		.enabledExtensionCount = 0,
// 		.ppEnabledExtensionNames = nullptr,
// 	};

// 	auto instance = context.createInstance(create_info);

// 	vk::raii::PhysicalDevice physical_device = nullptr;
// 	std::uint32_t queue_family_index;
// 	// A physical device is an actual GPU / vulkan-capable device, that vulkan has access to
// 	// It becomes a logical device when we actually connect to it
// 	for (auto& physical : instance.enumeratePhysicalDevices()) {
// 		if (physical.getProperties().apiVersion < vk_version)
// 			continue;

// 		// Vulkan has the concept of `Queue Families`. Queue families is a group of queues that all have the same capabilites.
// 		// Queue families will advertise capbilities such as `graphics`, `compute`, `transfer, `video decode` etc, implying their supported operations.
// 		// Queue families will then have multiple queues within them, all supporting these capabilities.
// 		// In our case we'll just take one queue, that supports graphics, and transfer capabilities (transfer allowing for staging buffers and the like)
// 		std::uint32_t acceptable_queue_family_index = std::numeric_limits<std::uint32_t>::max();
// 		auto queue_families = physical.getQueueFamilyProperties2();
// 		for (int i = 0; i < queue_families.size(); ++i) {
// 			auto flags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
// 			if ((queue_families[i].queueFamilyProperties.queueFlags & flags) == flags) {
// 				acceptable_queue_family_index = i;
// 				break;
// 			}
// 		}
// 		if (acceptable_queue_family_index == std::numeric_limits<std::uint32_t>::max())
// 			continue;

// 		// Get the supported features. Supported allows us to get the requested features values, as it fills it out
// 		auto supported = physical.getFeatures2<
// 			vk::PhysicalDeviceFeatures2,
// 			vk::PhysicalDeviceVulkan12Features,
// 			vk::PhysicalDeviceVulkan13Features,
// 			vk::PhysicalDeviceVulkan14Features>();

// 		auto& supported_12 = supported.get<vk::PhysicalDeviceVulkan12Features>();
// 		auto& supported_13 = supported.get<vk::PhysicalDeviceVulkan13Features>();
// 		auto& supported_14 = supported.get<vk::PhysicalDeviceVulkan14Features>();

// 		// Allows for ImageMemoryBarrier2, SubmitInfo2, etc, just the 2ver for synchronisation stuff
// 		if (supported_13.synchronization2 == false ||
// 			// Allows for cmd_buffer.begin_rendering() and cmd_buffer.end_rendering() rather than vk::RenderPass
// 			supported_13.dynamicRendering == false ||
// 			// Timeline semaphores are submitted with an integer, and then go up to that integer once the work is done
// 			// You can submit multiple things with the same semaphore, and since queues are linear, you know if the integer
// 			// is the highest one you submitted, then the rest are also done. It's an alternative to fences
// 			supported_12.timelineSemaphore == false ||
// 			// QOL features, the main one we use is that we can pass the shader module info directly to the pipeline now,
// 			// and the pipeline will create the module. Without this, we would need to do
// 			// auto vert_shader_module = device.createShaderModule(vert_shader_info); and then pass that to the pipeline.
// 			// It's just a bit cleaner
// 			supported_14.maintenance5 == false) {
// 			continue;
// 		}

// 		physical_device = std::move(physical);
// 		queue_family_index = acceptable_queue_family_index;
// 		break;
// 	}

// 	if (*physical_device == nullptr)
// 		MQ_XERROR("No suitable physical GPU device was found");

// 	// Create the logical device, with our specified features
// 	vk::PhysicalDeviceVulkan14Features features_14 = {
// 		.maintenance5 = true,
// 	};
// 	vk::PhysicalDeviceVulkan13Features features_13 = {
// 		.pNext = &features_14,
// 		.synchronization2 = true,
// 		.dynamicRendering = true,
// 	};
// 	vk::PhysicalDeviceVulkan12Features features_12 = {
// 		.pNext = &features_13,
// 		.timelineSemaphore = true,
// 	};
// 	// Get the actual queue
// 	// queue_prorities is a hint to the GPU of which queues should be favoured, if it becomes overloaded
// 	// If we were using multiple queues, we could assign one 1.0f, and the other 0.2f. In the event that
// 	// the GPU has to ration workpower, it will favour the workload of the queues with higher priorities.
// 	float queue_priorities[1] = {1.0f};
// 	vk::DeviceQueueCreateInfo queue_info = {
// 		.queueFamilyIndex = queue_family_index,
// 		.queueCount = 1,
// 		.pQueuePriorities = queue_priorities,
// 	};

// 	vk::DeviceCreateInfo device_info = {
// 		.pNext = &features_12,
// 		.queueCreateInfoCount = 1,
// 		.pQueueCreateInfos = &queue_info,
// 	};
// 	auto device = physical_device.createDevice(device_info);
// 	auto queue = device.getQueue(queue_family_index, 0); // If we requested 5 to be made, we could get 0 through to 4

// 	vk::ImageCreateInfo image_info = {
// 		.imageType = vk::ImageType::e2D, // e just means enum
// 		.format = image_format,
// 		.extent = {
// 			.width = 1920,
// 			.height = 1080,
// 			// We can specify a 3d image by setting depth to other than 1. What this means is that the image has a z axis,
// 			// interpreted exactly the same as X and Y. So in multi-sampling, it'll also apply across the Z-axis. Mip-maps
// 			// will also shrink the Z axis
// 			.depth = 1,
// 		},
// 		// How many mipmaps we will have in the future, stored in this image (they aren't autogenerated, we'll need to add them if we want them)
// 		// 2 would mean we will add a half sized image too, 3 would mean that, and a quarter sized one, etc.
// 		.mipLevels = 1,
// 		// How many of these images do we want. Eg. 3 would mean we have 3 of these 2d images stored in here. This is different to depth
// 		// because these images are separate from eachother, the z co-ordinate would be an integer not float, sampling wouldn't go across the z, etc.
// 		.arrayLayers = 1,
// 		// Only 1 sampling. A triangle could only go partially across a pixel, for example, through a pixel diagonal. With 1 sample, we'd only test the
// 		// middle and that would determine if it is fully coloured as that or not. If we had 4 samples, we'd test every quarter middle, and then only add
// 		// some colour depending on how many hits. Essentially, adds transparency, based on how many samples we miss.
// 		.samples = vk::SampleCountFlagBits::e1,
// 		// We know longer know what format the GPU is choosing to store this image in memory with. Eg. with RGBA we'd normally assume from the CPU that
// 		// we could traverse it literally byte by byte to get pixels, but with this the GPU can choose whatever way it prefers to store it (eg. interlaced,
// 		// or not consecutively), meaning we can't really assume the memory locations from the CPU any longer.
// 		.tiling = vk::ImageTiling::eOptimal,
// 		// We can use this as a colour attachment (for fragment shaders to render into) and as a transfer source into another image
// 		.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
// 		// Only one queue family will operate on this image, because we only have one queue lmao. We could set to concurrent if more than one queue
// 		// could use this image
// 		.sharingMode = vk::SharingMode::eExclusive,
// 		// Specifies what content is in this image memory, when we are going to transition
// 		.initialLayout = vk::ImageLayout::eUndefined,
// 	};
// 	vk::raii::DeviceMemory image_memory = nullptr; // Memory declared early, so it is dropped after the image
// 	vk::raii::Image image = device.createImage(image_info);

// 	auto memory_requirements = image.getMemoryRequirements();
// 	auto memory_properties = physical_device.getMemoryProperties();
// 	std::uint32_t memory_type_index = std::numeric_limits<std::uint32_t>::max();
// 	for (int i = 0; i < memory_properties.memoryTypeCount; ++i) {
// 		auto property_flag = vk::MemoryPropertyFlagBits::eDeviceLocal;

// 		// memoryTypeBits is a bitmask representing memory_properties.memoryTypes indices
// 		// eg 1011 means the 1st, 3rd, and 4th memoryTypes are compatible.
// 		if ((memory_requirements.memoryTypeBits & (1u << i)) != 0 &&
// 			// Ensure the memory is on the GPU (device local), by checking the property_flags on the memoryType
// 			(memory_properties.memoryTypes[i].propertyFlags & property_flag) == property_flag) {
// 			memory_type_index = i;
// 			break;
// 		}
// 	}

// 	if (memory_type_index == std::numeric_limits<std::uint32_t>::max())
// 		MQ_XERROR("No appropriate image memory found");

// 	vk::MemoryAllocateInfo allocation_info = {
// 		.allocationSize = memory_requirements.size,
// 		.memoryTypeIndex = memory_type_index,
// 	};
// 	image_memory = device.allocateMemory(allocation_info);
// 	image.bindMemory(*image_memory, 0);

// 	// Essentially a 'span' into our existing image, that lets us re-interpret it, and filter what we want from it
// 	vk::ImageViewCreateInfo image_view_info = {
// 		.image = image,
// 		// You could interpret a 3d image as a 2d image with array layers
// 		.viewType = vk::ImageViewType::e2D,
// 		// The format is allowed to differ, (ie. reading R8G8B8A8Unorm as R8G8B8A8Srgb), if the format is compatible
// 		.format = image_format,

// 		.subresourceRange = {
// 			// If the image stores multiple data in one (eg.interleaved depth/stencil), we can pick just the data we want
// 			// In our case, all our data is colour
// 			.aspectMask = vk::ImageAspectFlagBits::eColor,
// 			// MIP level 0 is the regular full-res image, so we choose mip level range from 0 to 1 (exclusive), so just 0
// 			.baseMipLevel = 0,
// 			.levelCount = 1,
// 			// Same with array layers
// 			.baseArrayLayer = 0,
// 			.layerCount = 1,
// 		},
// 	};
// 	auto image_view = device.createImageView(image_view_info);

// 	// Create shaders
// 	auto vert_spirv = read_spirv("shader.vert");
// 	auto frag_spirv = read_spirv("shader.frag");

// 	vk::ShaderModuleCreateInfo vert_shader_info = {
// 		.codeSize = vert_spirv.size() * sizeof(std::uint32_t),
// 		.pCode = vert_spirv.data()};
// 	vk::ShaderModuleCreateInfo frag_shader_info = {
// 		.codeSize = frag_spirv.size() * sizeof(std::uint32_t),
// 		.pCode = frag_spirv.data()};

// 	// Generally just need vertex + fragment shader here, but we could have stuff like tesselation too (subdividing triangles)
// 	std::array shader_stages = {
// 		vk::PipelineShaderStageCreateInfo {
// 			// We enabled the maintainence5 feature, so we can put info in pNext here, and the pipeline
// 			// will create the module for us (hence why we *dont* set the module field here)
// 			.pNext = &vert_shader_info,
// 			.stage = vk::ShaderStageFlagBits::eVertex,
// 			.pName = "main", // name of the primary function entry point, so main()
// 		},
// 		vk::PipelineShaderStageCreateInfo {
// 			.pNext = &frag_shader_info,
// 			.stage = vk::ShaderStageFlagBits::eFragment,
// 			.pName = "main",
// 		},
// 	};

// 	// We have no vertex input, it's built into the shader
// 	vk::PipelineVertexInputStateCreateInfo vertex_input_info = {};
// 	// How to interpret the vertices
// 	vk::PipelineInputAssemblyStateCreateInfo input_assembly_info = {
// 		// Every 3 verts is a separate triangle
// 		.topology = vk::PrimitiveTopology::eTriangleList,
// 	};

// 	// A viewport is essentially another "image view" into the image view, where we can specify what section of the image_view we want
// 	// We can have multiple viewports for one image view, and the shader can choose what image_view it renders into. That viewport will be
// 	// normalised to the regular -1 to 1 co-ordinate range. Scissors control the max bounds on the fragments to be rendered out, essentially,
// 	// on the x and y
// 	vk::PipelineViewportStateCreateInfo viewport_state_info = {
// 		.viewportCount = 1,
// 		.scissorCount = 1,
// 	};

// 	// We don't set  the exact scissor or viewport dimensions now, we do that at cmd buffer time
// 	constexpr std::array dynamic_states = {
// 		vk::DynamicState::eViewport,
// 		vk::DynamicState::eScissor,
// 	};

// 	// NOTE: The count is just the number of dynamic_states, so it can look through the array and
// 	// see we're using dynamic_x (ie. not setting it during pipeline creation). This is NOT the same
// 	// as viewport_state_info, like if we had another dynamic state, we'd add that to dynamic_states
// 	// For example, we could add if we want to cull back-faces (ie. inverted normals) or not as a dynamic property
// 	vk::PipelineDynamicStateCreateInfo dynamic_state_info = {
// 		.dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size()),
// 		.pDynamicStates = dynamic_states.data(),
// 	};

// 	vk::PipelineMultisampleStateCreateInfo multisample_info = {
// 		// Only one sample (no msaa)
// 		.rasterizationSamples = vk::SampleCountFlagBits::e1,
// 	};

// 	// Runs on every fragment shader, when it outputs to the colour attachment
// 	vk::PipelineColorBlendAttachmentState colour_blend_attachment = {
// 		// Specify that it can write these colour channels (by default, it can't write any channels,
// 		// so it will do nothing)
// 		.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
// 	};

// 	// We may have multiple blend attachments, in the case we have multiple colour (or image generally) attachments
// 	// They will match up by index (eg blend[1] matches colour[1]). We only have one.
// 	vk::PipelineColorBlendStateCreateInfo colour_blend_info = {
// 		.attachmentCount = 1,
// 		.pAttachments = &colour_blend_attachment,
// 	};

// 	// This would usually specify what push constants / descriptor sets ranges, but we have none
// 	auto pipeline_layout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo {});

// 	// Specify the formats of the colour attachments we will add
// 	vk::PipelineRenderingCreateInfo pipeline_rendering_info = {
// 		.colorAttachmentCount = 1,
// 		.pColorAttachmentFormats = &image_format,
// 	};

// 	// How to rasterize the traingles
// 	vk::PipelineRasterizationStateCreateInfo rasterisation_info = {
// 		// It defaults to filling the triangle (we could set it to draw the lines, or draw the vertices instead)
// 		// I've explicitly put it here to be obvious
// 		.polygonMode = vk::PolygonMode::eFill,
// 		// We don't draw lines, but the spec mandates it to be set to 1.0 if we don't enable the widelines feature
// 		// which contrary to its name, just means setting linewidth to a size other than 1.0
// 		// "wideLines specifies whether lines with width other than 1.0 are supported. If this feature is not enabled,
// 		// the lineWidth member of the VkPipelineRasterizationStateCreateInfo structure must be 1."
// 		.lineWidth = 1.0f,
// 	};

// 	/*  After running vkCmdDraw(image to render to), this is what the pipeline does (i.e. the pipeline stages), in order:
// 	 *
// 	 *  Input Assembler: Combines vertices inputted into primitives, such as triangles (or, strip triangles, etc)
// 	 *  Vertex Shader: Runs on every vertex, transforming its position.
// 	 *  (Optionally, tesselation shaders, etc run here)
// 	 *  Clip Volume: Clips primitives which fall outside of the region (new vertices can be generated, to 'cut' in half / clip it)
// 	 *  Viewport transformation: Map clip-space co-ordinates to the image space, ie. converts to actual pixel space (so -1, 1, becomes 1920x1080)
// 	 *  Rasterisation: Fragments are now produced (a fragment is basically a "potential pixel"). It also calculates depth of each fragment
// 	 *  Scissor test: Reject fragments outside of the scissor
// 	 *  Depth test: Reject fragments which cannot be seen
// 	 *  Fragment Shader: The fragment shader runs, and outputs a single vec4 per attached colour attachment
// 	 *  Colour Attachment Output stage: Applies blending of the fragments, and writes the result to the image.
// 	 */
// 	auto graphics_pipeline = device.createGraphicsPipeline(
// 		nullptr, // Optional cache to specify, otherwise shaders will need to be re-compiled, etc
// 		vk::GraphicsPipelineCreateInfo {
// 			.pNext = &pipeline_rendering_info, // It's in pnext, because this is a dynamic rendering extension
// 			.stageCount = shader_stages.size(),
// 			.pStages = shader_stages.data(),
// 			.pVertexInputState = &vertex_input_info,
// 			.pInputAssemblyState = &input_assembly_info,
// 			.pViewportState = &viewport_state_info,
// 			.pRasterizationState = &rasterisation_info,
// 			.pMultisampleState = &multisample_info,
// 			.pColorBlendState = &colour_blend_info,
// 			.pDynamicState = &dynamic_state_info,
// 			.layout = pipeline_layout,
// 		});

// 	// Command buffers
// 	vk::CommandPoolCreateInfo command_pool_info = {
// 		// the buffers won't live for a long time, hint to the driver
// 		.flags = vk::CommandPoolCreateFlagBits::eTransient,
// 		.queueFamilyIndex = queue_family_index,
// 	};
// 	auto command_pool = device.createCommandPool(command_pool_info);
// 	vk::CommandBufferAllocateInfo command_buffer_allocate_info = {
// 		.commandPool = *command_pool,
// 		// Primary buffers can be submitted directly to the queue
// 		// the other option is a secondary buffer, which is in turn submitted to the primary buffer
// 		.level = vk::CommandBufferLevel::ePrimary,
// 		.commandBufferCount = 1,
// 	};
// 	auto command_buffers = vk::raii::CommandBuffers(device, command_buffer_allocate_info);
// 	auto command_buffer = std::move(command_buffers.front());

// 	// Command buffers are the 'unit of work' on the gpu. When we are recording stuff into it, we're mainly setting metadata,
// 	// only command buffers start in queue order between themselves, not stuff that was recorded within them.
// 	command_buffer.begin(vk::CommandBufferBeginInfo {
// 		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
// 	});

// 	// There is no point of me explaining what a memory barrier is, I would do worse, than how elegantly this video does: https://youtu.be/GiKbGWI4M-Y?t=2046
// 	// Trust me, watch it
// 	// The layout just means the previous layout in memory we had, and the new layout means the new one we hint for it to take. This is because, the GPU
// 	// may have one layout more efficient for reading, another for writing etc. The subresource range just specifies what miplevels/arraylayers we're transitioning
// 	// in the image - the entire image needn't be transitioned.
// 	vk::ImageMemoryBarrier2 image_barrier = {
// 		.srcStageMask = vk::PipelineStageFlagBits2::eNone,
// 		.srcAccessMask = vk::AccessFlagBits2::eNone,
// 		.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
// 		.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
// 		.oldLayout = vk::ImageLayout::eUndefined,
// 		.newLayout = vk::ImageLayout::eAttachmentOptimal,
// 		.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
// 		.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
// 		.image = *image,
// 		.subresourceRange = {
// 			.aspectMask = vk::ImageAspectFlagBits::eColor,
// 			.baseMipLevel = 0,
// 			.levelCount = 1,
// 			.baseArrayLayer = 0,
// 			.layerCount = 1,
// 		},
// 	};

// 	// We didn't record anything before this, so it's not actually really doing anything other than ensuring we can write during the colour attachment output stage
// 	command_buffer.pipelineBarrier2(vk::DependencyInfo {
// 		.imageMemoryBarrierCount = 1,
// 		.pImageMemoryBarriers = &image_barrier,
// 	});

// 	// Colour output attachment info
// 	vk::RenderingAttachmentInfo colour_attachment_info = {
// 		.imageView = *image_view,
// 		// What layout the image is in, at the colour attachment stage
// 		// It does not do the transition, the memory barrier did that
// 		.imageLayout = vk::ImageLayout::eAttachmentOptimal,
// 		// Clear the image (within the render area, we set just below) with the clear colour on load, before draw
// 		.loadOp = vk::AttachmentLoadOp::eClear,
// 		// Keep the contents after rendering is done (we could set it to eDontCare, if it doesn't need to be valid)
// 		.storeOp = vk::AttachmentStoreOp::eStore,
// 		.clearValue = vk::ClearColorValue {
// 			std::array {
// 				0.5f,
// 				0.5f,
// 				0.5f,
// 				0.1f,
// 			}}};

// 	vk::Extent2D extent = {
// 		.width = 1080, .height = 1080};

// 	vk::RenderingInfo rendering_info = {
// 		.renderArea = {.offset = {
// 						   .x = 0,
// 						   .y = 0,
// 					   },
// 			.extent = extent},
// 		// What layers the shader has access to (shaders default to the first layer)
// 		.layerCount = 1,
// 		.colorAttachmentCount = 1,
// 		.pColorAttachments = &colour_attachment_info,
// 	};

// 	// Unlike the name suggests, it just specifies the colour attachment - it doesn't actually 'render' into it here
// 	command_buffer.beginRendering(rendering_info);
// 	// This is a graphics pipeline, we could have stuff like compute pipelines instead
// 	command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphics_pipeline);

// 	command_buffer.setViewport(0,
// 		vk::Viewport {
// 			.x = 0.0f,
// 			.y = 0.0f,
// 			.width = 1080.0f,
// 			.height = 1080.0f,
// 			.minDepth = 1.0f,
// 			.maxDepth = 1.0f,
// 		});

// 	command_buffer.setScissor(0,
// 		vk::Rect2D {
// 			.offset = {
// 				.x = 0,
// 				.y = 0,
// 			},
// 			.extent = extent});

// 	command_buffer.draw(3, 1, 0, 0);

// 	command_buffer.endRendering();
// 	command_buffer.end();

// 	vk::CommandBufferSubmitInfo submit_info = {
// 		.commandBuffer = command_buffer,
// 	};

// 	queue.submit2(vk::SubmitInfo2 {
// 		.commandBufferInfoCount = 1,
// 		.pCommandBufferInfos = &submit_info,
// 	});

// 	queue.waitIdle();
// }

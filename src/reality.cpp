module mayday.reality;
import vulkan;

Command Reality::beg_pool(std::uint32_t buffer_count) {
	// First field is the index, 2nd is the buffer count
	std::pair<std::uint32_t, std::uint32_t> viable_larger = {};
	std::pair<std::uint32_t, std::uint32_t> viable_smaller = {};
	for (int i = 0; i < render.free_pools.size();) {
		auto& command = render.free_pools[i];
		if (command.buffers.size() == buffer_count) {
			auto moved = std::move(command);
			render.free_pools.erase(render.free_pools.begin() + i);
			return std::move(moved);
		}
		if (command.buffers.size() > buffer_count && command.buffers.size() < viable_larger.second) {
			viable_larger.first = i;
			viable_larger.second = command.buffers.size();
			continue;
		}
		if (command.buffers.size() < buffer_count && command.buffers.size() > viable_smaller.second) {
			viable_smaller.first = i;
			viable_larger.second = command.buffers.size();
			continue;
		}
	}

	// Shrink the viable_larger if it exists
	if (viable_larger.first) {
		auto moved = std::move(render.free_pools[viable_larger.first]);
		render.free_pools.erase(render.free_pools.begin() + buffer_count, render.free_pools.end());
		return std::move(moved);
	}
	// Grow the viable_larger if it exists
	if (viable_smaller.first) {
		auto moved = std::move(render.free_pools[viable_smaller.first]);
		vk::CommandBufferAllocateInfo command_buffer_allocate_info = {
			.commandPool = *moved.pool,
			.level = vk::CommandBufferLevel::ePrimary,
			.commandBufferCount = buffer_count - viable_smaller.second,
		};
		auto command_buffers = render.device.allocateCommandBuffers(command_buffer_allocate_info);
		moved.buffers.insert(moved.buffers.end(),
			std::make_move_iterator(command_buffers.begin()),
			std::make_move_iterator(command_buffers.end()));
		return std::move(moved);
	}

	// No free command pool exists, we'll have to make a bespoke
	vk::CommandPoolCreateInfo command_pool_info = {
		// the buffers won't live for a long time, hint to the driver
		.flags = vk::CommandPoolCreateFlagBits::eTransient,
		// Buffers can be submitted to any queue in the same family
		.queueFamilyIndex = render.queue_family_index,
	};
	auto command_pool = render.device.createCommandPool(command_pool_info);
	vk::CommandBufferAllocateInfo command_buffer_allocate_info = {
		.commandPool = *command_pool,
		// Primary buffers can be submitted directly to the queue
		// the other option is a secondary buffer, which is in turn submitted to the primary buffer
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = buffer_count,
	};
	auto command_buffers = render.device.allocateCommandBuffers(command_buffer_allocate_info);
	return Command {
		.pool = std::move(command_pool),
		.buffers = std::move(command_buffers),
	};
}
void Reality::donate_pool(Command&& command) {
	render.free_pools.push_back(std::move(command));
}

// base_requirements is the memoryTypeBits you get from the image requirements. These are the base requirements for the image
// extended requirements are the additional memory property flags you want. These aren't native to the image (ie. the image doesn't care about device local memory), they're additional reqs you want
std::optional<std::uint32_t> Reality::get_memory_type_index(vk::PhysicalDevice physical_device, std::uint32_t base_requirements, vk::MemoryPropertyFlags extended_requirements) {
	auto memory_properties = physical_device.getMemoryProperties();
	for (int i = 0; i < memory_properties.memoryTypeCount; ++i) {
		// memoryTypeBits (base_requirements) is a bitmask representing memory_properties.memoryTypes indices
		// eg 1011 means the 1st, 3rd, and 4th memoryTypes are compatible.
		if ((base_requirements & (1u << i)) != 0 &&
			// Ensure all property flags (like being device local) are satisfied
			(memory_properties.memoryTypes[i].propertyFlags & extended_requirements) == extended_requirements) {
            return i;
		}
	}
    return std::nullopt;
}

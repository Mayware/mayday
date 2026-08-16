module mayquill:buffers;
import std;
import vulkan;
import mayday.reality;

namespace mayquill {
struct WlBufferDataInner {
	std::vector<int> plane_fds;
	std::vector<vk::raii::DeviceMemory> memories;
	vk::raii::Image image;
    UltraFormat ultra_format;
};

struct Shm {
	std::mutex lock;
	// Fills in WlBufferData inner after it's done, and also drops the mutex lock
	std::function<WlBufferDataInner()> kicker;
	std::jthread uploader; // Kept, solely so the destructor yields for the thread

	Shm(std::function<WlBufferDataInner()> kicker) : kicker(std::move(kicker)) {}
};

struct Dmabuf {
	// Set to whatever semaphore value needed to ensure the layout transition is done
	// (shm does its layout transition via the CPU on the thread, hence it doesn't need any queue work)
	std::uint64_t semaphore_value;
	// Command pool + buffer we got just for the upload, must return after
	std::optional<Command> command;
};

class WlBufferData {
  public:
	std::optional<Shm> shm;
	std::optional<Dmabuf> dmabuf;
	std::optional<WlBufferDataInner> inner;

	// Constructor for shm (ie. threaded)
	WlBufferData(std::function<WlBufferDataInner()> kicker) : shm(std::move(kicker)) {}

	// Constructor for immediate dmabuf
	WlBufferData(WlBufferDataInner inner) : inner(std::move(inner)) {}

	Dmabuf start_dmabuf_layout_transition(Reality& reality) {
		vk::ImageMemoryBarrier2 image_barrier = {
			.srcStageMask = vk::PipelineStageFlagBits2::eNone,
			.srcAccessMask = vk::AccessFlagBits2::eNone,
			.dstStageMask = vk::PipelineStageFlagBits2::eNone,
			.dstAccessMask = vk::AccessFlagBits2::eNone,
			.oldLayout = vk::ImageLayout::eUndefined,
			.newLayout = vk::ImageLayout::eAttachmentOptimal,
			// We need to tell vulkan we own the queue family now, foreign means we can't provide the original because it ain't ours
			.srcQueueFamilyIndex = vk::QueueFamilyForeignEXT,
			.dstQueueFamilyIndex = reality.render.queue_family_index,
			.image = (*inner).image,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		auto command = reality.beg_pool(1);
		auto& command_buffer = command.buffers.front();
		command_buffer.begin(vk::CommandBufferBeginInfo {
			.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
		});

		// This actually records into the command buffer
		command_buffer.pipelineBarrier2(vk::DependencyInfo {
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &image_barrier,
		});

		command_buffer.end();

		vk::CommandBufferSubmitInfo submit_info = {
			.commandBuffer = command_buffer,
		};

		auto semaphore_value = reality.increment_semaphore();
		vk::SemaphoreSubmitInfo signal_info = {
			.semaphore = reality.render.semaphore,
			.value = semaphore_value,
			.stageMask = vk::PipelineStageFlagBits2::eAllCommands,
		};

		reality.render.queue.submit2(vk::SubmitInfo2 {
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &submit_info,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos = &signal_info,
		});

        return Dmabuf {
            .semaphore_value = semaphore_value,
            .command = std::move(command),
        };
	}
};
} // namespace mayquill

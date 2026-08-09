module;
#include <drm_fourcc.h>
#include <mayday/macros.h>
#include <mayquill/logger.h>
#include <sys/mman.h>
#include <unistd.h>
module mayquill;
import :buffers;
import :surfaces;
import mayday.util;
import vulkan;
import mayday.reality;

namespace mayquill {
// Also owned by all the shm buffers that are from that shm pool. Upon the last shm buffer release, this is then destroyed,
// it isn't destroyed alongside the shm pool. Make sure to not accidentally drop this, eg. via copy construction
struct WlShmPoolData {
	int fd;
	std::byte* start;
	std::int32_t size;

	~WlShmPoolData() {
		close(fd);
		munmap(start, size);
	}
};

void WlShm::handle(Request request) {
	std::visit(overload {
				   [this](CreatePool& request) {
					   auto start = static_cast<std::byte*>(mmap(nullptr, request.size, PROT_READ | PROT_WRITE, MAP_SHARED, request.fd, 0));
					   if (start == MAP_FAILED) {
						   close(request.fd);
						   client.error(keyd.id, WlShm::ErrorEnum::InvalidFd, combo_errno("Failed to mmap"));
					   }
					   client.add_object<WlShmPool>(request.id, // The user data is a shared ptr
						   std::make_unique<std::shared_ptr<WlShmPoolData>>(std::make_shared<WlShmPoolData>(request.fd, start, request.size)));
				   },
				   [this](Release& request) {}},
		request);
}

void WlShmPool::handle(Request request) {
	auto& pool_data = gimme_data<std::shared_ptr<WlShmPoolData>>(user_data);
	std::visit(overload {
				   [this, &pool_data](CreateBuffer& request) {
					   auto create_shm = [this, request, &pool_data]() -> WlBufferDataInner {
						   std::uint32_t drm_format;
						   // https://wayland.app/protocols/wayland#wl_shm:enum:format Thank you to the beautiful, magestic, wayland specification
						   // for allowing me the privilege of adding these exceptional cases.
						   switch (request.format) {
						   case WlShm::FormatEnum::Argb8888:
							   drm_format = DRM_FORMAT_ARGB8888;
							   break;
						   case WlShm::FormatEnum::Xrgb8888:
							   drm_format = DRM_FORMAT_XRGB8888;
							   break;
						   default:
							   drm_format = static_cast<std::uint32_t>(request.format);
							   break;
						   }
						   auto& reality = gimme_reality(client);
						   auto it = std::ranges::find_if(reality.render.ultra_formats, [drm_format](const UltraFormat& format) { return format.drm_format == drm_format; });
						   if (it == reality.render.ultra_formats.end())
							   MQ_XERROR("Unable to find matching ultra format for drm format");
						   auto& ultra_format = *it;

						   // Skipping staging buffer via https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_host_image_copy.html#_promotion_to_vulkan_1_4
						   auto image = reality.render.device.createImage(vk::ImageCreateInfo {
							   .imageType = vk::ImageType::e2D,
							   .format = ultra_format.vk_format,
							   .extent = {
								   .width = static_cast<std::uint32_t>(request.width),
								   .height = static_cast<std::uint32_t>(request.height),
								   .depth = 1,
							   },
							   .mipLevels = 1,
							   .arrayLayers = 1,
							   .samples = vk::SampleCountFlagBits::e1,
							   .tiling = vk::ImageTiling::eOptimal,
							   .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eHostTransfer,
							   .sharingMode = vk::SharingMode::eExclusive,
							   .initialLayout = vk::ImageLayout::eUndefined,
						   });
						   auto requirements = image.getMemoryRequirements();
						   auto memory_type_index = reality.get_memory_type_index(*reality.render.physical_device, requirements, vk::MemoryPropertyFlagBits::eDeviceLocal);
						   if (!memory_type_index.has_value())
							   MQ_XERROR("No appropriate image memory found");
						   vk::MemoryAllocateInfo allocation_info = {
							   .allocationSize = requirements.size,
							   .memoryTypeIndex = *memory_type_index,
						   };
						   auto image_memory = reality.render.device.allocateMemory(allocation_info);
						   image.bindMemory(image_memory, 0);

						   reality.render.device.transitionImageLayout(vk::HostImageLayoutTransitionInfo {
							   .image = image,
							   .oldLayout = vk::ImageLayout::eUndefined,
							   .newLayout = vk::ImageLayout::eTransferDstOptimal,
							   .subresourceRange = {
								   .aspectMask = vk::ImageAspectFlagBits::eColor,
								   .baseMipLevel = 0,
								   .levelCount = 1,
								   .baseArrayLayer = 0,
								   .layerCount = 1,
							   },
						   });

						   // https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryToImageCopy.html
						   vk::MemoryToImageCopy region = {
							   .pHostPointer = pool_data->start + request.offset,
							   .memoryRowLength = static_cast<std::uint32_t>(request.stride) / vk::blockSize(ultra_format.vk_format), // Length in texels, not total texel byte siez

							   // The part of the image we're writing to
							   .imageSubresource = {
								   .aspectMask = vk::ImageAspectFlagBits::eColor,
								   .mipLevel = 0,
								   .baseArrayLayer = 0,
								   .layerCount = 1,

							   },
							   .imageExtent = {
								   .width = static_cast<std::uint32_t>(request.width),
								   .height = static_cast<std::uint32_t>(request.height),
								   .depth = 1,
							   }};

						   // https://docs.vulkan.org/refpages/latest/refpages/source/VkCopyMemoryToImageInfo.html
						   vk::CopyMemoryToImageInfo copy_info = {
							   .dstImage = image,
							   .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
							   .regionCount = 1,
							   .pRegions = &region,
						   };

						   reality.render.device.copyMemoryToImage(copy_info);

						   reality.render.device.transitionImageLayout(vk::HostImageLayoutTransitionInfo {
							   .image = image,
							   .oldLayout = vk::ImageLayout::eTransferDstOptimal,
							   .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
							   .subresourceRange = {
								   .aspectMask = vk::ImageAspectFlagBits::eColor,
								   .baseMipLevel = 0,
								   .levelCount = 1,
								   .baseArrayLayer = 0,
								   .layerCount = 1,
							   },
						   });

						   // Essentially a 'span' into our existing image, that lets us re-interpret it, and filter what we want from it
						   auto image_view = reality.render.device.createImageView(vk::ImageViewCreateInfo {
							   .image = image,
							   // You could interpret a 3d image as a 2d image with array layers
							   .viewType = vk::ImageViewType::e2D,
							   // The format is allowed to differ, (ie. reading R8G8B8A8Unorm as R8G8B8A8Srgb), if the format is compatible
							   .format = ultra_format.vk_format,
							   // The swizzle, ie. swapping colour channels
							   .components = ultra_format.vk_swizzed,
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
						   });

						   return WlBufferDataInner {
							   .memory = std::move(image_memory),
							   .image = std::move(image),
							   .image_view = std::move(image_view),
						   };
					   };
					   client.add_object<WlBuffer>(request.id, std::make_unique<WlBufferData>(std::move(create_shm)));
				   },
				   [this, &pool_data](Resize& request) {
					   auto start = static_cast<std::byte*>(mremap(pool_data.get()->start, pool_data->size, request.size, MREMAP_MAYMOVE));
					   if (start == MAP_FAILED) {
						   client.error(keyd.id, WlShm::ErrorEnum::InvalidFd, combo_errno("Failed to resize pool"));
					   }
					   pool_data.get()->start = start;
					   pool_data.get()->size = request.size;
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

struct Plane {
	int fd;
	std::uint32_t plane_index;
	std::uint32_t offset;
	std::uint32_t pitch;
	std::uint64_t modifier;
};

struct DmabufParams {
	std::vector<Plane> planes;
};

/*  Send format table - packed vector of (format, modifier)
 *  Send main device - default device
 *  for every device
 *      send tranche target deivce - device this tranche relates to
 *      send tranche flags - if we're going to direct scanout or sample
 *      send tranche formats - packed u16 indexes indexing into format table
 *      tranch done - reset target
 *  rof
 */
void ZwpLinuxDmabufV1::handle(Request request) {
	auto handle_feedback = [this](std::uint32_t id) {
		// Construct format table
		struct FormatEntry {
			std::uint32_t format;
			std::uint32_t padding;
			std::uint64_t modifier;
		};
		auto& reality = gimme_reality(client);
		std::vector<FormatEntry> format_table;
		for (auto& ultra_format : reality.render.ultra_formats) {
			for (auto& modifier : ultra_format.drm_modifiers) {
				format_table.push_back(FormatEntry {
					.format = ultra_format.drm_format,
					.modifier = modifier.drmFormatModifier,
				});
			}
		}
		auto size = format_table.size() * sizeof(FormatEntry);
		int fd = memfd_create("format-table", 0);
		if (fd == -1)
			MQ_XERRNO("Failed to create format table fd");
		DEFER([fd]() { close(fd); });
		ftruncate(fd, size); // Ironically, also used to grow fd
		auto start = static_cast<std::byte*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
		if (start == MAP_FAILED)
			MQ_XERRNO("Failed to mmap for format table");
		memcpy(start, format_table.data(), size);
		auto& feedback = client.add_object<ZwpLinuxDmabufFeedbackV1>(id).object;
		feedback.format_table(fd, size);
		// Construct main device
		std::vector<std::uint8_t> bytes(sizeof(reality.seat.rdev));
		std::memcpy(bytes.data(), &reality.seat.rdev, sizeof(reality.seat.rdev));
		feedback.main_device(bytes);
		// Set tranche target device
		feedback.tranche_target_device(bytes);
		// Set tranche flags
		feedback.tranche_flags(ZwpLinuxDmabufFeedbackV1::TrancheFlagsEnum::Sampling);
		// Set tranche formats
		std::vector<std::uint8_t> tranche_formats(format_table.size() * sizeof(std::uint16_t));
		for (std::uint16_t i = 0; i < format_table.size(); ++i) {
			std::memcpy(tranche_formats.data() + (i * sizeof(i)), &i, sizeof(i));
		}
		feedback.tranche_formats(tranche_formats);
		// Set tranche done
		feedback.tranche_done();
	};
	std::visit(overload {
				   [this](CreateParams& request) {
					   client.add_object<ZwpLinuxBufferParamsV1>(request.params_id, std::make_unique<DmabufParams>());
				   },
				   [this, handle_feedback](GetDefaultFeedback& request) {
					   handle_feedback(request.id);
				   },
				   [this, handle_feedback](GetSurfaceFeedback& request) {
					   handle_feedback(request.id);
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

void ZwpLinuxBufferParamsV1::handle(Request request) {
	auto& data = gimme_data<DmabufParams>(user_data);
	std::visit(overload {
				   [this, &data](Add& request) {
					   data.planes.push_back(Plane {
						   .fd = request.fd,
						   .plane_index = request.plane_idx,
						   .offset = request.offset,
						   .pitch = request.stride,
						   .modifier = combine_u32s(request.modifier_hi, request.modifier_lo),
					   });
				   },
				   [this](Create& request) {},
				   [this](CreateImmed& request) {},
				   [this](SetSamplingDevice& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}

} // namespace mayquill

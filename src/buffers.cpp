module;
#include <drm_fourcc.h>
#include <fcntl.h>
#include <mayday/macros.h>
#include <mayquill/logger.h>
#include <sys/mman.h>
#include <unistd.h>
module mayquill;
import mayday.buffers;
import mayday.surfaces;
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
	std::visit(
		overload {
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
	std::visit(
		overload {
			[this, &pool_data](CreateBuffer& request) {
				auto create_shm = [this, request, &pool_data]() -> WlBufferDataInner {
					std::uint32_t width = static_cast<std::uint32_t>(request.width);
					std::uint32_t height = static_cast<std::uint32_t>(request.height);
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
							.width = width,
							.height = height,
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
					auto memory_type_index = reality.get_memory_type_index(*reality.render.physical_device, requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
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
							.width = width,
							.height = height,
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
					// auto image_view = reality.render.device.createImageView(vk::ImageViewCreateInfo {
					// 	.image = image,
					// 	// You could interpret a 3d image as a 2d image with array layers
					// 	.viewType = vk::ImageViewType::e2D,
					// 	// The format is allowed to differ, (ie. reading R8G8B8A8Unorm as R8G8B8A8Srgb), if the format is compatible
					// 	.format = ultra_format.vk_format,
					// 	// The swizzle, ie. swapping colour channels
					// 	.components = ultra_format.vk_swizzed,
					// 	.subresourceRange = {
					// 		// If the image stores multiple data in one (eg.interleaved depth/stencil), we can pick just the data we want
					// 		// In our case, all our data is colour
					// 		.aspectMask = vk::ImageAspectFlagBits::eColor,
					// 		// MIP level 0 is the regular full-res image, so we choose mip level range from 0 to 1 (exclusive), so just 0
					// 		.baseMipLevel = 0,
					// 		.levelCount = 1,
					// 		// Same with array layers
					// 		.baseArrayLayer = 0,
					// 		.layerCount = 1,
					// 	},
					// });

					std::vector<vk::raii::DeviceMemory> memories;
					memories.push_back(std::move(image_memory));
					return WlBufferDataInner {
						.width = width,
						.height = height,
						.memories = std::move(memories),
						.image = std::move(image),
						.ultra_format = std::move(ultra_format),
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

/*  Send format table - packed vector of (format, modifier). Formats will repeat, for each modifier they have
 *  Send main device - default device
 *  for every device
 *      send tranche target deivce - device this tranche relates to
 *      send tranche flags - if we're going to direct scanout or sample
 *      send tranche formats - packed u16 indexes indexing into format table, saying which this device supports
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
	std::visit(
		overload {
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

WlBufferDataInner handle_dmabuf(Client& client, std::int32_t width, std::int32_t height, std::uint32_t drm_format, std::vector<Plane> planes) {
	// Sort by plane index
	std::ranges::sort(planes, std::ranges::less {}, &Plane::plane_index);
	std::vector<vk::SubresourceLayout> plane_layouts;
	plane_layouts.reserve(planes.size());

	for (int i = 0; i < planes.size(); ++i) {
		plane_layouts.push_back(vk::SubresourceLayout {
			.offset = planes[i].offset,
			.rowPitch = planes[i].pitch,
		});
	}

	auto& reality = gimme_reality(client);
	auto it = std::ranges::find_if(reality.render.ultra_formats, [drm_format](const UltraFormat& format) { return format.drm_format == drm_format; });
	if (it == reality.render.ultra_formats.end())
		MQ_XERROR("Unable to find matching ultra format for drm format");
	auto& ultra_format = *it;

	// The structure type is 'specialised' on the first type you provide it. Types such as imagecreateinfo define other types that can go in their pnext.
	// The chain will automatically fill the pnext chain to include the image drm format shit and the external memory image shit for us
	vk::StructureChain<vk::ImageCreateInfo, vk::ImageDrmFormatModifierExplicitCreateInfoEXT, vk::ExternalMemoryImageCreateInfo> image_chain = {
		vk::ImageCreateInfo {
			.flags = vk::ImageCreateFlagBits::eDisjoint, // The backing memory for each memory plane may be on different fds, we'll need to bind multiple memories
			.imageType = vk::ImageType::e2D,
			.format = ultra_format.vk_format,
			.extent = {
				.width = static_cast<std::uint32_t>(width),
				.height = static_cast<std::uint32_t>(height),
				.depth = 1,
			},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = vk::SampleCountFlagBits::e1,
			.tiling = vk::ImageTiling::eDrmFormatModifierEXT, // Modifier & memory planes provided in pnext
			.usage = vk::ImageUsageFlagBits::eSampled,
			.sharingMode = vk::SharingMode::eExclusive,
			.initialLayout = vk::ImageLayout::eUndefined,
		},
		// https://docs.vulkan.org/refpages/latest/refpages/source/VkImageDrmFormatModifierExplicitCreateInfoEXT.html
		// Vulkan must use this specific modifier, and plane layouts, we are not negotiating, we're telling vulkan it must use this (importing path)
		vk::ImageDrmFormatModifierExplicitCreateInfoEXT {
			// The modifier across all planes is the same, I'm unsure why we need to specify the number of planes, because the modifier already will specify the number of planes
			// it expects internally. Perhaps just a sanity check? The spec on that link even specifies that it must be equal to what it expects
			.drmFormatModifier = planes[0].modifier,
			.drmFormatModifierPlaneCount = static_cast<std::uint32_t>(plane_layouts.size()),
			// The offset tells the image to offset bound memory to that point, ie. 20 offset would mean the start is 20 bytes into the memory
			// We're using the offset from the fd as seen above. We could instead bind the memory at an offset, but we need to specify pitch anyways here so it's just easier
			.pPlaneLayouts = plane_layouts.data(),
		},
		// The image's memory will be from a dmabuf fd
		vk::ExternalMemoryImageCreateInfo {
			.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
		},
	};

	vk::raii::Image image = reality.render.device.createImage(image_chain.get<vk::ImageCreateInfo>());
	static constexpr vk::ImageAspectFlagBits memory_planes[4] = {
		vk::ImageAspectFlagBits::eMemoryPlane0EXT,
		vk::ImageAspectFlagBits::eMemoryPlane1EXT,
		vk::ImageAspectFlagBits::eMemoryPlane2EXT,
		vk::ImageAspectFlagBits::eMemoryPlane3EXT,
	};

	std::vector<int> plane_fds; // Keep the plane fds for later use
	// Create the multiple memories, and bind the fds to them
	std::vector<vk::raii::DeviceMemory> plane_proxy_memories;
	plane_proxy_memories.reserve(plane_layouts.size());
	std::vector<vk::BindImagePlaneMemoryInfo> memory_plane_bind_infos;
	memory_plane_bind_infos.reserve(plane_layouts.size());
	std::vector<vk::BindImageMemoryInfo> memory_bind_infos;
	memory_bind_infos.reserve(plane_layouts.size());
	for (int i = 0; i < plane_layouts.size(); ++i) {
		int fd = fcntl(planes[i].fd, F_DUPFD, 0);
		if (fd)
			MQ_XERRNO("Failed to dupe fd");
		plane_fds.push_back(fd);

		// Specify we want the requirements of the image, but just for that plane
		vk::StructureChain<vk::ImageMemoryRequirementsInfo2, vk::ImagePlaneMemoryRequirementsInfo> memory_chain = {
			vk::ImageMemoryRequirementsInfo2 {
				.image = image,
			},
			vk::ImagePlaneMemoryRequirementsInfo {
				.planeAspect = memory_planes[i],
			},
		};

		// Get the requirements of both the image plane, and the fd we're importing
		auto memory_requirements = reality.render.device.getImageMemoryRequirements2(memory_chain.get<vk::ImageMemoryRequirementsInfo2>());
		// https://docs.vulkan.org/refpages/latest/refpages/source/vkGetMemoryFdPropertiesKHR.html
		auto fd_properties = reality.render.device.getMemoryFdPropertiesKHR(vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT, planes[i].fd);
		std::uint32_t compatible_memory_types = memory_requirements.memoryRequirements.memoryTypeBits & fd_properties.memoryTypeBits;

		vk::StructureChain<vk::MemoryAllocateInfo, vk::ImportMemoryFdInfoKHR> allocation_chain = {
			vk::MemoryAllocateInfo {
				.allocationSize = memory_requirements.memoryRequirements.size,
				// You're probably wondering why we are even needing to provide a memory type, given the FD is already in memory. It's because we're not actually
				// allocating memory, as you can see below obviously, we're specifying the imported memory, but also the memory type is just a mask which limits
				// what access we have to the memory / provides additional details. Vulkan is unaware of what we're "allowed" to do with the memory, so we a valid mask
				// Crucially, multiple memory types can point to the same underlying memory, just with different controls on what we're allowed to do
				.memoryTypeIndex = *reality.get_memory_type_index(*reality.render.physical_device, compatible_memory_types, vk::MemoryPropertyFlags {}),
			},
			vk::ImportMemoryFdInfoKHR {
				.handleType = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
				// This transfers the ownership of the fd from us, to vulkan, we are no longer allowed to do anything with it
				// hence why we cloned it earlier
				.fd = planes[i].fd,
			}};

		plane_proxy_memories.push_back(reality.render.device.allocateMemory(allocation_chain.get<vk::MemoryAllocateInfo>()));

		// Specify the plane we're binding to in this extension: https://docs.vulkan.org/refpages/latest/refpages/source/VkBindImagePlaneMemoryInfo.html
		memory_plane_bind_infos.push_back(vk::BindImagePlaneMemoryInfo {
			.planeAspect = memory_planes[i],
		});
		// https://docs.vulkan.org/refpages/latest/refpages/source/VkBindImageMemoryInfo.html
		memory_bind_infos.push_back(vk::BindImageMemoryInfo {
			.pNext = &memory_plane_bind_infos[i],
			.image = image,
			.memory = plane_proxy_memories[i],
			// We specify the offset in the image for each plane. Theoretically, I think we could also set it here,
			// then remove it from the fd offset from the plane, but we need pPlaneLayout anyway for the plane pitches so it's pointless
			.memoryOffset = 0,
		});
	}

	// Notice we aren't using image.bindMemory(), hence the different BindImageMemoryInfo signature
	reality.render.device.bindImageMemory2(memory_bind_infos);

	return WlBufferDataInner {
		.plane_fds = std::move(plane_fds),
		.memories = std::move(plane_proxy_memories),
		.image = std::move(image),
		.ultra_format = std::move(ultra_format),
	};
}

void ZwpLinuxBufferParamsV1::handle(Request request) {
	auto& data = gimme_data<DmabufParams>(user_data);

	std::visit(
		overload {
			[this, &data](Add& request) {
				data.planes.push_back(Plane {
					.fd = request.fd,
					.plane_index = request.plane_idx,
					.offset = request.offset,
					.pitch = request.stride,
					.modifier = combine_u32s(request.modifier_hi, request.modifier_lo),
				});
			},
			[this, &data](Create& request) {
				std::optional<WlBufferDataInner> inner;
				try {
					inner = handle_dmabuf(client, request.width, request.height, request.format, std::move(data.planes));
				} catch (std::exception& e) {
					MQ_ERROR("{}", e.what());
					failed();
					return;
				}
				auto buffer = client.add_object<WlBuffer>(client.next_id(), std::make_unique<WlBufferData>(std::move(*inner)));
				created(buffer.key.id);
			},
			[this, &data](CreateImmed& request) {
				std::optional<WlBufferDataInner> inner;
				try {
					inner = handle_dmabuf(client, request.width, request.height, request.format, std::move(data.planes));
				} catch (std::exception& e) {
					MQ_ERROR("{}", e.what());
					client.error(keyd.id, ZwpLinuxBufferParamsV1::ErrorEnum::InvalidWlBuffer, std::format("Failed to import: {}", e.what()));
					return;
				}
				auto buffer = client.add_object<WlBuffer>(request.buffer_id, std::make_unique<WlBufferData>(std::move(*inner)));
			},
			[this](SetSamplingDevice& request) {},
			[this](Destroy& request) {},
		},
		request);
}

} // namespace mayquill

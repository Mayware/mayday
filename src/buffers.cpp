module;
#include <sys/mman.h>
#include <unistd.h>
module mayquill;
import :surfaces;
import mayday.util;

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

struct WlBufferData {
	std::shared_ptr<WlShmPoolData> pool;
	std::int32_t offset;
	std::int32_t width;
	std::int32_t height;
	std::int32_t stride;
	WlShm::FormatEnum format;
};

void WlShmPool::handle(Request request) {
	auto& pool_data = gimme_data<std::shared_ptr<WlShmPoolData>>(user_data);
	std::visit(overload {
				   [this, &pool_data](CreateBuffer& request) {
					   client.add_object<WlBuffer>(request.id,
						   std::make_unique<WlBufferData>(pool_data, request.offset, request.width, request.height, request.stride, request.format));
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
	std::uint32_t stride;
	std::uint64_t modifier;
};

struct DmabufParams {
	std::vector<Plane> planes;
};

void ZwpLinuxDmabufV1::handle(Request request) {
	std::visit(overload {
				   [this](CreateParams& request) {
					   client.add_object<ZwpLinuxBufferParamsV1>(request.params_id, std::make_unique<DmabufParams>());
				   },
				   [this](GetDefaultFeedback& request) {},
				   [this](GetSurfaceFeedback& request) {},
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
						   .stride = request.stride,
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

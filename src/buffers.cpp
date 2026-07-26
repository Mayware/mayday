module;
#include <sys/mman.h>
#include <unistd.h>
module mayquill;
import :surfaces;
import :util;

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
} // namespace mayquill

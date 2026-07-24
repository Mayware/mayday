module;
#include <sys/mman.h>
#include <unistd.h>
module mayquill;
import :surfaces;
import util;

namespace mayquill {
struct WpFractionalScaleData {
	Key surface;
};

void WpFractionalScaleManagerV1::handle(Request request) {
	std::visit(overload {
				   [this](GetFractionalScale& request) {
					   auto surface = client.grab_object<WlSurface>(request.surface);
					   auto fractional = client.add_object<WpFractionalScaleV1>(request.id);
					   static_cast<WlSurfaceData*>(surface.object.user_data.get())->fractional_scale = fractional.key;
				   },
				   [this](Destroy& request) {
				   }},
		request);
}

struct WlShmPoolData {
	int fd;
	// Shared with buffer data
	std::shared_ptr<std::byte*> start;
	std::int32_t size;
};

void WlShm::handle(Request request) {
	std::visit(overload {
				   [this](CreatePool& request) {
					   auto start = std::make_shared<std::byte*>(static_cast<std::byte*>(mmap(nullptr, request.size, PROT_READ | PROT_WRITE, MAP_SHARED, request.fd, 0)));

					   if (*start == MAP_FAILED) {
						   close(request.fd);
						   client.error(keyd.id, WlShm::ErrorEnum::InvalidFd, combo_errno("Failed to mmap"));
					   }
					   client.add_object<WlShmPool>(request.id,
						   std::make_unique<WlShmPoolData>(WlShmPoolData {
							   .fd = request.fd,
							   .start = start,
							   .size = request.size}));
				   },
				   [this](Release& request) {

				   }},
		request);
}

struct WlBufferData {
	// Shared with the owning pool
	std::shared_ptr<std::byte*> start;
	std::int32_t offset;
	std::int32_t width;
	std::int32_t height;
	std::int32_t stride;
	WlShm::FormatEnum format;
};

void WlShmPool::handle(Request request) {
	auto* pool_data = static_cast<WlShmPoolData*>(user_data.get());
	std::visit(overload {
				   [this, pool_data](CreateBuffer& request) {
					   client.add_object<WlBuffer>(request.id,
						   std::make_unique<WlBufferData>(WlBufferData {
							   .start = pool_data->start,
							   .offset = request.offset,
							   .width = request.width,
							   .height = request.height,
							   .stride = request.stride,
							   .format = request.format}));
				   },
				   [this, pool_data](Resize& request) {
					   auto start = static_cast<std::byte*>(mremap(*pool_data->start, pool_data->size, request.size, MREMAP_MAYMOVE));
					   if (start == MAP_FAILED) {
						   client.error(keyd.id, WlShm::ErrorEnum::InvalidFd, combo_errno("Failed to resize pool"));
					   }
					   *pool_data->start = start;
					   pool_data->size = request.size;
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

// void WlShmPool::handle_destroy() {
// 	auto* data = static_cast<WlShmPoolData*>(user_data.get());
// 	close(data->fd);
// 	munmap(*data->start, data->size);
// }


} // namespace mayquill

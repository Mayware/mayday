module;
#include <mayquill/logger.h>
#include <sys/mman.h>
#include <unistd.h>
module mayquill;
import util;
import std;

namespace mayquill {
void WlDisplay::handle(Request request) {
	std::visit(overload {
				   [this](Sync& request) {
					   auto [_, callback] = client.add_object<WlCallback, void>(request.callback);
					   callback.done(0);
					   callback.destroy();
				   },
				   [this](GetRegistry& request) {
					   MQ_INFO("Requested registry creation id {}", request.registry);
					   auto [_, registry] = client.add_object<WlRegistry, void>(request.registry);
					   registry.global(1, std::string(WlCompositor::interface), WlCompositor::version);
					   registry.global(2, std::string(WlSubcompositor::interface), WlSubcompositor::version);
					   registry.global(3, std::string(WlShm::interface), WlShm::version);
					   registry.global(4, std::string(XdgWmBase::interface), XdgWmBase::version);
					   MQ_INFO("Gave globals");
				   }},
		request);
}

void WlRegistry::handle(Request request) {
	std::visit(overload {
				   [this](Bind& request) {
					   MQ_INFO("name {} interface {} version {} id {}", request.name, request.interface, request.version, request.id);
					   switch (request.name) {
					   case 1:
						   client.add_object<WlCompositor, void>(request.id);
						   break;
					   case 2:
						   client.add_object<WlSubcompositor, void>(request.id);
						   break;
					   case 3:
						   client.add_object<WlShm, void>(request.id).second.format(WlShm::FormatEnum::Argb8888);
						   break;
					   case 4:
						   client.add_object<XdgWmBase, void>(request.id);
						   break;
					   }
				   }},
		request);
}

struct WlShmPoolData {
	int fd;
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
						   return;
					   }
					   auto data = std::make_unique<WlShmPoolData>(WlShmPoolData {
						   .fd = request.fd,
						   .start = start,
						   .size = request.size});
					   auto [_, pool] = client.add_object<WlShmPool, WlShmPoolData>(request.id);
					   pool.user_data.reset(data.release());
				   },
				   [this](Release& request) {

				   }},
		request);
}

struct WlBufferData {
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
					   auto data = std::make_unique<WlBufferData>(WlBufferData {
						   .start = pool_data->start,
						   .offset = request.offset,
						   .width = request.width,
						   .height = request.height,
						   .stride = request.stride,
						   .format = request.format});
					   auto [_, buffer] = client.add_object<WlBuffer, WlBufferData>(request.id);
					   buffer.user_data.reset(data.release());
				   },
				   [this, pool_data](Resize& request) {
					   auto start = static_cast<std::byte*>(mremap(*pool_data->start, pool_data->size, request.size, MREMAP_MAYMOVE));
					   if (start == MAP_FAILED) {
						   client.error(keyd.id, WlShm::ErrorEnum::InvalidFd, combo_errno("Failed to resize pool"));
						   return;
					   }
					   *pool_data->start = start;
					   pool_data->size = request.size;
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

void WlCompositor::handle(Request request) {
	std::visit(overload {
				   [this](CreateSurface& request) {
					   client.add_object<WlSurface, void>(request.id);
				   },
				   [this](CreateRegion& request) {
					   client.add_object<WlRegion, void>(request.id);
				   },
				   [this](Release& request) {
				   }},
		request);
};

struct XdgSurfaceData {
	std::uint32_t surface;
};

void XdgWmBase::handle(Request request) {
	std::visit(overload {
				   [this](CreatePositioner& request) {
					   client.add_object<XdgPositioner, void>(request.id);
				   },
				   [this](GetXdgSurface& request) {
					   auto data = std::make_unique<XdgSurfaceData>(XdgSurfaceData {
						   .surface = request.surface,
					   });
					   auto [_, surface] = client.add_object<XdgSurface, XdgSurfaceData>(request.id);
					   surface.user_data.reset(data.release());
				   },
				   [this](Pong& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}

// void WlShmPool::handle_destroy() {
//     auto* data = static_cast<WlShmPoolData*>(user_data.get());
//     close(data->fd);
//     munmap(*data->start, data->size);
// }
}; // namespace mayquill

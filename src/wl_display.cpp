module;
#include <mayquill/logger.h>
#include <sys/mman.h>
#include <unistd.h>
module mayquill;
import util;
import std;

namespace mayquill {
template<typename T>
class Buffered {
  private:
	bool dirty = false;
	T current;
	T pending;

  public:
	Buffered(T&& initial) : current(std::move(initial)) {}

	void buffer(T&& recent) {
		pending = std::move(recent);
		dirty = true;
	}

	void commit() {
		if (dirty == true) {
			current = std::move(pending);
			// Pending doesn't need to be changed, we won't read from it again
			dirty = false;
		}
	}
};

void WlDisplay::handle(Request request) {
	std::visit(overload {
				   [this](Sync& request) {
					   auto [_, callback] = client.add_object<WlCallback>(request.callback);
					   callback.done(0);
					   callback.destroy();
				   },
				   [this](GetRegistry& request) {
					   MQ_INFO("Requested registry creation id {}", request.registry);
					   auto [_, registry] = client.add_object<WlRegistry>(request.registry);
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
						   client.add_object<WlCompositor>(request.id);
						   break;
					   case 2:
						   client.add_object<WlSubcompositor>(request.id);
						   break;
					   case 3:
						   client.add_object<WlShm>(request.id).second.format(WlShm::FormatEnum::Argb8888);
						   break;
					   case 4:
						   client.add_object<XdgWmBase>(request.id);
						   break;
					   }
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
						   return;
					   }
					   auto [_, pool] = client.add_object<WlShmPool>(request.id,
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
					   auto [_, buffer] = client.add_object<WlBuffer>(request.id,
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
						   return;
					   }
					   *pool_data->start = start;
					   pool_data->size = request.size;
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

struct WlSurfaceData {
	Buffered<std::optional<Key>> buffer;
};

void WlCompositor::handle(Request request) {
	std::visit(overload {
				   [this](CreateSurface& request) {
					   client.add_object<WlSurface>(request.id,
						   std::make_unique<WlSurfaceData>(WlSurfaceData {
							   .buffer = Buffered<std::optional<Key>>(std::nullopt)}));
				   },
				   [this](CreateRegion& request) {
					   client.add_object<WlRegion>(request.id);
				   },
				   [this](Release& request) {
				   }},
		request);
};

void WlSurface::handle(Request request) {
	auto* surface_data = static_cast<WlSurfaceData*>(user_data.get());
	std::visit(overload {
				   [this](Attach& request) {

				   },
				   [this](Damage& request) {},
				   [this](Frame& request) {},
				   [this](SetOpaqueRegion& request) {},
				   [this](SetInputRegion& request) {},
				   [this, surface_data](Commit& request) {
					   surface_data->buffer.commit();
					   preferred_buffer_scale(1);
				   },
				   [this](SetBufferTransform& request) {},
				   [this](SetBufferScale& request) {},
				   [this](DamageBuffer& request) {},
				   [this](Offset& request) {},
				   [this](GetRelease& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}

struct XdgSurfaceData {
	Key surface;
};

void XdgWmBase::handle(Request request) {
	std::visit(overload {
				   [this](CreatePositioner& request) {
					   client.add_object<XdgPositioner>(request.id);
				   },
				   [this](GetXdgSurface& request) {
					   auto [_, surface] = client.add_object<XdgSurface>(request.id,
						   std::make_unique<XdgSurfaceData>(XdgSurfaceData {
							   .surface = client.get_key(request.surface),
						   }));
				   },
				   [this](Pong& request) {
					   ping(request.serial);
				   },
				   [this](Destroy& request) {},
			   },
		request);
}

void XdgSurface::handle(Request request) {
	std::visit(overload {

				   [this](GetToplevel& request) {
					   auto [_, toplevel] = client.add_object<XdgToplevel>(request.id);
				   },
				   [this](GetPopup& request) {},
				   [this](SetWindowGeometry& request) {},
				   [this](AckConfigure& request) {},
				   [this](Destroy& request) {},
			   },
		request);
}

void XdgToplevel::handle(Request request) {
	std::visit(overload {
				   [this](SetParent& request) {},
				   [this](SetTitle& request) {},
				   [this](SetAppId& request) {},
				   [this](ShowWindowMenu& request) {},
				   [this](Move& request) {},
				   [this](Resize& request) {},
				   [this](SetMaxSize& request) {},
				   [this](SetMinSize& request) {},
				   [this](SetMaximized& request) {},
				   [this](UnsetMaximized& request) {},
				   [this](SetFullscreen& request) {},
				   [this](UnsetFullscreen& request) {},
				   [this](SetMinimized& request) {},
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

module;
#include <drm_fourcc.h>
#include <fcntl.h>
#include <mayday/macros.h>
#include <mayday/libseat.h>
#include <mayquill/logger.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
export module mayday.drm;
import mayday.util;
import mayquill;
import std;

/*
 *  Framebuffer -> Plane -> CRTC -> Encoder -> Connector
 *
 *  Framebuffer: A container for the real backing memory(ies). It interprets the format, etc
 *  Plane: Specifies the cropping / scaling / rotation of the framebuffer.
 *         It also can specify the blending of the previous content in the CRTC.
 *         Please note that memory planes are a different concept.
 *  CRTC: The overall display pipeline, it receives pixel data from the plane(s) and blends them together
 *        It sets the refresh rate / timing, etc. It continuously scans out the framebuffer, so if you
 *        modify the framebuffer, it will automatically be picked up.
 *  Encoder: The connecting element between the CRTC (the overall pixel pipeline), and the connector.
 *           The CRTC feeds it pixel data, which is then converted to a suitable format for the connector.
 *           A historical relic - shouldn't really have been exposed to userspace.
 *  Connector: The port on the GPU (eg. DP-1).
 *  // https://docs.kernel.org/gpu/drm-kms.html
 *
 *  Note, each one of the objects above has a handle/id that is used to reference them. A key pointing to the actual struct.
 *  I will call the actual instantiated struct, an object, and the key, a handle.
 */

// Please see the 2 wrapper functions below for an explanation, this function returns both the <handle, value>
std::optional<std::pair<std::uint64_t, std::uint64_t>> get_property(int fd, std::int32_t handle, std::uint32_t object_type, std::string_view property_name) {
	drmModeObjectProperties* properties = drmModeObjectGetProperties(fd, handle, object_type);
	if (!properties)
		return std::nullopt;
	DEFER([properties]() { drmModeFreeObjectProperties(properties); });

	// The property name is a field of the property, therefore we iterate over the properties until we find the one with the same name
	// The property still has its own handle (properties->props is an array of ids/handles), but it's ->name field identifies to us what it is
	for (int i = 0; i < properties->count_props; ++i) {
		drmModePropertyRes* property = drmModeGetProperty(fd, properties->props[i]);
		DEFER([property]() { drmModeFreeProperty(property); });
		if (!property)
			continue;
		if (property_name == property->name) {
			return std::pair {properties->props[i], properties->prop_values[i]};
		}
	}
	return std::nullopt;
}

// Handles may have additional member variables, that are not within their object, called "properties". The reason is dual.
// The former being that it allows for additional member variables, without modifying the object's / struct's ABI, keeping it backwards-compatible
// The latter being that it allows for objects to have optional fields, that may not exist with some drivers.
// For example, one plane object may not support blending whereas another does. Therefore, the handle will have an associated property
// that is "blending", and if that exists, then the driver supports it & you can get the value, rather than just putting it in the object itself.
auto get_property_value(int fd, std::int32_t handle, std::uint32_t object_type, std::string_view property_name) {
	return get_property(fd, handle, object_type, property_name)
		.transform([](const auto& property) {
			return property.second;
		});
}

// As seen above, we can get property values, given the string name of a property (which is a field on the property object)
// However, if we aren't using the value directly, DRM APIs instead consume the property handle, rather than its name field.
// Hence, we also need a function which is also able to lookup what the handle is, given the name
auto get_property_handle(int fd, std::int32_t handle, std::uint32_t object_type, std::string_view property_name) {
	return get_property(fd, handle, object_type, property_name)
		.transform([](const auto& property) {
			return property.first;
		});
}

struct App {
	libseat* seat;
	bool active;
	int device_id;
	int device_fd;
};

void enable_seat(libseat* seat, void* user_data) {
	auto app = static_cast<App*>(user_data);
	app->active = true;
	MQ_INFO("Seat was enabled");
}

void disable_seat(libseat* seat, void* user_data) {
	auto app = static_cast<App*>(user_data);
	app->active = false;
	MQ_INFO("Seat was disabled");
	libseat_disable_seat(seat);
}

export void start() {
	App app = {};
	libseat_seat_listener listener = {
		.enable_seat = enable_seat,
		.disable_seat = disable_seat,
	};
	app.seat = libseat_open_seat(&listener, &app);
	if (!app.seat)
		MQ_XERRNO("Failed to open seat");
	DEFER([&app]() { libseat_close_seat(app.seat); });

	while (!app.active) {
		if (libseat_dispatch(app.seat, -1) == -1)
			MQ_XERRNO("Failed to dispatch libseat");
	}

	// Device id is libseats internal handle to the device, it takes it again when closing the device
	app.device_id = libseat_open_device(app.seat, "/dev/dri/card0", &app.device_fd);
	if (app.device_id == -1)
		MQ_XERRNO("Failed to open device");

	DEFER([&app]() { close(app.device_fd); });
	DEFER([&app]() { libseat_close_device(app.seat, app.device_id); });
	int fd = app.device_fd;

	if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1))
		MQ_XERRNO("Failed to enable atomic commits");

	// Gives us access to the real planes, i.e. the primary plane.
	// This isn't enabled by default for legacy programs not using the API
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1))
		MQ_XERRNO("Failed to enable universal planes");

	// A struct containing the handles & counts for encoders, connectors, and CRTCs
	// eg. 	uint32_t *encoders; is an array of the handles/ids of the encoders
	// Note that these are just handles, you get the actual objects later with drmModeGetX()
	// https://manpages.debian.org/testing/libdrm-dev/drmModeGetResources.3.en.html
	drmModeRes* resources = drmModeGetResources(fd);
	if (!resources)
		MQ_XERRNO("Failed to get resources");
	DEFER([resources]() { drmModeFreeResources(resources); });

	// Same, but for planes
	drmModePlaneRes* plane_resources = drmModeGetPlaneResources(fd);
	if (!plane_resources)
		MQ_XERRNO("Failed to get plane resources");
	DEFER([plane_resources]() { drmModeFreePlaneResources(plane_resources); });

	// Select the connector
	drmModeConnector* connector = nullptr;
	for (int i = 0; i < resources->count_connectors; ++i) {
		drmModeConnector* c = drmModeGetConnector(fd, resources->connectors[i]);
		if (!c)
			continue;
		// Check if there is atleast one mode on the connector, i'd think that all connectors that are connected
		// atleast already have one mode, but i've seen others do this check, so I will be a sheep
		// (The mode is refresh rate, resolution, etc)
		if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
			connector = c;
			break;
		} else {
			drmModeFreeConnector(c);
		}
	}
	if (!connector)
		MQ_XERROR("Failed to find a suitable connector");
	DEFER([connector]() { drmModeFreeConnector(connector); });

	// Select the CRTC
	std::int32_t crtc_index = -1; // There can be a max of 32 CRTCs specified by the possible_crtcs bitmask, so the range is fine
	for (int i = 0; i < connector->count_encoders && crtc_index == -1; ++i) {
		drmModeEncoder* encoder = drmModeGetEncoder(fd, connector->encoders[i]);
		if (!encoder)
			continue;
		DEFER([encoder]() { drmModeFreeEncoder(encoder); });
		for (int j = 0; j < resources->count_crtcs; ++j) {
			// possible_crtcs is a bitmask, mapping to resources->crtcs[bit]
			// Eg. 1011 would mean that the 1st, 3rd and 4th CRTCs are useable
			// 1 << j just means take 1, then shift it leftwards J times.
			// Anding this with possible_crtcs, will give some number, or 0
			if (encoder->possible_crtcs & (std::uint32_t {1} << j)) {
				crtc_index = j;
			}
		}
	}
	if (crtc_index == -1)
		MQ_XERROR("Failed to find matching CRTC");

	std::uint32_t plane_handle = 0; // handles cannot be 0. Only the handle is later needed, hence why we don't store the full object
	for (int i = 0; i < plane_resources->count_planes; ++i) {
		drmModePlane* plane = drmModeGetPlane(fd, plane_resources->planes[i]);
		if (!plane)
			continue;
		DEFER([plane]() { drmModeFreePlane(plane); });
		// Same trick, as with the CRTCs
		if (plane->possible_crtcs & (std::uint32_t {1} << crtc_index)) {
			// Type is a property value, because it's an additional cap from atomic
			auto type = get_property_value(fd, plane_resources->planes[i], DRM_MODE_OBJECT_PLANE, "type");
			if (type == DRM_PLANE_TYPE_PRIMARY)
				plane_handle = plane->plane_id;
		}
	}
	if (!plane_handle)
		MQ_XERROR("Failed to find primary plane");

	drmModeAtomicReq* atomic_request = drmModeAtomicAlloc();
	if (!atomic_request)
		MQ_XERROR("Failed to allocate atomic request");
	DEFER([atomic_request]() { drmModeAtomicFree(atomic_request); });
	// drmModeAtomicAddProperty takes atomic_request, object_id (object handle), property_id (property handle), property value

	std::uint32_t crtc_handle = resources->crtcs[crtc_index];
	// Set the chosen CRTC that drives this connector
	drmModeAtomicAddProperty(atomic_request, connector->connector_id, *get_property_handle(fd, connector->connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID"), crtc_handle);
	// Set the CRTC to be active (actually scanning out data)
	drmModeAtomicAddProperty(atomic_request, crtc_handle, *get_property_handle(fd, crtc_handle, DRM_MODE_OBJECT_CRTC, "ACTIVE"), 1);
	// Set the mode the CRTC is operating with (eg. refresh, res, etc). We're choosing the 0th mode arbitrarily
	// The connector->modes[] array is NOT an array of the mode handles. Modes do not have handles, they are infact just directly stored (as drmModeModeInfo)
	// in the array as their values. Hence, there is no handle value to pass as an integer to atomic add property, as the mode is directly the value.
	// Therefore, we use drmModeCreatePropertyBlob to upload the mode struct to the kernel, which will then give us an id/handle to use to refer to it.
	auto mode = connector->modes[0];
	std::uint32_t mode_blob_handle;
	if (drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &mode_blob_handle))
		MQ_XERRNO("Failed to create mode property blob");
	DEFER([fd, mode_blob_handle]() { drmModeDestroyPropertyBlob(fd, mode_blob_handle); });
	drmModeAtomicAddProperty(atomic_request, crtc_handle, *get_property_handle(fd, crtc_handle, DRM_MODE_OBJECT_CRTC, "MODE_ID"), mode_blob_handle);

	// Create the graphics-execution-manager (GEM) buffer on the GPU (or CPU if needs be)
	// This is nothing more than a pool of memory - a buffer can hold multiple memory planes for example
	drm_mode_create_dumb create_request = {
		.height = mode.vdisplay,
		.width = mode.hdisplay,
		.bpp = 32 // 32 bits per pixel, or 4 bytes. It's in bits for formats like 1bpp, where it's just on or off pixels
	};
	// Will assign the buffer for us on the kernel side, and set its handle to create_request.handle
	// Will also fill in other additional fields. The pitch only works for *linear* formats, hence why it is a dumb buffer.
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_request) < 0)
		MQ_XERRNO("Failed to create GEM buffer");
	DEFER([fd, &create_request]() {
		drm_mode_destroy_dumb destroy_request = {.handle = create_request.handle};
		drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_request);
	});

	struct {
		std::uint32_t framebuffer_handle, buffer_handle, pitch, width, height;
		std::uint64_t size;
		std::byte* map_start;
	} framebuffer = {
		.buffer_handle = create_request.handle,
		.pitch = create_request.pitch, // Bytes from start of one pixel row to the next. May be more than width * (bpp/8), because of padding, if the driver requires it
		.width = mode.hdisplay,
		.height = mode.vdisplay,
		.size = create_request.size,
	};

	// DRM/KMS supports a maximum of 4 memory planes; colour formats generally don't need any more than this
	// XRGB888 only uses 1 plane, but see https://www.youtube.com/watch?v=3dET-EoIMM8 to see how other formats use multiple.
	// For example, YCbCr(YUV) could have 2 planes, 1 for the brightness (Y), and 1 for the (Cb[blue-ness], Cr[red-ness])
	// Multiple planes can be stored in one GEM buffer, hence the offset to say where each plane starts.
	// We only need to supply the width+height once to the addFB2, because the format can work it out for the rest of the planes
	// (eg. with nv12, the width/height of the 2nd plane is halved). However, we must supply pitch per-plane because we could
	// pack multiple memory planes into one GEM buffer (hence the offset, if we do), but then it's on us to ensure it's padded
	// to the correct byte boundaries, which means that the pitch cannot be solely derived from the main plane's pitch
	// (only the minimum pitch could be)
	std::uint32_t handles[4] = {}, pitches[4] = {}, offsets[4] = {};
	handles[0] = framebuffer.buffer_handle;
	pitches[0] = framebuffer.pitch;
	offsets[0] = 0;
	if (drmModeAddFB2(fd, framebuffer.width, framebuffer.height, DRM_FORMAT_XRGB8888, handles, pitches, offsets, &framebuffer.framebuffer_handle, 0))
		MQ_XERRNO("Failed to create framebuffer");
	DEFER([fd, &framebuffer]() { drmModeRmFB(fd, framebuffer.framebuffer_handle); });

	// Map the buffer, so we can write to it. Since the one fd can have many mmappable buffers
	// this ioctl gives us the offset for the GEM buffer we just made;
	drm_mode_map_dumb map_request = {.handle = framebuffer.buffer_handle};
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_request) < 0)
		MQ_XERRNO("Failed to map buffer (drm)");
	framebuffer.map_start = static_cast<std::byte*>(mmap(0, framebuffer.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map_request.offset));
	if (framebuffer.map_start == MAP_FAILED)
		MQ_XERRNO("Failed to map buffer (mmap)");
	DEFER([&framebuffer]() { munmap(framebuffer.map_start, framebuffer.size); });

	for (std::uint32_t row = 0; row < framebuffer.height; ++row) {
		auto start = framebuffer.map_start + row * framebuffer.pitch;
		for (std::uint32_t column = 0; column < framebuffer.width; ++column) {
			*reinterpret_cast<std::uint32_t*>(start + column * 4) = 0x12345678;
		}
	}

	// Assign the frame buffer to the plane
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "FB_ID"), framebuffer.framebuffer_handle);
	// Tell the plane about the CRTC it feeds into. We do it on the planes end because a plane can only link to one CRTC, where as a CRTC may link to many planes
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), crtc_handle);
	// The SRC_ values use 16whole.16decimal numbers (ie. decimal point half way through in 32 bit number)
	// This is done to allow fractional co-ordinates, in cases where the SRC_size is smaller than the CRTC_output_size (hence you can still refer to pixels on the CRTC)
	// X position of where to start reading inside the framebuffer
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "SRC_X"), 0 << 16);
	// Y position of where to start reading inside the framebuffer
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "SRC_Y"), 0 << 16);
	// How many x pixels to read (width) from the framebuffer
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "SRC_W"), framebuffer.width << 16);
	// How many y pixels to read (height, downwards) from the framebuffer
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "SRC_H"), framebuffer.height << 16);
	// The output co-ordinates of where to put that sampled plane onto the CRTC
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_X"), 0);
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_Y"), 0);
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_W"), framebuffer.width);
	drmModeAtomicAddProperty(atomic_request, plane_handle, *get_property_handle(fd, plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_H"), framebuffer.height);

	if (drmModeAtomicCommit(fd, atomic_request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr))
		MQ_XERRNO("Failed attomic commit");

	sleep(2);
}

module;
#include <drm_fourcc.h>
#include <fcntl.h>
#include <mayday/libseat.h>
#include <mayday/macros.h>
#include <mayquill/logger.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
module mayday;
import mayday.util;
import mayquill;
import std;

/*
 *  Framebuffer -> Plane -> CRTC -> Encoder -> Connector
 *
 *  Framebuffer: A container for the real backing memory(ies). It interprets the format, etc
 *  Plane: Specifies the cropping / scaling / rotation of the framebuffer, attached to it.
 *         It also can specify the blending of the previous content in the CRTC.
 *         Eg. the primary plane, overlay planes etc.
 *         Please note that memory planes are a different concept.
 *  CRTC: The overall display pipeline, it receives pixel data from the plane(s) and blends them together
 *        It sets the refresh rate / timing, etc. The VSync marks the start of the CRTC scan out to the connecotr,
 *        and when that finishes, the vblank starts which is just dead air time (and hence the time to swap out the framebuffer),
 *        until we hit the next vsync where the CRTC again scans out the data to the connector.
 *        Vsync -> CRTC Scan out to monitor (10ms) -> Vblank (6ms, swap framebuffer here) -> Vsync, so the vblank is just the dead
 *        air as the CRTC didn't need the entire refresh budget to scan out. Monitors additionally "scan out" pixels line by line,
 *        pixel by pixel, as they receive data from the crtc, so literally pixel by pixel they'll scan out. Theoretically a monitor
 *        could wait to receive all pixel data, then swap all pixels at once, but that comes with latency costs obviously and the pixel
 *        by pixel scanout usually isn't noticeable.
 *  Encoder: The connecting element between the CRTC (the overall pixel pipeline), and the connector.
 *           The CRTC feeds it pixel data, which is then converted to a suitable format for the connector.
 *           A historical relic - shouldn't really have been exposed to userspace.
 *  Connector: The port on the GPU (eg. DP-1).
 *  // https://docs.kernel.org/gpu/drm-kms.html
 *
 *  Note, each one of the objects above has a handle/id that is used to reference them. A key pointing to the actual struct.
 *  I will call the actual instantiated struct, an object, and the key, a handle.
 */

constexpr std::uint32_t frame_count = 2;

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

struct DrmMonitor {
	std::uint32_t connector_handle;
	std::uint32_t encoder_handle;
	std::uint32_t crtc_handle;
	std::uint32_t plane_handle;
	drmModeModeInfo mode; // Notice how mode isn't a handle, it' just a struct representing the values, we can arbitrary modify it if we want to
};

// This is an explicit no-op, if the monitors aren't actually different to what the connector scan on the device_fd shows. Only changed monitors are regenerated
void Mayday::regenerate_monitors() {
	std::vector<DrmMonitor> viable_monitors;
	std::vector<drmModeConnector*> viable_connectors;

	// A struct containing the handles & counts for encoders, connectors, and CRTCs
	// eg. 	uint32_t *encoders; is an array of the handles/ids of the encoders
	// Note that these are just handles, you get the actual objects later with drmModeGetX()
	// https://manpages.debian.org/testing/libdrm-dev/drmModeGetResources.3.en.html
	drmModeRes* resources = drmModeGetResources(seat.device_fd);
	if (!resources)
		MQ_XERRNO("Failed to get resources");
	DEFER([resources]() { drmModeFreeResources(resources); });

	// Same, but for planes
	drmModePlaneRes* plane_resources = drmModeGetPlaneResources(seat.device_fd);
	if (!plane_resources)
		MQ_XERRNO("Failed to get plane resources");
	DEFER([plane_resources]() { drmModeFreePlaneResources(plane_resources); });

	for (int i = 0; i < resources->count_connectors; ++i) {
		drmModeConnector* connector = drmModeGetConnector(seat.device_fd, resources->connectors[i]);
		if (!connector)
			continue;
		// Check if there is atleast one mode on the connector, i'd think that all connectors that are connected
		// atleast already have one mode, but i've seen others do this check, so I will be a sheep
		// (The mode is refresh rate, resolution, etc)
		if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
			viable_connectors.push_back(connector);
		} else {
			drmModeFreeConnector(connector);
		}
	}

	for (auto connector : viable_connectors) {
		DEFER([connector]() { drmModeFreeConnector(connector); });
		auto mode = connector->modes[0];

		// We pick encoders, planes and CRTCs, ensuring we haven't used them in a previous loop (ie. each viable monitor has a unique encoder, plane and CRTC)
		// Currently, each viable connector just picks the first encoder plane and crtc that works with it. It could potentially be that this connector works with
		// multiple of X, but another one doesn't, so we should let that other connector have it but I cba implementing that

		std::uint32_t encoder_handle;
		std::uint32_t crtc_handle;	  // Recall that the handle is the actual id, whereas the index is just the position in the array it was in
		std::int32_t crtc_index = -1; // There can be a max of 32 CRTCs specified by the possible_crtcs bitmask, so the range is fine
		for (int i = 0; i < connector->count_encoders && crtc_index == -1; ++i) {
			drmModeEncoder* encoder = drmModeGetEncoder(seat.device_fd, connector->encoders[i]);
			if (!encoder)
				continue;
			DEFER([encoder]() { drmModeFreeEncoder(encoder); });
			// Check if we aren't already using this encoder, for another viable_monitor
			bool already_used = std::ranges::any_of(viable_monitors, [handle = encoder->encoder_id](const auto& monitor) { return monitor.encoder_handle == handle; });
			if (already_used)
				continue;

			for (int j = 0; j < resources->count_crtcs; ++j) {
				std::uint32_t handle = resources->crtcs[j];
				// Ensure this crtc isn't already used
				bool already_used = std::ranges::any_of(viable_monitors, [handle](const auto& monitor) { return monitor.crtc_handle == handle; });
				if (already_used)
					continue;

				// possible_crtcs is a bitmask, mapping to resources->crtcs[bit]
				// Eg. 1011 would mean that the 1st, 3rd and 4th CRTCs are useable
				// 1 << j just means take 1, then shift it leftwards J times.
				// Anding this with possible_crtcs, will give some number, or 0
				if (encoder->possible_crtcs & (std::uint32_t {1} << j)) {
					encoder_handle = encoder->encoder_id;
					crtc_handle = handle;
					crtc_index = j;
				}
			}
		}
		if (crtc_index == -1)
			MQ_XERROR("Failed to find matching CRTC");

		std::uint32_t plane_handle = 0; // handles cannot be 0. Only the handle is later needed, hence why we don't store the full object
		for (int i = 0; i < plane_resources->count_planes; ++i) {
			drmModePlane* plane = drmModeGetPlane(seat.device_fd, plane_resources->planes[i]);
			if (!plane)
				continue;
			DEFER([plane]() { drmModeFreePlane(plane); });
			// Ensure this plane isn't already used
			bool already_used = std::ranges::any_of(viable_monitors, [handle = plane->plane_id](const auto& monitor) { return monitor.plane_handle == handle; });
			if (already_used)
				continue;
			// Same trick, as with the CRTCs
			if (plane->possible_crtcs & (std::uint32_t {1} << crtc_index)) {
				// Type is a property value, because it's an additional cap from atomic
				auto type = get_property_value(seat.device_fd, plane_resources->planes[i], DRM_MODE_OBJECT_PLANE, "type");
				if (type == DRM_PLANE_TYPE_PRIMARY)
					plane_handle = plane->plane_id;
			}
		}
		if (!plane_handle)
			MQ_XERROR("Failed to find primary plane");

		std::uint32_t connector_handle = connector->connector_id;

		viable_monitors.push_back(DrmMonitor {
			.connector_handle = connector_handle,
			.encoder_handle = encoder_handle,
			.crtc_handle = crtc_handle,
			.plane_handle = plane_handle,
			.mode = mode,
		});
	}

	// Atomic commit set up. We just want one big atomic commit so state swaps can happen. Eg, connecter A uses CRTC B now and connector B uses CRTC A now, so they can just swap,
	// whereas if it was multiple commits then there'd be a point where they'd assigned the same CRTC. We also use this commit to disable old CRTCs etc that we no longer use,
	// for destroyed monitors, or ones that changed to use a different one
	drmModeAtomicReq* atomic_request = drmModeAtomicAlloc();
	if (!atomic_request)
		MQ_XERROR("Failed to allocate atomic request");
	DEFER([atomic_request]() { drmModeAtomicFree(atomic_request); });

	// Check if current monitors match viable monitors. This pass removes monitors that don't have a match in viable_monitors, or differ to their matching
	// viable_monitor in a meaningful way
	for (int i = 0; i < monitors.size();) {
		auto& monitor = monitors[i];
		bool avadakedavra = false; // To do do a full teardown (ie. also zero out fields)
		bool dirty = false;		   // To do a partial teardown

		// Check if the monitor still exists
		auto viable_monitor = std::find_if(viable_monitors.begin(), viable_monitors.end(), [&monitor](auto& viable_monitor) {
			return monitor.connector_handle == viable_monitor.connector_handle;
		});
		if (viable_monitor == viable_monitors.end()) {
			avadakedavra = true;
			dirty = true;
		}

		if (monitor.crtc_handle != (*viable_monitor).crtc_handle) {
			drmModeAtomicAddProperty(atomic_request, monitor.connector_handle, *get_property_handle(seat.device_fd, monitor.connector_handle, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID"), 0);
			drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), 0);
			drmModeAtomicAddProperty(atomic_request, monitor.crtc_handle, *get_property_handle(seat.device_fd, monitor.crtc_handle, DRM_MODE_OBJECT_CRTC, "ACTIVE"), 0);
			dirty = true;
		}
		if (monitor.encoder_handle != (*viable_monitor).encoder_handle) {
			dirty = true;
		}
		if (monitor.plane_handle != (*viable_monitor).plane_handle) {
			drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "FB_ID"), 0);
			drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), 0);
			dirty = true;
		}

		// Compare all mode fields (that aren't just derived from others)
		if (monitor.mode.clock != viable_monitor->mode.clock ||
			monitor.mode.hdisplay != viable_monitor->mode.hdisplay ||
			monitor.mode.hsync_start != viable_monitor->mode.hsync_start ||
			monitor.mode.hsync_end != viable_monitor->mode.hsync_end ||
			monitor.mode.htotal != viable_monitor->mode.htotal ||
			monitor.mode.hskew != viable_monitor->mode.hskew ||
			monitor.mode.vdisplay != viable_monitor->mode.vdisplay ||
			monitor.mode.vsync_start != viable_monitor->mode.vsync_start ||
			monitor.mode.vsync_end != viable_monitor->mode.vsync_end ||
			monitor.mode.vtotal != viable_monitor->mode.vtotal ||
			monitor.mode.vscan != viable_monitor->mode.vscan ||
			monitor.mode.vrefresh != viable_monitor->mode.vrefresh ||
			monitor.mode.flags != viable_monitor->mode.flags ||
			monitor.mode.type != viable_monitor->mode.type) {
			drmModeAtomicAddProperty(atomic_request, monitor.crtc_handle, *get_property_handle(seat.device_fd, monitor.crtc_handle, DRM_MODE_OBJECT_CRTC, "MODE_ID"), 0);
			dirty = true;
		}

		// Monitor is fine
		if (!(dirty || avadakedavra)) {
			++i;
			viable_monitors.erase(viable_monitor); // We already have it, and it's the same
			continue;
		}

		if (avadakedavra) {
			// This label clears ALL the atomic properties, rather than the fine-grained ones above
			drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "FB_ID"), 0);
			drmModeAtomicAddProperty(atomic_request, monitor.connector_handle, *get_property_handle(seat.device_fd, monitor.connector_handle, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID"), 0);
			drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), 0);
			drmModeAtomicAddProperty(atomic_request, monitor.crtc_handle, *get_property_handle(seat.device_fd, monitor.crtc_handle, DRM_MODE_OBJECT_CRTC, "MODE_ID"), 0);
			drmModeAtomicAddProperty(atomic_request, monitor.crtc_handle, *get_property_handle(seat.device_fd, monitor.crtc_handle, DRM_MODE_OBJECT_CRTC, "ACTIVE"), 0);
		}
		if (dirty) {
			monitors.erase(monitors.begin() + i);
		}
	}

	// Create any monitors which weren't found to exist
	// We first fork out to get_vk_monitor to create the vulkan backing components of images, memory etc, which we then import into DRM as the backing memory
	// The vulkan structs VkFrame, VkMonitor are counterparts to Frame and Monitor and exist solely for the return type of get_vk_monitor
	std::vector<Monitor> new_monitors;
	for (auto& monitor : viable_monitors) {
		// Get (create) the vulkan components of the monitor
		auto vk_monitor = get_vk_monitor(monitor.mode.hdisplay, monitor.mode.vdisplay, frame_count);

		std::vector<Frame> frames;
		// Import the VKFrames into DRM, give them GEM handles and register them as DRM framebuffers
		for (auto& vk_frame : vk_monitor.frames) {
			// We won't need the dmabuf_fd, after we import it into the GEM handle
			DEFER([fd = vk_frame.dmabuf_fd]() { close(fd); });
			std::uint32_t gem_handle;
			if (drmPrimeFDToHandle(seat.device_fd, vk_frame.dmabuf_fd, &gem_handle) < 0)
				MQ_XERRNO("Failed to import dmabuf into monitor");
			// We won't need the GEM handle after we get the framebuffer handle (using the GEM handle to get that, in the first place)
			DEFER([fd = seat.device_fd, handle = gem_handle]() { drmCloseBufferHandle(fd, handle); });

			// DRM/KMS supports a maximum of 4 memory planes; colour formats generally don't need any more than this
			// For example, YCbCr(YUV) could have 2 planes, 1 for the brightness (Y), and 1 for the (Cb[blue-ness], Cr[red-ness])
			// Even with modifiers, it doesn't tell us the full memory layout, see: https://docs.kernel.org/userspace-api/dma-buf-alloc-exchange.html#dimensions-and-size
			// We still need the pitch (synonym of stride) and offset (obviously offset). A 1000x1000 image may be allocated like it is infact 1024x1000 for aligned access
			// patterns, and hence,we still need to know pitch
			std::uint64_t modifiers[4] = {};
			std::uint32_t handles[4] = {}, pitches[4] = {}, offsets[4] = {};
			for (int i = 0; i < vk_frame.memory_planes_layouts.size(); ++i) {
				auto& layout = vk_frame.memory_planes_layouts[i];
				modifiers[i] = vk_frame.drm_modifier.drmFormatModifier;
				handles[i] = gem_handle;
				pitches[i] = layout.rowPitch;
				offsets[i] = layout.offset;
			}
			std::uint32_t framebuffer_handle;
			if (drmModeAddFB2WithModifiers(seat.device_fd, monitor.mode.hdisplay, monitor.mode.vdisplay, render.ultra_formats[0].drm_format,
					handles, pitches, offsets, modifiers, &framebuffer_handle, DRM_MODE_FB_MODIFIERS))
				MQ_XERRNO("Failed to create framebuffer");

			frames.push_back(Frame {
				.memory = std::move(vk_frame.memory),
				.image = std::move(vk_frame.image),
				.image_view = std::move(vk_frame.image_view),
				.drm_modifier = std::move(vk_frame.drm_modifier),
				.memory_planes_layouts = std::move(vk_frame.memory_planes_layouts),

				.framebuffer_handle = framebuffer_handle,
			});
		}

		new_monitors.push_back(Monitor {
			.connector_handle = std::move(monitor.connector_handle),
			.encoder_handle = std::move(monitor.encoder_handle),
			.crtc_handle = std::move(monitor.crtc_handle),
			.plane_handle = std::move(monitor.plane_handle),
			.mode = std::move(monitor.mode),
			.command = std::move(vk_monitor.command),
			.frames = std::move(frames),
		});
	}

	// Modeset all new monitors
	// drmModeAtomicAddProperty takes atomic_request, object_id (object handle), property_id (property handle), property value
	std::vector<std::uint32_t> mode_blob_handles;
	DEFER([fd = seat.device_fd, &mode_blob_handles]() {
		// Clean up the modes we allocated in the loop
		for (auto handle : mode_blob_handles) {
			drmModeDestroyPropertyBlob(fd, handle);
		}
	});
	for (auto& monitor : new_monitors) {
		// Set the chosen CRTC that drives this connector
		drmModeAtomicAddProperty(atomic_request, monitor.connector_handle, *get_property_handle(seat.device_fd, monitor.connector_handle, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID"), monitor.crtc_handle);
		// Set the CRTC to be active (actually scanning out data)
		drmModeAtomicAddProperty(atomic_request, monitor.crtc_handle, *get_property_handle(seat.device_fd, monitor.crtc_handle, DRM_MODE_OBJECT_CRTC, "ACTIVE"), 1);
		// Set the mode the CRTC is operating with (eg. refresh, res, etc). We're choosing the 0th mode arbitrarily
		// The connector->modes[] array is NOT an array of the mode handles. Modes do not have handles, they are infact just directly stored (as drmModeModeInfo)
		// in the array as their values. Hence, there is no handle value to pass as an integer to atomic add property, as the mode is directly the value.
		// Therefore, we use drmModeCreatePropertyBlob to upload the mode struct to the kernel, which will then give us an id/handle to use to refer to it.
		std::uint32_t mode_blob_handle;
		if (drmModeCreatePropertyBlob(seat.device_fd, &monitor.mode, sizeof(monitor.mode), &mode_blob_handle))
			MQ_XERRNO("Failed to create mode property blob");
		mode_blob_handles.push_back(mode_blob_handle);
		drmModeAtomicAddProperty(atomic_request, monitor.crtc_handle, *get_property_handle(seat.device_fd, monitor.crtc_handle, DRM_MODE_OBJECT_CRTC, "MODE_ID"), mode_blob_handle);
		// Assign the frame buffer to the plane
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "FB_ID"), monitor.frames[0].framebuffer_handle);
		// Tell the plane about the CRTC it feeds into. We do it on the planes end because a plane can only link to one CRTC, where as a CRTC may link to many planes
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_ID"), monitor.crtc_handle);
		// The SRC_ values use 16whole.16decimal numbers (ie. decimal point half way through in 32 bit number)
		// This is done to allow fractional co-ordinates, in cases where the SRC_size is smaller than the CRTC_output_size (hence you can still refer to pixels on the CRTC)
		// X position of where to start reading inside the framebuffer
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "SRC_X"), 0 << 16);
		// Y position of where to start reading inside the framebuffer
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "SRC_Y"), 0 << 16);
		// How many x pixels to read (width) from the framebuffer
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "SRC_W"), monitor.mode.hdisplay << 16);
		// How many y pixels to read (height, downwards) from the framebuffer
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "SRC_H"), monitor.mode.vdisplay << 16);
		// The output co-ordinates of where to put that sampled plane onto the CRTC
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_X"), 0);
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_Y"), 0);
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_W"), monitor.mode.hdisplay);
		drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "CRTC_H"), monitor.mode.vdisplay);
	}
	// LET IT RIPPPPP!
	if (drmModeAtomicCommit(seat.device_fd, atomic_request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr))
		MQ_XERRNO("Failed attomic commit");

	// New monitors are completely initialised now (God willing)
	// std::move on a vector only marks the vector itself as an rvalue, getting a .begin() still gives T&
	// std::vieww::as_rvalues also makes the it's rvalues too, so it does move the contents
	monitors.append_range(new_monitors | std::views::as_rvalue);
}

void Mayday::handle_vsync(int fd, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, unsigned int crtc_handle) {
	auto& monitor = *std::ranges::find(monitors, crtc_handle, &Monitor::crtc_handle);
	++monitor.current_frame;

	drmModeAtomicReq* atomic_request = drmModeAtomicAlloc();
	if (!atomic_request)
		MQ_XERROR("Failed to allocate atomic request");
	DEFER([atomic_request]() { drmModeAtomicFree(atomic_request); });

	drmModeAtomicAddProperty(atomic_request, monitor.plane_handle, *get_property_handle(seat.device_fd, monitor.plane_handle, DRM_MODE_OBJECT_PLANE, "FB_ID"),
		monitor.frames[monitor.current_frame].framebuffer_handle);

	if (drmModeAtomicCommit(seat.device_fd, atomic_request, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr))
		MQ_XERRNO("Failed attomic commit (frame update)");

    render_monitor(monitor);
}

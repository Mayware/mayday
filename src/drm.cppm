module;
#include <fcntl.h>
#include <mayday/macros.h>
#include <mayquill/logger.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
export module mayquill:drm;
import :logger;
import :util;
import std;

/*
 *  Framebuffer -> Plane -> CRTC -> Encoder -> Connector
 *
 *  Framebuffer: The actual image memory
 *  Plane: Specifies the cropping / scaling / rotation of the framebuffer.
 *         It also can specify the blending of the previous content in the CRTC.
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

void start() {
	int fd = open("/dev/dri/card1", O_RDWR);
	if (fd < 0)
		MQ_XERRNO("Failed to open card");
	DEFER([fd]() { close(fd); });

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

	// Same, but for planes
	drmModePlaneRes* plane_resources = drmModeGetPlaneResources(fd);
	if (!plane_resources)
		MQ_XERRNO("Failed to get plane resources");

	// Select the connector
	drmModeConnector* connector = nullptr;
	for (int i = 0; i < resources->count_connectors; ++i) {
		drmModeConnector* c = drmModeGetConnector(fd, resources->connectors[i]);
		if (!c)
			continue;
		if (c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
			connector = c;
		} else {
			drmModeFreeConnector(c);
		}
	}
	if (!connector)
		MQ_XERROR("Failed to find a suitable connector");

	// Select the CRTC
	std::int32_t crtc_index = -1; // There can be a max of 32 CRTCs specified by the possible_crtcs bitmask, so the range is fine
	for (int i = 0; i < connector->count_encoders && crtc_index == -1; ++i) {
		drmModeEncoder* encoder = drmModeGetEncoder(fd, connector->encoders[i]);
		DEFER([encoder]() { drmModeFreeEncoder(encoder); });
		if (!encoder)
			continue;
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
		DEFER([plane]() { drmModeFreePlane(plane); });
		if (!plane)
			continue;
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
}

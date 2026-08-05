#include "dynamicrenderer.h"

#include <cstdio>
#include <vector>

#include "../paths.h"

namespace olive
{

static std::string to_lower_copy(std::string s)
{
	for (char &c : s) {
		if (c >= 'A' && c <= 'Z') {
			c = char(c - 'A' + 'a');
		}
	}
	return s;
}

// Stores the requested backend name; the actual backend may later become
// OpenGL if loading or availability checks require a Vulkan fallback.
DynamicRenderer::DynamicRenderer(const std::string &backend)
	: backend_(to_lower_copy(backend))
{
}

// Tears down the backend in the reverse order used by Load(): release renderer
// resources, then destroy the opaque backend object. The shared library itself
// is deliberately NOT unloaded: multiple DynamicRenderer instances can wrap the
// same backend library, and one instance's dlclose can unmap code that other
// instances still reference, producing calls into unmapped memory.
// Backend libraries stay mapped until process exit.
DynamicRenderer::~DynamicRenderer()
{
	destroy();
	post_destroy();
	if (handle_ && destroy_) {
		destroy_(handle_);
		handle_ = nullptr;
	}
}

// Builds the private backend library path for the current platform.
// The search is intentionally restricted to Oak-controlled directories so a
// system libGL/libvulkan loader is never mistaken for an Oak render backend.
std::string DynamicRenderer::library_filename() const
{
	std::string base;
	if (backend_ == "opengl") {
		base = "oakgl";
	} else if (backend_ == "vulkan") {
		base = "oakvulkan";
	} else {
		// Unknown backend: use the name verbatim so the load fails and the
		// caller's OpenGL fallback engages
		base = backend_;
	}
#if defined(_WIN32)
	const std::string filename = base + ".dll";
#elif defined(__APPLE__)
	const std::string filename = "lib" + base + ".dylib";
#else
	const std::string filename = "lib" + base + ".so";
#endif

	namespace fs = std::filesystem;
	const fs::path app_dir(application_dir_path());
	const std::vector<std::string> candidates = {
		(app_dir / filename).string(),
		(app_dir / "render_backends" / filename).lexically_normal().string(),
		(app_dir / ".." / "lib" / filename).lexically_normal().string(),
		(app_dir / ".." / ".." / "lib" / filename).lexically_normal().string(),
		(app_dir / ".." / "engine" / filename).lexically_normal().string(),
		(app_dir / ".." / ".." / "engine" / filename).lexically_normal().string()
	};
	for (const std::string &candidate : candidates) {
		std::error_code ec;
		if (fs::exists(candidate, ec)) {
			return candidate;
		}
	}
	return candidates.front();
}

// Loads the selected backend, resolves its C ABI table, creates the opaque
// backend object, and optionally falls back from Vulkan to OpenGL when runtime
// availability checks fail.
bool DynamicRenderer::load()
{
	if (handle_) {
		return true;
	}

	library_.set_file_name(library_filename());
	if (!library_.load()) {
		if (backend_ == "vulkan") {
			fprintf(stderr,
					"Failed to load Vulkan render backend %s: %s; falling back "
					"to OpenGL backend\n",
					library_.file_name().c_str(),
					library_.error_string().c_str());
			backend_ = "opengl";
			library_.set_file_name(library_filename());
		}

		if (!library_.load()) {
			fprintf(stderr, "Failed to load render backend %s %s: %s\n",
					backend_.c_str(), library_.file_name().c_str(),
					library_.error_string().c_str());
			return false;
		}
	}

	if (!resolve_functions()) {
		fprintf(stderr, "Render backend is missing required symbols %s\n",
				backend_.c_str());
		library_.unload();
		return false;
	}

	// Pass this so the backend renderer is anchored to the adapter; that way it
	// follows DynamicRenderer when the latter is adopted by the render thread.
	// Otherwise it stays in the thread where Load() was called and every GL
	// operation is rejected as "wrong thread", producing a black screen.
	handle_ = create_(this);
	if (!handle_) {
		library_.unload();
		return false;
	}
	if (is_available_ && !is_available_(handle_)) {
		fprintf(stderr, "Render backend is not available %s %s\n",
				backend_.c_str(), library_.file_name().c_str());
		if (backend_ == "vulkan") {
			return fallback_to_open_gl();
		}
		destroy_(handle_);
		handle_ = nullptr;
		library_.unload();
		return false;
	}
	return handle_ != nullptr;
}

// Resolves the mandatory C ABI entry points from the loaded shared library.
// Optional information probes are resolved after the required render interface.
bool DynamicRenderer::resolve_functions()
{
	reset_functions();
#define RESOLVE(member, type, symbol)                          \
	member = reinterpret_cast<type>(library_.resolve(symbol)); \
	if (!member)                                               \
	return false

	RESOLVE(create_, OakBackendCreateFn, "oak_renderer_create");
	RESOLVE(destroy_, OakBackendDestroyFn, "oak_renderer_destroy");
	RESOLVE(init_, OakBackendInitFn, "oak_renderer_init");
	RESOLVE(init_with_context_, OakBackendInitWithContextFn,
			"oak_renderer_init_with_context");
	RESOLVE(post_init_, OakBackendPostInitFn, "oak_renderer_post_init");
	RESOLVE(post_destroy_, OakBackendPostDestroyFn,
			"oak_renderer_post_destroy");
	RESOLVE(destroy_internal_, OakBackendDestroyInternalFn,
			"oak_renderer_destroy_internal");
	RESOLVE(clear_destination_, OakBackendClearDestinationFn,
			"oak_renderer_clear_destination");
	RESOLVE(create_native_texture_, OakBackendCreateNativeTextureFn,
			"oak_renderer_create_native_texture");
	RESOLVE(destroy_native_texture_, OakBackendDestroyNativeTextureFn,
			"oak_renderer_destroy_native_texture");
	RESOLVE(create_native_shader_, OakBackendCreateNativeShaderFn,
			"oak_renderer_create_native_shader");
	RESOLVE(destroy_native_shader_, OakBackendDestroyNativeShaderFn,
			"oak_renderer_destroy_native_shader");
	RESOLVE(upload_to_texture_, OakBackendUploadToTextureFn,
			"oak_renderer_upload_to_texture");
	RESOLVE(download_from_texture_, OakBackendDownloadFromTextureFn,
			"oak_renderer_download_from_texture");
	RESOLVE(flush_, OakBackendFlushFn, "oak_renderer_flush");
	RESOLVE(get_pixel_from_texture_, OakBackendGetPixelFromTextureFn,
			"oak_renderer_get_pixel_from_texture");
	RESOLVE(blit_, OakBackendBlitFn, "oak_renderer_blit");
	RESOLVE(attach_output_texture_, OakBackendAttachOutputTextureFn,
			"oak_renderer_attach_output_texture");
	RESOLVE(detach_output_texture_, OakBackendDetachOutputTextureFn,
			"oak_renderer_detach_output_texture");
	RESOLVE(opengl_context_, OakBackendOpenGLContextFn,
			"oak_renderer_opengl_context");
#undef RESOLVE
	get_info_ = reinterpret_cast<OakBackendGetInfoFn>(
		library_.resolve("oak_renderer_get_info"));
	is_available_ = reinterpret_cast<OakBackendIsAvailableFn>(
		library_.resolve("oak_renderer_is_available"));
	return true;
}

// Discards a partially-created backend and restarts loading with the OpenGL
// backend. This keeps RenderManager's fallback path inside the adapter.
bool DynamicRenderer::fallback_to_open_gl()
{
	if (handle_ && destroy_) {
		destroy_(handle_);
		handle_ = nullptr;
	}
	if (library_.is_loaded()) {
		library_.unload();
	}
	reset_functions();
	backend_ = "opengl";
	return load();
}

// Clears all cached C function pointers so a failed backend cannot leave stale
// call targets behind for a later fallback load.
void DynamicRenderer::reset_functions()
{
	create_ = nullptr;
	destroy_ = nullptr;
	get_info_ = nullptr;
	is_available_ = nullptr;
	init_ = nullptr;
	init_with_context_ = nullptr;
	post_init_ = nullptr;
	post_destroy_ = nullptr;
	destroy_internal_ = nullptr;
	clear_destination_ = nullptr;
	create_native_texture_ = nullptr;
	destroy_native_texture_ = nullptr;
	create_native_shader_ = nullptr;
	destroy_native_shader_ = nullptr;
	upload_to_texture_ = nullptr;
	download_from_texture_ = nullptr;
	flush_ = nullptr;
	get_pixel_from_texture_ = nullptr;
	blit_ = nullptr;
	attach_output_texture_ = nullptr;
	detach_output_texture_ = nullptr;
	opengl_context_ = nullptr;
}

// Returns backend metadata exposed by the dynamic library when available.
bool DynamicRenderer::get_backend_info(OakRenderBackendInfo *out_info) const
{
	return handle_ && get_info_ && out_info && get_info_(handle_, out_info);
}

// Initializes the loaded backend using its own context/device creation path.
bool DynamicRenderer::init()
{
	return load() && init_(handle_);
}

// Initializes an OpenGL backend against an existing widget context; non-OpenGL
// backends may ignore the context on the library side.
bool DynamicRenderer::init_with_open_gl_context(OpenGLContext *context)
{
	if (!load()) {
		return false;
	}
	init_with_context_(handle_, context);
	return true;
}

// Forwards post-destroy cleanup to the backend while the library is still
// loaded and its symbols are still valid.
void DynamicRenderer::post_destroy()
{
	if (handle_ && post_destroy_) {
		post_destroy_(handle_);
	}
}

// Runs backend post-initialization after Init/InitWithOpenGLContext has
// established the device or GL context.
void DynamicRenderer::post_init()
{
	if (handle_) {
		post_init_(handle_);
	}
}

// Forwards render target clearing through the C ABI.
void DynamicRenderer::clear_destination(Texture *texture, double r, double g,
									   double b, double a)
{
	clear_destination_(handle_, texture, r, g, b, a);
}

// Creates a backend-native shader and receives the result as an opaque Variant
// because this first-generation ABI still shares C++ types between modules.
Variant DynamicRenderer::create_native_shader(ShaderCode code)
{
	Variant out;
	create_native_shader_(handle_, &code, &out);
	return out;
}

// Releases a backend-native shader handle.
void DynamicRenderer::destroy_native_shader(Variant shader)
{
	destroy_native_shader_(handle_, &shader);
}

// Uploads CPU pixel data into a backend texture through the dynamic ABI.
void DynamicRenderer::upload_to_texture(const Variant &handle,
									  const VideoParams &params,
									  const void *data, int linesize)
{
	upload_to_texture_(handle_, &handle, &params, data, linesize);
}

// Downloads backend texture data into a caller-provided CPU buffer.
void DynamicRenderer::download_from_texture(const Variant &handle,
										  const VideoParams &params, void *data,
										  int linesize)
{
	download_from_texture_(handle_, &handle, &params, data, linesize);
}

// Waits for backend work to become visible to subsequent CPU or GPU consumers.
void DynamicRenderer::flush()
{
	flush_(handle_);
}

// Reads a single pixel through the backend-provided readback hook.
Color DynamicRenderer::get_pixel_from_texture(Texture *texture, const PointF &pt)
{
	Color out;
	get_pixel_from_texture_(handle_, texture, &pt, &out);
	return out;
}

// Exposes the wrapped OpenGL context when the backend is OpenGL; Vulkan returns
// null so callers can avoid GL-only paths.
OpenGLContext *DynamicRenderer::open_gl_context() const
{
	return opengl_context_ && handle_ ?
			   static_cast<OpenGLContext *>(opengl_context_(handle_)) :
			   nullptr;
}

// Reports the effective backend after any load-time fallback has completed.
bool DynamicRenderer::is_open_gl() const
{
	return backend_ == "opengl";
}

bool DynamicRenderer::is_vulkan() const
{
	return backend_ == "vulkan";
}

// Dispatches a shader blit to the loaded backend.
void DynamicRenderer::blit(Variant shader, AcceleratedJob &job,
						   Texture *destination, VideoParams destination_params,
						   bool clear_destination)
{
	blit_(handle_, &shader, &job, destination, &destination_params,
		  clear_destination);
}

// Allocates a backend-native texture and wraps its opaque handle in Variant.
Variant DynamicRenderer::create_native_texture(int width, int height, int depth,
											 PixelFormat format,
											 int channel_count,
											 const void *data, int linesize)
{
	Variant out;
	create_native_texture_(handle_, width, height, depth, format, channel_count,
						   data, linesize, &out);
	return out;
}

// Releases a backend-native texture handle.
void DynamicRenderer::destroy_native_texture(Variant texture)
{
	destroy_native_texture_(handle_, &texture);
}

// Releases renderer-owned backend resources before the backend object itself is
// destroyed.
void DynamicRenderer::destroy_internal()
{
	if (handle_) {
		destroy_internal_(handle_);
	}
}

// Exposes OFX OpenGL output binding through the dynamic backend when supported.
void DynamicRenderer::attach_output_texture(Texture *texture)
{
	if (attach_output_texture_ && texture) {
		Variant id = texture->id();
		attach_output_texture_(handle_, &id);
	}
}

// Clears any OFX output texture binding owned by the backend.
void DynamicRenderer::detach_output_texture()
{
	if (detach_output_texture_) {
		detach_output_texture_(handle_);
	}
}

}

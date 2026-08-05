/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAK_OPENGLCONTEXT_H
#define OAK_OPENGLCONTEXT_H

// De-Qt replacement for QOpenGLContext/QOffscreenSurface.
//
// A thin abstraction over the platform-native GL context APIs:
//   - macOS:  CGL (offscreen contexts need no drawable for FBO rendering)
//   - Linux:  EGL with a pbuffer surface
//   - Windows: WGL with a hidden window providing the HDC
//
// The renderer only ever renders into FBOs, so no on-screen surface/window
// integration lives here; presenting to a window remains an app-layer
// responsibility (the app hands an externally-owned context to
// OpenGLRenderer::init(OpenGLContext *) via adopt_external()).

#include <thread>

#include "openglfunctions.h"

namespace olive
{

class OpenGLContext {
public:
	OpenGLContext(const OpenGLContext &) = delete;
	OpenGLContext &operator=(const OpenGLContext &) = delete;
	~OpenGLContext();

	// Creates an owned offscreen context. `share` may be null; when given, the
	// new context joins the share group of that context (mirrors the former
	// QOpenGLContext::globalShareContext() behavior when the app passes its
	// global context down).
	static OpenGLContext *create_offscreen(const OpenGLContext *share = nullptr);

	// Wraps an externally owned native context (e.g. a viewer context created
	// by the app layer). Non-owning: the caller guarantees the native context
	// outlives this wrapper and calls set_external_invalidated() (or simply
	// stops using the renderer) before destroying it — there is no QPointer
	// auto-nulling anymore.
	//   native_context: CGLContextObj / EGLContext / HGLRC
	//   native_surface: EGLSurface / HDC (unused on macOS, may be null)
	static OpenGLContext *adopt_external(void *native_context,
										 void *native_surface = nullptr);

	bool is_valid() const;
	bool is_owned() const
	{
		return owned_;
	}

	// Makes this context current on the calling thread. For external contexts
	// this is a no-op that only reports whether the context is already current
	// (the app layer owns make-current for its own contexts).
	bool make_current();
	bool is_current() const;

	// Desktop GL port: always false. Kept so shader preamble logic keeps its
	// original shape.
	bool is_open_gles() const
	{
		return false;
	}
	// GL context major version where known (owned contexts), else 0.
	int major_version() const
	{
		return major_version_;
	}

	// The thread this context is bound to (mirrors QOpenGLContext::thread()).
	// Owned contexts are bound at creation; rebind with set_owner_thread().
	std::thread::id owner_thread() const
	{
		return owner_thread_;
	}
	void set_owner_thread(std::thread::id id)
	{
		owner_thread_ = id;
	}

	// Framebuffer to bind when no texture destination is attached (mirrors
	// QOpenGLContext::defaultFramebufferObject() for viewer contexts). Zero for
	// offscreen contexts; the app layer sets it for external viewer contexts.
	unsigned int default_framebuffer() const
	{
		return default_framebuffer_;
	}
	void set_default_framebuffer(unsigned int fbo)
	{
		default_framebuffer_ = fbo;
	}

	// Platform native handles (CGLContextObj / EGLContext / HGLRC; null on
	// failure). Exposed for GL-specific integrations (OFX, debugging).
	void *native_context() const
	{
		return native_context_;
	}
	void *native_surface() const
	{
		return native_surface_;
	}

	// Resolves this context's GL entry points into `out` (context must be
	// current or resolvable without a current context on the platform).
	bool resolve_functions(OpenGLFunctions *out) const;

private:
	OpenGLContext() = default;
	void destroy_native();

	bool owned_ = false;
	bool valid_ = false;
	int major_version_ = 0;
	unsigned int default_framebuffer_ = 0;
	std::thread::id owner_thread_;
	void *native_context_ = nullptr;
	void *native_surface_ = nullptr;
	// Platform bookkeeping (EGLDisplay / HWND etc.), opaque here.
	[[maybe_unused]] void *native_display_ = nullptr;
	[[maybe_unused]] void *native_window_ = nullptr;
};

}

#endif // OAK_OPENGLCONTEXT_H

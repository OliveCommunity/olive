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

#include "openglcontext.h"

#include <cstdio>

#if defined(__APPLE__)
#include <OpenGL/OpenGL.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <EGL/egl.h>
#endif

namespace olive
{

#if defined(__APPLE__)

static CGLPixelFormatObj create_cgl_pixel_format()
{
	CGLPixelFormatAttribute attrs[] = {
		kCGLPFAOpenGLProfile,
		static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),
		kCGLPFAAccelerated,
		static_cast<CGLPixelFormatAttribute>(0),
	};
	CGLPixelFormatObj pix = nullptr;
	GLint npix = 0;
	if (CGLChoosePixelFormat(attrs, &pix, &npix) != kCGLNoError || !pix) {
		return nullptr;
	}
	return pix;
}

OpenGLContext *OpenGLContext::create_offscreen(const OpenGLContext *share)
{
	CGLPixelFormatObj pix = create_cgl_pixel_format();
	if (!pix) {
		fprintf(stderr, "OpenGLContext: failed to choose CGL pixel format\n");
		return nullptr;
	}

	CGLContextObj share_ctx =
		share ? static_cast<CGLContextObj>(share->native_context_) : nullptr;
	CGLContextObj ctx = nullptr;
	if (CGLCreateContext(pix, share_ctx, &ctx) != kCGLNoError || !ctx) {
		CGLReleasePixelFormat(pix);
		fprintf(stderr, "OpenGLContext: failed to create CGL context\n");
		return nullptr;
	}
	CGLReleasePixelFormat(pix);

	OpenGLContext *self = new OpenGLContext();
	self->owned_ = true;
	self->valid_ = true;
	self->major_version_ = 3;
	self->native_context_ = ctx;
	self->owner_thread_ = std::this_thread::get_id();
	return self;
}

OpenGLContext *OpenGLContext::adopt_external(void *native_context,
											 void *native_surface)
{
	if (!native_context) {
		return nullptr;
	}
	OpenGLContext *self = new OpenGLContext();
	self->owned_ = false;
	self->valid_ = true;
	self->native_context_ = native_context;
	self->native_surface_ = native_surface;
	self->owner_thread_ = std::this_thread::get_id();
	return self;
}

void OpenGLContext::destroy_native()
{
	if (owned_ && native_context_) {
		CGLSetCurrentContext(nullptr);
		CGLReleaseContext(static_cast<CGLContextObj>(native_context_));
	}
	native_context_ = nullptr;
}

bool OpenGLContext::make_current()
{
	CGLContextObj ctx = static_cast<CGLContextObj>(native_context_);
	if (!ctx) {
		return false;
	}
	if (!owned_) {
		// External contexts are made current by their owner (app layer).
		return CGLGetCurrentContext() == ctx;
	}
	return CGLSetCurrentContext(ctx) == kCGLNoError;
}

bool OpenGLContext::is_current() const
{
	return native_context_ &&
		   CGLGetCurrentContext() == static_cast<CGLContextObj>(native_context_);
}

bool OpenGLContext::resolve_functions(OpenGLFunctions *out) const
{
	return resolve_open_gl_functions(out, nullptr);
}

#elif defined(_WIN32)

// WGL requires a current HDC to create an enhanced context; a hidden window
// provides one for offscreen rendering.
typedef HGLRC(WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif

static void *wgl_get_proc(const char *name)
{
	void *p = reinterpret_cast<void *>(wglGetProcAddress(name));
	if (!p) {
		static HMODULE gl_module = LoadLibraryA("opengl32.dll");
		p = reinterpret_cast<void *>(GetProcAddress(gl_module, name));
	}
	return p;
}

static LRESULT CALLBACK oak_wgl_wnd_proc(HWND hwnd, UINT msg, WPARAM wp,
										 LPARAM lp)
{
	return DefWindowProc(hwnd, msg, wp, lp);
}

OpenGLContext *OpenGLContext::create_offscreen(const OpenGLContext *share)
{
	static ATOM wnd_class = 0;
	if (!wnd_class) {
		WNDCLASSA wc = {};
		wc.lpfnWndProc = oak_wgl_wnd_proc;
		wc.hInstance = GetModuleHandle(nullptr);
		wc.lpszClassName = "OakOpenGLOffscreen";
		wnd_class = RegisterClassA(&wc);
	}
	HWND hwnd = CreateWindowExA(0, "OakOpenGLOffscreen", "", 0, 0, 0, 1, 1,
								nullptr, nullptr, GetModuleHandle(nullptr),
								nullptr);
	if (!hwnd) {
		return nullptr;
	}
	HDC hdc = GetDC(hwnd);

	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.iLayerType = PFD_MAIN_PLANE;
	int pf = ChoosePixelFormat(hdc, &pfd);
	if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
		DestroyWindow(hwnd);
		return nullptr;
	}

	HGLRC bootstrap = wglCreateContext(hdc);
	if (!bootstrap) {
		DestroyWindow(hwnd);
		return nullptr;
	}
	wglMakeCurrent(hdc, bootstrap);

	HGLRC ctx = bootstrap;
	auto create_attribs = reinterpret_cast<PFN_wglCreateContextAttribsARB>(
		wgl_get_proc("wglCreateContextAttribsARB"));
	if (create_attribs) {
		const int attrs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
			WGL_CONTEXT_MINOR_VERSION_ARB, 2,
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
			0,
		};
		HGLRC share_ctx =
			share ? static_cast<HGLRC>(share->native_context_) : nullptr;
		HGLRC modern = create_attribs(hdc, share_ctx, attrs);
		if (modern) {
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(bootstrap);
			ctx = modern;
		}
	} else if (share && share->native_context_) {
		wglShareLists(static_cast<HGLRC>(share->native_context_), ctx);
	}
	wglMakeCurrent(nullptr, nullptr);

	OpenGLContext *self = new OpenGLContext();
	self->owned_ = true;
	self->valid_ = true;
	self->major_version_ = 3;
	self->native_context_ = ctx;
	self->native_surface_ = hdc;
	self->native_window_ = hwnd;
	self->owner_thread_ = std::this_thread::get_id();
	return self;
}

OpenGLContext *OpenGLContext::adopt_external(void *native_context,
											 void *native_surface)
{
	if (!native_context) {
		return nullptr;
	}
	OpenGLContext *self = new OpenGLContext();
	self->owned_ = false;
	self->valid_ = true;
	self->native_context_ = native_context;
	self->native_surface_ = native_surface;
	self->owner_thread_ = std::this_thread::get_id();
	return self;
}

void OpenGLContext::destroy_native()
{
	if (owned_ && native_context_) {
		wglMakeCurrent(nullptr, nullptr);
		wglDeleteContext(static_cast<HGLRC>(native_context_));
		if (native_window_) {
			DestroyWindow(static_cast<HWND>(native_window_));
		}
	}
	native_context_ = nullptr;
	native_window_ = nullptr;
}

bool OpenGLContext::make_current()
{
	if (!native_context_) {
		return false;
	}
	if (!owned_) {
		return wglGetCurrentContext() ==
			   static_cast<HGLRC>(native_context_);
	}
	return wglMakeCurrent(static_cast<HDC>(native_surface_),
						  static_cast<HGLRC>(native_context_)) == TRUE;
}

bool OpenGLContext::is_current() const
{
	return native_context_ &&
		   wglGetCurrentContext() == static_cast<HGLRC>(native_context_);
}

bool OpenGLContext::resolve_functions(OpenGLFunctions *out) const
{
	return resolve_open_gl_functions(out, wgl_get_proc);
}

#else // Linux: EGL + pbuffer surface

static void *egl_get_proc(const char *name)
{
	return reinterpret_cast<void *>(eglGetProcAddress(name));
}

OpenGLContext *OpenGLContext::create_offscreen(const OpenGLContext *share)
{
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
		fprintf(stderr, "OpenGLContext: failed to initialize EGL\n");
		return nullptr;
	}
	eglBindAPI(EGL_OPENGL_API);

	const EGLint config_attrs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE,
	};
	EGLConfig config = nullptr;
	EGLint num_configs = 0;
	if (!eglChooseConfig(display, config_attrs, &config, 1, &num_configs) ||
		num_configs < 1) {
		eglTerminate(display);
		return nullptr;
	}

	const EGLint pbuffer_attrs[] = {
		EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE,
	};
	EGLSurface surface =
		eglCreatePbufferSurface(display, config, pbuffer_attrs);
	if (surface == EGL_NO_SURFACE) {
		eglTerminate(display);
		return nullptr;
	}

	const EGLint ctx_attrs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 2,
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
		EGL_NONE,
	};
	EGLContext share_ctx = share ?
							   static_cast<EGLContext>(share->native_context_) :
							   EGL_NO_CONTEXT;
	EGLContext ctx =
		eglCreateContext(display, config, share_ctx, ctx_attrs);
	if (ctx == EGL_NO_CONTEXT) {
		// Fall back to a default-version context on drivers without 3.2 core.
		ctx = eglCreateContext(display, config, share_ctx, nullptr);
	}
	if (ctx == EGL_NO_CONTEXT) {
		eglDestroySurface(display, surface);
		eglTerminate(display);
		fprintf(stderr, "OpenGLContext: failed to create EGL context\n");
		return nullptr;
	}

	OpenGLContext *self = new OpenGLContext();
	self->owned_ = true;
	self->valid_ = true;
	self->major_version_ = 3;
	self->native_context_ = ctx;
	self->native_surface_ = surface;
	self->native_display_ = display;
	self->owner_thread_ = std::this_thread::get_id();
	return self;
}

OpenGLContext *OpenGLContext::adopt_external(void *native_context,
											 void *native_surface)
{
	if (!native_context) {
		return nullptr;
	}
	OpenGLContext *self = new OpenGLContext();
	self->owned_ = false;
	self->valid_ = true;
	self->native_context_ = native_context;
	self->native_surface_ = native_surface;
	self->native_display_ = eglGetCurrentDisplay();
	self->owner_thread_ = std::this_thread::get_id();
	return self;
}

void OpenGLContext::destroy_native()
{
	if (owned_ && native_context_) {
		EGLDisplay display = static_cast<EGLDisplay>(native_display_);
		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
					   EGL_NO_CONTEXT);
		eglDestroyContext(display, static_cast<EGLContext>(native_context_));
		if (native_surface_) {
			eglDestroySurface(display,
							  static_cast<EGLSurface>(native_surface_));
		}
	}
	native_context_ = nullptr;
	native_surface_ = nullptr;
}

bool OpenGLContext::make_current()
{
	if (!native_context_) {
		return false;
	}
	if (!owned_) {
		return eglGetCurrentContext() ==
			   static_cast<EGLContext>(native_context_);
	}
	return eglMakeCurrent(static_cast<EGLDisplay>(native_display_),
						  static_cast<EGLSurface>(native_surface_),
						  static_cast<EGLSurface>(native_surface_),
						  static_cast<EGLContext>(native_context_)) == EGL_TRUE;
}

bool OpenGLContext::is_current() const
{
	return native_context_ &&
		   eglGetCurrentContext() == static_cast<EGLContext>(native_context_);
}

bool OpenGLContext::resolve_functions(OpenGLFunctions *out) const
{
	return resolve_open_gl_functions(out, egl_get_proc);
}

#endif

OpenGLContext::~OpenGLContext()
{
	destroy_native();
}

bool OpenGLContext::is_valid() const
{
	return valid_;
}

}

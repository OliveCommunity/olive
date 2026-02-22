/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

***/

#include "platformglcontext.h"

#include <QDebug>

#ifdef Q_OS_LINUX
#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <GL/gl.h>
#include <GL/wgl.h>
#endif

namespace olive {

PlatformGLContext::PlatformGLContext()
    : native_context_(nullptr),
      native_surface_(nullptr),
      display_connection_(nullptr),
      is_current_(false) {
}

PlatformGLContext::~PlatformGLContext() {
    Cleanup();
}

#ifdef Q_OS_LINUX

// Linux implementation using GLX (EGL can be added later for better performance)

bool PlatformGLContext::Create(QOpenGLContext *share_with, const QSurfaceFormat &format) {
    // Get X11 display - use XOpenDisplay to get our own connection
    // This allows the context to be used in any thread
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        qCritical() << "Failed to open X11 display";
        return false;
    }
    display_connection_ = dpy;

    // Choose FBConfig
    int visual_attribs[] = {
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, format.hasAlpha() ? 8 : 0,
        GLX_DEPTH_SIZE, format.depthBufferSize(),
        GLX_STENCIL_SIZE, format.stencilBufferSize(),
        GLX_SAMPLE_BUFFERS, format.samples() > 0 ? 1 : 0,
        GLX_SAMPLES, format.samples(),
        None
    };

    int fbcount;
    GLXFBConfig *fbc = glXChooseFBConfig(dpy, DefaultScreen(dpy), visual_attribs, &fbcount);
    if (!fbc || fbcount == 0) {
        qCritical() << "No matching FBConfig found";
        XCloseDisplay(dpy);
        display_connection_ = nullptr;
        return false;
    }

    // Create pbuffer
    int pbuffer_attribs[] = {
        GLX_PBUFFER_WIDTH, 1,
        GLX_PBUFFER_HEIGHT, 1,
        None
    };

    GLXPbuffer pbuf = glXCreatePbuffer(dpy, fbc[0], pbuffer_attribs);
    if (!pbuf) {
        qCritical() << "Failed to create GLX pbuffer";
        XFree(fbc);
        XCloseDisplay(dpy);
        display_connection_ = nullptr;
        return false;
    }
    native_surface_ = reinterpret_cast<void *>(pbuf);

    // Create context
    GLXContext share_ctx = None;
    if (share_with) {
        share_ctx = reinterpret_cast<GLXContext>(share_with->nativeInterface<QNativeInterface::QGLXContext>()->context());
    }

    // Try to create context with version
    GLXContext ctx = nullptr;
    typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 
        (glXCreateContextAttribsARBProc)glXGetProcAddress((const GLubyte*)"glXCreateContextAttribsARB");

    if (glXCreateContextAttribsARB) {
        int context_attribs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, format.majorVersion(),
            GLX_CONTEXT_MINOR_VERSION_ARB, format.minorVersion(),
            GLX_CONTEXT_PROFILE_MASK_ARB, 
                format.profile() == QSurfaceFormat::CoreProfile ? GLX_CONTEXT_CORE_PROFILE_BIT_ARB :
                format.profile() == QSurfaceFormat::CompatibilityProfile ? GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB :
                GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
            None
        };
        ctx = glXCreateContextAttribsARB(dpy, fbc[0], share_ctx, True, context_attribs);
    }

    // Fallback to legacy context creation
    if (!ctx) {
        ctx = glXCreateNewContext(dpy, fbc[0], GLX_RGBA_TYPE, share_ctx, True);
    }

    XFree(fbc);

    if (!ctx) {
        qCritical() << "Failed to create GLX context";
        glXDestroyPbuffer(dpy, pbuf);
        XCloseDisplay(dpy);
        display_connection_ = nullptr;
        native_surface_ = nullptr;
        return false;
    }

    native_context_ = reinterpret_cast<void *>(ctx);
    return true;
}

bool PlatformGLContext::MakeCurrent() {
    if (!native_context_ || !native_surface_ || !display_connection_) {
        return false;
    }
    Display *dpy = static_cast<Display *>(display_connection_);
    GLXContext ctx = reinterpret_cast<GLXContext>(native_context_);
    GLXPbuffer pbuf = reinterpret_cast<GLXPbuffer>(native_surface_);
    
    Bool result = glXMakeContextCurrent(dpy, pbuf, pbuf, ctx);
    if (result) {
        is_current_ = true;
    }
    return result;
}

void PlatformGLContext::DoneCurrent() {
    if (!display_connection_) return;
    Display *dpy = static_cast<Display *>(display_connection_);
    glXMakeContextCurrent(dpy, None, None, nullptr);
    is_current_ = false;
}

void PlatformGLContext::SwapBuffers() {
    // Pbuffers don't need swapping
}

void PlatformGLContext::Cleanup() {
    if (display_connection_) {
        Display *dpy = static_cast<Display *>(display_connection_);
        
        if (native_context_) {
            GLXContext ctx = reinterpret_cast<GLXContext>(native_context_);
            if (is_current_) {
                glXMakeContextCurrent(dpy, None, None, nullptr);
                is_current_ = false;
            }
            glXDestroyContext(dpy, ctx);
            native_context_ = nullptr;
        }
        
        if (native_surface_) {
            GLXPbuffer pbuf = reinterpret_cast<GLXPbuffer>(native_surface_);
            glXDestroyPbuffer(dpy, pbuf);
            native_surface_ = nullptr;
        }
        
        XCloseDisplay(dpy);
        display_connection_ = nullptr;
    }
}

bool PlatformGLContext::IsSupported() {
    // Check if GLX is available
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return false;
    
    int glx_major, glx_minor;
    Bool result = glXQueryVersion(dpy, &glx_major, &glx_minor);
    XCloseDisplay(dpy);
    
    return result && (glx_major > 1 || (glx_major == 1 && glx_minor >= 3));
}

#elif defined(Q_OS_WIN)

// Windows implementation using WGL with pbuffer

bool PlatformGLContext::Create(QOpenGLContext *share_with, const QSurfaceFormat &format) {
    // Create a hidden window for WGL context creation
    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"OakGLHiddenWindow";
    RegisterClass(&wc);

    HWND hidden_wnd = CreateWindowEx(
        0, L"OakGLHiddenWindow", L"Hidden",
        WS_OVERLAPPEDWINDOW, 0, 0, 1, 1,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!hidden_wnd) {
        qCritical() << "Failed to create hidden window";
        return false;
    }

    // Get DC
    HDC hdc = GetDC(hidden_wnd);
    if (!hdc) {
        DestroyWindow(hidden_wnd);
        return false;
    }

    // Set pixel format
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = format.depthBufferSize();
    pfd.cStencilBits = format.stencilBufferSize();

    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        ReleaseDC(hidden_wnd, hdc);
        DestroyWindow(hidden_wnd);
        return false;
    }

    // Create temporary context to get wglCreateContextAttribsARB
    HGLRC temp_ctx = wglCreateContext(hdc);
    if (!temp_ctx) {
        ReleaseDC(hidden_wnd, hdc);
        DestroyWindow(hidden_wnd);
        return false;
    }

    wglMakeCurrent(hdc, temp_ctx);

    // Get extension function
    typedef HGLRC (WINAPI *wglCreateContextAttribsARBProc)(HDC, HGLRC, const int*);
    wglCreateContextAttribsARBProc wglCreateContextAttribsARB = 
        (wglCreateContextAttribsARBProc)wglGetProcAddress("wglCreateContextAttribsARB");

    // Get share context handle from Qt's context
    HGLRC share_hglrc = nullptr;
    if (share_with) {
        // Get native handles using wglGetCurrentContext since Qt 6 WGL interface varies
        HDC current_dc = wglGetCurrentDC();
        HGLRC current_ctx = wglGetCurrentContext();
        if (current_ctx) {
            share_hglrc = current_ctx;
        }
    }

    // Create real context
    HGLRC real_ctx = nullptr;
    if (wglCreateContextAttribsARB) {
        int attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, format.majorVersion(),
            WGL_CONTEXT_MINOR_VERSION_ARB, format.minorVersion(),
            WGL_CONTEXT_PROFILE_MASK_ARB,
                format.profile() == QSurfaceFormat::CoreProfile ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB :
                WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
            0
        };
        real_ctx = wglCreateContextAttribsARB(hdc, share_hglrc, attribs);
    }

    // Cleanup temp context
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(temp_ctx);

    if (!real_ctx) {
        qCritical() << "Failed to create WGL context";
        ReleaseDC(hidden_wnd, hdc);
        DestroyWindow(hidden_wnd);
        return false;
    }

    // We keep the hidden window and DC for the context to work
    // Store them as the "surface"
    display_connection_ = hdc;
    native_surface_ = hidden_wnd;
    native_context_ = real_ctx;

    return true;
}

bool PlatformGLContext::MakeCurrent() {
    if (!native_context_ || !display_connection_) return false;
    HDC hdc = static_cast<HDC>(display_connection_);
    HGLRC ctx = reinterpret_cast<HGLRC>(native_context_);
    BOOL result = wglMakeCurrent(hdc, ctx);
    if (result) is_current_ = true;
    return result;
}

void PlatformGLContext::DoneCurrent() {
    wglMakeCurrent(nullptr, nullptr);
    is_current_ = false;
}

void PlatformGLContext::SwapBuffers() {
    if (display_connection_) {
        ::SwapBuffers(static_cast<HDC>(display_connection_));
    }
}

void PlatformGLContext::Cleanup() {
    if (native_context_) {
        if (is_current_) {
            wglMakeCurrent(nullptr, nullptr);
            is_current_ = false;
        }
        wglDeleteContext(reinterpret_cast<HGLRC>(native_context_));
        native_context_ = nullptr;
    }
    if (display_connection_) {
        HWND wnd = static_cast<HWND>(native_surface_);
        ReleaseDC(wnd, static_cast<HDC>(display_connection_));
        display_connection_ = nullptr;
    }
    if (native_surface_) {
        DestroyWindow(static_cast<HWND>(native_surface_));
        native_surface_ = nullptr;
    }
}

bool PlatformGLContext::IsSupported() {
    return true;  // WGL is always available on Windows
}

#else

// macOS and other platforms - fallback to Qt's implementation

bool PlatformGLContext::Create(QOpenGLContext *share_with, const QSurfaceFormat &format) {
    // On macOS, we need to use CGL or NSOpenGL
    // For now, return false to indicate platform-specific implementation needed
    qWarning() << "PlatformGLContext not implemented for this platform, using Qt fallback";
    return false;
}

bool PlatformGLContext::MakeCurrent() { return false; }
void PlatformGLContext::DoneCurrent() {}
void PlatformGLContext::SwapBuffers() {}
void PlatformGLContext::Cleanup() {}
bool PlatformGLContext::IsSupported() { return false; }

#endif

}  // namespace olive

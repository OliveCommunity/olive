/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

***/

#ifndef PLATFORMGLCONTEXT_H
#define PLATFORMGLCONTEXT_H

// GLEW provides OpenGL function declarations
#include <GL/glew.h>

#include <QOpenGLContext>
#include <QSurfaceFormat>

namespace olive {

/**
 * @brief Platform-specific OpenGL context for offscreen rendering in any thread.
 * 
 * This class provides a cross-platform way to create OpenGL contexts and surfaces
 * that can be created and used in any thread (not just GUI thread), bypassing
 * Qt 6's QOffscreenSurface thread affinity restrictions.
 * 
 * Supported platforms:
 * - Linux: GLX or EGL (EGL preferred for better thread support)
 * - Windows: WGL with pbuffer
 * - macOS: CGL (limited support)
 */
class PlatformGLContext {
public:
    PlatformGLContext();
    ~PlatformGLContext();

    /**
     * @brief Create a platform-specific OpenGL context.
     * @param share_with Context to share resources with (can be nullptr)
     * @param format Desired surface format
     * @return true on success
     */
    bool Create(QOpenGLContext *share_with, const QSurfaceFormat &format);

    /**
     * @brief Make this context current.
     * @return true on success
     */
    bool MakeCurrent();

    /**
     * @brief Done with current context.
     */
    void DoneCurrent();

    /**
     * @brief Swap buffers (if applicable).
     */
    void SwapBuffers();

    /**
     * @brief Get the native OpenGL context handle.
     */
    void *native_context() const { return native_context_; }

    /**
     * @brief Get the native surface/drawable handle.
     */
    void *native_surface() const { return native_surface_; }

    /**
     * @brief Check if this platform supports offscreen rendering in any thread.
     */
    static bool IsSupported();

private:
    void *native_context_;
    void *native_surface_;
    void *display_connection_;  // Display/DC/etc
    bool is_current_;

    void Cleanup();
};

}  // namespace olive

#endif  // PLATFORMGLCONTEXT_H

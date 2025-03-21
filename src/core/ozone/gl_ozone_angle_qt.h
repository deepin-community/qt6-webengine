// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef GL_OZONE_ANGLE_QT_H
#define GL_OZONE_ANGLE_QT_H

#if defined(USE_OZONE)
#include "ui/ozone/common/gl_ozone_egl.h"

namespace ui {

class GLOzoneANGLEQt : public GLOzoneEGL
{
public:
    bool InitializeStaticGLBindings(const gl::GLImplementationParts &implementation) override;
    bool InitializeExtensionSettingsOneOffPlatform(gl::GLDisplay *display) override;
    scoped_refptr<gl::GLSurface> CreateViewGLSurface(gl::GLDisplay *display,
                                                     gfx::AcceleratedWidget window) override;
    scoped_refptr<gl::GLSurface> CreateOffscreenGLSurface(gl::GLDisplay *display,
                                                          const gfx::Size &size) override;
    bool CanImportNativePixmap() override;
    std::unique_ptr<NativePixmapGLBinding>
    ImportNativePixmap(scoped_refptr<gfx::NativePixmap> pixmap, gfx::BufferFormat plane_format,
                       gfx::BufferPlane plane, gfx::Size plane_size,
                       const gfx::ColorSpace &color_space, GLenum target,
                       GLuint texture_id) override;

protected:
    // Returns native platform display handle. This is used to obtain the EGL
    // display connection for the native display.
    gl::EGLDisplayPlatform GetNativeDisplay() override;

    // Sets up GL bindings for the native surface.
    bool LoadGLES2Bindings(const gl::GLImplementationParts &implementation) override;
};

} // namespace ui

#endif // defined(USE_OZONE)

#endif // GL_OZONE_ANGLE_QT_H

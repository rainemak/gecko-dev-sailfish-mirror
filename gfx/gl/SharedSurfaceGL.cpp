/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 4; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceGL.h"

#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLReadTexImageHelper.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/layers/TextureForwarder.h"
#include "ScopedGLHelpers.h"

namespace mozilla {
namespace gl {

using gfx::IntSize;

/*static*/
UniquePtr<SharedSurface_Basic> SharedSurface_Basic::Create(
    const SharedSurfaceDesc& desc) {
  auto fb = MozFramebuffer::Create(desc.gl, desc.size, 0, false);
  if (!fb) return nullptr;

  return AsUnique(new SharedSurface_Basic(desc, std::move(fb)));
}

SharedSurface_Basic::SharedSurface_Basic(const SharedSurfaceDesc& desc,
                                         UniquePtr<MozFramebuffer>&& fb)
    : SharedSurface(desc, std::move(fb)),
      mTex(0),
      mOwnsTex(false),
      mFB(0)
{
}

Maybe<layers::SurfaceDescriptor> SharedSurface_Basic::ToSurfaceDescriptor() {
  return Nothing();
}

////////////////////////////////////////////////////////////////////////

SurfaceFactory_Basic::SurfaceFactory_Basic(GLContext& gl)
    : SurfaceFactory({&gl, SharedSurfaceType::Basic,
                      layers::TextureType::Unknown, true}, nullptr, layers::TextureFlags(0)) {}

SurfaceFactory_Basic::SurfaceFactory_Basic(GLContext* gl,
                                           const layers::TextureFlags& flags)
    : SurfaceFactory({gl, SharedSurfaceType::Basic,
                      layers::TextureType::Unknown, true}, nullptr, flags) {}

SurfaceFactory_GL::SurfaceFactory_GL(GLContext& gl)
    : SurfaceFactory({&gl, SharedSurfaceType::Basic,
                      layers::TextureType::Unknown, true}, nullptr, layers::TextureFlags(0)) {}

SurfaceFactory_GL::SurfaceFactory_GL(GLContext* gl,
                                           const layers::TextureFlags& flags)
    : SurfaceFactory({gl, SharedSurfaceType::Basic,
                      layers::TextureType::Unknown, true}, nullptr, flags) {}

////////////////////////////////////////////////////////////////////////

/*static*/
UniquePtr<SharedSurface_Basic> SharedSurface_Basic::Create(
    GLContext* gl, const IntSize& size) {
  UniquePtr<SharedSurface_Basic> ret;
  gl->MakeCurrent();

  GLContext::LocalErrorScope localError(*gl);
  GLuint tex = CreateTexture(*gl, size);

  GLenum err = localError.GetError();
  MOZ_ASSERT_IF(err != LOCAL_GL_NO_ERROR, err == LOCAL_GL_OUT_OF_MEMORY);
  if (err) {
    gl->fDeleteTextures(1, &tex);
    return ret;
  }

  bool ownsTex = true;
  ret.reset(new SharedSurface_Basic(gl, size, tex, ownsTex));
  return ret;
}

SharedSurface_Basic::SharedSurface_Basic(GLContext* gl, const IntSize& size,
                                         GLuint tex,
                                         bool ownsTex)
    : SharedSurface(SharedSurfaceType::Basic, gl, size, true),
      mTex(tex),
      mOwnsTex(ownsTex),
      mFB(0) {
  gl->MakeCurrent();
  gl->fGenFramebuffers(1, &mFB);

  ScopedBindFramebuffer autoFB(gl, mFB);
  gl->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER, LOCAL_GL_COLOR_ATTACHMENT0,
                             LOCAL_GL_TEXTURE_2D, mTex, 0);

  DebugOnly<GLenum> status = gl->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
  MOZ_ASSERT(status == LOCAL_GL_FRAMEBUFFER_COMPLETE);
}

SharedSurface_Basic::~SharedSurface_Basic() {
  if (!mDesc.gl || !mDesc.gl->MakeCurrent()) return;

  if (mFB) mDesc.gl->fDeleteFramebuffers(1, &mFB);

  if (mOwnsTex) mDesc.gl->fDeleteTextures(1, &mTex);
}

}  // namespace gl
}  // namespace mozilla

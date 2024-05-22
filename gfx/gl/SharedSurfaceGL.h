/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 4; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SHARED_SURFACE_GL_H_
#define SHARED_SURFACE_GL_H_

#include "SharedSurface.h"

namespace mozilla {
namespace gl {

class MozFramebuffer;

// For readback and bootstrapping:
class SharedSurface_Basic final : public SharedSurface {
 public:
  static UniquePtr<SharedSurface_Basic> Create(const SharedSurfaceDesc&);

  static UniquePtr<SharedSurface_Basic> Create(GLContext* gl,
                                               const gfx::IntSize& size);

  static SharedSurface_Basic* Cast(SharedSurface* surf) {
    MOZ_ASSERT(surf->mType == SharedSurfaceType::Basic);

    return (SharedSurface_Basic*)surf;
  }

 private:
  SharedSurface_Basic(const SharedSurfaceDesc& desc,
                      UniquePtr<MozFramebuffer>&& fb);

 public:
  Maybe<layers::SurfaceDescriptor> ToSurfaceDescriptor() override;

 protected:
  const GLuint mTex;
  const bool mOwnsTex;
  GLuint mFB;

  SharedSurface_Basic(GLContext* gl, const gfx::IntSize& size,
                      GLuint tex, bool ownsTex);

 public:
  virtual ~SharedSurface_Basic();

  virtual void LockProdImpl() override {}
  virtual void UnlockProdImpl() override {}

  virtual void ProducerAcquireImpl() override {}
  virtual void ProducerReleaseImpl() override {}

  virtual GLuint ProdTexture() override { return mTex; }
};

class SurfaceFactory_Basic final : public SurfaceFactory {
 public:
  explicit SurfaceFactory_Basic(GLContext& gl);
  explicit SurfaceFactory_Basic(GLContext* gl,
                                const layers::TextureFlags& flags);

  virtual UniquePtr<SharedSurface> CreateSharedImpl(
      const SharedSurfaceDesc& desc) override {
    return SharedSurface_Basic::Create(desc);
  }
};

// We need to create SharedSurface_Basic instances differently depending
// on whether we're using them for the WebView or WebGL rendering
// SurfaceFactory_Basic creates the WebGL variant
// SurfaceFactory_GL creates the WebView variant
class SurfaceFactory_GL final : public SurfaceFactory {
 public:
  explicit SurfaceFactory_GL(GLContext& gl);
  explicit SurfaceFactory_GL(GLContext* gl,
                                const layers::TextureFlags& flags);

  virtual UniquePtr<SharedSurface> CreateSharedImpl(
      const SharedSurfaceDesc& desc) override {
    return SharedSurface_Basic::Create(mDesc.gl, desc.size);
  }
};

}  // namespace gl
}  // namespace mozilla

#endif  // SHARED_SURFACE_GL_H_

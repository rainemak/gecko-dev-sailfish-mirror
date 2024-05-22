/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 4; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* GLScreenBuffer is the abstraction for the "default framebuffer" used
 * by an offscreen GLContext. Since it's only for offscreen GLContext's,
 * it's only useful for things like WebGL, and is NOT used by the
 * compositor's GLContext. Remember that GLContext provides an abstraction
 * so that even if you want to draw to the 'screen', even if that's not
 * actually the screen, just draw to 0. This GLScreenBuffer class takes the
 * logic handling out of GLContext.
 */

#ifndef SCREEN_BUFFER_H_
#define SCREEN_BUFFER_H_

#include "GLContextTypes.h"
#include "GLDefs.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/UniquePtr.h"
#include "SharedSurface.h"
#include "SurfaceTypes.h"

#include <queue>
#include <memory>

namespace mozilla {
namespace layers {
class KnowsCompositor;
class LayersIPCChannel;
class SharedSurfaceTextureClient;
enum class LayersBackend : int8_t;
}  // namespace layers

namespace gl {

class GLContext;
class ShSurfHandle;
class SharedSurface;
class SurfaceFactory;
class SwapChain;

class SwapChainPresenter final {
  friend class SwapChain;

  SwapChain* mSwapChain;
  std::shared_ptr<SharedSurface> mBackBuffer;

 public:
  explicit SwapChainPresenter(SwapChain& swapChain);
  ~SwapChainPresenter();

  const auto& BackBuffer() const { return mBackBuffer; }

  std::shared_ptr<SharedSurface> SwapBackBuffer(std::shared_ptr<SharedSurface>);
  GLuint Fb() const;
};

// -

class SwapChain final {
  friend class SwapChainPresenter;

 public:
  UniquePtr<SurfaceFactory> mFactory;
  bool mPreserve = false;

 private:
  std::queue<std::shared_ptr<SharedSurface>> mPool;
  std::shared_ptr<SharedSurface> mFrontBuffer;

 public:
  std::shared_ptr<SharedSurface>
      mPrevFrontBuffer;  // Hold this ref while it's in-flight.
 private:
  SwapChainPresenter* mPresenter = nullptr;

 public:
  SwapChain();
  virtual ~SwapChain();

  void ClearPool();
  const auto& FrontBuffer() const { return mFrontBuffer; }
  UniquePtr<SwapChainPresenter> Acquire(const gfx::IntSize&);

  const gfx::IntSize& Size() const;
  const gfx::IntSize& OffscreenSize() const;
  bool Resize(const gfx::IntSize & size);
  bool PublishFrame(const gfx::IntSize& size) { return Swap(size); }
  void Morph(UniquePtr<SurfaceFactory> newFactory);

private:
  // Returns false on error or inability to resize.
  bool Swap(const gfx::IntSize& size);
};

class ReadBuffer {
 public:
  // Infallible, always non-null.
  static UniquePtr<ReadBuffer> Create(GLContext* gl, SharedSurface* surf);

 protected:
  GLContext* const mGL;

 public:
  const GLuint mFB;

 protected:
  // mFB has the following attachments:
  const GLuint mDepthRB;
  const GLuint mStencilRB;
  // note no mColorRB here: this is provided by mSurf.
  SharedSurface* mSurf;

  ReadBuffer(GLContext* gl, GLuint fb, GLuint depthRB, GLuint stencilRB,
             SharedSurface* surf)
      : mGL(gl),
        mFB(fb),
        mDepthRB(depthRB),
        mStencilRB(stencilRB),
        mSurf(surf) {}

 public:
  virtual ~ReadBuffer();

  // Cannot attach a surf of a different AttachType or Size than before.
  void Attach(SharedSurface* surf);

  const gfx::IntSize& Size() const;

  SharedSurface* SharedSurf() const { return mSurf; }
};

class GLScreenBuffer final {
 public:
  // Infallible.
  static UniquePtr<GLScreenBuffer> Create(GLContext* gl, const gfx::IntSize& size);

 private:
  GLContext* const mGL;  // Owns us.

 private:
  UniquePtr<SurfaceFactory> mFactory;

  RefPtr<layers::SharedSurfaceTextureClient> mBack;
  RefPtr<layers::SharedSurfaceTextureClient> mFront;

  UniquePtr<ReadBuffer> mRead;

  // Below are the parts that help us pretend to be framebuffer 0:
  GLuint mUserDrawFB;
  GLuint mUserReadFB;
  GLuint mInternalDrawFB;
  GLuint mInternalReadFB;

#ifdef DEBUG
  bool mInInternalMode_DrawFB;
  bool mInInternalMode_ReadFB;
#endif

  GLScreenBuffer(GLContext* gl, UniquePtr<SurfaceFactory> factory);

 public:
  virtual ~GLScreenBuffer();

  const auto& Factory() const { return mFactory; }
  const auto& Front() const { return mFront; }

  SharedSurface* SharedSurf() const {
    MOZ_ASSERT(mRead);
    return mRead->SharedSurf();
  }

 private:
  GLuint DrawFB() const { return ReadFB(); }
  GLuint ReadFB() const { return mRead->mFB; }

 public:
  void DeletingFB(GLuint fb);

  const gfx::IntSize& Size() const {
    MOZ_ASSERT(mRead);
    return mRead->Size();
  }

  bool IsReadBufferReady() const { return mRead.get() != nullptr; }

  // Morph changes the factory used to create surfaces.
  void Morph(UniquePtr<SurfaceFactory> newFactory);

 private:
  // Returns false on error or inability to resize.
  bool Swap(const gfx::IntSize& size);

 public:
  bool PublishFrame(const gfx::IntSize& size) { return Swap(size); }

  bool Resize(const gfx::IntSize& size);

 private:
  bool Attach(SharedSurface* surf, const gfx::IntSize& size);

 public:
  /* `fb` in these functions is the framebuffer the GLContext is hoping to
   * bind. When this is 0, we intercept the call and bind our own
   * framebuffers. As a client of these functions, just bind 0 when you want
   * to draw to the default framebuffer/'screen'.
   */
  void BindFB(GLuint fb);
  void BindDrawFB(GLuint fb);
  void BindReadFB(GLuint fb);
};

}  // namespace gl
}  // namespace mozilla

#endif  // SCREEN_BUFFER_H_

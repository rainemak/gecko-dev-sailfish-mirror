/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 4; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLScreenBuffer.h"

#include "CompositorTypes.h"
#include "GLContext.h"
#include "gfx2DGlue.h"
#include "MozFramebuffer.h"
#include "SharedSurface.h"

#include <cstring>
#include "mozilla/StaticPrefs_webgl.h"
#include "GLBlitHelper.h"
#include "GLReadTexImageHelper.h"
#include "SharedSurfaceEGL.h"
#include "SharedSurfaceGL.h"
#include "ScopedGLHelpers.h"
#include "gfx2DGlue.h"
#include "../layers/ipc/ShadowLayers.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/layers/TextureClientSharedSurface.h"

#include <unistd.h>
#include <stdio.h>

namespace mozilla::gl {

// -
// SwapChainPresenter

// We need to apply pooling on Android because of the AndroidSurface slow
// destructor bugs. They cause a noticeable performance hit. See bug
// #1646073.
static constexpr size_t kPoolSize =
#if defined(MOZ_WIDGET_ANDROID)
    4;
#else
    0;
#endif

UniquePtr<SwapChainPresenter> SwapChain::Acquire(const gfx::IntSize& size) {
  MOZ_ASSERT(mFactory);

  std::shared_ptr<SharedSurface> surf;
  if (!mPool.empty() && mPool.front()->mDesc.size != size) {
    mPool = {};
  }
  if (kPoolSize && mPool.size() == kPoolSize) {
    surf = mPool.front();
    mPool.pop();
  }
  if (!surf) {
    auto uniquePtrSurf = mFactory->CreateShared(size);
    if (!uniquePtrSurf) return nullptr;
    surf.reset(uniquePtrSurf.release());
  }
  mPool.push(surf);
  while (mPool.size() > kPoolSize) {
    mPool.pop();
  }

  auto ret = MakeUnique<SwapChainPresenter>(*this);
  const auto old = ret->SwapBackBuffer(surf);
  MOZ_ALWAYS_TRUE(!old);
  return ret;
}

void SwapChain::ClearPool() {
  mPool = {};
  mPrevFrontBuffer = nullptr;
}

// -

SwapChainPresenter::SwapChainPresenter(SwapChain& swapChain)
    : mSwapChain(&swapChain) {
  MOZ_RELEASE_ASSERT(mSwapChain->mPresenter == nullptr);
  mSwapChain->mPresenter = this;
}

SwapChainPresenter::~SwapChainPresenter() {
  if (!mSwapChain) return;
  MOZ_RELEASE_ASSERT(mSwapChain->mPresenter == this);
  mSwapChain->mPresenter = nullptr;

  auto newFront = SwapBackBuffer(nullptr);
  if (newFront) {
    mSwapChain->mPrevFrontBuffer = mSwapChain->mFrontBuffer;
    mSwapChain->mFrontBuffer = newFront;
  }
}

std::shared_ptr<SharedSurface> SwapChainPresenter::SwapBackBuffer(
    std::shared_ptr<SharedSurface> back) {
  if (mBackBuffer) {
    mBackBuffer->UnlockProd();
    mBackBuffer->ProducerRelease();
    mBackBuffer->Commit();
  }
  auto old = mBackBuffer;
  mBackBuffer = back;
  if (mBackBuffer) {
    mBackBuffer->WaitForBufferOwnership();
    mBackBuffer->ProducerAcquire();
    mBackBuffer->LockProd();
  }
  return old;
}

GLuint SwapChainPresenter::Fb() const {
  if (!mBackBuffer) return 0;
  const auto& fb = mBackBuffer->mFb;
  if (!fb) return 0;
  return fb->mFB;
}

// -
// SwapChain

SwapChain::SwapChain() = default;

SwapChain::~SwapChain() {
  if (mPresenter) {
    // Out of order destruction, but ok.
    (void)mPresenter->SwapBackBuffer(nullptr);
    mPresenter->mSwapChain = nullptr;
    mPresenter = nullptr;
  }
}

void SwapChain::Morph(UniquePtr<SurfaceFactory> newFactory) {
  MOZ_RELEASE_ASSERT(newFactory, "newFactory must not be null");
  mFactory = std::move(newFactory);
}

const gfx::IntSize& SwapChain::Size() const {
  return mFrontBuffer->mFb->mSize;
}

const gfx::IntSize& SwapChain::OffscreenSize() const {
  return mPresenter->mBackBuffer->mFb->mSize;
}

bool SwapChain::Resize(const gfx::IntSize& size) {
  UniquePtr<SharedSurface> newBack =
      mFactory->CreateShared(size);
  if (!newBack) return false;

  if (mPresenter->mBackBuffer) mPresenter->mBackBuffer->ProducerRelease();

  mPresenter->mBackBuffer.reset(newBack.release());

  mPresenter->mBackBuffer->ProducerAcquire();

  return true;
}

bool SwapChain::Swap(const gfx::IntSize& size) {
  mFrontBuffer = mPresenter->SwapBackBuffer(mFrontBuffer);

  return true;
}

////////////////////////////////////////////////////////////////////////
// GLScreenBuffer

UniquePtr<GLScreenBuffer> GLScreenBuffer::Create(GLContext* gl,
                                                 const gfx::IntSize& size) {
  UniquePtr<GLScreenBuffer> ret;

  layers::TextureFlags flags = layers::TextureFlags::ORIGIN_BOTTOM_LEFT;

  UniquePtr<SurfaceFactory> factory =
      MakeUnique<SurfaceFactory_GL>(*gl);

  ret.reset(new GLScreenBuffer(gl, std::move(factory)));
  return ret;
}

GLScreenBuffer::GLScreenBuffer(GLContext* gl, UniquePtr<SurfaceFactory> factory)
    : mGL(gl),
      mFactory(std::move(factory)),
      mUserDrawFB(0),
      mUserReadFB(0),
      mInternalDrawFB(0),
      mInternalReadFB(0)
#ifdef DEBUG
      ,
      mInInternalMode_DrawFB(true),
      mInInternalMode_ReadFB(true)
#endif
{
}

GLScreenBuffer::~GLScreenBuffer() {
  mFactory = nullptr;
  mRead = nullptr;

  if (!mBack) return;

  // Detach mBack cleanly.
  mBack->Surf()->ProducerRelease();
}

void GLScreenBuffer::BindFB(GLuint fb) {
  GLuint drawFB = DrawFB();
  GLuint readFB = ReadFB();

  mUserDrawFB = fb;
  mUserReadFB = fb;
  mInternalDrawFB = (fb == 0) ? drawFB : fb;
  mInternalReadFB = (fb == 0) ? readFB : fb;

  if (mInternalDrawFB == mInternalReadFB) {
    mGL->raw_fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mInternalDrawFB);
  } else {
    MOZ_ASSERT(mGL->IsSupported(GLFeature::split_framebuffer));
    mGL->raw_fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER_EXT, mInternalDrawFB);
    mGL->raw_fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER_EXT, mInternalReadFB);
  }

#ifdef DEBUG
  mInInternalMode_DrawFB = false;
  mInInternalMode_ReadFB = false;
#endif
}

void GLScreenBuffer::BindDrawFB(GLuint fb) {
  MOZ_ASSERT(mGL->IsSupported(GLFeature::split_framebuffer));

  GLuint drawFB = DrawFB();
  mUserDrawFB = fb;
  mInternalDrawFB = (fb == 0) ? drawFB : fb;

  mGL->raw_fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER_EXT, mInternalDrawFB);

#ifdef DEBUG
  mInInternalMode_DrawFB = false;
#endif
}

void GLScreenBuffer::BindReadFB(GLuint fb) {
  MOZ_ASSERT(mGL->IsSupported(GLFeature::split_framebuffer));

  GLuint readFB = ReadFB();
  mUserReadFB = fb;
  mInternalReadFB = (fb == 0) ? readFB : fb;

  mGL->raw_fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER_EXT, mInternalReadFB);

#ifdef DEBUG
  mInInternalMode_ReadFB = false;
#endif
}

void GLScreenBuffer::DeletingFB(GLuint fb) {
  if (fb == mInternalDrawFB) {
    mInternalDrawFB = 0;
    mUserDrawFB = 0;
  }
  if (fb == mInternalReadFB) {
    mInternalReadFB = 0;
    mUserReadFB = 0;
  }
}

void GLScreenBuffer::Morph(UniquePtr<SurfaceFactory> newFactory) {
  MOZ_RELEASE_ASSERT(newFactory, "newFactory must not be null");
  mFactory = std::move(newFactory);
}

bool GLScreenBuffer::Attach(SharedSurface* surf, const gfx::IntSize& size) {
  ScopedBindFramebuffer autoFB(mGL);

  const bool readNeedsUnlock = (mRead && SharedSurf());
  if (readNeedsUnlock) {
    SharedSurf()->UnlockProd();
  }

  surf->LockProd();

  if (mRead && size == Size()) {
    // Same size, same type, ready for reuse!
    mRead->Attach(surf);
  } else {
    UniquePtr<ReadBuffer> read = ReadBuffer::Create(mFactory->mDesc.gl, surf);

    if (!read) {
      surf->UnlockProd();
      if (readNeedsUnlock) {
        SharedSurf()->LockProd();
      }
      return false;
    }

    mRead = std::move(read);
  }

  // Check that we're all set up.
  MOZ_ASSERT(SharedSurf() == surf);

  return true;
}

bool GLScreenBuffer::Swap(const gfx::IntSize& size) {
  RefPtr<layers::SharedSurfaceTextureClient> newBack =
      mFactory->NewTexClient(size);
  if (!newBack) return false;

  // In the case of DXGL interop, the new surface needs to be acquired before
  // it is attached so that the interop surface is locked, which populates
  // the GL renderbuffer. This results in the renderbuffer being ready and
  // attachment to framebuffer succeeds in Attach() call.
  newBack->Surf()->ProducerAcquire();

  if (!Attach(newBack->Surf(), size)) {
    newBack->Surf()->ProducerRelease();
    return false;
  }
  // Attach was successful.

  mFront = mBack;
  mBack = newBack;

  // XXX: We would prefer to fence earlier on platforms that don't need
  // the full ProducerAcquire/ProducerRelease semantics, so that the fence
  // doesn't include the copy operation. Unfortunately, the current API
  // doesn't expose a good way to do that.
  if (mFront) {
    mFront->Surf()->ProducerRelease();
  }

  return true;
}

bool GLScreenBuffer::Resize(const gfx::IntSize& size) {
  RefPtr<layers::SharedSurfaceTextureClient> newBack =
      mFactory->NewTexClient(size);
  if (!newBack) return false;

  if (!Attach(newBack->Surf(), size)) return false;

  if (mBack) mBack->Surf()->ProducerRelease();

  mBack = newBack;

  mBack->Surf()->ProducerAcquire();

  return true;
}

////////////////////////////////////////////////////////////////////////
// ReadBuffer

UniquePtr<ReadBuffer> ReadBuffer::Create(GLContext* gl, SharedSurface* surf) {
  MOZ_ASSERT(surf);

  GLContext::LocalErrorScope localError(*gl);

  GLuint colorTex = 0;
  GLenum target = 0;

  colorTex = surf->ProdTexture();
  target = surf->ProdTextureTarget();
  MOZ_ASSERT(colorTex);

  GLuint fb = 0;
  gl->fGenFramebuffers(1, &fb);
  gl->AttachBuffersToFB(colorTex, 0, 0, 0, fb, target);

  UniquePtr<ReadBuffer> ret(new ReadBuffer(gl, fb, 0, 0, surf));

  GLenum err = localError.GetError();
  MOZ_ASSERT_IF(err != LOCAL_GL_NO_ERROR, err == LOCAL_GL_OUT_OF_MEMORY);
  if (err) return nullptr;

  const bool needsAcquire = !surf->IsProducerAcquired();
  if (needsAcquire) {
    surf->ProducerReadAcquire();
  }
  const bool isComplete = gl->IsFramebufferComplete(fb);
  if (needsAcquire) {
    surf->ProducerReadRelease();
  }

  if (!isComplete) return nullptr;

  return ret;
}

ReadBuffer::~ReadBuffer() {
  if (!mGL->MakeCurrent()) return;

  GLuint fb = mFB;
  GLuint rbs[] = {
      mDepthRB,
      (mStencilRB != mDepthRB) ? mStencilRB
                               : 0,  // Don't double-delete DEPTH_STENCIL RBs.
  };

  mGL->fDeleteFramebuffers(1, &fb);
  mGL->fDeleteRenderbuffers(2, rbs);
}

void ReadBuffer::Attach(SharedSurface* surf) {
  MOZ_ASSERT(surf && mSurf);
  MOZ_ASSERT(surf->mSize == mSurf->mSize);

  // Nothing else is needed for AttachType Screen.
  GLuint colorTex = 0;
  GLenum target = 0;

  colorTex = surf->ProdTexture();
  target = surf->ProdTextureTarget();

  mGL->AttachBuffersToFB(colorTex, 0, 0, 0, mFB, target);
  MOZ_GL_ASSERT(mGL, mGL->IsFramebufferComplete(mFB));

  mSurf = surf;
}

const gfx::IntSize& ReadBuffer::Size() const { return mSurf->mDesc.size; }

}  // namespace mozilla::gl

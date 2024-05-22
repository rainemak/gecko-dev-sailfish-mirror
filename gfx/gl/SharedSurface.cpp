/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 4; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurface.h"

#include "../2d/2D.h"
#include "GLBlitHelper.h"
#include "GLContext.h"
#include "GLReadTexImageHelper.h"
#include "GLScreenBuffer.h"
#include "nsThreadUtils.h"
#include "ScopedGLHelpers.h"
#include "SharedSurfaceGL.h"
#include "SharedSurfaceEGL.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/TextureClientSharedSurface.h"
#include "mozilla/layers/TextureForwarder.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/Unused.h"
#include "VRManagerChild.h"

#ifdef XP_WIN
#  include "SharedSurfaceANGLE.h"
#  include "SharedSurfaceD3D11Interop.h"
#endif

#ifdef XP_MACOSX
#  include "SharedSurfaceIO.h"
#endif

#ifdef MOZ_X11
#  include "GLXLibrary.h"
#  include "SharedSurfaceGLX.h"
#endif

#ifdef MOZ_WAYLAND
#  include "gfxPlatformGtk.h"
#  include "SharedSurfaceDMABUF.h"
#endif

#ifdef MOZ_WIDGET_ANDROID
#  include "SharedSurfaceAndroidHardwareBuffer.h"
#endif

namespace mozilla {
namespace gl {

////////////////////////////////////////////////////////////////////////
// SharedSurface

SharedSurface::SharedSurface(const SharedSurfaceDesc& desc,
                             UniquePtr<MozFramebuffer> fb)
    : mDesc(desc), mFb(std::move(fb)) {}

SharedSurface::SharedSurface(SharedSurfaceType type,
                             GLContext* gl, const gfx::IntSize& size,
                             bool canRecycle)
    : mDesc{gl, type, layers::TextureType(0), canRecycle, size},
      mIsLocked(false),
      mIsProducerAcquired(false)
{
}

SharedSurface::~SharedSurface() = default;

layers::TextureFlags SharedSurface::GetTextureFlags() const {
  return layers::TextureFlags::NO_FLAGS;
}

void SharedSurface::LockProd() {
  MOZ_ASSERT(!mIsLocked);

  LockProdImpl();

  mDesc.gl->LockSurface(this);
  mIsLocked = true;
}

void SharedSurface::UnlockProd() {
  if (!mIsLocked) return;

  UnlockProdImpl();

  mDesc.gl->UnlockSurface(this);
  mIsLocked = false;
}

////////////////////////////////////////////////////////////////////////
// SurfaceFactory

/* static */
UniquePtr<SurfaceFactory> SurfaceFactory::Create(
    GLContext* const pGl, const layers::TextureType consumerType) {
  auto& gl = *pGl;
  switch (consumerType) {
    case layers::TextureType::D3D11:
#ifdef XP_WIN
      if (gl.IsANGLE()) {
        return SurfaceFactory_ANGLEShareHandle::Create(gl);
      }
      if (StaticPrefs::webgl_dxgl_enabled()) {
        return SurfaceFactory_D3D11Interop::Create(gl);
      }
#endif
      return nullptr;

    case layers::TextureType::MacIOSurface:
#ifdef XP_MACOSX
      return MakeUnique<SurfaceFactory_IOSurface>(gl);
#else
      return nullptr;
#endif

    case layers::TextureType::X11:
#ifdef MOZ_X11
      if (gl.GetContextType() != GLContextType::GLX) return nullptr;
      if (!sGLXLibrary.UseTextureFromPixmap()) return nullptr;
      return MakeUnique<SurfaceFactory_GLXDrawable>(gl);
#else
      return nullptr;
#endif

    case layers::TextureType::DMABUF:
#ifdef MOZ_WAYLAND
      if (gl.GetContextType() == GLContextType::EGL &&
          gfxPlatformGtk::GetPlatform()->UseDMABufWebGL()) {
        return SurfaceFactory_DMABUF::Create(gl);
      }
#endif
      return nullptr;

    case layers::TextureType::AndroidNativeWindow:
#ifdef MOZ_WIDGET_ANDROID
      return MakeUnique<SurfaceFactory_SurfaceTexture>(gl);
#else
      return nullptr;
#endif

    case layers::TextureType::AndroidHardwareBuffer:
#ifdef MOZ_WIDGET_ANDROID
      return SurfaceFactory_AndroidHardwareBuffer::Create(gl);
#else
      return nullptr;
#endif

    case layers::TextureType::EGLImage:
#ifdef MOZ_WIDGET_ANDROID
      if (XRE_IsParentProcess()) {
        return SurfaceFactory_EGLImage::Create(gl);
      }
#endif
      return nullptr;

    case layers::TextureType::Unknown:
    case layers::TextureType::DIB:
    case layers::TextureType::Last:
      break;
  }
  return nullptr;
}

SurfaceFactory::SurfaceFactory(const PartialSharedSurfaceDesc& partialDesc,
                               const RefPtr<layers::LayersIPCChannel>& allocator,
                               const layers::TextureFlags& flags)
    : mDesc(partialDesc),
      mAllocator(allocator),
      mFlags(flags),
      mMutex("SurfaceFactor::mMutex")
{
}

SurfaceFactory::~SurfaceFactory() {
  while (!mRecycleTotalPool.empty()) {
    RefPtr<layers::SharedSurfaceTextureClient> tex = *mRecycleTotalPool.begin();
    StopRecycling(tex);
  }

  MOZ_RELEASE_ASSERT(mRecycleTotalPool.empty(),
                     "GFX: Surface recycle pool not empty.");

  // If we mRecycleFreePool.clear() before StopRecycling(), we may try to
  // recycle it, fail, call StopRecycling(), then return here and call it again.
  mRecycleFreePool.clear();
}

already_AddRefed<layers::SharedSurfaceTextureClient>
SurfaceFactory::NewTexClient(const gfx::IntSize& size) {
  while (!mRecycleFreePool.empty()) {
    RefPtr<layers::SharedSurfaceTextureClient> cur = mRecycleFreePool.front();
    mRecycleFreePool.pop();

    if (cur->Surf()->mDesc.size == size) {
      cur->Surf()->WaitForBufferOwnership();
      return cur.forget();
    }

    StopRecycling(cur);
  }

  UniquePtr<SharedSurface> surf = CreateShared(size);
  if (!surf) return nullptr;

  RefPtr<layers::SharedSurfaceTextureClient> ret;
  ret = layers::SharedSurfaceTextureClient::Create(std::move(surf), this,
                                                   mAllocator, mFlags);

  StartRecycling(ret);

  return ret.forget();
}

void SurfaceFactory::StartRecycling(layers::SharedSurfaceTextureClient* tc) {
  tc->SetRecycleCallback(&SurfaceFactory::RecycleCallback,
                         static_cast<void*>(this));

  bool didInsert = mRecycleTotalPool.insert(tc);
  MOZ_RELEASE_ASSERT(
      didInsert,
      "GFX: Shared surface texture client was not inserted to recycle.");
  mozilla::Unused << didInsert;
}

void SurfaceFactory::StopRecycling(layers::SharedSurfaceTextureClient* tc) {
  MutexAutoLock autoLock(mMutex);
  // Must clear before releasing ref.
  tc->ClearRecycleCallback();

  bool didErase = mRecycleTotalPool.erase(tc);
  MOZ_RELEASE_ASSERT(didErase,
                     "GFX: Shared texture surface client was not erased.");
  mozilla::Unused << didErase;
}

/*static*/
void SurfaceFactory::RecycleCallback(layers::TextureClient* rawTC,
                                     void* rawFactory) {
  RefPtr<layers::SharedSurfaceTextureClient> tc;
  tc = static_cast<layers::SharedSurfaceTextureClient*>(rawTC);
  SurfaceFactory* factory = static_cast<SurfaceFactory*>(rawFactory);

  if (tc->Surf()->mDesc.canRecycle) {
    if (factory->Recycle(tc)) return;
  }

  // Did not recover the tex client. End the (re)cycle!
  factory->StopRecycling(tc);
}

bool SurfaceFactory::Recycle(layers::SharedSurfaceTextureClient* texClient) {
  MOZ_ASSERT(texClient);
  MutexAutoLock autoLock(mMutex);

  if (mRecycleFreePool.size() >= 2) {
    return false;
  }

  RefPtr<layers::SharedSurfaceTextureClient> texClientRef = texClient;
  mRecycleFreePool.push(texClientRef);
  return true;
}

}  // namespace gl
}  // namespace mozilla

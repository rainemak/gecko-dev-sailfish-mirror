/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VideoFrameUtils.h"
#include "webrtc/api/video/video_frame.h"
#include "mozilla/ShmemPool.h"
#include "libyuv/rotate.h"

namespace mozilla {

uint32_t VideoFrameUtils::TotalRequiredBufferSize(
    const webrtc::VideoFrame& aVideoFrame) {
  auto i420 = aVideoFrame.video_frame_buffer()->ToI420();
  auto height = i420->height();
  size_t size = height * i420->StrideY() +
                ((height + 1) / 2) * i420->StrideU() +
                ((height + 1) / 2) * i420->StrideV();
  MOZ_RELEASE_ASSERT(size < std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(size);
}

void VideoFrameUtils::InitFrameBufferProperties(
    const webrtc::VideoFrame& aVideoFrame,
    camera::VideoFrameProperties& aDestProps) {
  // The VideoFrameBuffer image data stored in the accompanying buffer
  // the buffer is at least this size of larger.
  aDestProps.bufferSize() = TotalRequiredBufferSize(aVideoFrame);

  aDestProps.timeStamp() = aVideoFrame.timestamp();
  aDestProps.ntpTimeMs() = aVideoFrame.ntp_time_ms();
  aDestProps.renderTimeMs() = aVideoFrame.render_time_ms();

  // Rotation will be applied during CopyVideoFrameBuffers().
  aDestProps.rotation() = aVideoFrame.rotation();

  auto i420 = aVideoFrame.video_frame_buffer()->ToI420();
  auto height = i420->height();
  auto width = i420->width();

  if (aVideoFrame.rotation() == webrtc::kVideoRotation_90 ||
      aVideoFrame.rotation() == webrtc::kVideoRotation_270) {
    std::swap(width, height);
  }

  aDestProps.height() = height;
  aDestProps.width() = width;

  aDestProps.yStride() = width;
  aDestProps.uStride() = (width + 1) / 2;
  aDestProps.vStride() = (width + 1) / 2;

  aDestProps.yAllocatedSize() = height * aDestProps.yStride();
  aDestProps.uAllocatedSize() = ((height + 1) / 2) * aDestProps.uStride();
  aDestProps.vAllocatedSize() = ((height + 1) / 2) * aDestProps.vStride();
}

// Performs copying to a shared memory or a temporary buffer.
// Apply rotation here to avoid extra copying.
void VideoFrameUtils::CopyVideoFrameBuffers(uint8_t* aDestBuffer,
                                            const size_t aDestBufferSize,
                                            const webrtc::VideoFrame& aFrame) {
  size_t aggregateSize = TotalRequiredBufferSize(aFrame);

  MOZ_ASSERT(aDestBufferSize >= aggregateSize);
  auto i420 = aFrame.video_frame_buffer()->ToI420();

  // If planes are ordered YUV and contiguous then do a single copy
  if ((i420->DataY() != nullptr) &&
      // Check that the three planes are ordered
      (i420->DataY() < i420->DataU()) && (i420->DataU() < i420->DataV()) &&
      //  Check that the last plane ends at firstPlane[totalsize]
      (&i420->DataY()[aggregateSize] ==
       &i420->DataV()[((i420->height() + 1) / 2) * i420->StrideV()]) &&
      aFrame.rotation() == webrtc::kVideoRotation_0) {
    memcpy(aDestBuffer, i420->DataY(), aggregateSize);
    return;
  }

  libyuv::RotationMode rotationMode;
  int width = i420->width();
  int height = i420->height();

  switch (aFrame.rotation()) {
    case webrtc::kVideoRotation_90:
      rotationMode = libyuv::kRotate90;
      std::swap(width, height);
      break;
    case webrtc::kVideoRotation_270:
      rotationMode = libyuv::kRotate270;
      std::swap(width, height);
      break;
    case webrtc::kVideoRotation_180:
      rotationMode = libyuv::kRotate180;
      break;
    case webrtc::kVideoRotation_0:
    default:
      rotationMode = libyuv::kRotate0;
      break;
  }

  int strideY = width;
  int strideUV = (width + 1) / 2;
  off_t offsetY = 0;
  off_t offsetU = height * strideY;
  off_t offsetV = offsetU + ((height + 1) / 2) * strideUV;

  libyuv::I420Rotate(i420->DataY(), i420->StrideY(),
                     i420->DataU(), i420->StrideU(),
                     i420->DataV(), i420->StrideV(),
                     &aDestBuffer[offsetY], strideY,
                     &aDestBuffer[offsetU], strideUV,
                     &aDestBuffer[offsetV], strideUV,
                     i420->width(), i420->height(),
                     rotationMode);
}

void VideoFrameUtils::CopyVideoFrameBuffers(
    ShmemBuffer& aDestShmem, const webrtc::VideoFrame& aVideoFrame) {
  CopyVideoFrameBuffers(aDestShmem.Get().get<uint8_t>(),
                        aDestShmem.Get().Size<uint8_t>(), aVideoFrame);
}

}  // namespace mozilla

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <new>

#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"
#include "api/video/i420_buffer.h"
#include "rtc_base/refcount.h"
#include "rtc_base/refcountedobject.h"
#include "rtc_base/scoped_ref_ptr.h"

#include "libyuv/convert.h"

#include "modules/video_capture/sfos/video_capture_sfos.h"

namespace webrtc {
namespace videocapturemodule {

class GeckoVideoBuffer : public I420BufferInterface {
public:
    static rtc::scoped_refptr<GeckoVideoBuffer> Create(std::shared_ptr<const gecko::camera::YCbCrFrame> frame) {
        return new rtc::RefCountedObject<GeckoVideoBuffer>(frame);
    }

    GeckoVideoBuffer(std::shared_ptr<const gecko::camera::YCbCrFrame> frame)
        : ycbcr(frame) {}

    int width () const { return ycbcr->width; };
    int height() const { return ycbcr->height; };

    const uint8_t* DataY() const { return (const uint8_t*)ycbcr->y; };
    const uint8_t* DataU() const { return (const uint8_t*)ycbcr->cb; };
    const uint8_t* DataV() const { return (const uint8_t*)ycbcr->cr; };

    int StrideY() const { return ycbcr->yStride; };
    int StrideU() const { return ycbcr->cStride; };
    int StrideV() const { return ycbcr->cStride; };

    void *native_handle() const { return (void *)&ycbcr; };

    Type type() const override { return ycbcr->chromaStep == 1 ? Type::kI420 : Type::kNative; }

    rtc::scoped_refptr<I420BufferInterface> ToI420() {
        if (ycbcr->chromaStep == 1) {
            return this;
        } else if (ycbcr->chromaStep == 2) {
            rtc::scoped_refptr<I420Buffer> buffer = I420Buffer::Create(
                    ycbcr->width, ycbcr->height);

            libyuv::NV12ToI420(
                    (const uint8_t*)ycbcr->y, ycbcr->yStride,
                    (const uint8_t*)ycbcr->cb, ycbcr->cStride,
                    buffer->MutableDataY(), buffer->StrideY(),
                    buffer->MutableDataU(), buffer->StrideU(),
                    buffer->MutableDataV(), buffer->StrideV(),
                    ycbcr->width, ycbcr->height);
            return buffer;
        } else {
            // Unsupported format
            rtc::scoped_refptr<I420Buffer> buffer = I420Buffer::Create(
                    ycbcr->width, ycbcr->height);
            I420Buffer::SetBlack(buffer.get());
            return buffer;
        }
    }

private:
    std::shared_ptr<const gecko::camera::YCbCrFrame> ycbcr;
};

rtc::scoped_refptr<VideoCaptureModule> VideoCaptureImpl::Create(
    const char* deviceUniqueId) {
    rtc::scoped_refptr<VideoCaptureModuleSFOS> implementation(
        new rtc::RefCountedObject<VideoCaptureModuleSFOS>());

    if (implementation->Init(deviceUniqueId) != 0)
        return nullptr;

    return implementation;
}

VideoCaptureModuleSFOS::VideoCaptureModuleSFOS()
    : VideoCaptureImpl()
{
    mozilla::hal::ScreenConfiguration screenConfig;

    mozilla::hal::RegisterScreenConfigurationObserver (this);
    mozilla::hal::GetCurrentScreenConfiguration (&screenConfig);
    _screenRotationAngle = ScreenOrientationToAngle (screenConfig.orientation());
}

int32_t VideoCaptureModuleSFOS::Init(const char* deviceUniqueIdUTF8)
{
    /* Fill current device name for the parent class */
    int len = strlen(deviceUniqueIdUTF8);
    _deviceUniqueId = new (std::nothrow) char[len + 1];
    if (_deviceUniqueId) {
        memcpy(_deviceUniqueId, deviceUniqueIdUTF8, len + 1);
    } else {
        return -1;
    }

    if (gecko_camera_manager()->openCamera(deviceUniqueIdUTF8, _camera)) {
        gecko::camera::CameraInfo info;
        if (_camera->getInfo(info)) {
            _rearFacingCamera = info.facing == gecko::camera::GECKO_CAMERA_FACING_REAR;
            _sensorMountAngle = info.mountAngle;
            _camera->setListener(this);
            return 0;
        }
        _camera.reset();
    }
    return -1;
}

VideoCaptureModuleSFOS::~VideoCaptureModuleSFOS()
{
    mozilla::hal::UnregisterScreenConfigurationObserver(this);
}

int32_t VideoCaptureModuleSFOS::StartCapture(
    const VideoCaptureCapability& capability)
{
    if (_camera->captureStarted()) {
        if (capability.width == _requestedCapability.width
                && capability.height == _requestedCapability.height
                && capability.maxFPS == _requestedCapability.maxFPS) {
            return 0;
        } else {
            _camera->stopCapture();
        }
    }

    _startNtpTimeMs = webrtc::Clock::GetRealTimeClock()->CurrentNtpInMilliseconds();
    UpdateCaptureRotation();

    _requestedCapability = capability;
    _requestedCapability.videoType = VideoType::kI420;

    gecko::camera::CameraCapability cap;
    cap.width = capability.width;
    cap.height = capability.height;
    cap.fps = capability.maxFPS;
    return _camera->startCapture(cap) ? 0 : -1;
}

int32_t VideoCaptureModuleSFOS::StopCapture()
{
    return _camera->stopCapture() ? 0 : -1;
}

bool VideoCaptureModuleSFOS::CaptureStarted()
{
    return _camera->captureStarted();
}

void VideoCaptureModuleSFOS::Notify(const mozilla::hal::ScreenConfiguration& aConfiguration)
{
    RTC_LOG(LS_INFO) << "VideoCaptureModuleSFOS::Notify ScreenConfiguration.orientation: " << aConfiguration.orientation();
    _screenRotationAngle = ScreenOrientationToAngle(aConfiguration.orientation());
    UpdateCaptureRotation();
}

int VideoCaptureModuleSFOS::ScreenOrientationToAngle(mozilla::hal::ScreenOrientation orientation)
{
    switch (orientation) {
    // The default orientation is portrait for Sailfish OS.
    case mozilla::hal::eScreenOrientation_Default:
    case mozilla::hal::eScreenOrientation_PortraitPrimary:
        return 0;
    case mozilla::hal::eScreenOrientation_LandscapePrimary:
        return 90;
    case mozilla::hal::eScreenOrientation_PortraitSecondary:
        return 180;
    case mozilla::hal::eScreenOrientation_LandscapeSecondary:
        return 270;
    default:
        return 0;
    }
}

void VideoCaptureModuleSFOS::onCameraFrame(std::shared_ptr<gecko::camera::GraphicBuffer> grb)
{
    int64_t captureTime = grb->timestampUs / 1000 + _startNtpTimeMs;
    RTC_LOG(LS_VERBOSE) << "frame ts=" << captureTime;

    if (grb->imageFormat == gecko::camera::ImageFormat::YCbCr) {
        std::shared_ptr<const gecko::camera::YCbCrFrame> frame = grb->mapYCbCr();
        if (frame) {
            auto buffer = GeckoVideoBuffer::Create(frame);
            IncomingVideoBuffer(std::move(buffer->ToI420()), captureTime);
            return;
        }
    }

    RTC_LOG(LS_ERROR) << "Invalid image format";
}

void VideoCaptureModuleSFOS::onCameraError(std::string errorDescription)
{
    RTC_LOG(LS_ERROR) << "Camera error " << errorDescription << "\n";
}

void VideoCaptureModuleSFOS::UpdateCaptureRotation()
{
    VideoRotation rotation;
    int rotateAngle = 360 + _sensorMountAngle + (_rearFacingCamera ? -_screenRotationAngle : _screenRotationAngle);

    switch (rotateAngle % 360) {
    case 90:
        rotation = kVideoRotation_90;
        break;
    case 180:
        rotation = kVideoRotation_180;
        break;
    case 270:
        rotation = kVideoRotation_270;
        break;
    default:
        rotation = kVideoRotation_0;
        break;
    }

    RTC_LOG(LS_INFO) << "Sensor mount angle=" << _sensorMountAngle
                   << " Screen rotation=" << _screenRotationAngle
                   << " Capture rotation=" << rotateAngle;
    VideoCaptureImpl::SetCaptureRotation (rotation);
}

int32_t VideoCaptureModuleSFOS::CaptureSettings(VideoCaptureCapability& settings)
{
    settings = _requestedCapability;
    return 0;
}
}  // namespace videocapturemodule
}  // namespace webrtc

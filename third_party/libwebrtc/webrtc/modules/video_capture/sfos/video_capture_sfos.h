/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBRTC_MODULES_VIDEO_CAPTURE_MAIN_SOURCE_SFOS_VIDEO_CAPTURE_SFOS_H_
#define WEBRTC_MODULES_VIDEO_CAPTURE_MAIN_SOURCE_SFOS_VIDEO_CAPTURE_SFOS_H_

#include <memory>

#include "mozilla/Hal.h"
#include "mozilla/dom/ScreenOrientation.h"

#include "common_types.h"
#include "modules/video_capture/video_capture_impl.h"

#include "geckocamera.h"

namespace webrtc
{
class CriticalSectionWrapper;
namespace videocapturemodule
{
class VideoCaptureModuleSFOS
    : public VideoCaptureImpl,
      public gecko::camera::CameraListener,
      public mozilla::hal::ScreenConfigurationObserver
{
public:
    VideoCaptureModuleSFOS();
    virtual ~VideoCaptureModuleSFOS();
    virtual int32_t Init(const char* deviceUniqueId);
    virtual int32_t StartCapture(const VideoCaptureCapability& capability);
    virtual int32_t StopCapture();
    virtual bool CaptureStarted();
    virtual int32_t CaptureSettings(VideoCaptureCapability& settings);

    void onCameraFrame(std::shared_ptr<gecko::camera::GraphicBuffer> buffer) override;
    void onCameraError(std::string errorDescription) override;

    virtual void Notify(const mozilla::hal::ScreenConfiguration& aConfiguration) override;

private:
    int ScreenOrientationToAngle(mozilla::hal::ScreenOrientation orientation);
    void UpdateCaptureRotation();

    int _screenRotationAngle = 0;
    int _sensorMountAngle = 0;
    bool _rearFacingCamera = false;
    uint64_t _startNtpTimeMs = 0;
    std::shared_ptr<gecko::camera::Camera> _camera;
};
}  // namespace videocapturemodule
}  // namespace webrtc

#endif // WEBRTC_MODULES_VIDEO_CAPTURE_MAIN_SOURCE_SFOS_VIDEO_CAPTURE_SFOS_H_

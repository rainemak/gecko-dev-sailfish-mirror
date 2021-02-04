/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBRTC_MODULES_VIDEO_CAPTURE_MAIN_SOURCE_SFOS_DEVICE_INFO_SFOS_H_
#define WEBRTC_MODULES_VIDEO_CAPTURE_MAIN_SOURCE_SFOS_DEVICE_INFO_SFOS_H_

#include "modules/video_capture/device_info_impl.h"
#include "modules/video_capture/video_capture_impl.h"
#include "base/platform_thread.h"

#include "geckocamera.h"

namespace webrtc
{
namespace videocapturemodule
{
class DeviceInfoSFOS: public DeviceInfoImpl
{
public:
    DeviceInfoSFOS();
    virtual ~DeviceInfoSFOS();
    virtual uint32_t NumberOfDevices();
    virtual int32_t GetDeviceName(
        uint32_t deviceNumber,
        char* deviceNameUTF8,
        uint32_t deviceNameLength,
        char* deviceUniqueIdUTF8,
        uint32_t deviceUniqueIdUTF8Length,
        char* productUniqueIdUTF8=0,
        uint32_t productUniqueIdUTF8Length=0,
        pid_t* pid=0);
    virtual int32_t CreateCapabilityMap (const char* deviceUniqueIdUTF8);
    virtual int32_t DisplayCaptureSettingsDialogBox(
        const char* /*deviceUniqueIdUTF8*/,
        const char* /*dialogTitleUTF8*/,
        void* /*parentWindow*/,
        uint32_t /*positionX*/,
        uint32_t /*positionY*/) { return -1; }
    void FillCapabilities(const char* devName);
    int32_t Init();

private:
    gecko::camera::CameraManager *cameraManager;
    std::vector<gecko::camera::CameraInfo> cameraList;
};
}  // namespace videocapturemodule
}  // namespace webrtc
#endif // WEBRTC_MODULES_VIDEO_CAPTURE_MAIN_SOURCE_SFOS_DEVICE_INFO_SFOS_H_

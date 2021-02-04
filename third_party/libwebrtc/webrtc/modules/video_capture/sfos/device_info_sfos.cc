/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mozilla/Preferences.h"
#include "rtc_base/logging.h"
#include "modules/video_capture/sfos/device_info_sfos.h"

#define EXPECTED_CAPTURE_DELAY_PREF "media.getusermedia.camera.expected_capture_delay"
#define MAX_WIDTH_PREF "media.getusermedia.camera.max_width"
#define MAX_HEIGHT_PREF "media.getusermedia.camera.max_height"

namespace webrtc
{
namespace videocapturemodule
{
VideoCaptureModule::DeviceInfo*
VideoCaptureImpl::CreateDeviceInfo()
{
    // The caller doesn't check for return value, so I can't Init() here
    return new videocapturemodule::DeviceInfoSFOS();
}

DeviceInfoSFOS::DeviceInfoSFOS()
    : DeviceInfoImpl()
{
    Init();
}

int32_t DeviceInfoSFOS::Init()
{
    cameraManager = gecko_camera_manager();
    // Initialize parent class member
    _lastUsedDeviceName = (char *)malloc(1);
    _lastUsedDeviceName[0] = 0;
    return 0;
}

DeviceInfoSFOS::~DeviceInfoSFOS()
{
}

uint32_t DeviceInfoSFOS::NumberOfDevices()
{
    RTC_LOG(LS_VERBOSE);

    if (cameraManager) {
        cameraList.clear();
        for (int i = 0; i < cameraManager->getNumberOfCameras(); i++) {
            gecko::camera::CameraInfo info;
            if (cameraManager->getCameraInfo(i, info)) {
                cameraList.push_back(info);
            }
        }
        // Put front cameras at the top of the list, they are most often used during video chat.
        std::sort(cameraList.begin(), cameraList.end(),
                [](const gecko::camera::CameraInfo& c1, const gecko::camera::CameraInfo& c2) {
                    bool c1front = c1.facing == gecko::camera::GECKO_CAMERA_FACING_FRONT;
                    bool c2front = c2.facing == gecko::camera::GECKO_CAMERA_FACING_FRONT;
                    return c1front > c2front;
            });
        return cameraList.size();
    }
    return 0;
}

int32_t DeviceInfoSFOS::GetDeviceName(
        uint32_t deviceNumber,
        char* deviceNameUTF8,
        uint32_t deviceNameLength,
        char* deviceUniqueIdUTF8,
        uint32_t deviceUniqueIdUTF8Length,
        char* productUniqueIdUTF8,
        uint32_t productUniqueIdUTF8Length,
        pid_t* /*pid*/)
{
    if (deviceNumber < cameraList.size()) {
        gecko::camera::CameraInfo info = cameraList.at(deviceNumber);
        strncpy(deviceNameUTF8, info.name.c_str(), deviceNameLength);
        strncpy(deviceUniqueIdUTF8, info.id.c_str(), deviceUniqueIdUTF8Length);
        strncpy(productUniqueIdUTF8, info.provider.c_str(), productUniqueIdUTF8Length);
        return 0;
    }

    return -1;
}

int32_t DeviceInfoSFOS::CreateCapabilityMap(const char* deviceUniqueIdUTF8)
{
    const int32_t deviceUniqueIdUTF8Length =
                            (int32_t) strlen((char*) deviceUniqueIdUTF8);
    if (deviceUniqueIdUTF8Length > kVideoCaptureUniqueNameSize) {
        RTC_LOG(LS_ERROR) << "Device name too long";
        return -1;
    }

    FillCapabilities(deviceUniqueIdUTF8);

    // Store the new used device name. The parent class needs this for some reason
    _lastUsedDeviceNameLength = deviceUniqueIdUTF8Length;
    _lastUsedDeviceName = (char*) realloc(_lastUsedDeviceName,
                                                   _lastUsedDeviceNameLength + 1);
    memcpy(_lastUsedDeviceName, deviceUniqueIdUTF8, _lastUsedDeviceNameLength + 1);

    RTC_LOG(LS_INFO) << "Capability map for device " << deviceUniqueIdUTF8
                   << " size " << _captureCapabilities.size();

    return _captureCapabilities.size();
}

void DeviceInfoSFOS::FillCapabilities(const char *devName)
{
    std::vector<gecko::camera::CameraCapability> caps;
    unsigned int captureDelay = mozilla::Preferences::GetUint(EXPECTED_CAPTURE_DELAY_PREF, 500);
    unsigned int maxWidth = mozilla::Preferences::GetUint(MAX_WIDTH_PREF, 640);
    unsigned int maxHeight = mozilla::Preferences::GetUint(MAX_HEIGHT_PREF, 480);

    _captureCapabilities.clear();
    if (cameraManager && cameraManager->queryCapabilities(devName, caps)) {
        for (auto cap : caps) {
            if (cap.width <= maxWidth && cap.height <= maxHeight) {
                VideoCaptureCapability vcaps;
                vcaps.width = cap.width;
                vcaps.height = cap.height;
                vcaps.maxFPS = cap.fps;
                vcaps.videoType = VideoType::kI420;
                _captureCapabilities.push_back(vcaps);
            }
        }
    }
}

}  // namespace videocapturemodule
}  // namespace webrtc

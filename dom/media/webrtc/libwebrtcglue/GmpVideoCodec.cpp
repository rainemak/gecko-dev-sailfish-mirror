/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebrtcGmpVideoCodec.h"
#include "GmpVideoCodec.h"

namespace mozilla {

WebrtcVideoEncoder* GmpVideoCodec::CreateEncoder(webrtc::VideoCodecType type) {
  return new WebrtcVideoEncoderProxy(type);
}

WebrtcVideoDecoder* GmpVideoCodec::CreateDecoder(webrtc::VideoCodecType type) {
  return new WebrtcVideoDecoderProxy(type);
}

}  // namespace mozilla

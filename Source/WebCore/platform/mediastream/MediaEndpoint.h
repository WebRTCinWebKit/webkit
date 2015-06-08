/*
 * Copyright (C) 2015 Ericsson AB. All rights reserved.
 * Copyright (C) 2015 Temasys Communications. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MediaEndpoint_h
#define MediaEndpoint_h

#if ENABLE(MEDIA_STREAM)

// #include "RTCConfigurationPrivate.h"
#include "MediaEndpointInit.h"
#include "RTCDataChannelHandler.h"
#include "RTCDataChannelHandlerClient.h"
#include <wtf/text/WTFString.h>

namespace WebCore {

class IceCandidate;
class MediaEndpoint;
class MediaEndpointConfiguration;
class RTCDataChannelHandler;
class RTCDataChannelHandlerClient;
class RealtimeMediaSource; 



struct RTCDataChannelInit_Endpoint {
public:
    RTCDataChannelInit_Endpoint()
        : ordered(true)
        , maxRetransmitTime(-1)
        , maxRetransmits(-1)
        , negotiated(false)
        , id(-1) { }
    bool ordered;
    int maxRetransmitTime;
    int maxRetransmits;
    String protocol;
    bool negotiated;
    int id;
};

class MediaEndpointClient {
public:
    virtual void gotSendSSRC(unsigned mdescIndex, unsigned ssrc, const String& cname) = 0;
    virtual void gotDtlsCertificate(unsigned mdescIndex, const String& certificate) = 0;
    virtual void gotIceCandidate(unsigned mdescIndex, RefPtr<IceCandidate>&&, const String& ufrag, const String& password) = 0;
    virtual void doneGatheringCandidates(unsigned mdescIndex) = 0;
    virtual void gotRemoteSource(unsigned mdescIndex, RefPtr<RealtimeMediaSource>&&) = 0;
    virtual void gotDataChannel(unsigned mdescIndex, std::unique_ptr<RTCDataChannelHandler>) = 0;

    virtual ~MediaEndpointClient() { }
};

typedef std::unique_ptr<MediaEndpoint> (*CreateMediaEndpoint)(MediaEndpointClient*);

class MediaEndpoint {
public:
    WEBCORE_EXPORT static CreateMediaEndpoint create;
    virtual ~MediaEndpoint() { }

    // FIMXE: look over naming
    virtual void setConfiguration(RefPtr<MediaEndpointInit>&&) = 0;

    virtual void prepareToReceive(MediaEndpointConfiguration*, bool isInitiator) = 0;
    virtual void prepareToSend(MediaEndpointConfiguration*, bool isInitiator) = 0;

    virtual void addRemoteCandidate(IceCandidate&, unsigned mdescIndex, const String& ufrag, const String& password) = 0;

    virtual std::unique_ptr<RTCDataChannelHandler> createDataChannel(const String& label, RTCDataChannelInit_Endpoint& initData) = 0;

    virtual void stop() = 0;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // MediaEndpoint_h

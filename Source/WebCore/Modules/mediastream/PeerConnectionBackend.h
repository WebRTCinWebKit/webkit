/*
 * Copyright (C) 2015 Ericsson AB. All rights reserved.
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

#ifndef PeerConnectionBackend_h
#define PeerConnectionBackend_h

#if ENABLE(MEDIA_STREAM)

#include "RTCDataChannelHandler.h"
#include "RTCDataChannel.h"
#include "MediaEndpoint.h"

namespace WebCore {

class DOMError;
class Event;
class PeerConnectionBackend;
class RTCAnswerOptions;
class RTCConfiguration;
class RTCIceCandidate;
class RTCOfferOptions;
class RTCDataChannel;
class RTCRtpSender;
class RTCSessionDescription;
class ScriptExecutionContext;
class RTCDataChannelHandler;

class PeerConnectionBackendClient {
public:
    virtual ScriptExecutionContext* context() const = 0;

    virtual Vector<RefPtr<RTCRtpSender>> senders() const = 0;
    virtual Vector<RefPtr<RTCDataChannel>> dataChannels() const = 0;
    virtual bool isClosed() const = 0;

    virtual void updateSignalingState() = 0;
    virtual void scheduleEvent(RefPtr<Event>&&) = 0;

    virtual ~PeerConnectionBackendClient() { }
};

typedef std::unique_ptr<PeerConnectionBackend> (*CreatePeerConnectionBackend)(PeerConnectionBackendClient*);

class PeerConnectionBackend {
public:
    WEBCORE_EXPORT static CreatePeerConnectionBackend create;
    virtual ~PeerConnectionBackend() { }

    typedef std::function<void(RTCSessionDescription&)> OfferAnswerResolveCallback;
    typedef std::function<void()> VoidResolveCallback;
    typedef std::function<void(DOMError&)> RejectCallback;

    virtual void createOffer(const RefPtr<RTCOfferOptions>&, OfferAnswerResolveCallback, RejectCallback) = 0;
    virtual void createAnswer(const RefPtr<RTCAnswerOptions>&, OfferAnswerResolveCallback, RejectCallback) = 0;

    virtual void setLocalDescription(RTCSessionDescription*, VoidResolveCallback, RejectCallback) = 0;
    virtual RefPtr<RTCSessionDescription> localDescription() const = 0;

    virtual void setRemoteDescription(RTCSessionDescription*, VoidResolveCallback, RejectCallback) = 0;
    virtual RefPtr<RTCSessionDescription> remoteDescription() const = 0;

    virtual void setConfiguration(RTCConfiguration&) = 0;
    virtual void addIceCandidate(RTCIceCandidate*, VoidResolveCallback, RejectCallback) = 0;

    virtual std::unique_ptr<RTCDataChannelHandler> createDataChannel(const String& label, RTCDataChannelInit_Endpoint& initData) = 0;
    virtual void stop() = 0;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // PeerConnectionBackend_h

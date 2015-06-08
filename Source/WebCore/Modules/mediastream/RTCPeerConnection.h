/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Nokia Corporation and/or its subsidiary(-ies).
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
 * 3. Neither the name of Google Inc. nor the names of its contributors
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

#ifndef RTCPeerConnection_h
#define RTCPeerConnection_h

#if ENABLE(MEDIA_STREAM)

#include "ActiveDOMObject.h"
#include "Dictionary.h"
#include "EventTarget.h"
#include "MediaEndpoint.h"
#include "ScriptWrappable.h"
#include "Timer.h"
#include <wtf/HashMap.h>
#include <wtf/RefCounted.h>

namespace WebCore {

class MediaEndpointConfiguration;
class MediaStream;
class MediaStreamTrack;
class RTCConfiguration;
class RTCDataChannel;
class RTCIceCandidate;
class RTCPeerConnectionErrorCallback;
class RTCRtpReceiver;
class RTCRtpSender;
class RTCSessionDescription;
class RTCStatsCallback;

class RTCPeerConnection final : public RefCounted<RTCPeerConnection>, public ScriptWrappable, public MediaEndpointClient, public EventTargetWithInlineData, public ActiveDOMObject {
public:
    static PassRefPtr<RTCPeerConnection> create(ScriptExecutionContext&, const Dictionary& rtcConfiguration, ExceptionCode&);
    ~RTCPeerConnection();

    typedef std::function<void(RTCSessionDescription&)> OfferAnswerResolveCallback;
    typedef std::function<void()> VoidResolveCallback;
    typedef std::function<void(DOMError&)> RejectCallback;

    Vector<RefPtr<RTCRtpSender>> getSenders() const;
    Vector<RefPtr<RTCRtpReceiver>> getReceivers() const;

    RefPtr<RTCRtpSender> addTrack(RefPtr<MediaStreamTrack>&&, const MediaStream* stream, ExceptionCode&);
    void removeTrack(RTCRtpSender*, ExceptionCode&);

    void createOffer(const Dictionary& offerOptions, OfferAnswerResolveCallback, RejectCallback, ExceptionCode&);
    void createAnswer(const Dictionary& answerOptions, OfferAnswerResolveCallback, RejectCallback, ExceptionCode&);

    void setLocalDescription(RTCSessionDescription*, VoidResolveCallback, RejectCallback, ExceptionCode&);
    RefPtr<RTCSessionDescription> localDescription() const;

    void setRemoteDescription(RTCSessionDescription*, VoidResolveCallback, RejectCallback, ExceptionCode&);
    RefPtr<RTCSessionDescription> remoteDescription() const;

    String signalingState() const;

    void updateIce(const Dictionary& rtcConfiguration, ExceptionCode&);
    void addIceCandidate(RTCIceCandidate*, VoidResolveCallback, RejectCallback, ExceptionCode&);

    String iceGatheringState() const;
    String iceConnectionState() const;

    RTCConfiguration* getConfiguration() const;

    void getStats(PassRefPtr<RTCStatsCallback> successCallback, PassRefPtr<RTCPeerConnectionErrorCallback>, PassRefPtr<MediaStreamTrack> selector);

    PassRefPtr<RTCDataChannel> createDataChannel(String label, const Dictionary& dataChannelDict, ExceptionCode&);

    void close();

    // MediaEndpointClient
    virtual void gotSendSSRC(unsigned mdescIndex, unsigned ssrc, const String& cname) override;
    virtual void gotDtlsCertificate(unsigned mdescIndex, const String& certificate) override;
    virtual void gotIceCandidate(unsigned mdescIndex, RefPtr<IceCandidate>&&, const String& ufrag, const String& password) override;
    virtual void doneGatheringCandidates(unsigned mdescIndex) override;
    virtual void gotRemoteSource(unsigned mdescIndex, RefPtr<RealtimeMediaSource>&&) override;
    virtual void gotDataChannel(unsigned mdescIndex, std::unique_ptr<RTCDataChannelHandler>) override;

    // EventTarget
    virtual EventTargetInterface eventTargetInterface() const override { return RTCPeerConnectionEventTargetInterfaceType; }
    virtual ScriptExecutionContext* scriptExecutionContext() const override { return ActiveDOMObject::scriptExecutionContext(); }

    using RefCounted<RTCPeerConnection>::ref;
    using RefCounted<RTCPeerConnection>::deref;

private:
    enum SetterType {
        SetterTypeLocal = 1,
        SetterTypeRemote = 2
    };

    enum DescriptionType {
        DescriptionTypeOffer = 1,
        DescriptionTypePranswer = 2,
        DescriptionTypeAnswer = 3
    };

    enum SignalingState {
        SignalingStateStable = 1,
        SignalingStateHaveLocalOffer = 2,
        SignalingStateHaveRemoteOffer = 3,
        SignalingStateHaveLocalPrAnswer = 4,
        SignalingStateHaveRemotePrAnswer = 5,
        SignalingStateClosed = 6,
        SignalingStateInvalid = 7
    };

    enum IceConnectionState {
        IceConnectionStateNew = 1,
        IceConnectionStateChecking = 2,
        IceConnectionStateConnected = 3,
        IceConnectionStateCompleted = 4,
        IceConnectionStateFailed = 5,
        IceConnectionStateDisconnected = 6,
        IceConnectionStateClosed = 7
    };

    enum IceGatheringState {
        IceGatheringStateNew = 1,
        IceGatheringStateGathering = 2,
        IceGatheringStateComplete = 3
    };

    RTCPeerConnection(ScriptExecutionContext&, PassRefPtr<RTCConfiguration>, ExceptionCode&);

    SignalingState targetSignalingState(SetterType, DescriptionType) const;
    DescriptionType parseDescriptionType(const String& typeName) const;

    enum ResolveSetLocalDescriptionResult {
        LocalConfigurationIncomplete,
        SetLocalDescriptionResolvedSuccessfully,
        SetLocalDescriptionAlreadyResolved
    };

    bool isLocalConfigurationComplete() const;
    ResolveSetLocalDescriptionResult maybeResolveSetLocalDescription();
    void maybeDispatchGatheringDone();

    String toSDP(const String& json) const;
    String fromSDP(const String& sdp) const;
    String iceCandidateToSDP(const String& json) const;
    String iceCandidateFromSDP(const String& sdpFragment) const;
    String sdpConversion(const String& functionName, const String& argument) const;

    void scheduleDispatchEvent(PassRefPtr<Event>);
    void scheduledEventTimerFired();

    // EventTarget implementation.
    virtual void refEventTarget() override { ref(); }
    virtual void derefEventTarget() override { deref(); }

    // ActiveDOMObject
    void stop() override;
    const char* activeDOMObjectName() const override;
    bool canSuspendForPageCache() const override;

    void changeSignalingState(SignalingState);
    void changeIceGatheringState(IceGatheringState);
    void changeIceConnectionState(IceConnectionState);

    SignalingState m_signalingState;
    IceGatheringState m_iceGatheringState;
    IceConnectionState m_iceConnectionState;

    HashMap<String, RefPtr<RTCRtpSender>> m_senderSet;
    HashMap<String, RefPtr<RTCRtpReceiver>> m_receiverSet;

    RefPtr<MediaEndpointConfiguration> m_localConfiguration;
    RefPtr<MediaEndpointConfiguration> m_remoteConfiguration;

    String m_localConfigurationType;
    String m_remoteConfigurationType;

    std::function<void()> m_resolveSetLocalDescription;

    Vector<RefPtr<RTCDataChannel>> m_dataChannels;

    std::unique_ptr<MediaEndpoint> m_mediaEndpoint;

    mutable RefPtr<DOMWrapperWorld> m_isolatedWorld;

    Timer m_scheduledEventTimer;
    Vector<RefPtr<Event>> m_scheduledEvents;

    RefPtr<RTCConfiguration> m_configuration;

    bool m_stopped;
};

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

#endif // RTCPeerConnection_h

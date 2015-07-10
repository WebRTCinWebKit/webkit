/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Nokia Corporation and/or its subsidiary(-ies).
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

#include "config.h"

#if ENABLE(MEDIA_STREAM)

#include "RTCPeerConnection.h"

#include "DOMError.h"
#include "Document.h"
#include "Event.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "MediaStream.h"
#include "MediaStreamTrack.h"
#include "RTCConfiguration.h"
#include "RTCDataChannel.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCRtpReceiver.h"
#include "RTCRtpSender.h"
#include "RTCSessionDescription.h"
#include "RTCTrackEvent.h"
#include <wtf/MainThread.h>
#include <wtf/text/Base64.h>

namespace WebCore {

RefPtr<RTCPeerConnection> RTCPeerConnection::create(ScriptExecutionContext& context, const Dictionary& rtcConfiguration, ExceptionCode& ec)
{
    printf("-> RTCConfiguration::create\n");

    RefPtr<RTCConfiguration> configuration = RTCConfiguration::create(rtcConfiguration, ec);
    if (ec)
        return nullptr;

    printf("RTCPeerConnection: config was ok\n");

    RefPtr<RTCPeerConnection> peerConnection = adoptRef(new RTCPeerConnection(context, configuration.release(), ec));
    peerConnection->suspendIfNeeded();
    if (ec)
        return nullptr;

    printf("RTCPeerConnection: before release\n");
    return peerConnection;
}

RTCPeerConnection::RTCPeerConnection(ScriptExecutionContext& context, PassRefPtr<RTCConfiguration> configuration, ExceptionCode& ec)
    : ActiveDOMObject(&context)
    , m_signalingState(SignalingStateStable)
    , m_iceGatheringState(IceGatheringStateNew)
    , m_iceConnectionState(IceConnectionStateNew)
    , m_scheduledEventTimer(*this, &RTCPeerConnection::scheduledEventTimerFired)
    , m_configuration(configuration)
    , m_stopped(false)
{
    printf("-> RTCConfiguration::RTCConfiguration\n");

    Document& document = downcast<Document>(context);

    if (!document.frame()) {
        ec = NOT_SUPPORTED_ERR;
        return;
    }

    m_backend = PeerConnectionBackend::create(this);
    if (!m_backend) {
        ec = NOT_SUPPORTED_ERR;
        return;
    }

    m_backend->setConfiguration(*m_configuration);
}

RTCPeerConnection::~RTCPeerConnection()
{
    stop();
}

Vector<RefPtr<RTCRtpSender>> RTCPeerConnection::getSenders() const
{
    Vector<RefPtr<RTCRtpSender>> senders;
    for (auto& sender : m_senderSet.values())
        senders.append(sender);

    return senders;
}

Vector<RefPtr<RTCRtpReceiver>> RTCPeerConnection::getReceivers() const
{
    Vector<RefPtr<RTCRtpReceiver>> receivers;
    for (auto& receiver : m_receiverSet.values())
        receivers.append(receiver);

    return receivers;
}

RefPtr<RTCRtpSender> RTCPeerConnection::addTrack(RefPtr<MediaStreamTrack>&& track, const MediaStream* stream, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return nullptr;
    }

    if (m_senderSet.contains(track->id())) {
        // FIXME: Spec says InvalidParameter
        ec = INVALID_MODIFICATION_ERR;
        return nullptr;
    }

    RefPtr<RTCRtpSender> sender = RTCRtpSender::create(WTF::move(track), stream->id());
    m_senderSet.add(track->id(), sender);

    return sender;
}

void RTCPeerConnection::removeTrack(RTCRtpSender* sender, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    if (!m_senderSet.remove(sender->track()->id()))
        return;

    // FIXME: Mark connection as needing negotiation.
}

void RTCPeerConnection::createOffer(const Dictionary& offerOptions, OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    if (m_signalingState == SignalingStateClosed) {
        RefPtr<DOMError> error = DOMError::create("InvalidStateError");
        rejectCallback(*error);
        return;
    }

    ExceptionCode ec = 0;
    RefPtr<RTCOfferOptions> options = RTCOfferOptions::create(offerOptions, ec);
    if (ec) {
        RefPtr<DOMError> error = DOMError::create("Invalid createOffer argument.");
        rejectCallback(*error);
        return;
    }

    m_backend->createOffer(options, resolveCallback, rejectCallback);
}

void RTCPeerConnection::createAnswer(const Dictionary& answerOptions, OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    if (m_signalingState == SignalingStateClosed) {
        RefPtr<DOMError> error = DOMError::create("InvalidStateError");
        rejectCallback(*error);
        return;
    }

    ExceptionCode ec = 0;
    RefPtr<RTCAnswerOptions> options = RTCAnswerOptions::create(answerOptions, ec);
    if (ec) {
        RefPtr<DOMError> error = DOMError::create("Invalid createAnswer argument.");
        rejectCallback(*error);
        return;
    }

    if (!remoteDescription()) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidStateError (no remote description)");
        rejectCallback(*error);
        return;
    }

    m_backend->createAnswer(options, resolveCallback, rejectCallback);
}

void RTCPeerConnection::setLocalDescription(RTCSessionDescription* description, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    if (m_signalingState == SignalingStateClosed) {
        RefPtr<DOMError> error = DOMError::create("InvalidStateError");
        rejectCallback(*error);
        return;
    }

    DescriptionType descriptionType = parseDescriptionType(description->type());

    SignalingState targetState = targetSignalingState(SetterTypeLocal, descriptionType);
    if (targetState == SignalingStateInvalid) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError");
        rejectCallback(*error);
        return;
    }

    m_nextSignalingState = targetState;
    m_backend->setLocalDescription(description, resolveCallback, rejectCallback);
}

RefPtr<RTCSessionDescription> RTCPeerConnection::localDescription() const
{
    return m_backend->localDescription();
}

void RTCPeerConnection::setRemoteDescription(RTCSessionDescription* description, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    if (m_signalingState == SignalingStateClosed) {
        RefPtr<DOMError> error = DOMError::create("InvalidStateError");
        rejectCallback(*error);
        return;
    }

    DescriptionType descriptionType = parseDescriptionType(description->type());

    SignalingState targetState = targetSignalingState(SetterTypeRemote, descriptionType);
    if (targetState == SignalingStateInvalid) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError");
        rejectCallback(*error);
        return;
    }

    m_nextSignalingState = targetState;
    m_backend->setRemoteDescription(description, resolveCallback, rejectCallback);
}

RefPtr<RTCSessionDescription> RTCPeerConnection::remoteDescription() const
{
    return m_backend->remoteDescription();
}

void RTCPeerConnection::updateIce(const Dictionary& rtcConfiguration, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    m_configuration = RTCConfiguration::create(rtcConfiguration, ec);
    if (ec)
        return;

    m_backend->setConfiguration(*m_configuration);
}

void RTCPeerConnection::addIceCandidate(RTCIceCandidate* rtcCandidate, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    if (m_signalingState == SignalingStateClosed) {
        RefPtr<DOMError> error = DOMError::create("InvalidStateError");
        rejectCallback(*error);
        return;
    }

    if (!remoteDescription()) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidStateError (no remote description)");
        rejectCallback(*error);
        return;
    }

    m_backend->addIceCandidate(rtcCandidate, resolveCallback, rejectCallback);
}

String RTCPeerConnection::signalingState() const
{
    switch (m_signalingState) {
    case SignalingStateStable:
        return ASCIILiteral("stable");
    case SignalingStateHaveLocalOffer:
        return ASCIILiteral("have-local-offer");
    case SignalingStateHaveRemoteOffer:
        return ASCIILiteral("have-remote-offer");
    case SignalingStateHaveLocalPrAnswer:
        return ASCIILiteral("have-local-pranswer");
    case SignalingStateHaveRemotePrAnswer:
        return ASCIILiteral("have-remote-pranswer");
    case SignalingStateClosed:
        return ASCIILiteral("closed");
    case SignalingStateInvalid:
        break;
    }

    ASSERT_NOT_REACHED();
    return String();
}

String RTCPeerConnection::iceGatheringState() const
{
    switch (m_iceGatheringState) {
    case IceGatheringStateNew:
        return ASCIILiteral("new");
    case IceGatheringStateGathering:
        return ASCIILiteral("gathering");
    case IceGatheringStateComplete:
        return ASCIILiteral("complete");
    }

    ASSERT_NOT_REACHED();
    return String();
}

String RTCPeerConnection::iceConnectionState() const
{
    switch (m_iceConnectionState) {
    case IceConnectionStateNew:
        return ASCIILiteral("new");
    case IceConnectionStateChecking:
        return ASCIILiteral("checking");
    case IceConnectionStateConnected:
        return ASCIILiteral("connected");
    case IceConnectionStateCompleted:
        return ASCIILiteral("completed");
    case IceConnectionStateFailed:
        return ASCIILiteral("failed");
    case IceConnectionStateDisconnected:
        return ASCIILiteral("disconnected");
    case IceConnectionStateClosed:
        return ASCIILiteral("closed");
    }

    ASSERT_NOT_REACHED();
    return String();
}

RTCConfiguration* RTCPeerConnection::getConfiguration() const
{
    return m_configuration.get();
}

void RTCPeerConnection::getStats(PassRefPtr<RTCStatsCallback>, PassRefPtr<RTCPeerConnectionErrorCallback>, PassRefPtr<MediaStreamTrack>)
{
}

PassRefPtr<RTCDataChannel> RTCPeerConnection::createDataChannel(String label, const Dictionary& options, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return nullptr;
    }

    RefPtr<RTCDataChannel> channel = RTCDataChannel::create(scriptExecutionContext(), m_backend.get(), label, options, ec);
    m_dataChannels.append(channel.get());
    return channel;
}

void RTCPeerConnection::close()
{
    if (m_signalingState == SignalingStateClosed)
        return;

    m_signalingState = SignalingStateClosed;
}

void RTCPeerConnection::stop()
{
    if (m_stopped)
        return;

    m_stopped = true;
    m_iceConnectionState = IceConnectionStateClosed;
    m_signalingState = SignalingStateClosed;
}

RTCPeerConnection::SignalingState RTCPeerConnection::targetSignalingState(SetterType setter, DescriptionType description) const
{
    // "map" describing the valid state transitions
    switch (m_signalingState) {
    case SignalingStateStable:
        if (setter == SetterTypeLocal && description == DescriptionTypeOffer)
            return SignalingStateHaveLocalOffer;
        if (setter == SetterTypeRemote && description == DescriptionTypeOffer)
            return SignalingStateHaveRemoteOffer;
        break;
    case SignalingStateHaveLocalOffer:
        if (setter == SetterTypeLocal && description == DescriptionTypeOffer)
            return SignalingStateHaveLocalOffer;
        if (setter == SetterTypeRemote && description == DescriptionTypeAnswer)
            return SignalingStateStable;
        break;
    case SignalingStateHaveRemoteOffer:
        if (setter == SetterTypeLocal && description == DescriptionTypeAnswer)
            return SignalingStateStable;
        if (setter == SetterTypeRemote && description == DescriptionTypeOffer)
            return SignalingStateHaveRemoteOffer;
        break;
    default:
        break;
    };
    return SignalingStateInvalid;
}

RTCPeerConnection::DescriptionType RTCPeerConnection::parseDescriptionType(const String& typeName) const
{
    if (typeName == "offer")
        return DescriptionTypeOffer;
    if (typeName == "pranswer")
        return DescriptionTypePranswer;

    ASSERT(typeName == "answer");
    return DescriptionTypeAnswer;
}

const char* RTCPeerConnection::activeDOMObjectName() const
{
    return "RTCPeerConnection";
}

bool RTCPeerConnection::canSuspendForPageCache() const
{
    // FIXME: We should try and do better here.
    return false;
}

void RTCPeerConnection::changeSignalingState(SignalingState signalingState)
{
    if (m_signalingState != SignalingStateClosed && m_signalingState != signalingState) {
        m_signalingState = signalingState;
        scheduleDispatchEvent(Event::create(eventNames().signalingstatechangeEvent, false, false));
    }
}

void RTCPeerConnection::changeIceGatheringState(IceGatheringState iceGatheringState)
{
    m_iceGatheringState = iceGatheringState;
}

void RTCPeerConnection::changeIceConnectionState(IceConnectionState iceConnectionState)
{
    if (m_iceConnectionState != IceConnectionStateClosed && m_iceConnectionState != iceConnectionState) {
        m_iceConnectionState = iceConnectionState;
        scheduleDispatchEvent(Event::create(eventNames().iceconnectionstatechangeEvent, false, false));
    }
}

void RTCPeerConnection::scheduleDispatchEvent(PassRefPtr<Event> event)
{
    m_scheduledEvents.append(event);

    if (!m_scheduledEventTimer.isActive())
        m_scheduledEventTimer.startOneShot(0);
}

void RTCPeerConnection::scheduledEventTimerFired()
{
    if (m_stopped)
        return;

    Vector<RefPtr<Event>> events;
    events.swap(m_scheduledEvents);

    Vector<RefPtr<Event>>::iterator it = events.begin();
    for (; it != events.end(); ++it)
        dispatchEvent((*it).release());

    events.clear();
}

ScriptExecutionContext* RTCPeerConnection::context() const
{
    return scriptExecutionContext();
}

Vector<RefPtr<RTCRtpSender>> RTCPeerConnection::senders() const
{
    return getSenders();
}

Vector<RefPtr<RTCDataChannel>> RTCPeerConnection::dataChannels() const
{
    return m_dataChannels;
}

// FIXME: consider doing this differently
void RTCPeerConnection::updateSignalingState()
{
    printf("-> RTCPeerConnection::updateSignalingState()\n");
    m_signalingState = m_nextSignalingState;
}

void RTCPeerConnection::scheduleEvent(RefPtr<Event>&& event)
{
    scheduleDispatchEvent(event.release());
}

bool RTCPeerConnection::isClosed() const
{
    return m_signalingState == SignalingStateClosed;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

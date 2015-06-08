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

#include "config.h"

#if ENABLE(MEDIA_STREAM)

#include "RTCPeerConnection.h"

#include "CryptoDigest.h"
#include "DOMError.h"
#include "Document.h"
#include "Event.h"
#include "ExceptionCode.h"
#include "Frame.h"
#include "MediaEndpointConfiguration.h"
#include "MediaEndpointConfigurationConversions.h"
#include "MediaEndpointInit.h"
#include "MediaStream.h"
#include "MediaStreamTrack.h"
#include "PeerMediaDescription.h"
#include "RTCConfiguration.h"
#include "RTCDataChannel.h"
#include "RTCDataChannelEvent.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCRtpReceiver.h"
#include "RTCRtpSender.h"
#include "RTCSessionDescription.h"
#include "RTCTrackEvent.h"
#include "UUID.h"
#include <wtf/MainThread.h>
#include <wtf/text/Base64.h>

// FIXME: Headers for sdp.js supported SDP conversion is kept on the side for now
#include "JSDOMWindow.h"
#include "ScriptController.h"
#include "ScriptGlobalObject.h"
#include "ScriptSourceCode.h"
#include "SDPScriptResource.h"
#include <bindings/ScriptObject.h>

namespace WebCore {

static RefPtr<MediaEndpointInit> createMediaEndpointInit(RTCConfiguration& rtcConfig)
{
    Vector<RefPtr<IceServerInfo>> iceServers;
    for (auto& server : rtcConfig.iceServers())
        iceServers.append(IceServerInfo::create(server->urls(), server->credential(), server->username()));

    return MediaEndpointInit::create(iceServers, rtcConfig.iceTransportPolicy(), rtcConfig.bundlePolicy());
}

PassRefPtr<RTCPeerConnection> RTCPeerConnection::create(ScriptExecutionContext& context, const Dictionary& rtcConfiguration, ExceptionCode& ec)
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
    return peerConnection.release();
}

RTCPeerConnection::RTCPeerConnection(ScriptExecutionContext& context, PassRefPtr<RTCConfiguration> configuration, ExceptionCode& ec)
    : ActiveDOMObject(&context)
    , m_signalingState(SignalingStateStable)
    , m_iceGatheringState(IceGatheringStateNew)
    , m_iceConnectionState(IceConnectionStateNew)
    , m_resolveSetLocalDescription(nullptr)
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

    m_mediaEndpoint = MediaEndpoint::create(this);
    if (!m_mediaEndpoint) {
        ec = NOT_SUPPORTED_ERR;
        return;
    }

    m_mediaEndpoint->setConfiguration(createMediaEndpointInit(*m_configuration));
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

static RTCRtpSender* takeFirstSenderOfType(Vector<RTCRtpSender*>& senders, const String& type)
{
    for (unsigned i = 0; i < senders.size(); ++i) {
        if (senders[i]->track()->kind() == type) {
            RTCRtpSender* sender = senders[i];
            senders.remove(i);
            return sender;
        }
    }
    return nullptr;
}

static void updateMediaDescriptionsWithSenders(const Vector<RefPtr<PeerMediaDescription>>& mediaDescriptions, Vector<RTCRtpSender*>& senders)
{
    // Remove any sender(s) from the senders list that already have their tracks represented by a media
    // description. Mark media descriptions that don't have a sender/track (anymore) as "available".
    for (auto& mdesc : mediaDescriptions) {
        const String& mdescTrackId = mdesc->mediaStreamTrackId();
        bool foundSender = senders.removeFirstMatching([mdescTrackId](const RTCRtpSender* sender) -> bool {
            return sender->track()->id() == mdescTrackId;
        });
        if (!foundSender) {
            mdesc->setMediaStreamId(emptyString());
            mdesc->setMediaStreamTrackId(emptyString());
        }
    }

    // Remove any sender(s) from the senders list that can be matched (by track type) to an "available"
    // media description. Mark media descriptions that don't get matched with a sender as receive only.
    for (auto& mdesc : mediaDescriptions) {
        if (mdesc->mediaStreamTrackId() != emptyString())
            continue;

        RTCRtpSender* sender = takeFirstSenderOfType(senders, mdesc->type());
        if (sender) {
            mdesc->setMediaStreamId(sender->mediaStreamId());
            mdesc->setMediaStreamTrackId(sender->track()->id());
            mdesc->setMode("sendrecv");
        } else
            mdesc->setMode("recvonly");
    }
}

// FIXME: This information should be fetched from the platform
static Vector<RefPtr<MediaPayload>> createDefaultPayloads(const String& type)
{
    Vector<RefPtr<MediaPayload>> payloads;
    RefPtr<MediaPayload> payload;

    if (type == "audio") {
        payload = MediaPayload::create();
        payload->setType(111);
        payload->setEncodingName("OPUS");
        payload->setClockRate(48000);
        payload->setChannels(2);
        payloads.append(payload);

        payload = MediaPayload::create();
        payload->setType(8);
        payload->setEncodingName("PCMA");
        payload->setClockRate(8000);
        payload->setChannels(1);
        payloads.append(payload);

        payload = MediaPayload::create();
        payload->setType(0);
        payload->setEncodingName("PCMU");
        payload->setClockRate(8000);
        payload->setChannels(1);
        payloads.append(payload);
    } else {
        // payload = MediaPayload::create();
        // payload->setType(103);
        // payload->setEncodingName("H264");
        // payload->setClockRate(90000);
        // payload->setCcmfir(true);
        // payload->setNackpli(true);
        // payload->addParameter("packetizationMode", 1);
        // payloads.append(payload);

        payload = MediaPayload::create();
        payload->setType(100);
        payload->setEncodingName("VP8");
        payload->setClockRate(90000);
        payload->setCcmfir(true);
        payload->setNackpli(true);
        payload->setNack(true);
        payloads.append(payload);

        payload = MediaPayload::create();
        payload->setType(120);
        payload->setEncodingName("RTX");
        payload->setClockRate(90000);
        payload->addParameter("apt", 100);
        payload->addParameter("rtxTime", 200);
        payloads.append(payload);
    }

    return payloads;
}

void RTCPeerConnection::createOffer(const Dictionary& offerOptions, OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    RefPtr<RTCOfferOptions> options = RTCOfferOptions::create(offerOptions, ec);
    if (ec) {
        RefPtr<DOMError> error = DOMError::create("Invalid createOffer argument.");
        rejectCallback(*error);
        return;
    }

    RefPtr<MediaEndpointConfiguration> configurationSnapshot = m_localConfiguration ?
        MediaEndpointConfigurationConversions::fromJSON(MediaEndpointConfigurationConversions::toJSON(m_localConfiguration.get())) : MediaEndpointConfiguration::create();

    Vector<RTCRtpSender*> senders;
    for (auto& sender : m_senderSet.values())
        senders.append(sender.get());

    updateMediaDescriptionsWithSenders(configurationSnapshot->mediaDescriptions(), senders);

    // Add media descriptions for remaining senders.
    for (auto sender : senders) {
        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();
        MediaStreamTrack* track = sender->track();

        mediaDescription->setMediaStreamId(sender->mediaStreamId());
        mediaDescription->setMediaStreamTrackId(track->id());
        mediaDescription->setType(track->kind());
        mediaDescription->setMode("sendrecv");
        mediaDescription->setPayloads(createDefaultPayloads(track->kind()));
        mediaDescription->setRtcpMux(true);
        mediaDescription->setDtlsSetup("actpass");

        configurationSnapshot->addMediaDescription(WTF::move(mediaDescription));
    }

    int extraMediaDescriptionCount = options->offerToReceiveAudio() + options->offerToReceiveVideo();
    for (int i = 0; i < extraMediaDescriptionCount; ++i) {
        String type = i < options->offerToReceiveAudio() ? "audio" : "video";
        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();

        mediaDescription->setType(type);
        mediaDescription->setMode("recvonly");
        mediaDescription->setPayloads(createDefaultPayloads(type));
        mediaDescription->setRtcpMux(true);
        mediaDescription->setDtlsSetup("actpass");

        configurationSnapshot->addMediaDescription(WTF::move(mediaDescription));
    }

    printf("RTCPeerConnection:createOffer Datachannel size %d \n",m_dataChannels.size());
    for (RefPtr<RTCDataChannel> dataChannel : m_dataChannels) {

        RefPtr<PeerMediaDescription> mediaDescription = PeerMediaDescription::create();

        mediaDescription->setType("application");
        //mediaDescription->setMode("recvonly");
        //mediaDescription->setRtcpMux(true);
        //mediaDescription->setDtlsSetup("actpass");

        configurationSnapshot->addMediaDescription(WTF::move(mediaDescription));
    }

    String json = MediaEndpointConfigurationConversions::toJSON(configurationSnapshot.get());
    RefPtr<RTCSessionDescription> offer = RTCSessionDescription::create("offer", toSDP(json));
    resolveCallback(*offer);
}

void RTCPeerConnection::createAnswer(const Dictionary& answerOptions, OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    RefPtr<RTCAnswerOptions> options = RTCAnswerOptions::create(answerOptions, ec);
    if (ec) {
        RefPtr<DOMError> error = DOMError::create("Invalid createAnswer argument.");
        rejectCallback(*error);
        return;
    }

    if (!m_remoteConfiguration) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidStateError (no remote description)");
        rejectCallback(*error);
        return;
    }

    RefPtr<MediaEndpointConfiguration> configurationSnapshot = m_localConfiguration ?
        MediaEndpointConfigurationConversions::fromJSON(MediaEndpointConfigurationConversions::toJSON(m_localConfiguration.get())) : MediaEndpointConfiguration::create();

    for (unsigned i = 0; i < m_remoteConfiguration->mediaDescriptions().size(); ++i) {
        RefPtr<PeerMediaDescription> remoteMediaDescription = m_remoteConfiguration->mediaDescriptions()[i];
        RefPtr<PeerMediaDescription> localMediaDescription;

        if (i < configurationSnapshot->mediaDescriptions().size())
            localMediaDescription = configurationSnapshot->mediaDescriptions()[i];
        else {
            localMediaDescription = PeerMediaDescription::create();
            localMediaDescription->setType(remoteMediaDescription->type());
            localMediaDescription->setDtlsSetup(remoteMediaDescription->dtlsSetup() == "active" ? "passive" : "active");

            configurationSnapshot->addMediaDescription(localMediaDescription.copyRef());
        }

        localMediaDescription->setPayloads(remoteMediaDescription->payloads());

        localMediaDescription->setRtcpMux(remoteMediaDescription->rtcpMux());

        if (localMediaDescription->dtlsSetup() == "actpass")
            localMediaDescription->setDtlsSetup("passive");
      
        if(localMediaDescription->type() == "application"){
          localMediaDescription->setPort(remoteMediaDescription->port());
        }
    }

    Vector<RTCRtpSender*> senders;
    for (auto& sender : m_senderSet.values())
        senders.append(sender.get());

    updateMediaDescriptionsWithSenders(configurationSnapshot->mediaDescriptions(), senders);

    String json = MediaEndpointConfigurationConversions::toJSON(configurationSnapshot.get());
    RefPtr<RTCSessionDescription> answer = RTCSessionDescription::create("answer", toSDP(json));
    resolveCallback(*answer);
}

void RTCPeerConnection::setLocalDescription(RTCSessionDescription* description, VoidResolveCallback resolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    printf("-> start setLocalDescription\n");
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    DescriptionType descriptionType = parseDescriptionType(description->type());
    SignalingState targetState = targetSignalingState(SetterTypeLocal, descriptionType);
    /*if (targetState == SignalingStateInvalid) {
        callOnMainThread([rejectCallback] {
            // FIXME: Error type?
            RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError");
            rejectCallback(error.get());
        });
        return;
    }*///not pass here
    unsigned previousNumberOfMediaDescriptions = m_localConfiguration != NULL ? m_localConfiguration->mediaDescriptions().size() : 0;
    printf("-> set m_localConfiguration\n");
    String json = fromSDP(description->sdp());
    m_localConfiguration = MediaEndpointConfigurationConversions::fromJSON(json);

    m_localConfigurationType = description->type();

    if (!m_localConfiguration) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError (unable to parse description)");
        rejectCallback(*error);
        return;
    }

    bool hasNewMediaDescriptions = m_localConfiguration->mediaDescriptions().size() > previousNumberOfMediaDescriptions;
    bool isInitiator = descriptionType == DescriptionTypeOffer;

    RefPtr<RTCPeerConnection> protectedThis(this);
    m_resolveSetLocalDescription = [targetState, resolveCallback, protectedThis]() mutable {
        protectedThis->m_signalingState = targetState;
        resolveCallback();
    };

    if (hasNewMediaDescriptions) {
        printf("-> setLocalDescription::prepareToReceive()\n");
        m_mediaEndpoint->prepareToReceive(m_localConfiguration.get(), isInitiator);
    }
        

    if (m_remoteConfiguration != NULL) {
        printf("-> setLocalDescription::prepareToSend()\n");
        m_mediaEndpoint->prepareToSend(m_remoteConfiguration.get(), isInitiator);
    }       
}

RefPtr<RTCSessionDescription> RTCPeerConnection::localDescription() const
{
    if (!m_localConfiguration)
        return nullptr;

    String json = MediaEndpointConfigurationConversions::toJSON(m_localConfiguration.get());
    return RTCSessionDescription::create(m_localConfigurationType, toSDP(json));
}

static Vector<RefPtr<MediaPayload>> filterPayloads(const Vector<RefPtr<MediaPayload>>& remotePayloads, const String& type)
{
    Vector<RefPtr<MediaPayload>> defaultPayloads = createDefaultPayloads(type);
    Vector<RefPtr<MediaPayload>> filteredPayloads;

    for (auto& remotePayload : remotePayloads) {
        MediaPayload* defaultPayload = nullptr;
        for (auto& p : defaultPayloads) {
            if (p->encodingName() == remotePayload->encodingName().upper()) {
                defaultPayload = p.get();
                break;
            }
        }
        if (!defaultPayload)
            continue;

        if (defaultPayload->parameters().contains("packetizationMode") && remotePayload->parameters().contains("packetizationMode")
            && (defaultPayload->parameters().get("packetizationMode") != defaultPayload->parameters().get("packetizationMode")))
            continue;

        filteredPayloads.append(remotePayload);
    }

    return filteredPayloads;
}

void RTCPeerConnection::setRemoteDescription(RTCSessionDescription* description, VoidResolveCallback resolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    printf("-> start setRemoteDescription\n");
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    DescriptionType descriptionType = parseDescriptionType(description->type());

    SignalingState targetState = targetSignalingState(SetterTypeRemote, descriptionType);
    /*if (targetState == SignalingStateInvalid) {
        callOnMainThread([rejectCallback] {
            // FIXME: Error type?
            RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError");
            rejectCallback(error.get());
        });
        return;
    }*/
    m_remoteConfiguration = MediaEndpointConfigurationConversions::fromJSON(description->sdp());
    printf("-> set m_remoteConfiguration\n");
    m_remoteConfigurationType = description->type();
    
    //bool isInitiator = descriptionType == DescriptionTypeAnswer;
  
    printf("-> setRemoteDescription::prepareToSend()\n");

    String json = fromSDP(description->sdp());
    m_remoteConfiguration = MediaEndpointConfigurationConversions::fromJSON(json);
    m_remoteConfigurationType = description->type();

    if (!m_remoteConfiguration) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidSessionDescriptionError (unable to parse description)");
        rejectCallback(*error);
        return;
    }

    Vector<RTCRtpSender*> senders;
    for (auto& sender : m_senderSet.values())
        senders.append(sender.get());

    for (auto& mediaDescription : m_remoteConfiguration->mediaDescriptions()) {
        if (mediaDescription->type() != "audio" && mediaDescription->type() != "video")
            continue;

        mediaDescription->setPayloads(filterPayloads(mediaDescription->payloads(), mediaDescription->type()));

        RTCRtpSender* sender = takeFirstSenderOfType(senders, mediaDescription->type());
        if (sender)
            mediaDescription->setSource(sender->track()->source());
    }

    bool isInitiator = m_remoteConfigurationType == "answer";
    m_mediaEndpoint->prepareToSend(m_remoteConfiguration.get(), isInitiator);

    // FIXME: event firing task should update state
    m_signalingState = targetState;

    resolveCallback();
}

RefPtr<RTCSessionDescription> RTCPeerConnection::remoteDescription() const
{
    if (!m_remoteConfiguration)
        return nullptr;

    String json = MediaEndpointConfigurationConversions::toJSON(m_remoteConfiguration.get());
    return RTCSessionDescription::create(m_remoteConfigurationType, toSDP(json));
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

    // FIXME: updateIce() might be renamed to setConfiguration(). It's also possible
    // that its current behavior with update deltas will change.
    m_mediaEndpoint->setConfiguration(createMediaEndpointInit(*m_configuration));
}

void RTCPeerConnection::addIceCandidate(RTCIceCandidate* rtcCandidate, VoidResolveCallback resolveCallback, RejectCallback rejectCallback, ExceptionCode& ec)
{
    if (m_signalingState == SignalingStateClosed) {
        ec = INVALID_STATE_ERR;
        return;
    }

    if (!m_remoteConfiguration) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidStateError (no remote description)");
        rejectCallback(*error);
        return;
    }

    String json = iceCandidateFromSDP(rtcCandidate->candidate());
    RefPtr<IceCandidate> candidate = MediaEndpointConfigurationConversions::iceCandidateFromJSON(json);
    if (!candidate) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("SyntaxError (malformed candidate)");
        rejectCallback(*error);
        return;
    }

    unsigned mdescIndex = rtcCandidate->sdpMLineIndex();
    if (mdescIndex >= m_remoteConfiguration->mediaDescriptions().size()) {
        // FIXME: Error type?
        RefPtr<DOMError> error = DOMError::create("InvalidSdpMlineIndex (sdpMLineIndex out of range");
        rejectCallback(*error);
        return;
    }

    PeerMediaDescription& mdesc = *m_remoteConfiguration->mediaDescriptions()[mdescIndex];
    mdesc.addIceCandidate(candidate.copyRef());

    m_mediaEndpoint->addRemoteCandidate(*candidate, mdescIndex, mdesc.iceUfrag(), mdesc.icePassword());

    resolveCallback();
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
    PassRefPtr<RTCDataChannel> channel = RTCDataChannel::create(scriptExecutionContext(), m_mediaEndpoint.get(), label, options, ec);
    m_dataChannels.append(channel.get());
    return channel;
}

void RTCPeerConnection::close()
{
    if (m_signalingState == SignalingStateClosed)
        return;

    m_mediaEndpoint->stop();

    m_signalingState = SignalingStateClosed;
}

void RTCPeerConnection::gotSendSSRC(unsigned mdescIndex, unsigned ssrc, const String& cname)
{
    printf("-> gotSendSSRC()\n");

    m_localConfiguration->mediaDescriptions()[mdescIndex]->addSsrc(ssrc);
    m_localConfiguration->mediaDescriptions()[mdescIndex]->setCname(cname);

    maybeResolveSetLocalDescription();
}

void RTCPeerConnection::gotDtlsCertificate(unsigned mdescIndex, const String& certificate)
{
    Vector<String> certificateRows;
    Vector<uint8_t> der;

    der.reserveCapacity(certificate.length() * 3/4 + 2);
    certificate.split("\n", certificateRows);

    for (auto& row : certificateRows) {
        if (row.startsWith("-----"))
            continue;

        Vector<uint8_t> decodedRow;
        if (!base64Decode(row, decodedRow, Base64FailOnInvalidCharacterOrExcessPadding)) {
            ASSERT_NOT_REACHED();
            return;
        }
        der.appendVector(decodedRow);
    }

    std::unique_ptr<CryptoDigest> digest = CryptoDigest::create(CryptoAlgorithmIdentifier::SHA_256);
    if (!digest) {
        ASSERT_NOT_REACHED();
        return;
    }

    digest->addBytes(der.data(), der.size());
    Vector<uint8_t> fingerprintVector = digest->computeHash();

    StringBuilder fingerprint;
    for (unsigned i = 0; i < fingerprintVector.size(); ++i)
        fingerprint.append(String::format(i ? ":%02X" : "%02X", fingerprintVector[i]));

    m_localConfiguration->mediaDescriptions()[mdescIndex]->setDtlsFingerprintHashFunction("sha-256");
    m_localConfiguration->mediaDescriptions()[mdescIndex]->setDtlsFingerprint(fingerprint.toString());

    if (maybeResolveSetLocalDescription() == SetLocalDescriptionResolvedSuccessfully)
        maybeDispatchGatheringDone();
}

void RTCPeerConnection::gotIceCandidate(unsigned mdescIndex, RefPtr<IceCandidate>&& candidate, const String& ufrag, const String& password)
{
    printf("-> gotIceCandidate()\n");

    ASSERT(scriptExecutionContext()->isContextThread());

    PeerMediaDescription& mdesc = *m_localConfiguration->mediaDescriptions()[mdescIndex];
    if (mdesc.iceUfrag().isEmpty()) {
        mdesc.setIceUfrag(ufrag);
        mdesc.setIcePassword(password);
    }
    
    mdesc.addIceCandidate(candidate.copyRef());

    if (!candidate->address().contains(':')) { // not IPv6
        if (candidate->componentId() == 1) { // RTP
            if (mdesc.address().isEmpty() || mdesc.address() == "0.0.0.0") {
                mdesc.setAddress(candidate->address());
                mdesc.setPort(candidate->port());
            }
        } else { // RTCP
            if (mdesc.rtcpAddress().isEmpty() || !mdesc.rtcpPort()) {
                mdesc.setRtcpAddress(candidate->address());
                mdesc.setRtcpPort(candidate->port());
            }
        }
    }

    ResolveSetLocalDescriptionResult result = maybeResolveSetLocalDescription();
   // if (result == SetLocalDescriptionResolvedSuccessfully)
    //    maybeDispatchGatheringDone();
   // else if (result == SetLocalDescriptionAlreadyResolved) {
        String candidateString = MediaEndpointConfigurationConversions::iceCandidateToJSON(candidate.get());
        String sdpFragment = iceCandidateToSDP(candidateString);
        RefPtr<RTCIceCandidate> iceCandidate = RTCIceCandidate::create(sdpFragment, "", mdescIndex);
        scheduleDispatchEvent(RTCIceCandidateEvent::create(false, false, WTF::move(iceCandidate)));
   // }
}

void RTCPeerConnection::doneGatheringCandidates(unsigned mdescIndex)
{
    printf("-> doneGatheringCandidates()\n");

    ASSERT(scriptExecutionContext()->isContextThread());

    m_localConfiguration->mediaDescriptions()[mdescIndex]->setIceCandidateGatheringDone(true);
    // Test: No trickle
    maybeResolveSetLocalDescription();
    maybeDispatchGatheringDone();
}

void RTCPeerConnection::gotDataChannel(unsigned mdescIndex, std::unique_ptr<RTCDataChannelHandler> handler)
{
    printf("-> gotDataChannel()\n");

    PassRefPtr<RTCDataChannel> channel = RTCDataChannel::create(scriptExecutionContext(), WTF::move(handler));
    m_dataChannels.append(channel.get());
    scheduleDispatchEvent(RTCDataChannelEvent::create(false, false, WTF::move(channel)));
}
  
void RTCPeerConnection::gotRemoteSource(unsigned mdescIndex, RefPtr<RealtimeMediaSource>&& source)
{
    if (m_signalingState == SignalingStateClosed)
        return;

    if (mdescIndex >= m_remoteConfiguration->mediaDescriptions().size()) {
        printf("Warning: No remote configuration for incoming source.\n");
        return;
    }

    PeerMediaDescription& mediaDescription = *m_remoteConfiguration->mediaDescriptions()[mdescIndex];
    String trackId = mediaDescription.mediaStreamTrackId();

    if (trackId.isEmpty()) {
        // Non WebRTC media description (e.g. legacy)
        trackId = createCanonicalUUIDString();
    }

    // FIXME: track should be set to muted (not supported yet)
    // FIXME: MediaStream handling not implemented

    RefPtr<MediaStreamTrackPrivate> trackPrivate = MediaStreamTrackPrivate::create(WTF::move(source), trackId);
    RefPtr<MediaStreamTrack> track = MediaStreamTrack::create(*scriptExecutionContext(), *trackPrivate);
    RefPtr<RTCRtpReceiver> receiver = RTCRtpReceiver::create(track.copyRef());

    scheduleDispatchEvent(RTCTrackEvent::create(eventNames().trackEvent, false, false, WTF::move(receiver), WTF::move(track)));
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

bool RTCPeerConnection::isLocalConfigurationComplete() const
{
    for (auto& mdesc : m_localConfiguration->mediaDescriptions()) {
        if (mdesc->dtlsFingerprint().isEmpty() || mdesc->iceUfrag().isEmpty())
            return false;
        // Test: No trickle
        if (!mdesc->iceCandidateGatheringDone())
            return false;
        if (mdesc->type() == "audio" || mdesc->type() == "video") {
            if (!mdesc->ssrcs().size() || mdesc->cname().isEmpty())
                return false;
        }
    }

    return true;
}

RTCPeerConnection::ResolveSetLocalDescriptionResult RTCPeerConnection::maybeResolveSetLocalDescription()
{
    if (!m_resolveSetLocalDescription) {
        ASSERT(isLocalConfigurationComplete());
        return SetLocalDescriptionAlreadyResolved;
    }

    if (isLocalConfigurationComplete()) {
        m_resolveSetLocalDescription();
        m_resolveSetLocalDescription = nullptr;
        return SetLocalDescriptionResolvedSuccessfully;
    }

    return LocalConfigurationIncomplete;
}

void RTCPeerConnection::maybeDispatchGatheringDone()
{
    if (!isLocalConfigurationComplete())
        return;

    for (auto& mdesc : m_localConfiguration->mediaDescriptions()) {
        if (!mdesc->iceCandidateGatheringDone())
            return;
    }

    scheduleDispatchEvent(RTCIceCandidateEvent::create(false, false, nullptr));
}

String RTCPeerConnection::toSDP(const String& json) const
{
    return sdpConversion("toSDP", json);
}

String RTCPeerConnection::fromSDP(const String& sdp) const
{
    return sdpConversion("fromSDP", sdp);
}

String RTCPeerConnection::iceCandidateToSDP(const String& json) const
{
    return sdpConversion("iceCandidateToSDP", json);
}

String RTCPeerConnection::iceCandidateFromSDP(const String& sdpFragment) const
{
    return sdpConversion("iceCandidateFromSDP", sdpFragment);
}

String RTCPeerConnection::sdpConversion(const String& functionName, const String& argument) const
{
    Document* document = downcast<Document>(scriptExecutionContext());

    if (!m_isolatedWorld)
        m_isolatedWorld = DOMWrapperWorld::create(JSDOMWindow::commonVM());

    ScriptController& scriptController = document->frame()->script();
    JSDOMGlobalObject* globalObject = JSC::jsCast<JSDOMGlobalObject*>(scriptController.globalObject(*m_isolatedWorld));
    JSC::ExecState* exec = globalObject->globalExec();
    JSC::JSLockHolder lock(exec);

    JSC::JSValue probeFunctionValue = globalObject->get(exec, JSC::Identifier::fromString(exec, "toSDP"));
    if (!probeFunctionValue.isFunction()) {
        URL scriptURL;
        scriptController.evaluateInWorld(ScriptSourceCode(SDPScriptResource::getString(), scriptURL), *m_isolatedWorld);
        if (exec->hadException()) {
            exec->clearException();
            return emptyString();
        }
    }

    JSC::JSValue functionValue = globalObject->get(exec, JSC::Identifier::fromString(exec, functionName));
    if (!functionValue.isFunction())
        return emptyString();

    JSC::JSObject* function = functionValue.toObject(exec);
    JSC::CallData callData;
    JSC::CallType callType = function->methodTable()->getCallData(function, callData);
    if (callType == JSC::CallTypeNone)
        return emptyString();

    JSC::MarkedArgumentBuffer argList;
    argList.append(JSC::jsString(exec, argument));

    JSC::JSValue result = JSC::call(exec, function, callType, callData, globalObject, argList);
    if (exec->hadException()) {
        printf("sdpConversion: js function (%s) threw\n", functionName.ascii().data());
        exec->clearException();
        return emptyString();
    }

    return result.isString() ? result.getString(exec) : emptyString();
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

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

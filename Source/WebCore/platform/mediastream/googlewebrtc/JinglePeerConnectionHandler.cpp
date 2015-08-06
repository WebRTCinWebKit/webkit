/*
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



#include "config.h"

#if ENABLE(MEDIA_STREAM)
#if ENABLE(GOOGLE_WEBRTC)
#include "JinglePeerConnectionHandler.h"

#include "DTLSIdentityService.h"
#include "MediaEndpointConfigurationConversions.h"
#include "PeerConnectionBackend.h"
#include "RTCConfiguration.h"
#include "RTCDataChannel.h"
#include "RTCDataChannelEvent.h"
#include "RTCDataChannelHandlerGoogle.h"
#include "RTCIceCandidate.h"
#include "RTCIceCandidateEvent.h"
#include "RTCOfferAnswerOptions.h"
#include "RTCSessionDescription.h"

 // FIXME: Headers for sdp.js supported SDP conversion is kept on the side for now
#include "Document.h"
#include "Frame.h"
#include "JSDOMWindow.h"
#include "ScriptController.h"
#include "ScriptGlobalObject.h"
#include "ScriptSourceCode.h"
#include "SDPScriptResource.h"
#include <bindings/ScriptObject.h>
#include <wtf/text/Base64.h>

#include <talk/app/webrtc/jsepicecandidate.h>
#include <talk/app/webrtc/jsepsessiondescription.h>
#include <talk/app/webrtc/portallocatorfactory.h>
//#include <talk/app/webrtc/test/fakedtlsidentityservice.h>
#include <webrtc/base/thread.h>
#include "pk11pub.h"
namespace WebCore {

static std::unique_ptr<PeerConnectionBackend> createJinglePeerConnectionHandler(PeerConnectionBackendClient* client)
{
    return std::unique_ptr<PeerConnectionBackend>(new JinglePeerConnectionHandler(client));
}

CreatePeerConnectionBackend PeerConnectionBackend::create = createJinglePeerConnectionHandler;

static RefPtr<MediaEndpointInit> createMediaEndpointInit(RTCConfiguration& rtcConfig)
{
    /*Vector<RefPtr<IceServerInfo>> iceServers;
    for (auto& server : rtcConfig.iceServers())
        iceServers.append(IceServerInfo::create(server->urls(), server->credential(), server->username()));

    return MediaEndpointInit::create(iceServers, rtcConfig.iceTransportPolicy(), rtcConfig.bundlePolicy());*/
}

JinglePeerConnectionHandler::JinglePeerConnectionHandler(PeerConnectionBackendClient* client)
    : m_client(client)
    //, m_peerConnectionFactory(webrtc::CreatePeerConnectionFactory())
{
    printf("----> JinglePeerConnectionHandler()\n");
    //m_constraints = new WebRTCMediaConstraints();
    rtc::Thread* worker = new rtc::Thread;
    m_signaling_thread = new rtc::Thread();
    //signaling->Start();
    worker->Start();
    m_signaling_thread->Start();
    m_signaling_thread->WrapCurrent();

    //rtc::ThreadManager::Instance()->SetCurrentThread(worker);
    printf("----> JinglePeerConnectionHandler() 2\n");
    m_peerConnectionFactory = webrtc::CreatePeerConnectionFactory(worker, m_signaling_thread, NULL, NULL, NULL);
    //m_peerConnectionFactory = webrtc::CreatePeerConnectionFactory();
    printf("----> JinglePeerConnectionHandler() 3\n");
}

JinglePeerConnectionHandler::~JinglePeerConnectionHandler()
{
}

void JinglePeerConnectionHandler::createOffer(const RefPtr<RTCOfferOptions>& options, OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    printf("----> createOffer()\n");
    rtc::scoped_refptr<JingleCreateSessionDescriptionObserver> observer(new rtc::RefCountedObject<JingleCreateSessionDescriptionObserver>(this, resolveCallback, rejectCallback));
    m_constraints.SetMandatoryReceiveAudio(options->offerToReceiveAudio());
    m_constraints.SetMandatoryReceiveVideo(options->offerToReceiveVideo());
    m_constraints.SetMandatoryIceRestart(options->iceRestart());
    m_peerConnection->CreateOffer(observer, &m_constraints);

    //m_peerConnection->CreateOffer(observer, NULL);
    printf("----> createOffer() END\n");
}

void JinglePeerConnectionHandler::createAnswer(const RefPtr<RTCAnswerOptions>&, OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    printf("----> createAnswer()\n");
    rtc::scoped_refptr<JingleCreateSessionDescriptionObserver> observer(new rtc::RefCountedObject<JingleCreateSessionDescriptionObserver>(this, resolveCallback, rejectCallback));

    //m_peerConnection->CreateAnswer(observer, &m_constraints);
    m_peerConnection->CreateAnswer(observer, NULL);
    printf("----> createAnswer() END\n");
}

void JinglePeerConnectionHandler::setLocalDescription(RTCSessionDescription* description, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    printf("----> setLocalDescription()\n");
    String sdp = description->sdp();
    String type = description->type();
    m_localConfiguration = MediaEndpointConfigurationConversions::fromJSON(fromSDP(sdp));
    m_localConfigurationType = type;
    webrtc::SessionDescriptionInterface* desc = webrtc::CreateSessionDescription(type.utf8().data(), sdp.utf8().data(), NULL);
    rtc::scoped_refptr<JingleSetSessionDescriptionObserver> observer(new rtc::RefCountedObject<JingleSetSessionDescriptionObserver>(m_client, resolveCallback, rejectCallback));
    m_peerConnection->SetLocalDescription(observer, desc);
    printf("----> setLocalDescription() END\n");
}

RefPtr<RTCSessionDescription> JinglePeerConnectionHandler::localDescription() const
{
    if (!m_localConfiguration)
        return nullptr;

    String json = MediaEndpointConfigurationConversions::toJSON(m_localConfiguration.get());
    return RTCSessionDescription::create(m_localConfigurationType, toSDP(json));
}

void JinglePeerConnectionHandler::setRemoteDescription(RTCSessionDescription* description, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    printf("----> setRemoteDescription()\n");
    String sdp = description->sdp();
    String type = description->type();
    m_remoteConfiguration = MediaEndpointConfigurationConversions::fromJSON(fromSDP(sdp));
    m_remoteConfigurationType = type;
    webrtc::SessionDescriptionInterface* desc = webrtc::CreateSessionDescription(type.utf8().data(), sdp.utf8().data(), NULL);
    rtc::scoped_refptr<JingleSetSessionDescriptionObserver> observer(new rtc::RefCountedObject<JingleSetSessionDescriptionObserver>(m_client, resolveCallback, rejectCallback));
    m_peerConnection->SetRemoteDescription(observer, desc);
    printf("----> setRemoteDescription() END\n");
}

RefPtr<RTCSessionDescription> JinglePeerConnectionHandler::remoteDescription() const
{
    if (!m_remoteConfiguration)
        return nullptr;

    String json = MediaEndpointConfigurationConversions::toJSON(m_remoteConfiguration.get());
    return RTCSessionDescription::create(m_remoteConfigurationType, toSDP(json));
}

void JinglePeerConnectionHandler::setConfiguration(RTCConfiguration& configuration)
{
    // FIXME: updateIce() might be renamed to setConfiguration(). It's also possible
    // that its current behavior with update deltas will change.
    printf("----> setConfiguration()\n");
    m_configuration = adoptRef(configuration);

    m_google_configuration = new webrtc::PeerConnectionInterface::RTCConfiguration();
    webrtc::PeerConnectionInterface::IceServer google_ice_server;

    for (auto& server : m_configuration->iceServers())
    {
        for (String uri : server->urls())
        {
            google_ice_server.uri = uri.utf8().data();
            google_ice_server.username = server->username().utf8().data();
            google_ice_server.password = server->credential().utf8().data();
            m_google_configuration->servers.push_back(google_ice_server);
        }       
    }

    if(m_configuration->iceTransportPolicy() == "kNone")
        m_google_configuration->type = webrtc::PeerConnectionInterface::kNone;
    else if(m_configuration->iceTransportPolicy() == "kRelay")
        m_google_configuration->type = webrtc::PeerConnectionInterface::kRelay;
    else if(m_configuration->iceTransportPolicy() == "kNoHost")
        m_google_configuration->type = webrtc::PeerConnectionInterface::kNoHost;
    else if(m_configuration->iceTransportPolicy() == "kAll")
        m_google_configuration->type = webrtc::PeerConnectionInterface::kAll;

    if(m_configuration->bundlePolicy() == "kBundlePolicyBalanced")
        m_google_configuration->bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyBalanced;
    else if(m_configuration->bundlePolicy() == "kBundlePolicyMaxBundle")
        m_google_configuration->bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
    else if(m_configuration->bundlePolicy() == "kBundlePolicyMaxCompat")
        m_google_configuration->bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxCompat;

    printf("----> setConfiguration()\n");
    createPeerConnection();
}

void JinglePeerConnectionHandler::addIceCandidate(RTCIceCandidate* rtcCandidate, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
{
    printf("----> addIceCandidate()\n");
    String candidate = rtcCandidate->candidate();
    String sdpMid = rtcCandidate->sdpMid();
    int sdpMLineIndex = rtcCandidate->sdpMLineIndex();

    webrtc::IceCandidateInterface* iceCandidate = webrtc::CreateIceCandidate(sdpMid.utf8().data(), sdpMLineIndex, candidate.utf8().data(), NULL);
    m_peerConnection->AddIceCandidate(iceCandidate);
}

std::unique_ptr<RTCDataChannelHandler> JinglePeerConnectionHandler::createDataChannel(const String& label, RTCDataChannelInit_Endpoint& initData)
{
    printf("----> createDataChannel()\n");
    webrtc::InternalDataChannelInit webrtc_initData;
    webrtc_initData.ordered = initData.ordered;
    webrtc_initData.maxRetransmitTime = 5000;
    webrtc_initData.maxRetransmits = -1;
    webrtc_initData.protocol = initData.protocol.utf8().data();
    webrtc_initData.negotiated = initData.negotiated;
    webrtc_initData.id = initData.id;
    printf("----> DataChannel BEFORE LAUNCH\n");
    if(!m_peerConnection)
        printf("----> m_peerConnection NULL\n");
    rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel = m_peerConnection->CreateDataChannel(label.utf8().data(), &webrtc_initData);
    //rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel = m_peerConnection->CreateDataChannel(label.utf8().data(), NULL);
    std::unique_ptr<RTCDataChannelHandler> handler = RTCDataChannelHandler::create(label, initData.ordered, 5000, -1, initData.protocol, initData.negotiated, initData.id, data_channel.get());
    
    
    return handler;
}

void JinglePeerConnectionHandler::stop()
{
    printf("----> stop()\n");
}

// Triggered when the SignalingState changed.
void JinglePeerConnectionHandler::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state)
{
    printf("----> OnSignalingChange()\n");
}

// Triggered when SignalingState or IceState have changed.
// TODO(bemasc): Remove once callers transition to OnSignalingChange.
void JinglePeerConnectionHandler::OnStateChange(webrtc::PeerConnectionObserver::StateType state_changed)
{
    printf("----> OnStateChange()\n");
}

// Triggered when media is received on a new stream from remote peer.
void JinglePeerConnectionHandler::OnAddStream(webrtc::MediaStreamInterface* stream)
{
    printf("----> OnAddStream()\n");
}

// Triggered when a remote peer close a stream.
void JinglePeerConnectionHandler::OnRemoveStream(webrtc::MediaStreamInterface* stream)
{
    printf("----> OnRemoveStream()\n");
}

// Triggered when a remote peer open a data channel.
void JinglePeerConnectionHandler::OnDataChannel(webrtc::DataChannelInterface* data_channel)
{
    printf("----> gotDataChannel()\n");
    std::unique_ptr<RTCDataChannelHandler> handler = RTCDataChannelHandler::create(String::fromUTF8(data_channel->label().c_str()), data_channel->ordered(), data_channel->maxRetransmitTime(), data_channel->maxRetransmits(), String::fromUTF8(data_channel->protocol().c_str()), data_channel->negotiated(), data_channel->id(), data_channel);
    PassRefPtr<RTCDataChannel> channel = RTCDataChannel::create(m_client->context(), WTF::move(handler));
    m_client->scheduleEvent(RTCDataChannelEvent::create(eventNames().datachannelEvent, false, false, WTF::move(channel)));
}

// Triggered when renegotiation is needed, for example the ICE has restarted.
void JinglePeerConnectionHandler::OnRenegotiationNeeded() 
{
    printf("----> OnRenegotiationNeeded()\n");
}

// Called any time the IceConnectionState changes
void JinglePeerConnectionHandler::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state)
{
     printf("----> OnIceConnectionChange()\n");
}

// Called any time the IceGatheringState changes
void JinglePeerConnectionHandler::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) 
{
    printf("----> OnIceGatheringChange()\n");
}

// New Ice candidate have been found.
void JinglePeerConnectionHandler::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) 
{
    printf("----> OnIceCandidate()\n");
    std::string* sdpIceCandidate;
    candidate->ToString(sdpIceCandidate);
    RefPtr<RTCIceCandidate> iceCandidate = RTCIceCandidate::create(String::fromUTF8(sdpIceCandidate->c_str()), "",candidate->sdp_mline_index());
    m_client->scheduleEvent(RTCIceCandidateEvent::create(false, false, WTF::move(iceCandidate)));
}

// TODO(bemasc): Remove this once callers transition to OnIceGatheringChange.
// All Ice candidates have been found.
void JinglePeerConnectionHandler::OnIceComplete() 
{
    printf("----> OnIceComplete()\n");
}


// Called when the ICE connection receiving status changes.
void JinglePeerConnectionHandler::OnIceConnectionReceivingChange(bool receiving) 
{
    printf("----> OnIceConnectionReceivingChange()\n");
}

String JinglePeerConnectionHandler::toSDP(const String& json) const
{
    return sdpConversion("toSDP", json);
}

String JinglePeerConnectionHandler::fromSDP(const String& sdp) const
{
    return sdpConversion("fromSDP", sdp);
}

String JinglePeerConnectionHandler::iceCandidateToSDP(const String& json) const
{
    return sdpConversion("iceCandidateToSDP", json);
}

String JinglePeerConnectionHandler::iceCandidateFromSDP(const String& sdpFragment) const
{
    return sdpConversion("iceCandidateFromSDP", sdpFragment);

}

String JinglePeerConnectionHandler::sdpConversion(const String& functionName, const String& argument) const
{
    
    Document* document = downcast<Document>(m_client->context());

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
        printf("sdpConversion: js function (%s) threw\n", functionName.utf8().data());
        exec->clearException();
        return emptyString();
    }

    return result.isString() ? result.getString(exec) : emptyString();
}


rtc::scoped_refptr<webrtc::PeerConnectionInterface> JinglePeerConnectionHandler::createPeerConnection()
{
    printf("----> createPeerConnection()\n");
    rtc::scoped_refptr<webrtc::PortAllocatorFactoryInterface> allocator_factory_;
    //webrtc::DTLSIdentityServiceInterface* dtls_identity_service = dynamic_cast<webrtc::DTLSIdentityServiceInterface*>(new DTLSIdentityService());
    webrtc::DTLSIdentityServiceInterface* dtls_identity_service = new DTLSIdentityService(m_signaling_thread);
    //webrtc::DTLSIdentityServiceInterface* dtls_identity_service = new FakeIdentityService();
    
    m_constraints.SetAllowDtlsSctpDataChannels();
    //m_constraints.SetAllowRtpDataChannels();
    m_peerConnection = m_peerConnectionFactory->CreatePeerConnection(*m_google_configuration, &m_constraints, NULL, NULL, this);
    
    printf("PK11_ImportDERPrivateKeyInfoAndReturnKey BEFORE\n");
    SECKEYPrivateKey* privkey = NULL;
    //PK11_ImportDERPrivateKeyInfoAndReturnKey(NULL, NULL, NULL, NULL, PR_FALSE, PR_FALSE, 2, &privkey, NULL);
    printf("PK11_ImportDERPrivateKeyInfoAndReturnKey AFTER\n");





printf("----> createPeerConnection() 2 \n");

    m_peerConnection = m_peerConnectionFactory->CreatePeerConnection(*m_google_configuration, &m_constraints, NULL, dtls_identity_service, this);
printf("----> createPeerConnection() 3 \n");
}

webrtc::PeerConnectionInterface::RTCConfiguration JinglePeerConnectionHandler::toGoogleRTCConfiguration ()
{
    
}



} // namespace WebCore

#endif // ENABLE(GOOGLE_WEBRTC)
#endif // ENABLE(MEDIA_STREAM)
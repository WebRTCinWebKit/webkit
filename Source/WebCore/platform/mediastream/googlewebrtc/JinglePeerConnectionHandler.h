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

#ifndef JinglePeerConnectionHandler_h
#define JinglePeerConnectionHandler_h


#if ENABLE(MEDIA_STREAM)
#if ENABLE(GOOGLE_WEBRTC)
#include "RTCIceCandidateEvent.h"
#include "DOMError.h"
#include "MediaEndpointConfigurationConversions.h"
#include "PeerConnectionBackend.h"
#include "RTCSessionDescription.h"
#include "WebRTCMediaConstraints.h"

#include <talk/app/webrtc/peerconnectioninterface.h>
#include <talk/app/webrtc/peerconnectionfactory.h>
#include <talk/app/webrtc/datachannel.h>
#include <wtf/RefPtr.h>
#include <wtf/RefCounted.h>
#include <wtf/text/WTFString.h>

/*
#define V8_DEPRECATION_WARNINGS 
#define EXPAT_RELATIVE_PATH 
#define FEATURE_ENABLE_VOICEMAIL 
#define GTEST_RELATIVE_PATH 
#define JSONCPP_RELATIVE_PATH 
#define LOGGING 1 
#define SRTP_RELATIVE_PATH 
#define FEATURE_ENABLE_SSL 
#define FEATURE_ENABLE_PSTN 
#define HAVE_SCTP 
#define HAVE_SRTP 
#define HAVE_WEBRTC_VIDEO 
#define HAVE_WEBRTC_VOICE 
#define _FILE_OFFSET_BITS 64 
#define CHROMIUM_BUILD 
#define CR_CLANG_REVISION "233105-2"
#define TOOLKIT_VIEWS 1 
#define UI_COMPOSITOR_IMAGE_TRANSPORT 
#define USE_AURA 1 
#define USE_ASH 1 
#define USE_PANGO 1 
#define USE_CAIRO 1 
#define USE_DEFAULT_RENDER_THEME 1 
#define USE_LIBJPEG_TURBO 1 
#define USE_X11 1 
#define USE_CLIPBOARD_AURAX11 1 
#define ENABLE_ONE_CLICK_SIGNIN 
#define ENABLE_PRE_SYNC_BACKUP 
#define ENABLE_REMOTING 1 
#define ENABLE_WEBRTC 1 
#define ENABLE_MEDIA_ROUTER 1 
#define ENABLE_PEPPER_CDMS 
#define ENABLE_CONFIGURATION_POLICY 
#define ENABLE_NOTIFICATIONS 
#define ENABLE_HIDPI 1 
#define USE_UDEV 
#define DONT_EMBED_BUILD_METADATA 
#define ENABLE_TASK_MANAGER 1 
#define ENABLE_EXTENSIONS 1 
#define ENABLE_PLUGINS 1 
#define ENABLE_SESSION_SERVICE 1 
#define ENABLE_THEMES 1 
#define ENABLE_AUTOFILL_DIALOG 1 
#define ENABLE_BACKGROUND 1 
#define ENABLE_GOOGLE_NOW 1 
#define CLD_VERSION 2 
#define ENABLE_PRINTING 1 
#define ENABLE_BASIC_PRINTING 1 
#define ENABLE_PRINT_PREVIEW 1 
#define ENABLE_SPELLCHECK 1 
#define ENABLE_CAPTIVE_PORTAL_DETECTION 1 
#define ENABLE_APP_LIST 1 
#define ENABLE_SETTINGS_APP 1 
#define ENABLE_SUPERVISED_USERS 1 
#define ENABLE_MDNS 1 
#define ENABLE_SERVICE_DISCOVERY 1 
#define V8_USE_EXTERNAL_STARTUP_DATA 
#define FULL_SAFE_BROWSING 
#define SAFE_BROWSING_CSD 
#define SAFE_BROWSING_DB_LOCAL 
#define SAFE_BROWSING_SERVICE 
#define LIBPEERCONNECTION_LIB 1 

#define HASH_NAMESPACE "__gnu_cxx" 
#define DISABLE_DYNAMIC_CAST 
#define _REENTRANT 
#define USE_LIBPCI 1 
#define USE_GLIB 1 
#define USE_NSS_CERTS 1 
#define NDEBUG 
#define NVALGRIND 
#define DYNAMIC_ANNOTATIONS_ENABLED 0
*/

namespace WebCore {

class DOMWrapperWorld;

class JinglePeerConnectionHandler : public PeerConnectionBackend, public webrtc::PeerConnectionObserver {
public:
    JinglePeerConnectionHandler(PeerConnectionBackendClient*);
    ~JinglePeerConnectionHandler();

    void createOffer(const RefPtr<RTCOfferOptions>&, OfferAnswerResolveCallback, RejectCallback) override;
    void createAnswer(const RefPtr<RTCAnswerOptions>&, OfferAnswerResolveCallback, RejectCallback) override;

    void setLocalDescription(RTCSessionDescription*, VoidResolveCallback, RejectCallback) override;
    RefPtr<RTCSessionDescription> localDescription() const override;

    void setRemoteDescription(RTCSessionDescription*, VoidResolveCallback, RejectCallback) override;
    RefPtr<RTCSessionDescription> remoteDescription() const override;

    void setConfiguration(RTCConfiguration&) override;
    void addIceCandidate(RTCIceCandidate*, VoidResolveCallback, RejectCallback) override;

    std::unique_ptr<RTCDataChannelHandler> createDataChannel(const String& label, RTCDataChannelInit_Endpoint& initData) override;

    void stop() override;

private:

    // PeerConnectionObserver
    virtual void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override;
    virtual void OnStateChange(webrtc::PeerConnectionObserver::StateType state_changed) override;
    virtual void OnAddStream(webrtc::MediaStreamInterface* stream) override;
    virtual void OnRemoveStream(webrtc::MediaStreamInterface* stream) override;
    virtual void OnDataChannel(webrtc::DataChannelInterface* data_channel) override;
    virtual void OnRenegotiationNeeded() override;
    virtual void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state)override;
    virtual void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
    virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
    virtual void OnIceComplete() override;
    virtual void OnIceConnectionReceivingChange(bool receiving);

    String toSDP(const String& json) const;
    String fromSDP(const String& sdp) const;
    String iceCandidateToSDP(const String& json) const;
    String iceCandidateFromSDP(const String& sdpFragment) const;
    String sdpConversion(const String& functionName, const String& argument) const;



    rtc::scoped_refptr<webrtc::PeerConnectionInterface> createPeerConnection();
    webrtc::PeerConnectionInterface::RTCConfiguration toGoogleRTCConfiguration ();


    PeerConnectionBackendClient* m_client;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_peerConnectionFactory;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> m_peerConnection;
    WebRTCMediaConstraints m_constraints;

    mutable RefPtr<DOMWrapperWorld> m_isolatedWorld;

    RefPtr<MediaEndpointConfiguration> m_localConfiguration;
    RefPtr<MediaEndpointConfiguration> m_remoteConfiguration;

    String m_localConfigurationType;
    String m_remoteConfigurationType;

    std::function<void()> m_resolveSetLocalDescription;

    RefPtr<RTCConfiguration> m_configuration;
    webrtc::PeerConnectionInterface::RTCConfiguration* m_google_configuration;

    rtc::Thread* m_signaling_thread;

    class JingleCreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver {
    public:
        JingleCreateSessionDescriptionObserver(OfferAnswerResolveCallback resolveCallback, RejectCallback rejectCallback)
            : m_resolveCallback(resolveCallback)
            , m_rejectCallback(rejectCallback)
        {

        }
        virtual ~JingleCreateSessionDescriptionObserver() {}
        virtual void OnSuccess(webrtc::SessionDescriptionInterface* desc) 
        {
            printf("----> successCreateSessionDescription() : type = %s\n", desc->type().c_str());
            std::string out;

            desc->ToString(&out);
            String json = String::fromUTF8(out.c_str());
            String type = String::fromUTF8(desc->type().c_str());

            RefPtr<RTCSessionDescription> offer = RTCSessionDescription::create(type, json);
            m_resolveCallback(*offer);
        }
        virtual void OnFailure(const std::string& error) 
        {
            printf("----> errorCreateSessionDescription() :  %s\n", error.c_str() );
            RefPtr<DOMError> err = DOMError::create(error.c_str());
            m_rejectCallback(*err);
        }

    private:
        OfferAnswerResolveCallback m_resolveCallback;
        RejectCallback m_rejectCallback;
    };

    class JingleSetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
    public:
        JingleSetSessionDescriptionObserver(PeerConnectionBackendClient* client, VoidResolveCallback resolveCallback, RejectCallback rejectCallback)
            : m_resolveCallback(resolveCallback)
            , m_rejectCallback(rejectCallback)
            , m_client(client)  
        {

        }
        ~JingleSetSessionDescriptionObserver() {}
        void OnSuccess() 
        {
            printf("----> successSetSessionDescriptionObserver()\n");
            m_client->scheduleEvent(RTCIceCandidateEvent::create(false, false, nullptr));
            m_client->updateSignalingState();
            //m_resolveCallback();
        }
        void OnFailure(const std::string& error) 
        {
            printf("----> errorSetSessionDescriptionObserver() :  %s\n", error.c_str());
            RefPtr<DOMError> err = DOMError::create(error.c_str());
            m_rejectCallback(*err);
        }

    private:
        VoidResolveCallback m_resolveCallback;
        RejectCallback m_rejectCallback;
        PeerConnectionBackendClient* m_client;
    };
};

} // namespace WebCore

#endif // ENABLE(GOOGLE_WEBRTC)
#endif // ENABLE(MEDIA_STREAM)

#endif // JinglePeerConnectionHandler_h

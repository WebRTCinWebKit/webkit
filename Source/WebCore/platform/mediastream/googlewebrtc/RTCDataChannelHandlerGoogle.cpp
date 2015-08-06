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
 * 3. Neither the name of Temasys nor the names of its contributors
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
#include "RTCDataChannelHandlerGoogle.h"

#include "MediaEndpoint.h"
#include "RTCDataChannelHandlerClient.h"
#include <wtf/text/CString.h>

namespace WebCore {

static std::unique_ptr<RTCDataChannelHandler> createRTCDataChannelHandlerGoogle(const String& label, bool ordered, unsigned short maxRetransmitTime, unsigned short maxRetransmits, const String& protocol, bool negotiated, unsigned short id, void* channel)
{
    return std::unique_ptr<RTCDataChannelHandler>(new RTCDataChannelHandlerGoogle(label, ordered, maxRetransmitTime, maxRetransmits, protocol, negotiated, id, channel));
}

CreateRTCDataChannelHandler RTCDataChannelHandler::create = createRTCDataChannelHandlerGoogle;

RTCDataChannelHandlerGoogle::RTCDataChannelHandlerGoogle(const String& label, bool ordered, unsigned short maxRetransmitTime, unsigned short maxRetransmits, const String& protocol, bool negotiated, unsigned short id, void* channel)
    : m_label(label)
    , m_ordered(ordered)
    , m_maxRetransmitTime(maxRetransmitTime)
    , m_maxRetransmits(maxRetransmits)
    , m_protocol(protocol)
    , m_negotiated(negotiated)
    , m_id(id)
    , m_googleDatachannel((webrtc::DataChannelInterface*) channel)
{

}

RTCDataChannelHandlerGoogle::~RTCDataChannelHandlerGoogle()
{
}

void RTCDataChannelHandlerGoogle::setClient(RTCDataChannelHandlerClient* client)
{
    m_client = client;
}

void* RTCDataChannelHandlerGoogle::datachannel()
{
    return m_googleDatachannel;
}
RTCDataChannelHandlerClient* RTCDataChannelHandlerGoogle::client()
{
    return m_client;
}

String RTCDataChannelHandlerGoogle::label()
{
    return m_label;
}

bool RTCDataChannelHandlerGoogle::ordered()
{
    return m_ordered;
}

unsigned short RTCDataChannelHandlerGoogle::maxRetransmitTime()
{
    return m_maxRetransmitTime;
}

unsigned short RTCDataChannelHandlerGoogle::maxRetransmits()
{
    return m_maxRetransmits;
}

String RTCDataChannelHandlerGoogle::protocol()
{
    return m_protocol;
}

bool RTCDataChannelHandlerGoogle::negotiated()
{
    return m_negotiated;
}

unsigned short RTCDataChannelHandlerGoogle::id()
{
    return m_id;
}

unsigned long RTCDataChannelHandlerGoogle::bufferedAmount()
{
    return m_bufferedAmount;    
}

bool RTCDataChannelHandlerGoogle::sendStringData(const String& data)
{
    printf("RTCDataChannelHandlerGoogle::sendStringData\n");    
    //rtc::Buffer buffer = new rtc::Buffer(data.ascii().data());
    webrtc::DataBuffer buffer(data.ascii().data());
    m_googleDatachannel->Send(buffer);
}

bool RTCDataChannelHandlerGoogle::sendRawData(const char* data, size_t size)
{
    printf("RTCDataChannelHandlerGoogle::sendRawData\n"); 

    rtc::Buffer buffer(data,size);
    webrtc::DataBuffer data_buffer(buffer,true);
    m_googleDatachannel->Send(data_buffer);
}

void RTCDataChannelHandlerGoogle::close()
{
    //owr_data_channel_close(m_owrDataChannel);
}

void RTCDataChannelHandlerGoogle::OnMessage(const webrtc::DataBuffer& buffer)
{
    printf("RTCDataChannelHandlerGoogle::onMessage\n");
    const char* data = buffer.data.data<char>();
    String str = String::fromUTF8(data);
    m_client->didReceiveStringData(str);
}

void RTCDataChannelHandlerGoogle::OnStateChange()
{
    printf("RTCDataChannelHandlerGoogle::OnStateChange\n");
    webrtc::DataChannelInterface::DataState state = m_googleDatachannel->state();
    RTCDataChannelHandlerClient::ReadyState state_client;

    switch (state) {
        case webrtc::DataChannelInterface::kConnecting:
        {
            state_client = RTCDataChannelHandlerClient::ReadyStateConnecting;
            m_client->didChangeReadyState(state_client);
        }
        case webrtc::DataChannelInterface::kOpen:
        {
            state_client = RTCDataChannelHandlerClient::ReadyStateOpen;
            m_client->didChangeReadyState(state_client);
        }
        case webrtc::DataChannelInterface::kClosing:
        {
            state_client = RTCDataChannelHandlerClient::ReadyStateClosing;
            m_client->didChangeReadyState(state_client);
        }
        case webrtc::DataChannelInterface::kClosed:
        {
            state_client = RTCDataChannelHandlerClient::ReadyStateClosed;
            m_client->didChangeReadyState(state_client);
        }
    }
}
} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

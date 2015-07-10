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
#include "RTCDataChannelHandlerOwr.h"

#include "MediaEndpoint.h"
#include "RTCDataChannelHandlerClient.h"
#include <wtf/text/CString.h>

namespace WebCore {

static std::unique_ptr<RTCDataChannelHandler> createRTCDataChannelHandlerOwr(const String& label, bool ordered, unsigned short maxRetransmitTime, unsigned short maxRetransmits, const String& protocol, bool negotiated, unsigned short id, void* channel)
{
    return std::unique_ptr<RTCDataChannelHandler>(new RTCDataChannelHandlerOwr(label, ordered, maxRetransmitTime, maxRetransmits, protocol, negotiated, id, channel));
}

static void onData(OwrDataChannel*, const gchar *string, RTCDataChannelHandler*);
static void onRawData(OwrDataChannel*, const gchar *data, guint length, RTCDataChannelHandler*);
static void onReadyState(OwrDataChannel*, GParamSpec *pspec, RTCDataChannelHandler*);

CreateRTCDataChannelHandler RTCDataChannelHandler::create = createRTCDataChannelHandlerOwr;

RTCDataChannelHandlerOwr::RTCDataChannelHandlerOwr(const String& label, bool ordered, unsigned short maxRetransmitTime, unsigned short maxRetransmits, const String& protocol, bool negotiated, unsigned short id, void* channel)
    : m_label(label)
    , m_ordered(ordered)
    , m_maxRetransmitTime(maxRetransmitTime)
    , m_maxRetransmits(maxRetransmits)
    , m_protocol(protocol)
    , m_negotiated(negotiated)
    , m_id(id)
    , m_owrDataChannel((OwrDataChannel*) channel)
{
    g_signal_connect(m_owrDataChannel, "on-data", G_CALLBACK(onData), this);
    g_signal_connect(m_owrDataChannel, "on-binary-data", G_CALLBACK(onRawData), this);
    g_signal_connect(m_owrDataChannel, "notify::ready-state", G_CALLBACK(onReadyState), this);
}

RTCDataChannelHandlerOwr::~RTCDataChannelHandlerOwr()
{
}

void RTCDataChannelHandlerOwr::setClient(RTCDataChannelHandlerClient* client)
{
    m_client = client;
}

void* RTCDataChannelHandlerOwr::owrDatachannel()
{
    return m_owrDataChannel;
}
RTCDataChannelHandlerClient* RTCDataChannelHandlerOwr::client()
{
    return m_client;
}

String RTCDataChannelHandlerOwr::label()
{
    return m_label;
}

bool RTCDataChannelHandlerOwr::ordered()
{
    return m_ordered;
}

unsigned short RTCDataChannelHandlerOwr::maxRetransmitTime()
{
    return m_maxRetransmitTime;
}

unsigned short RTCDataChannelHandlerOwr::maxRetransmits()
{
    return m_maxRetransmits;
}

String RTCDataChannelHandlerOwr::protocol()
{
    return m_protocol;
}

bool RTCDataChannelHandlerOwr::negotiated()
{
    return m_negotiated;
}

unsigned short RTCDataChannelHandlerOwr::id()
{
    return m_id;
}

unsigned long RTCDataChannelHandlerOwr::bufferedAmount()
{
    return m_bufferedAmount;    
}

bool RTCDataChannelHandlerOwr::sendStringData(const String& data)
{
    printf("RTCDataChannelHandlerOwr::sendStringData\n");    
    owr_data_channel_send(m_owrDataChannel, data.ascii().data());
    return 1;
}

bool RTCDataChannelHandlerOwr::sendRawData(const char* data, size_t size)
{
    const gchar* binaryMessage;
    binaryMessage = data;
    owr_data_channel_send_binary(m_owrDataChannel, (const guint8 *) binaryMessage, size);
}

void RTCDataChannelHandlerOwr::close()
{
    owr_data_channel_close(m_owrDataChannel);
}

static void onData(OwrDataChannel *data_channel, const gchar *string, RTCDataChannelHandler *handler)
{
    printf("RTCDataChannelHandlerOwr::onData\n");
    RTCDataChannelHandlerClient* client = handler->client();
    client->didReceiveStringData(string);
}

static void onRawData(OwrDataChannel *data_channel, const gchar *data, guint length, RTCDataChannelHandler *handler)
{
    RTCDataChannelHandlerClient* client = handler->client();
    client->didReceiveRawData(data, length);
}

static void onReadyState(OwrDataChannel *data_channel, GParamSpec *pspec, RTCDataChannelHandler *handler)
{
    printf("RTCDataChannelHandlerOwr::onReadyState\n");
    gint readyState;

    g_object_get(data_channel, "ready-state", &readyState, NULL);

    RTCDataChannelHandlerClient::ReadyState state;
    if (readyState == OWR_DATA_CHANNEL_READY_STATE_CONNECTING) {
        state = RTCDataChannelHandlerClient::ReadyStateConnecting;
        handler->client()->didChangeReadyState(state);
    } else if (readyState == OWR_DATA_CHANNEL_READY_STATE_OPEN) {
        state = RTCDataChannelHandlerClient::ReadyStateOpen;
        handler->client()->didChangeReadyState(state);
    } else if (readyState == OWR_DATA_CHANNEL_READY_STATE_CLOSING) {
        state = RTCDataChannelHandlerClient::ReadyStateClosing;
        handler->client()->didChangeReadyState(state);
    } else if (readyState == OWR_DATA_CHANNEL_READY_STATE_CLOSED) {
        state = RTCDataChannelHandlerClient::ReadyStateClosed;
        handler->client()->didChangeReadyState(state);
    }
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)

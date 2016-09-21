/*
 * Copyright (C) 2011 Ericsson AB. All rights reserved.
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2015 Igalia S.L. All rights reserved.
 * Copyright (C) 2015 Metrological. All rights reserved.
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

#if ENABLE(MEDIA_STREAM) && USE(OPENWEBRTC)
#include "RealtimeMediaSourceCenterOwr.h"

#include "CaptureDevice.h"
#include "MediaStreamCreationClient.h"
#include "MediaStreamPrivate.h"
#include "NotImplemented.h"
#include "OpenWebRTCUtilities.h"
#include "RealtimeMediaSourceCapabilities.h"
#include <owr/owr.h>
#include <owr/owr_local.h>
#include <owr/owr_media_source.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/MainThread.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/SHA1.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringHash.h>


namespace WebCore {

RealtimeMediaSourceCenter& RealtimeMediaSourceCenter::platformCenter()
{
    ASSERT(isMainThread());
    static NeverDestroyed<RealtimeMediaSourceCenterOwr> center;
    return center;
}

static void mediaSourcesAvailableCallback(GList* sources, gpointer userData)
{
    RealtimeMediaSourceCenterOwr* center = reinterpret_cast<RealtimeMediaSourceCenterOwr*>(userData);
    center->mediaSourcesAvailable(sources);
}

RealtimeMediaSourceCenterOwr::RealtimeMediaSourceCenterOwr()
{
    initializeOpenWebRTC();

    // Temporary solution to hint about preferred device names. Each call to getUserMedia() will
    // prefer the device name at the first element of the list and then move that element last.
    char* envString = getenv("WEBKIT_AUDIO_SOURCE_NAMES");
    if (envString)
        String(envString).split(',', false, m_preferredAudioSourceNames);

    envString = getenv("WEBKIT_VIDEO_SOURCE_NAMES");
    if (envString)
        String(envString).split(',', false, m_preferredVideoSourceNames);
}

RealtimeMediaSourceCenterOwr::~RealtimeMediaSourceCenterOwr()
{
}

void RealtimeMediaSourceCenterOwr::validateRequestConstraints(MediaStreamCreationClient* client, MediaConstraints& audioConstraints, MediaConstraints& videoConstraints)
{
    m_client = client;

    // FIXME: Actually do constraints validation. The MediaConstraints
    // need to comply with the available audio/video device(s)
    // capabilities. See bug #123345.
    int types = OWR_MEDIA_TYPE_UNKNOWN;
    if (audioConstraints.isValid())
        types |= OWR_MEDIA_TYPE_AUDIO;
    if (videoConstraints.isValid())
        types |= OWR_MEDIA_TYPE_VIDEO;

    owr_get_capture_sources(static_cast<OwrMediaType>(types), mediaSourcesAvailableCallback, this);
}

void RealtimeMediaSourceCenterOwr::createMediaStream(PassRefPtr<MediaStreamCreationClient> prpQueryClient, MediaConstraints& audioConstraints, MediaConstraints& videoConstraints)
{
    RefPtr<MediaStreamCreationClient> client = prpQueryClient;
    ASSERT(client);

    UNUSED_PARAM(audioConstraints);
    UNUSED_PARAM(videoConstraints);

    Vector<RefPtr<RealtimeMediaSource>> audioSources;
    Vector<RefPtr<RealtimeMediaSource>> videoSources;

    if (audioConstraints.isValid()) {
        // TODO: verify constraints according to registered
        // sources. For now, unconditionally pick the first source, see bug #123345.
        RefPtr<RealtimeMediaSource> audioSource = selectSource(RealtimeMediaSource::Audio);
        if (audioSource) {
            audioSource->reset();
            audioSources.append(audioSource.release());
        }
    }

    if (videoConstraints.isValid()) {
        // TODO: verify constraints according to registered
        // sources. For now, unconditionally pick the first source, see bug #123345.
        RefPtr<RealtimeMediaSource> videoSource = selectSource(RealtimeMediaSource::Video);
        if (videoSource) {
            videoSource->reset();
            videoSources.append(videoSource.release());
        }
    }

    client->didCreateStream(MediaStreamPrivate::create(audioSources, videoSources));
}

void RealtimeMediaSourceCenterOwr::createMediaStream(MediaStreamCreationClient* client, const String& audioDeviceID, const String& videoDeviceID)
{
    ASSERT(client);
    Vector<RefPtr<RealtimeMediaSource>> audioSources;
    Vector<RefPtr<RealtimeMediaSource>> videoSources;

    if (!audioDeviceID.isEmpty()) {
        RealtimeMediaSourceOwrMap::iterator sourceIterator = m_sourceMap.find(audioDeviceID);
        if (sourceIterator != m_sourceMap.end()) {
            RefPtr<RealtimeMediaSource> source = sourceIterator->value;
            if (source->type() == RealtimeMediaSource::Audio)
                audioSources.append(source.release());
        }
    }
    if (!videoDeviceID.isEmpty()) {
        RealtimeMediaSourceOwrMap::iterator sourceIterator = m_sourceMap.find(videoDeviceID);
        if (sourceIterator != m_sourceMap.end()) {
            RefPtr<RealtimeMediaSource> source = sourceIterator->value;
            if (source->type() == RealtimeMediaSource::Video)
                videoSources.append(source.release());
        }
    }

    client->didCreateStream(MediaStreamPrivate::create(audioSources, videoSources));
}

Vector<CaptureDevice> RealtimeMediaSourceCenterOwr::getMediaStreamDevices()
{
    notImplemented();
    return Vector<CaptureDevice>();
}

static String getSourceId(OwrMediaSource* source)
{
    String idData = String::format("%p", source);
    SHA1 digest;
    digest.addBytes(idData.ascii());
    return String(digest.computeHexDigest().data());
}

void RealtimeMediaSourceCenterOwr::mediaSourcesAvailable(GList* sources)
{
    Vector<RefPtr<RealtimeMediaSource>> audioSources;
    Vector<RefPtr<RealtimeMediaSource>> videoSources;

    RealtimeMediaSourceOwrMap newSourceMap;

    for (auto item = sources; item; item = item->next) {
        OwrMediaSource* source = OWR_MEDIA_SOURCE(item->data);
        String id(getSourceId(source));

        if (m_sourceMap.contains(id))
            newSourceMap.add(id, m_sourceMap.take(id));
        else {
            GUniqueOutPtr<gchar> name;
            OwrMediaType mediaType;
            g_object_get(source, "media-type", &mediaType, "name", &name.outPtr(), NULL);
            String sourceName(name.get());

            RealtimeMediaSource::Type sourceType;
            if (mediaType & OWR_MEDIA_TYPE_AUDIO)
                sourceType = RealtimeMediaSource::Audio;
            else if (mediaType & OWR_MEDIA_TYPE_VIDEO)
                sourceType = RealtimeMediaSource::Video;
            else {
                sourceType = RealtimeMediaSource::None;
                ASSERT_NOT_REACHED();
            }

            newSourceMap.add(id, adoptRef(new RealtimeMediaSourceOwr(source, id, sourceType, sourceName)));
        }
    }

    // Disconnected sources, left in m_sourceMap, will be discarded by swap
    m_sourceMap.swap(newSourceMap);

    RefPtr<RealtimeMediaSource> audioSource = selectSource(RealtimeMediaSource::Audio);
    if (audioSource)
        audioSources.append(WTFMove(audioSource));
    RefPtr<RealtimeMediaSource> videoSource = selectSource(RealtimeMediaSource::Video);
    if (videoSource)
        videoSources.append(WTFMove(videoSource));

    // TODO: Make sure contraints are actually validated by checking source types.
    m_client->constraintsValidated(audioSources, videoSources);
}

static String getNextPreferredSourceName(Vector<String>& sourceNames)
{
    if (sourceNames.isEmpty())
        return emptyString();

    String name = sourceNames.first();
    sourceNames.remove(0);
    sourceNames.append(name);

    return name;
}

PassRefPtr<RealtimeMediaSource> RealtimeMediaSourceCenterOwr::selectSource(RealtimeMediaSource::Type type)
{
    RefPtr<RealtimeMediaSource> selectedSource = nullptr;
    const String& preferredSourceName = getNextPreferredSourceName(type == RealtimeMediaSource::Audio ? m_preferredAudioSourceNames : m_preferredVideoSourceNames);

    for (auto iter = m_sourceMap.begin(); iter != m_sourceMap.end(); ++iter) {
        RefPtr<RealtimeMediaSource> source = iter->value;
        bool foundPreferred = source->name() == preferredSourceName;
        if (source->type() == type && (!selectedSource || foundPreferred)) {
            selectedSource = source;
            if (foundPreferred)
                break;
        }
    }

    return selectedSource;
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM) && USE(OPENWEBRTC)

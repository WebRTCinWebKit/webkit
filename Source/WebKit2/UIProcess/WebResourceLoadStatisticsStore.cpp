/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebResourceLoadStatisticsStore.h"

#include "APIWebsiteDataStore.h"
#include "WebProcessMessages.h"
#include "WebProcessPool.h"
#include "WebResourceLoadStatisticsStoreMessages.h"
#include "WebsiteDataFetchOption.h"
#include "WebsiteDataType.h"
#include <WebCore/KeyedCoding.h>
#include <WebCore/ResourceLoadStatistics.h>
#include <wtf/CurrentTime.h>
#include <wtf/MainThread.h>
#include <wtf/MathExtras.h>
#include <wtf/RunLoop.h>
#include <wtf/threads/BinarySemaphore.h>

using namespace WebCore;

namespace WebKit {

static const auto numberOfSecondsBetweenClearingDataRecords = 600;
static const auto featureVectorLengthThreshold = 3;

Ref<WebResourceLoadStatisticsStore> WebResourceLoadStatisticsStore::create(const String& resourceLoadStatisticsDirectory)
{
    return adoptRef(*new WebResourceLoadStatisticsStore(resourceLoadStatisticsDirectory));
}

WebResourceLoadStatisticsStore::WebResourceLoadStatisticsStore(const String& resourceLoadStatisticsDirectory)
    : m_resourceStatisticsStore(ResourceLoadStatisticsStore::create())
    , m_statisticsQueue(WorkQueue::create("WebResourceLoadStatisticsStore Process Data Queue"))
    , m_storagePath(resourceLoadStatisticsDirectory)
{
}

WebResourceLoadStatisticsStore::~WebResourceLoadStatisticsStore()
{
}

bool WebResourceLoadStatisticsStore::hasPrevalentResourceCharacteristics(const ResourceLoadStatistics& resourceStatistic)
{
    auto subresourceUnderTopFrameOriginsCount = resourceStatistic.subresourceUnderTopFrameOrigins.size();
    auto subresourceUniqueRedirectsToCount = resourceStatistic.subresourceUniqueRedirectsTo.size();
    auto subframeUnderTopFrameOriginsCount = resourceStatistic.subframeUnderTopFrameOrigins.size();
    
    if (!subresourceUnderTopFrameOriginsCount
        && !subresourceUniqueRedirectsToCount
        && !subframeUnderTopFrameOriginsCount)
        return false;

    if (subresourceUnderTopFrameOriginsCount > featureVectorLengthThreshold
        || subresourceUniqueRedirectsToCount > featureVectorLengthThreshold
        || subframeUnderTopFrameOriginsCount > featureVectorLengthThreshold)
        return true;

    // The resource is considered prevalent if the feature vector
    // is longer than the threshold.
    // Vector length for n dimensions is sqrt(a^2 + (...) + n^2).
    double vectorLength = 0;
    vectorLength += subresourceUnderTopFrameOriginsCount * subresourceUnderTopFrameOriginsCount;
    vectorLength += subresourceUniqueRedirectsToCount * subresourceUniqueRedirectsToCount;
    vectorLength += subframeUnderTopFrameOriginsCount * subframeUnderTopFrameOriginsCount;

    ASSERT(vectorLength > 0);

    return sqrt(vectorLength) > featureVectorLengthThreshold;
}
    
void WebResourceLoadStatisticsStore::classifyResource(ResourceLoadStatistics& resourceStatistic)
{
    if (!resourceStatistic.isPrevalentResource && hasPrevalentResourceCharacteristics(resourceStatistic)) {
        resourceStatistic.isPrevalentResource = true;
    }
}

void WebResourceLoadStatisticsStore::clearDataRecords()
{
    if (m_dataStoreClearPending)
        return;

    Vector<String> prevalentResourceDomains = coreStore().prevalentResourceDomainsWithoutUserInteraction();
    if (!prevalentResourceDomains.size())
        return;

    double now = currentTime();
    if (!m_lastTimeDataRecordsWereCleared) {
        m_lastTimeDataRecordsWereCleared = now;
        return;
    }

    if (now < (m_lastTimeDataRecordsWereCleared + numberOfSecondsBetweenClearingDataRecords))
        return;

    m_dataStoreClearPending = true;
    m_lastTimeDataRecordsWereCleared = now;

    // Switch to the main thread to get the default website data store
    RunLoop::main().dispatch([prevalentResourceDomains = WTFMove(prevalentResourceDomains), this] () mutable {
        auto& websiteDataStore = API::WebsiteDataStore::defaultDataStore()->websiteDataStore();

        websiteDataStore.fetchData(WebsiteDataType::Cookies, { }, [prevalentResourceDomains = WTFMove(prevalentResourceDomains), this](auto websiteDataRecords) {
            Vector<WebsiteDataRecord> dataRecords;
            for (auto& websiteDataRecord : websiteDataRecords) {
                for (auto& prevalentResourceDomain : prevalentResourceDomains) {
                    if (websiteDataRecord.displayName.endsWithIgnoringASCIICase(prevalentResourceDomain)) {
                        auto suffixStart = websiteDataRecord.displayName.length() - prevalentResourceDomain.length();
                        if (!suffixStart || websiteDataRecord.displayName[suffixStart - 1] == '.')
                            dataRecords.append(websiteDataRecord);
                    }
                }
            }

            if (!dataRecords.size()) {
                m_dataStoreClearPending = false;
                return;
            }

            auto& websiteDataStore = API::WebsiteDataStore::defaultDataStore()->websiteDataStore();
            websiteDataStore.removeData(WebsiteDataType::Cookies, { WTFMove(dataRecords) }, [this] {
                m_dataStoreClearPending = false;
            });
        });
    });
}

void WebResourceLoadStatisticsStore::resourceLoadStatisticsUpdated(const Vector<WebCore::ResourceLoadStatistics>& origins)
{
    coreStore().mergeStatistics(origins);

    if (coreStore().hasEnoughDataForStatisticsProcessing()) {
        coreStore().processStatistics([this] (ResourceLoadStatistics& resourceStatistic) {
            classifyResource(resourceStatistic);
            clearDataRecords();
        });
    }

    auto encoder = coreStore().createEncoderFromData();
    
    writeEncoderToDisk(*encoder.get(), "full_browsing_session");
}

void WebResourceLoadStatisticsStore::setResourceLoadStatisticsEnabled(bool enabled)
{
    if (enabled == m_resourceLoadStatisticsEnabled)
        return;

    m_resourceLoadStatisticsEnabled = enabled;

    readDataFromDiskIfNeeded();
}

bool WebResourceLoadStatisticsStore::resourceLoadStatisticsEnabled() const
{
    return m_resourceLoadStatisticsEnabled;
}

void WebResourceLoadStatisticsStore::readDataFromDiskIfNeeded()
{
    if (!m_resourceLoadStatisticsEnabled)
        return;

    m_statisticsQueue->dispatch([this, protectedThis = makeRef(*this)] {
        coreStore().clear();

        auto decoder = createDecoderFromDisk("full_browsing_session");
        if (!decoder)
            return;

        coreStore().readDataFromDecoder(*decoder);
    });
}

void WebResourceLoadStatisticsStore::processWillOpenConnection(WebProcessProxy&, IPC::Connection& connection)
{
    connection.addWorkQueueMessageReceiver(Messages::WebResourceLoadStatisticsStore::messageReceiverName(), &m_statisticsQueue.get(), this);
}

void WebResourceLoadStatisticsStore::processDidCloseConnection(WebProcessProxy&, IPC::Connection& connection)
{
    connection.removeWorkQueueMessageReceiver(Messages::WebResourceLoadStatisticsStore::messageReceiverName());
}

void WebResourceLoadStatisticsStore::applicationWillTerminate()
{
    BinarySemaphore semaphore;
    m_statisticsQueue->dispatch([this, &semaphore] {
        // Make sure any ongoing work in our queue is finished before we terminate.
        semaphore.signal();
    });
    semaphore.wait(std::numeric_limits<double>::max());
}

String WebResourceLoadStatisticsStore::persistentStoragePath(const String& label) const
{
    if (m_storagePath.isEmpty())
        return emptyString();

    // TODO Decide what to call this file
    return pathByAppendingComponent(m_storagePath, label + "_resourceLog.plist");
}

void WebResourceLoadStatisticsStore::writeEncoderToDisk(KeyedEncoder& encoder, const String& label) const
{
    RefPtr<SharedBuffer> rawData = encoder.finishEncoding();
    if (!rawData)
        return;

    String resourceLog = persistentStoragePath(label);
    if (resourceLog.isEmpty())
        return;

    if (!m_storagePath.isEmpty())
        makeAllDirectories(m_storagePath);

    auto handle = openFile(resourceLog, OpenForWrite);
    if (!handle)
        return;
    
    int64_t writtenBytes = writeToFile(handle, rawData->data(), rawData->size());
    closeFile(handle);

    if (writtenBytes != static_cast<int64_t>(rawData->size()))
        WTFLogAlways("WebResourceLoadStatisticsStore: We only wrote %d out of %d bytes to disk", static_cast<unsigned>(writtenBytes), rawData->size());
}

std::unique_ptr<KeyedDecoder> WebResourceLoadStatisticsStore::createDecoderFromDisk(const String& label) const
{
    String resourceLog = persistentStoragePath(label);
    if (resourceLog.isEmpty())
        return nullptr;

    RefPtr<SharedBuffer> rawData = SharedBuffer::createWithContentsOfFile(resourceLog);
    if (!rawData)
        return nullptr;

    return KeyedDecoder::decoder(reinterpret_cast<const uint8_t*>(rawData->data()), rawData->size());
}

} // namespace WebKit

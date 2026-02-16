/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "framememorycache.h"
#include "codec/frame.h"
#include "render/framehashcache.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <QMutexLocker>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

using namespace olive;

QHash<FrameMemCacheKey, FrameMemCacheValue> FrameMemCache::cache_;
QHash<QUuid, QString> FrameMemCache::cache_paths_;
QMutex FrameMemCache::cache_mutex_;
std::thread FrameMemCache::lru_thread_;
std::atomic<bool> FrameMemCache::lru_thread_running_(false);
std::atomic<bool> FrameMemCache::lru_thread_stop_requested_(false);
std::atomic<int> FrameMemCache::instance_count_(0);
std::atomic<int64_t> FrameMemCache::budget(0);

namespace {

[[maybe_unused]] uint64_t GetAvailableMemoryBytesWindows()
{
#if defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status)) {
        return 0;
    }
    return static_cast<uint64_t>(status.ullAvailPhys);
#else
    return 0;
#endif
}

[[maybe_unused]] uint64_t GetAvailableMemoryBytesLinux()
{
#if defined(__linux__)
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) {
        return 0;
    }

    std::string key;
    uint64_t value_kb = 0;
    std::string unit;
    uint64_t mem_free_kb = 0;
    uint64_t buffers_kb = 0;
    uint64_t cached_kb = 0;

    while (meminfo >> key >> value_kb >> unit) {
        if (key == "MemAvailable:") {
            return value_kb * 1024ULL;
        }
        if (key == "MemFree:") {
            mem_free_kb = value_kb;
        } else if (key == "Buffers:") {
            buffers_kb = value_kb;
        } else if (key == "Cached:") {
            cached_kb = value_kb;
        }
    }

    // Fallback for older kernels where MemAvailable is not present.
    return (mem_free_kb + buffers_kb + cached_kb) * 1024ULL;
#else
    return 0;
#endif
}

[[maybe_unused]] uint64_t GetAvailableMemoryBytesMacOS()
{
#if defined(__APPLE__)
    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    if (host_page_size(host, &page_size) != KERN_SUCCESS) {
        return 0;
    }

    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(host, HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm_stats),
                          &count) != KERN_SUCCESS) {
        return 0;
    }

    return static_cast<uint64_t>(vm_stats.free_count) * static_cast<uint64_t>(page_size);
#else
    return 0;
#endif
}

[[maybe_unused]] uint64_t GetAvailableMemoryBytes()
{
#if defined(_WIN32)
    return GetAvailableMemoryBytesWindows();
#elif defined(__linux__)
    return GetAvailableMemoryBytesLinux();
#elif defined(__APPLE__)
    return GetAvailableMemoryBytesMacOS();
#else
    return 0;
#endif
}

} // namespace

FramePtr FrameMemCache::LoadCacheFrame(const int64_t &time) const{
    const FrameMemCacheKey key = FrameMemCacheKey::create(GetUuid(), time);
    {
        QMutexLocker locker(&cache_mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            it.value().touch();
            return it.value().frame();
        }
    }

    QString backing_path;
    {
        QMutexLocker locker(&cache_mutex_);
        backing_path = cache_paths_.value(GetUuid());
    }
    if (backing_path.isEmpty()) {
        return nullptr;
    }

    FramePtr disk_frame = FrameHashCache::LoadCacheFrame(backing_path, GetUuid(), time);
    if (!disk_frame) {
        return nullptr;
    }

    {
        QMutexLocker locker(&cache_mutex_);
        const FrameMemCacheKey insert_key = FrameMemCacheKey::create(GetUuid(), time);
        cache_.remove(insert_key);
        cache_.insert(insert_key, FrameMemCacheValue::create(disk_frame));
    }
    doLru();
    return disk_frame;
}

bool FrameMemCache::SaveCacheFrame(const int64_t &time, FramePtr frame)
{
    if (!frame) {
        return false;
    }

    const FrameMemCacheKey key =
        FrameMemCacheKey::create(GetUuid(), time);
    {
        QMutexLocker locker(&cache_mutex_);
        cache_.remove(key);
        cache_.insert(key, FrameMemCacheValue::create(frame));
    }

    doLru();
    return true;
}

void FrameMemCache::SetBackingCachePath(const QString &cache_path)
{
    if (GetUuid().isNull()) {
        return;
    }

    QMutexLocker locker(&cache_mutex_);
    if (cache_path.isEmpty()) {
        cache_paths_.remove(GetUuid());
    } else {
        cache_paths_.insert(GetUuid(), cache_path);
    }
}

FrameMemCache::FrameMemCache()
{
    if (instance_count_.fetch_add(1, std::memory_order_acq_rel) == 0) {
        StartLruThread();
    }
}

FrameMemCache::~FrameMemCache()
{
    if (instance_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        StopLruThread();
    }
}

void FrameMemCache::StartLruThread()
{
    if (lru_thread_running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    lru_thread_stop_requested_.store(false, std::memory_order_release);
    lru_thread_ = std::thread(&FrameMemCache::LruWorkerLoop);
}

void FrameMemCache::StopLruThread()
{
    lru_thread_stop_requested_.store(true, std::memory_order_release);

    if (lru_thread_.joinable()) {
        lru_thread_.join();
    }

    lru_thread_running_.store(false, std::memory_order_release);
}

void FrameMemCache::LruWorkerLoop()
{
    using namespace std::chrono_literals;

    int tick_count = 0;

    while (!lru_thread_stop_requested_.load(std::memory_order_acquire)) {
        if (tick_count % 5 == 0) {
            const uint64_t available_bytes = GetAvailableMemoryBytes();
            if (available_bytes > 0) {
                const uint64_t budget_bytes = available_bytes * 3ULL / 10ULL;
                const uint64_t clamped = std::min<uint64_t>(
                    budget_bytes, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
                budget.store(static_cast<int64_t>(clamped), std::memory_order_release);
            }
        }

        doLru();
        ++tick_count;
        std::this_thread::sleep_for(1s);
    }
}

void FrameMemCache::doLru(){
    struct EvictedFrame {
        QString cache_path;
        QUuid uuid;
        int64_t timestamp = 0;
        FramePtr frame;
    };
    std::vector<EvictedFrame> evicted;
    {
        QMutexLocker locker(&cache_mutex_);

        if (cache_.isEmpty()) {
            return;
        }

        const int64_t configured_budget = budget.load(std::memory_order_acquire);
        if (configured_budget <= 0) {
            return;
        }

        uint64_t total_bytes = 0;
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            total_bytes += static_cast<uint64_t>(it.value().size_bytes());
        }

        const uint64_t target_budget = static_cast<uint64_t>(configured_budget);

        if (total_bytes <= target_budget) {
            return;
        }

        while (!cache_.isEmpty() && total_bytes > target_budget) {
            auto oldest_it = cache_.begin();
            std::time_t oldest_ts = oldest_it.value().last_access_unix();

            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                const std::time_t current_ts = it.value().last_access_unix();
                if (current_ts < oldest_ts) {
                    oldest_ts = current_ts;
                    oldest_it = it;
                }
            }

            const QUuid uuid = oldest_it.key().uuid();
            const QString cache_path = cache_paths_.value(uuid);
            if (!cache_path.isEmpty() && oldest_it.value().frame()) {
                evicted.push_back(
                    {cache_path, uuid, oldest_it.key().frame_time(), oldest_it.value().frame()});
            }

            total_bytes -=
                std::min(total_bytes, static_cast<uint64_t>(oldest_it.value().size_bytes()));
            cache_.erase(oldest_it);
        }
    }

    for (const EvictedFrame &e : evicted) {
        FrameHashCache::SaveCacheFrame(e.cache_path, e.uuid, e.timestamp, e.frame);
    }
}

void FrameMemCache::InvalidateAll()
{
    QMutexLocker locker(&cache_mutex_);
    const QUuid cache_uuid = GetUuid();
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it.key().uuid() == cache_uuid) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

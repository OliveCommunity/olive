/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

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
std::mutex FrameMemCache::lru_thread_mutex_;

// 异步磁盘写入线程静态成员
std::thread FrameMemCache::disk_writer_thread_;
std::atomic<bool> FrameMemCache::disk_writer_running_(false);
std::atomic<bool> FrameMemCache::disk_writer_stop_requested_(false);
std::queue<FrameMemCache::EvictedFrame> FrameMemCache::disk_write_queue_;
std::mutex FrameMemCache::disk_write_mutex_;
std::condition_variable FrameMemCache::disk_write_cv_;
std::atomic<int> FrameMemCache::disk_writer_instance_count_(0);
std::mutex FrameMemCache::disk_writer_thread_mutex_;

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
    if (disk_writer_instance_count_.fetch_add(1, std::memory_order_acq_rel) == 0) {
        StartDiskWriterThread();
    }
}

FrameMemCache::~FrameMemCache()
{
    if (instance_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        StopLruThread();
    }
    if (disk_writer_instance_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        StopDiskWriterThread();
    }
}

void FrameMemCache::StartLruThread()
{
    std::lock_guard<std::mutex> locker(lru_thread_mutex_);
    
    if (lru_thread_running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    lru_thread_stop_requested_.store(false, std::memory_order_release);
    
    // 确保之前的线程已经结束
    if (lru_thread_.joinable()) {
        try {
            lru_thread_.join();
        } catch (...) {
            // 忽略异常
        }
    }
    
    lru_thread_ = std::thread(&FrameMemCache::LruWorkerLoop);
}

void FrameMemCache::StopLruThread()
{
    std::lock_guard<std::mutex> locker(lru_thread_mutex_);
    
    // 检查线程是否正在运行
    if (!lru_thread_running_.load(std::memory_order_acquire)) {
        return;
    }

    lru_thread_stop_requested_.store(true, std::memory_order_release);

    // 安全地 join 线程
    if (lru_thread_.joinable()) {
        try {
            lru_thread_.join();
        } catch (...) {
            // 忽略 join 失败
        }
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

    // 异步写入磁盘，不阻塞渲染线程
    for (const EvictedFrame &e : evicted) {
        {
            std::lock_guard<std::mutex> locker(disk_write_mutex_);
            disk_write_queue_.push(e);
        }
        disk_write_cv_.notify_one();
    }
}

void FrameMemCache::StartDiskWriterThread()
{
    std::lock_guard<std::mutex> locker(disk_writer_thread_mutex_);
    
    if (disk_writer_running_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    disk_writer_stop_requested_.store(false, std::memory_order_release);
    
    // 确保之前的线程已经结束
    if (disk_writer_thread_.joinable()) {
        try {
            disk_writer_thread_.join();
        } catch (...) {
            // 忽略异常
        }
    }
    
    disk_writer_thread_ = std::thread(&FrameMemCache::DiskWriterLoop);
}

void FrameMemCache::StopDiskWriterThread()
{
    std::lock_guard<std::mutex> locker(disk_writer_thread_mutex_);
    
    // 检查线程是否正在运行
    if (!disk_writer_running_.load(std::memory_order_acquire)) {
        return;
    }

    disk_writer_stop_requested_.store(true, std::memory_order_release);
    disk_write_cv_.notify_all();
    
    // 安全地 join 线程
    if (disk_writer_thread_.joinable()) {
        try {
            disk_writer_thread_.join();
        } catch (...) {
            // 忽略 join 失败
        }
    }
    
    disk_writer_running_.store(false, std::memory_order_release);
}

void FrameMemCache::DiskWriterLoop()
{
    while (!disk_writer_stop_requested_.load(std::memory_order_acquire)) {
        EvictedFrame e;
        bool has_task = false;
        
        {
            std::unique_lock<std::mutex> locker(disk_write_mutex_);
            if (disk_write_queue_.empty()) {
                // 等待新任务，最多等待100ms
                disk_write_cv_.wait_for(locker, std::chrono::milliseconds(100));
            }
            
            if (!disk_write_queue_.empty()) {
                e = disk_write_queue_.front();
                disk_write_queue_.pop();
                has_task = true;
            }
        }
        
        if (has_task) {
            FrameHashCache::SaveCacheFrame(e.cache_path, e.uuid, e.timestamp, e.frame);
        }
    }
    
    // 处理剩余的任务
    std::vector<EvictedFrame> remaining;
    {
        std::lock_guard<std::mutex> locker(disk_write_mutex_);
        while (!disk_write_queue_.empty()) {
            remaining.push_back(disk_write_queue_.front());
            disk_write_queue_.pop();
        }
    }
    
    for (const auto& e : remaining) {
        FrameHashCache::SaveCacheFrame(e.cache_path, e.uuid, e.timestamp, e.frame);
    }
}

void FrameMemCache::AsyncSaveToDisk(const QString &cache_path, const QUuid &uuid, 
                                    const int64_t &timestamp, FramePtr frame)
{
    if (cache_path.isEmpty() || uuid.isNull() || !frame) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> locker(disk_write_mutex_);
        disk_write_queue_.push({cache_path, uuid, timestamp, frame});
    }
    disk_write_cv_.notify_one();
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

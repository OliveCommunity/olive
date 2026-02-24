/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#include "codec/frame.h"
#include "olive/core/util/rational.h"
#include "render/videoparams.h"
#include <atomic>
#include <cstdint>
#include <ctime>
#include <locale>
#include <qhash.h>
#include <quuid.h>
#include <thread>
#include <queue>
#include <condition_variable>
#include "render/playbackcache.h"
#include <QHash>
#include <mutex>
#include <QString>
namespace olive{
class FrameMemCacheKey{
public:
    static FrameMemCacheKey create(QUuid uuid, int64_t time){
        FrameMemCacheKey key;
        key.uuid_=uuid;
        key.time=time;
        return key;
    }
    friend uint qHash(const FrameMemCacheKey &key, uint seed);
    bool operator==(const FrameMemCacheKey &that) const{
        return this->uuid_ == that.uuid_
            && this->time == that.time;
    }
    QUuid uuid() const{
        return uuid_;
    }
    int64_t frame_time() const{
        return time;
    }
	~FrameMemCacheKey() = default;
private:
	FrameMemCacheKey() = default;
	QUuid uuid_;
	int64_t time = 0;
};

inline uint qHash(const FrameMemCacheKey &key, uint seed = 0)
{
    return qHashMulti(seed,
                      key.uuid_,
                      key.time);
}

class FrameMemCacheValue{
public:
    static FrameMemCacheValue create(FramePtr frame){
        FrameMemCacheValue value;
        value.frame_ = frame;
        value.size_bytes_ = static_cast<size_t>(frame->allocated_size() * 1.3);
        value.last_access_unix_ = std::time(nullptr);
        return value;
    }
    FramePtr frame() const{
        return frame_;
    }

    size_t size_bytes() const{
        return size_bytes_;
    }
    std::time_t last_access_unix() const{
        return last_access_unix_;
    }
    void touch(){
        last_access_unix_ = std::time(nullptr);
    }
private:
	FrameMemCacheValue() = default;
	FramePtr frame_;
    size_t size_bytes_ = 0;
    std::time_t last_access_unix_ = 0;
};

class FrameMemCache : public PlaybackCache {
public:
    FrameMemCache();
    ~FrameMemCache();

    void SetBackingCachePath(const QString &cache_path);

	bool SaveCacheFrame(const int64_t &time, FramePtr frame) override;

    FramePtr LoadCacheFrame(const int64_t &time) const override;
    void InvalidateAll();
    
    // 异步写入磁盘缓存
    void AsyncSaveToDisk(const QString &cache_path, const QUuid &uuid, 
                         const int64_t &timestamp, FramePtr frame);
private: 
    struct EvictedFrame {
        QString cache_path;
        QUuid uuid;
        int64_t timestamp = 0;
        FramePtr frame;
    };
    
    // LRU 线程相关
    static void StartLruThread();
    static void StopLruThread();
    static void LruWorkerLoop();
    
    // 异步磁盘写入线程相关
    static void StartDiskWriterThread();
    static void StopDiskWriterThread();
    static void DiskWriterLoop();
    
    static QHash<FrameMemCacheKey, FrameMemCacheValue> cache_;
    static QHash<QUuid, QString> cache_paths_;
    static QMutex cache_mutex_;
    
    // LRU 线程
    static std::thread lru_thread_;
    static std::atomic<bool> lru_thread_running_;
    static std::atomic<bool> lru_thread_stop_requested_;
    static std::atomic<int> instance_count_;
    static std::atomic<int64_t> budget;
    static std::mutex lru_thread_mutex_; // 保护LRU线程操作
    static void doLru();
    
    // 异步磁盘写入线程
    static std::thread disk_writer_thread_;
    static std::atomic<bool> disk_writer_running_;
    static std::atomic<bool> disk_writer_stop_requested_;
    static std::queue<EvictedFrame> disk_write_queue_;
    static std::mutex disk_write_mutex_;
    static std::condition_variable disk_write_cv_;
    static std::atomic<int> disk_writer_instance_count_;
    static std::mutex disk_writer_thread_mutex_; // 保护磁盘写入线程操作
};
}

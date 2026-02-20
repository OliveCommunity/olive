# Oak Video Editor 视频编辑卡顿优化计划

## 一、主要性能瓶颈识别

### 1. 渲染管线瓶颈

- **视频渲染单线程**: 视频渲染只有一个 `video_thread_` 线程，所有视频帧串行处理，无法利用多核 CPU（虽然有多个
  `waveform_threads_` 用于波形渲染）
- **同步 GPU 操作**: OpenGL 渲染器中大量 `glFinish/glFlush` 调用阻塞渲染线程
- **VAO/VBO 重复创建**: 每次 Blit 操作都创建新的顶点数组对象和缓冲区

### 2. 缓存系统缺陷

- **LRU 算法低效**: `FrameMemCache::doLru()` 每次都要线性遍历所有缓存项找最老的帧，O(n) 复杂度
- **节点遍历缓存无限增长**: `NodeTraverser::value_cache_` 没有大小限制和淘汰机制
- **解码器帧缓存过小**: FFmpeg 解码器帧队列只有 2 帧，导致频繁 seek 和重解码

### 3. 内存管理问题

- **纹理缓存静态限制**: OpenGL 纹理缓存固定 5000 个，不根据 GPU 显存动态调整
- **同步磁盘写入**: 缓存驱逐时同步写入磁盘，阻塞渲染线程
- **重复内存分配**: 每帧都重新分配内存和 GPU 纹理

### 4. 解码性能问题

- **软件缩放阻塞**: `sws_scale` 在主线程执行，无硬件加速
- **颜色空间转换**: 每帧都进行 YUV->RGB 转换，没有缓存
- **Seek 操作频繁**: 帧缓存过小导致来回播放时需要频繁 seek

---

## 二、优化计划（按优先级排序）

### 🔴 高优先级（卡顿主要原因）

#### 1. 渲染管线并行化

**文件**: `app/render/rendermanager.cpp`

```cpp
// 修改：创建多个视频渲染线程
const int kVideoThreadCount = qMax(2, QThread::idealThreadCount() - 1);
for (int i = 0; i < kVideoThreadCount; i++) {
    video_threads_.push_back(CreateThread(context_));
}

// 添加优先级队列：当前播放帧优先于后台缓存帧
enum class RenderPriority { kPlayback = 0, kPreview = 1, kCache = 2 };
```

**预期收益**: 多核利用率提升 60-80%，播放响应延迟降低 50%

---

#### 2. OpenGL 渲染优化

**文件**: `app/render/opengl/openglrenderer.cpp`

```cpp
// 问题：每次 Blit 都创建 VAO/VBO
// 优化：使用对象池复用 VAO/VBO
class GLResourcePool {
    QStack<QOpenGLVertexArrayObject*> vao_pool_;
    QStack<QOpenGLBuffer*> vbo_pool_;
public:
    QOpenGLVertexArrayObject* acquireVAO();
    void releaseVAO(QOpenGLVertexArrayObject* vao);
};

// 问题：纹理缓存静态限制
// 优化：根据 GPU 显存动态调整
void OpenGLRenderer::AdjustTextureCacheSize() {
    GLint gpu_memory_mb = 0;
    glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &gpu_memory_mb);
    kTextureCacheMaxSize = (gpu_memory_mb * 1024 * 1024) / average_texture_size * 0.8;
}
```

**预期收益**: 渲染吞吐量提升 30-40%，GPU 内存使用更合理

---

#### 3. LRU 缓存算法优化

**文件**: `app/render/framememorycache.cpp`

```cpp
// 问题：O(n) 线性查找最老帧
// 优化：使用 std::map 按时间排序 + 双链表实现 O(1) LRU

class LRUCache {
    struct Node {
        FramePtr frame;
        std::time_t access_time;
        std::list<FrameMemCacheKey>::iterator list_iter;
    };
    
    QHash<FrameMemCacheKey, Node> cache_map_;
    std::list<FrameMemCacheKey> access_list_; // 按访问时间排序
    std::mutex mutex_;
    
public:
    void touch(const FrameMemCacheKey& key) {
        // O(1) 移动到链表尾部
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            access_list_.erase(it->list_iter);
            access_list_.push_back(key);
            it->list_iter = --access_list_.end();
        }
    }
    
    void evict_oldest() {
        // O(1) 移除链表头部
        if (!access_list_.empty()) {
            cache_map_.remove(access_list_.front());
            access_list_.pop_front();
        }
    }
};
```

**预期收益**: 缓存操作从 O(n) 降到 O(1)，高分辨率视频下卡顿减少 70%

---

#### 4. 节点遍历缓存优化

**文件**: `app/node/traverser.cpp`

```cpp
// 问题：value_cache_ 无限增长
// 优化：添加 LRU 限制和异步清理

class NodeTraverser {
    // 使用有限大小的 LRU 缓存
    LRUCache<CacheKey, NodeValueTable> value_cache_{1000}; // 最多1000项
    
    // 定期清理resolved_texture_cache_
    QTimer cleanup_timer_;
    void cleanup_expired_textures() {
        // 清理超过5秒未使用的纹理
    }
};
```

**预期收益**: 内存占用稳定，长时间编辑不会越来越卡

---

#### 5. FFmpeg 解码器帧缓存优化

**文件**: `app/codec/ffmpeg/ffmpegdecoder.cpp`

```cpp
// 问题：MaximumQueueSize() 只返回 2
// 优化：根据分辨率和内存动态调整

int FFmpegDecoder::MaximumQueueSize() {
    // 根据视频分辨率和可用内存计算
    int64_t frame_size = avstream_->codecpar->width * 
                         avstream_->codecpar->height * 4; // RGBA估算
    int64_t max_cache_bytes = 512 * 1024 * 1024; // 512MB上限
    int max_frames = qBound(5, int(max_cache_bytes / frame_size), 30);
    return max_frames;
}

// 添加预读线程
void FFmpegDecoder::StartPreloadThread(const rational& current_time) {
    // 在后台线程预加载接下来的帧
}
```

**预期收益**: 播放流畅度提升，减少 80% 的 seek 操作

---

### 🟡 中优先级（性能优化）

#### 6. 异步磁盘缓存写入

**文件**: `app/render/framememorycache.cpp`

```cpp
// 问题：doLru() 中同步写入磁盘
// 优化：使用写入队列 + 后台线程

class FrameMemCache {
    std::thread disk_writer_thread_;
    ConcurrentQueue<EvictedFrame> disk_write_queue_;
    
    void doLru() {
        // 只收集要驱逐的帧，不立即写入
        std::vector<EvictedFrame> evicted;
        // ... 收集代码 ...
        
        // 异步写入磁盘
        for (auto& e : evicted) {
            disk_write_queue_.push(std::move(e));
        }
    }
    
    void disk_writer_loop() {
        while (running_) {
            EvictedFrame e;
            if (disk_write_queue_.pop_wait(e, 100ms)) {
                FrameHashCache::SaveCacheFrame(e.cache_path, e.uuid, 
                                               e.timestamp, e.frame);
            }
        }
    }
};
```

**预期收益**: 渲染线程不再被磁盘 I/O 阻塞

---

#### 7. 颜色空间转换缓存

**文件**: `app/codec/ffmpeg/ffmpegdecoder.cpp`

```cpp
// 问题：ProcessFrameIntoTexture 每次都要创建 YUV 纹理
// 优化：缓存 YUV->RGB 转换结果

class FFmpegDecoder {
    struct ColorConversionCache {
        TexturePtr y_plane, u_plane, v_plane;
        TexturePtr rgb_result;
        rational time;
        int divider;
    };
    
    std::optional<ColorConversionCache> color_cache_;
    
    TexturePtr ProcessFrameIntoTexture(AVFramePtr f, const RetrieveVideoParams& p) {
        // 如果参数相同，直接返回缓存
        if (color_cache_ && color_cache_->time == p.time && 
            color_cache_->divider == p.divider) {
            return color_cache_->rgb_result;
        }
        // ... 正常转换 ...
    }
};
```

**预期收益**: 重复帧渲染速度提升 50%

---

### 🟢 低优先级（锦上添花）

#### 8. 着色器预编译缓存

**文件**: `app/render/renderer.cpp`

```cpp
// 问题：GetColorContext 每次都要编译着色器
// 优化：将编译好的着色器程序持久化到磁盘

class ShaderDiskCache {
    QDir cache_dir_;
    
    void save_compiled_shader(const QString& id, GLuint program) {
        GLint binary_length;
        glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binary_length);
        std::vector<uint8_t> binary(binary_length);
        GLenum format;
        glGetProgramBinary(program, binary_length, nullptr, &format, binary.data());
        // 保存到磁盘
    }
};
```

**预期收益**: 首次启动后着色器编译时间减少 90%

---

#### 9. 智能预加载策略

**文件**: `app/render/previewautocacher.cpp`

```cpp
// 问题：TryRender 中 max_tasks = 4 硬编码
// 优化：根据系统负载动态调整

void PreviewAutoCacher::TryRender() {
    // 根据当前 CPU 负载和剩余任务数动态调整
    int max_tasks = qBound(2, QThread::idealThreadCount() - 1, 8);
    
    // 根据播放方向优先预加载
    if (playback_direction_ > 0) {
        // 正向播放，优先加载后面的帧
        std::sort(pending_video_jobs_.begin(), pending_video_jobs_.end(),
                  [](const VideoJob& a, const VideoJob& b) {
                      return a.iterator.Current() < b.iterator.Current();
                  });
    }
}
```

**预期收益**: 后台缓存更智能，不影响前台播放

---

## 三、实施建议

### 第一阶段（1-2周）- 解决主要卡顿

1. 实现 LRU 缓存 O(1) 优化
2. 增加 FFmpeg 解码器帧缓存大小
3. 优化 OpenGL VAO/VBO 复用

### 第二阶段（2-3周）- 并行化

4. 渲染管线多线程化
5. 异步磁盘写入

### 第三阶段（1-2周）- 精细优化

6. 颜色空间转换缓存
7. 智能预加载策略
8. 节点遍历缓存限制

### 验证指标

- **帧率稳定性**: 播放 4K 视频时帧率波动 < 5%
- **内存占用**: 长时间编辑后内存增长 < 20%
- **响应延迟**: 拖动时间线后画面更新 < 100ms
- **CPU 利用率**: 多核利用率 > 60%

---

## 四、多线程渲染优先级调度实现方案

### 4.1 核心调度算法推荐

#### 算法一：加权公平队列（WFQ - Weighted Fair Queuing）

**适用场景**: 需要保证各优先级任务按比例获得渲染时间，避免低优先级任务饥饿

**原理**: 为每个优先级分配权重，计算虚拟完成时间（VFT），选择VFT最小的任务执行

```cpp
// WFQ 调度器实现
class WFQScheduler {
    struct Task {
        RenderTicketPtr ticket;
        RenderPriority priority;
        double weight;           // kPlayback=0.7, kPreview=0.2, kCache=0.1
        double virtual_time;     // 虚拟完成时间
        double start_time;       // 实际开始时间
    };
    
    // 权重配置（可配置化）
    static constexpr double kWeights[] = {0.7, 0.2, 0.1};
    
    // 选择下一个任务：VFT = max(当前VFT, 实际时间) + 任务大小/权重
    RenderTicketPtr SelectNext() {
        Task* best = nullptr;
        double min_vft = std::numeric_limits<double>::max();
        
        for (auto& task : all_tasks) {
            double vft = std::max(task.virtual_time, current_time_) 
                         + EstimateRenderCost(task) / task.weight;
            if (vft < min_vft) {
                min_vft = vft;
                best = &task;
            }
        }
        return best ? best->ticket : nullptr;
    }
};
```

**优点**: 公平性好，数学上可证明的带宽分配保证  
**缺点**: 需要预估任务执行时间

---

#### 算法二：最早截止时间优先（EDF - Earliest Deadline First）

**适用场景**: 实时播放保证，适合 60fps 流畅播放要求

**原理**: 为每个任务设置截止时间，总是执行截止时间最近的任务

```cpp
// EDF 调度器实现
class EDFScheduler {
    struct Task {
        RenderTicketPtr ticket;
        RenderPriority priority;
        std::chrono::steady_clock::time_point deadline;
        rational time;           // 帧时间戳（用于播放同步）
    };
    
    // 截止时间计算
    std::chrono::steady_clock::time_point CalculateDeadline(
        const Task& task, 
        rational playback_time,
        double frame_rate) 
    {
        // kPlayback: 必须在下一帧显示前完成
        // kPreview: 允许100ms延迟
        // kCache: 无严格截止时间
        switch (task.priority) {
            case RenderPriority::kPlayback:
                return now_ + std::chrono::milliseconds(
                    static_cast<int>(1000.0 / frame_rate));
            case RenderPriority::kPreview:
                return now_ + std::chrono::milliseconds(100);
            case RenderPriority::kCache:
                return now_ + std::chrono::hours(1); // 无限制
        }
    }
    
    // 选择截止时间最近的任务
    RenderTicketPtr SelectNext() {
        auto it = std::min_element(tasks_.begin(), tasks_.end(),
            [](const Task& a, const Task& b) {
                return a.deadline < b.deadline;
            });
        return it != tasks_.end() ? it->ticket : nullptr;
    }
};
```

**优点**: 实时性强，适合播放场景  
**缺点**: 高负载时可能产生多米诺效应

---

#### 算法三：多级反馈队列（MLFQ - Multi-Level Feedback Queue）

**适用场景**: 混合负载，需要自适应调整任务优先级

**原理**: 多级队列，任务超时未执行则自动降级，长时间等待则升级

```cpp
// MLFQ 调度器实现
class MLFQScheduler {
    static constexpr int kNumLevels = 3;
    static constexpr int kTimeQuantum[] = {16, 33, 66}; // ms
    
    struct Task {
        RenderTicketPtr ticket;
        int level;               // 当前队列级别 0-2
        int time_spent;          // 已执行时间
        int wait_time;           // 等待时间
    };
    
    std::deque<Task> queues_[kNumLevels];
    
    void UpdatePriorities() {
        // 等待时间过长的任务升级
        for (int level = 1; level < kNumLevels; level++) {
            for (auto& task : queues_[level]) {
                task.wait_time += kTimeSlice;
                if (task.wait_time > kBoostThreshold) {
                    task.level--;
                    task.wait_time = 0;
                    MoveToUpperQueue(task);
                }
            }
        }
    }
    
    RenderTicketPtr SelectNext() {
        // 从高优先级队列开始查找
        for (int level = 0; level < kNumLevels; level++) {
            if (!queues_[level].empty()) {
                auto task = queues_[level].front();
                queues_[level].pop_front();
                
                // 超时则降级
                if (task.time_spent > kTimeQuantum[level] && level < kNumLevels - 1) {
                    task.level++;
                    queues_[level + 1].push_back(task);
                    continue; // 选择下一个
                }
                return task.ticket;
            }
        }
        return nullptr;
    }
};
```

**优点**: 自适应，自动平衡长短任务  
**缺点**: 参数调优复杂

---

### 4.2 视频编辑专用调度策略

**组合策略**：针对视频编辑的特殊场景优化

```cpp
class VideoEditorScheduler {
public:
    // 三层调度架构
    RenderTicketPtr Schedule() {
        // Layer 1: 播放帧绝对优先（EDF）
        if (auto playback = GetPlaybackFrame()) {
            return playback;
        }
        
        // Layer 2: 预览帧按比例分配（WFQ）
        static int frame_count = 0;
        if (frame_count++ % 5 == 0) { // 每5帧渲染1帧预览
            if (auto preview = GetPreviewFrame()) {
                return preview;
            }
        }
        
        // Layer 3: 后台缓存填充（MLFQ，空闲时执行）
        if (IsIdle()) {
            return GetCacheFrame();
        }
        
        return nullptr;
    }
    
private:
    bool IsIdle() {
        // 判断当前负载：如果最近100ms内渲染帧数 < 目标帧数 * 0.8
        return recent_fps_ < target_fps_ * 0.8;
    }
    
    // 播放帧检查：必须在16.6ms内完成（60fps）
    RenderTicketPtr GetPlaybackFrame() {
        auto now = std::chrono::steady_clock::now();
        for (auto& task : playback_queue_) {
            if (task.deadline - now < std::chrono::milliseconds(16)) {
                return task.ticket;
            }
        }
        return nullptr;
    }
};
```

---

### 4.3 线程池实现架构

```cpp
// 优先级渲染线程池
class PriorityRenderPool {
public:
    PriorityRenderPool(int num_threads, Renderer* context) {
        for (int i = 0; i < num_threads; i++) {
            threads_.emplace_back(
                std::make_unique<PriorityRenderThread>(context, &scheduler_));
        }
    }
    
    void Submit(RenderTicketPtr ticket, RenderPriority priority) {
        scheduler_.Enqueue(ticket, priority);
        cv_.notify_one();
    }
    
private:
    class PriorityRenderThread : public QThread {
        void run() override {
            while (running_) {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return !scheduler_->Empty() || !running_; });
                
                if (auto ticket = scheduler_->Dequeue()) {
                    lock.unlock();
                    RenderProcessor::Process(ticket, context_, 
                                           decoder_cache_, shader_cache_);
                }
            }
        }
    };
    
    std::vector<std::unique_ptr<PriorityRenderThread>> threads_;
    std::shared_ptr<VideoEditorScheduler> scheduler_;
    std::mutex mutex_;
    std::condition_variable cv_;
};
```

---

### 4.4 线程选择策略详解

**核心问题**：有 N 个渲染线程，M 个任务，如何把任务分配到合适的线程？

#### 工作窃取（Work Stealing）算法

**原理**：每个线程有自己的本地队列，空闲时从其他线程"窃取"任务

```cpp
class WorkStealingScheduler {
    struct alignas(64) ThreadQueue {  // 缓存行对齐，避免伪共享
        std::deque<RenderTask> local_queue;
        std::mutex mutex;
    };
    
    std::vector<ThreadQueue> thread_queues_;
    std::atomic<size_t> next_queue_{0};
    
public:
    // 提交任务：轮询选择初始队列（Round-Robin）
    void Submit(RenderTask task) {
        size_t idx = next_queue_.fetch_add(1) % thread_queues_.size();
        std::lock_guard<std::mutex> lock(thread_queues_[idx].mutex);
        thread_queues_[idx].local_queue.push_back(std::move(task));
    }
    
    // 线程获取任务：先查本地，再窃取其他线程
    std::optional<RenderTask> Pop(size_t thread_id) {
        auto& local = thread_queues_[thread_id];
        
        // 1. 先尝试无锁获取本地任务
        {
            std::lock_guard<std::mutex> lock(local.mutex);
            if (!local.local_queue.empty()) {
                auto task = std::move(local.local_queue.front());
                local.local_queue.pop_front();
                return task;
            }
        }
        
        // 2. 本地为空，尝试从其他线程窃取（从队尾窃取，减少竞争）
        for (size_t i = 0; i < thread_queues_.size(); ++i) {
            if (i == thread_id) continue;
            
            auto& victim = thread_queues_[i];
            std::lock_guard<std::mutex> lock(victim.mutex);
            
            if (!victim.local_queue.empty()) {
                // 从队尾窃取，原线程从队头消费，减少冲突
                auto task = std::move(victim.local_queue.back());
                victim.local_queue.pop_back();
                return task;
            }
        }
        
        return std::nullopt; // 所有队列都为空
    }
};
```

**为什么从队尾窃取？**

- 原线程从队头取任务（FIFO，保证顺序）
- 窃取线程从队尾取（LIFO，最新任务通常缓存更热）
- 两者不会冲突，减少锁竞争

---

#### 负载均衡策略对比

| 策略                   | 实现方式        | 优点      | 缺点        | 适用场景    |
|----------------------|-------------|---------|-----------|---------|
| **轮询 (Round-Robin)** | 依次分配给每个线程   | 简单，绝对公平 | 不考虑线程当前负载 | 任务均匀场景  |
| **最少任务优先**           | 选择队列最短的线程   | 动态均衡    | 需要全局锁查询   | 任务大小差异大 |
| **工作窃取**             | 本地队列 + 空闲窃取 | 低竞争，自均衡 | 实现复杂      | 通用最佳方案  |
| **优先级分区**            | 专用线程处理高优先级  | 响应快     | 可能浪费资源    | 实时性要求高  |

---

### 4.5 避免线程空置：忙等 vs 阻塞 vs 混合

**问题**：没有任务时，线程应该忙等（spin）还是阻塞（sleep）？

```cpp
class AdaptiveWaitStrategy {
    std::atomic<bool> has_task_{false};
    
public:
    // 混合策略：先自旋，再让步，最后阻塞
    std::optional<RenderTask> PopWithAdaptiveWait(size_t thread_id, 
                                                  WorkStealingScheduler& scheduler) {
        // Phase 1: 自旋等待（适合高吞吐场景，延迟敏感）
        for (int i = 0; i < 100; ++i) {
            if (auto task = scheduler.Pop(thread_id)) {
                return task;
            }
            // CPU pause 指令，降低功耗
            _mm_pause(); // x86
            // __builtin_arm_yield(); // ARM
        }
        
        // Phase 2: 短暂自旋 + 线程让步
        for (int i = 0; i < 10; ++i) {
            if (auto task = scheduler.Pop(thread_id)) {
                return task;
            }
            std::this_thread::yield(); // 让出时间片
        }
        
        // Phase 3: 条件变量阻塞（等待新任务通知）
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(1), [&] {
            return has_task_.load(std::memory_order_relaxed);
        });
        
        return scheduler.Pop(thread_id); // 最后再试一次
    }
    
    void NotifyNewTask() {
        has_task_.store(true, std::memory_order_relaxed);
        cv_.notify_one();
    }
};
```

**策略选择指南**：

- **高帧率播放**（60fps）：使用自旋（1ms内必须响应）
- **后台缓存**：使用阻塞（节省CPU）
- **混合场景**：自适应策略（如上代码）

---

### 4.6 任务路由：如何避免放错队列

**核心问题**：播放帧被放到慢线程，缓存任务占用了快线程

#### 方案一：线程角色分区

```cpp
class PartitionedScheduler {
    // 专用线程：各司其职
    std::vector<RenderThread> playback_threads_;  // 只处理播放
    std::vector<RenderThread> cache_threads_;     // 只处理缓存
    std::vector<RenderThread> general_threads_;   // 处理其他
    
public:
    void RouteTask(RenderTask task) {
        switch (task.priority) {
            case RenderPriority::kPlayback:
                // 播放帧：选择负载最低的播放专用线程
                SelectLeastLoaded(playback_threads_).Submit(task);
                break;
                
            case RenderPriority::kCache:
                // 缓存任务：放到缓存线程，绝不占用播放线程
                SelectLeastLoaded(cache_threads_).Submit(task);
                break;
                
            case RenderPriority::kPreview:
                // 预览：可以用通用线程，如果播放线程空闲也可以用
                if (HasIdleThread(playback_threads_)) {
                    SelectIdle(playback_threads_).Submit(task);
                } else {
                    SelectLeastLoaded(general_threads_).Submit(task);
                }
                break;
        }
    }
};
```

**优点**：播放帧绝对不会被缓存任务阻塞  
**缺点**：播放线程可能空闲时缓存线程很忙

---

#### 方案二：动态优先级抢占

```cpp
class PreemptiveScheduler {
    struct ThreadState {
        std::atomic<RenderTask*> current_task{nullptr};
        std::atomic<bool> should_preempt{false};
    };
    
    std::vector<ThreadState> thread_states_;
    
public:
    // 提交高优先级任务时，可以抢占低优先级任务
    void SubmitHighPriority(RenderTask urgent_task) {
        // 1. 先找空闲线程
        for (size_t i = 0; i < thread_states_.size(); ++i) {
            if (thread_states_[i].current_task.load() == nullptr) {
                threads_[i].Submit(urgent_task);
                return;
            }
        }
        
        // 2. 没有空闲，尝试抢占最低优先级的任务
        RenderTask* lowest_priority_task = nullptr;
        size_t victim_thread = 0;
        int lowest_priority = INT_MAX;
        
        for (size_t i = 0; i < thread_states_.size(); ++i) {
            auto* task = thread_states_[i].current_task.load();
            if (task && task->priority < lowest_priority) {
                lowest_priority = task->priority;
                lowest_priority_task = task;
                victim_thread = i;
            }
        }
        
        // 3. 如果抢占的是缓存任务，且新任务是播放任务，则抢占
        if (lowest_priority_task && 
            lowest_priority_task->priority == RenderPriority::kCache &&
            urgent_task.priority == RenderPriority::kPlayback) {
            
            // 标记该线程应该放弃当前任务
            thread_states_[victim_thread].should_preempt.store(true);
            
            // 将抢占的任务重新放回队列
            Requeue(*lowest_priority_task);
            
            // 提交紧急任务
            threads_[victim_thread].Submit(urgent_task);
        } else {
            // 无法抢占，加入队列等待
            global_queue_.push(std::move(urgent_task));
        }
    }
    
    // 线程定期检查是否应该被抢占
    void CheckPreemption(size_t thread_id) {
        if (thread_states_[thread_id].should_preempt.exchange(false)) {
            // 保存当前状态，优雅退出当前任务
            SaveCurrentState();
            throw PreemptionException(); // 或者使用 longjmp
        }
    }
};
```

**注意**：抢占实现复杂，需要任务支持可中断/可恢复，建议先实现合作式抢占（任务定期让出）

---

#### 方案三：双队列 + 快速通道

最简单实用的方案：

```cpp
class DualQueueScheduler {
    // 高优先级队列：播放帧专用
    std::queue<RenderTask> playback_queue_;
    std::mutex playback_mutex_;
    
    // 普通队列：预览和缓存
    WorkStealingScheduler normal_scheduler_;
    
public:
    void Submit(RenderTask task) {
        if (task.priority == RenderPriority::kPlayback) {
            // 播放帧：直接放入快速通道
            std::lock_guard<std::mutex> lock(playback_mutex_);
            playback_queue_.push(std::move(task));
            cv_.notify_one(); // 唤醒等待的播放线程
        } else {
            // 其他任务：进入工作窃取池
            normal_scheduler_.Submit(std::move(task));
        }
    }
    
    // 专用播放线程
    void PlaybackThreadLoop() {
        while (running_) {
            std::unique_lock<std::mutex> lock(playback_mutex_);
            cv_.wait(lock, [&] { return !playback_queue_.empty() || !running_; });
            
            if (!playback_queue_.empty()) {
                auto task = std::move(playback_queue_.front());
                playback_queue_.pop();
                lock.unlock();
                
                RenderProcessor::Process(task);
            }
        }
    }
    
    // 普通渲染线程
    void NormalThreadLoop(size_t thread_id) {
        while (running_) {
            // 先检查是否有播放任务溢出（快速通道满了）
            std::unique_lock<std::mutex> lock(playback_mutex_);
            if (!playback_queue_.empty()) {
                auto task = std::move(playback_queue_.front());
                playback_queue_.pop();
                lock.unlock();
                RenderProcessor::Process(task);
                continue;
            }
            lock.unlock();
            
            // 否则从普通队列取任务
            if (auto task = normal_scheduler_.Pop(thread_id)) {
                RenderProcessor::Process(*task);
            } else {
                // 自适应等待
                AdaptiveWait();
            }
        }
    }
};
```

---

### 4.7 完整实现：结合所有最佳实践

```cpp
class ProductionScheduler {
    // 三层调度架构
    LockFreeQueue playback_queue_;           // SPSC，无锁
    WorkStealingScheduler preview_pool_;     // 工作窃取
    BackgroundScheduler cache_scheduler_;    // 后台批处理
    
    struct ThreadConfig {
        enum Role { kPlayback, kPreview, kGeneral } role;
        size_t id;
        std::atomic<bool> idle{true};
    };
    std::vector<ThreadConfig> thread_configs_;
    
public:
    ProductionScheduler() {
        // 线程0-1：播放专用（绑定到特定CPU核心，减少缓存失效）
        // 线程2-3：预览（工作窃取）
        // 线程4：后台缓存（最低优先级，空闲时运行）
    }
    
    void Submit(RenderTask task) {
        switch (task.priority) {
            case RenderPriority::kPlayback:
                // 播放帧：直接发送到播放队列
                // 如果播放线程忙，任务会在队列中等待（最多1帧延迟）
                playback_queue_.enqueue(std::move(task));
                
                // 唤醒播放线程（使用eventfd / pipe / condition variable）
                WakePlaybackThread();
                break;
                
            case RenderPriority::kPreview:
                // 预览：先尝试给空闲的播放线程（如果播放线程空闲）
                if (TryAssignToIdlePlaybackThread(task)) {
                    return;
                }
                // 否则进入预览池
                preview_pool_.Submit(std::move(task));
                break;
                
            case RenderPriority::kCache:
                // 缓存：只给后台线程，绝不干扰播放
                cache_scheduler_.Submit(std::move(task));
                break;
        }
    }
    
private:
    bool TryAssignToIdlePlaybackThread(const RenderTask& task) {
        for (size_t i = 0; i < 2; ++i) { // 只查前2个播放线程
            bool expected = true;
            if (thread_configs_[i].idle.compare_exchange_strong(expected, false)) {
                // 原子操作成功，该线程确实是空闲的
                playback_threads_[i].Submit(task);
                return true;
            }
        }
        return false;
    }
};
```

---

### 4.8 推荐实施路线

| 阶段       | 算法            | 适用场景          | 实现难度 |
|----------|---------------|---------------|------|
| **第一阶段** | 严格优先级 + 时间片轮转 | 快速验证多线程效果     | ⭐    |
| **第二阶段** | EDF           | 保证 60fps 播放流畅 | ⭐⭐   |
| **第三阶段** | WFQ + EDF 混合  | 平衡播放与预览       | ⭐⭐⭐  |
| **第四阶段** | 完整 MLFQ       | 复杂项目长期优化      | ⭐⭐⭐⭐ |

**建议起步方案**：

1. 先用**严格优先级队列**（`std::priority_queue`）实现基础多线程
2. 验证效果后引入**EDF**确保播放实时性
3. 最后根据实际负载数据选择 WFQ 或 MLFQ 进行精细调优

**关键配置参数**：

```cpp
// 推荐初始配置
constexpr int kPlaybackThreads = 2;      // 播放专用线程
constexpr int kPreviewThreads = 1;       // 预览线程
constexpr int kCacheThreads = 1;         // 后台缓存线程
constexpr double kPlaybackPriority = 1.0; // 播放权重100%
constexpr double kPreviewPriority = 0.25; // 预览权重25%
constexpr double kCachePriority = 0.1;   // 缓存权重10%
```

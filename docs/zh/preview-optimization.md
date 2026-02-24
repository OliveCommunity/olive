# 预览系统优化建议

本文档记录了 Oak Video Editor 预览系统的可优化点，按优先级排序。

## 高优先级优化

### 1. 音频预加载缓冲区优化

**当前问题**：`kAudioPlaybackInterval = 1/4 秒`，只预加载 2 个缓冲区（500ms），在网络延迟或高负载时可能出现音频断续。

**文件位置**：`app/widget/viewer/viewer.h` (第 65 行)

**优化建议**：
```cpp
// 根据播放速度动态调整预加载数量
static const int kMinAudioPrequeue = 2;  // 正常速度
static const int kMaxAudioPrequeue = 4;  // 高速播放时

// 在 PlayInternal() 中：
int prequeue_count = (playback_speed_ > 1) ? kMaxAudioPrequeue : kMinAudioPrequeue;
```

**预期效果**：减少高速播放时的音频断续问题。

---

### 2. 渲染优先级动态调整

**当前问题**：播放时的预览渲染和后台缓存使用相同的 `RenderPriority::kCache` 优先级，可能导致播放卡顿。

**文件位置**：`app/render/previewautocacher.cpp` (第 667 行)

**优化建议**：
```cpp
// 根据播放状态动态调整优先级
RenderPriority priority = IsPlaying() ? RenderPriority::kPlayback 
                                      : RenderPriority::kCache;
```

**预期效果**：播放时优先渲染当前帧，后台缓存降低优先级。

---

### 3. 预加载队列大小动态计算

**当前问题**：`DeterminePlaybackQueueSize()` 使用固定最大帧数，没有考虑实际帧率和播放速度。

**文件位置**：`app/widget/viewer/viewer.cpp` (第 1358-1380 行)

**当前代码**：
```cpp
int max_frames = qCeil(kVideoPlaybackInterval.toDouble() / timebase().toDouble());
```

**优化建议**：
```cpp
// 根据播放速度动态调整队列大小
int playback_step = qMax(1, qAbs(playback_speed_));
int max_frames = qCeil(kVideoPlaybackInterval.toDouble() / 
                       (timebase().toDouble() / playback_step));
// 设置上下限防止内存过度使用
max_frames = qBound(4, max_frames, 16);
```

**预期效果**：高速播放时预加载更多帧，防止画面卡顿。

---

## 中优先级优化

### 4. 解码器缓存 LRU 策略

**当前问题**：`ClearOldDecoders()` 只根据时间过期清理（5秒），没有考虑内存使用和访问频率。

**文件位置**：`app/render/rendermanager.cpp` (第 249-271 行)

**优化建议**：
```cpp
// 添加内存限制和 LRU 策略
static constexpr size_t kMaxDecoderCacheSize = 10;

void RenderManager::ClearOldDecoders()
{
    // 如果时间过期或超出大小限制，清理最久未使用的解码器
    if (decoder_cache_->size() > kMaxDecoderCacheSize) {
        RemoveLRUDecoder();
    }
    // ... 原有逻辑
}
```

**预期效果**：减少内存使用，保留常用的解码器。

---

### 5. 单帧渲染取消优化

**当前问题**：`CancelQueuedSingleFrameRender()` 只是标记取消，但任务可能仍在渲染队列中占用资源。

**文件位置**：`app/render/previewautocacher.cpp` (第 354-361 行)

**优化建议**：
```cpp
void PreviewAutoCacher::CancelQueuedSingleFrameRender()
{
    if (single_frame_render_) {
        // 从渲染队列中立即移除，而不仅仅是标记取消
        RenderManager::instance()->RemoveTicket(single_frame_render_);
        single_frame_render_->Finish();
        single_frame_render_ = nullptr;
    }
}
```

**预期效果**：快速拖动播放头时减少不必要的渲染任务。

---

### 6. 后台缓存任务节流

**当前问题**：播放时后台缓存任务可能与播放竞争资源，导致帧率下降。

**文件位置**：`app/render/previewautocacher.cpp` (第 560-604 行)

**优化建议**：
```cpp
// 在 TryRender() 中，处理视频任务前：
if (IsPlaying() && running_video_tasks_.size() >= kMaxPlaybackTasks) {
    // 播放时暂停后台缓存，优先播放
    return;
}
```

**预期效果**：播放时减少后台渲染，提高播放流畅度。

---

## 低优先级优化

### 7. 音频/视频同步精确度优化

**当前问题**：`UpdateFromQueue()` 使用近似比较（半个帧间隔），可能在某些帧率下不够精确。

**文件位置**：`app/widget/viewer/viewerdisplay.cpp` (第 1417-1475 行)

**优化建议**：
```cpp
// 使用显示刷新率作为容差基准
double vsync_interval = 1.0 / GetDisplayRefreshRate();  // ~16.67ms for 60Hz
bool time_matches = time_diff.toDouble() < vsync_interval / 1000.0;
```

**预期效果**：提高音视频同步精度。

---

### 8. 解码器预打开优化

**当前问题**：首次播放时需要等待解码器打开，造成卡顿。

**文件位置**：新项目加载时

**优化建议**：
```cpp
// 在 PreviewAutoCacher 中添加：
void PreviewAutoCacher::PreopenDecoders(ViewerOutput *context)
{
    // 预打开时间线中使用的所有素材解码器
    // 可以在后台线程中执行，不影响UI响应
}
```

**预期效果**：减少首次播放的启动时间。

---

## 性能监控建议

建议添加以下性能指标监控，用于识别瓶颈：

1. **帧渲染时间**：单帧渲染耗时
2. **队列等待时间**：任务在队列中的等待时间
3. **解码器命中率**：缓存解码器的使用频率
4. **音频缓冲区状态**：音频队列的大小和欠载次数

```cpp
// 示例：在 RenderProcessor 中添加计时
class RenderPerformanceMonitor {
    void RecordFrameRenderTime(qint64 ms);
    void RecordDecoderCacheHit(bool hit);
    void RecordAudioUnderrun();
};
```

---

## 总结

| 优先级 | 优化项 | 预期收益 | 实现复杂度 |
|--------|--------|----------|------------|
| 高 | 音频预加载缓冲区 | 减少音频断续 | 低 |
| 高 | 渲染优先级动态调整 | 播放更流畅 | 低 |
| 高 | 预加载队列大小 | 高速播放更流畅 | 中 |
| 中 | 解码器缓存 LRU | 减少内存使用 | 中 |
| 中 | 单帧渲染取消优化 | 拖动更响应 | 低 |
| 中 | 后台缓存任务节流 | 播放更流畅 | 低 |
| 低 | 音视频同步精度 | 同步更精确 | 低 |
| 低 | 解码器预打开 | 减少启动时间 | 高 |

建议按优先级逐步实施，并配合性能测试验证效果。

---

*英文版参考：[Preview System Optimization](../preview-optimization.md)*

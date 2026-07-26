# M6 · oakaudio 拆分手册

> 内容：`engine/audio/`（AudioManager、AudioProcessor、输出设备
> 抽象、audiovisualwaveform）。
> 依赖：common 5、core 5、config 2、render 2、codec 1。拆分顺序第 6 位
> （小模块，穿插在 render 前做掉）。

## 1. 目标形态

```
oakaudio/
  include/oakaudio/{manager.h, processor.h, types.h, export.h}
  src/
  tests/  # oakaudio_gtest
```

## 2. 冻结 C API

### 2.1 `oakaudio/processor.h`（实时重采样/格式转换，R6 已建过
`oakengine_audio_processor_*`，本表即其 oakaudio 版，语义不变）

```c
typedef struct OakAudioProcessor OakAudioProcessor;
OAKAU_API OakAudioProcessor *oakaudio_processor_init(void);
OAKAU_API void oakaudio_processor_free(OakAudioProcessor *p);
OAKAU_API int oakaudio_processor_open(OakAudioProcessor *p,
	int in_rate, uint64_t in_layout, int in_format,
	int out_rate, uint64_t out_layout, int out_format, double speed);
OAKAU_API void oakaudio_processor_close(OakAudioProcessor *p);
OAKAU_API int oakaudio_processor_convert(OakAudioProcessor *p,
	float **data, int frame_count);
```

### 2.2 `oakaudio/manager.h`

```c
OAKAU_API int oakaudio_manager_init(int prefer_backend /* -1=auto */);
OAKAU_API void oakaudio_manager_shutdown(void);
OAKAU_API int oakaudio_manager_set_output_params(int rate,
	uint64_t layout, int format);
OAKAU_API int oakaudio_manager_get_output_params(int *rate,
	uint64_t *layout, int *format);
/* 推流：播放路径逐块喂 samples（float 交错） */
OAKAU_API int oakaudio_manager_push(const float *samples,
	int frame_count, double speed);
OAKAU_API int oakaudio_manager_is_playing(void);
OAKAU_API void oakaudio_manager_stop(void);
/* 输出参数变化事件 */
OAKAU_API int64_t oakaudio_manager_subscribe_params_changed(
	oakaudio_event_fn fn, void *userdata);
OAKAU_API void oakaudio_unsubscribe(int64_t id);
```

## 3. 切割点

| 现状 | 处理 |
|---|---|
| audio → render/ 2 | audiovisualwaveform 对 render 类型的引用：POD 化（波形 min/max 对数组），残留类型随 M3.5 下沉 |
| audio → codec/ 1 | 经 oakcodec C ABI（M5 已就位） |

## 4. 测试（映射 03 §2/§3）

- processor：open/convert/close 全链（44.1k stereo → 48k stereo，
  帧数换算正确、无爆音断言用能量差阈值）、速度 1.5x。
- manager：无音频设备环境用 null backend 初始化（现有后端探测
  模式），params set/get 往返、事件触发。
- `oakaudio_debug_alive_count()` 泄漏断言。

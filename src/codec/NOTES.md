# oakcodec 中间态与行为变化备忘（M5）

## 中间态（等待后续里程碑收口）

1. **Task 回调注册**（M8 收口）：conform/proxy 的后台任务经
   `include/codec/task.h` 的全局提交回调（`oakcodec_set_task_submit_cb`）。
   未注册时：conform 查询返回 `k_conform_unavailable`，proxy 保持
   `k_proxy_missing`，不崩溃不阻塞。注册语义为同步提交（回调内完成或
   排队后立即返回）；`SubmitTask` 持锁调回调，回调内不可重入注册函数。
   conform/proxy 任务的 working→finished 改名生命周期整体移交 M8 oaktask。
2. **Config**（config 波次已收口）：`ProxyManager::proxy_params_from_config()`
   现经 `oakcommon_config_*` C ABI 读取 ProxyWidth/ProxyHeight/ProxyDivider/
   ProxyCRF/ProxyPreset/ProxyIncludeAudio，ProxyParams 成员默认值兼作
   getter fallback（与 oakcommon 编译期默认值一致：1280x720/div1/crf23/
   veryfast/含音频）。
3. **纹理路径功能回退**（oakrender 增补 shader-blit C API 后可恢复）：
   oakrender C API 无通用 shader-blit，FFmpegDecoder 的 yuv2rgb GLSL 路径与
   去隔行 shader 路径已删除；YUV 帧改在 CPU 上 swscale 转 RGBA 后
   `oakrender_display_texture_upload`（功能保留但更慢；去隔行在纹理路径
   丢失，CPU 帧路径本就不做去隔行）。Texture 零拷贝持有 hw frame 一并删除。
4. **FootageDescription 为 codec 内部结构**（src/codec/src/footagedescription.h）：
   oaknode C API 无对应物；未实现探针缓存 XML load/save 与
   `get_type_of_stream()`（oaknode `Track::Type` 映射），oaknode footage
   侧需要时再补。
5. **RenderMode**：oakrender C API 无对应物，codec 本地 enum
   （decoder.h，k_offline=0/k_online=1，值对齐 engine/render/rendermodes.h）。
6. **无 adapter 层**（2026-08 第二轮拍板）：codec 内部跨模块调用全部直调
   `oakcommon_*` / `oakrender_*` C 函数，句柄（OakVideoParams/
   OakColorTransform/OakCancelAtom/OakSubtitleParams）就地按值管理计数；
   只有真正多处重复的转换保留文件内 static 小函数（如
   fill_render_params、cancel_atom_is_cancelled）。早期的一版
   src/codec/src/adapter/ 包装类已删除。
7. **XmlStreamWriter/Reader**：照 DEQT.md 用 oakcommon 的 C++ 类
   （src/common/src/xmlutils.h，与 oaknode/oakrender 的实践一致），未走
   C API —— 决策 7 的唯一例外，记录在案。

## 行为变化（相对 Qt 版）

- Decoder 的 `index_progress` 信号 → `std::function<void(double)>`
  回调（`set_index_progress_callback`）；conform_ready/proxy_ready/
  proxy_finished 信号删除（通知归 facade/task 系统）。
- ConformManager 无状态化：`conforming_` 列表与完成 slot 删除；
  `get_conform_state` 去掉 `decoder_id` 参数；等待语义改为同步提交后
  重查文件系统。
- `Encoder::write_subtitle(const SubtitleBlock*)` →
  `write_subtitle(const char *text, double in_seconds, double out_seconds)`。
  注意原实现传的是 `sub_block->length()`（时长），新调用方传 out=in+length。
- `EncodingParams::generate_matrix` 返回 `std::array<float,16>`（行主序），
  原 QMatrix4x4；`load/save` 的 QIODevice 版本变
  `load(const std::string&)`/`save_to_string()`，预设 XML 不再含声明与
  缩进（紧凑 XML，元素/属性名与顺序不变）；`video_opts_` 的 XML 顺序
  由 QHash 无序变为字典序。保留了 load_v1 不赋 custom_range_ 的原 bug。
- `PlanarFileDevice::open` 用 `std::vector<std::string>` + 类内
  `OpenMode` 枚举（k_read_only/k_write_only），FILE* 实现。
- FFmpegDecoder 无后台 QThread（现 engine 版本已是同步 retrieve 循环）。
- 音频 decode（C API）：需要 conform 的媒体在无 task 注册方时返回
  `OAKCODEC_E_STATE`（不产生后台 conform）。
- `oakcodec_audio_stream_info.duration_ts` 恒 0（AudioParams 不带时长）。

## 符号可见性

oakcodec 以 `-fvisibility=hidden` 编译，仅导出 `OAKCODEC_API` 标记的
C 函数（include/codec/error.h 定义宏）。必须如此：codec 内部 adapter
类（olive::VideoParams 等）与 oakcommon/oakrender 内同名弱符号会
interpose（曾在 oakcommon_videoparams_init_with_time_base 内部把
VideoParams::width() 绑进 liboakcodec 导致崩溃）。

## oakcommon 侧修复（随 M5 落地）

- `frame_to_buffer`/`buffer_to_frame` 移入 codec（oiioframebridge.h，
  内部 C++ 函数），oakcommon 的 OIIO 映射函数保留。
- 修复 `src/common/c_api/videoparams.cpp` 的 `convert_to_olive_format`
  switch 缺 break 穿透 bug（U8 穿透到 f32，bytes_per_pixel 返回 16）。

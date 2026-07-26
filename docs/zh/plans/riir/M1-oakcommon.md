# M1 · oakcommon 拆分手册

> 内容：`engine/common/`（41 文件通用工具集）+ `engine/config/`。
> 依赖：oakcore（2）。被依赖：几乎全员（node 31、render 24、
> codec 12、plugin 6、audio 5、task 4、timeline 4、undo 2）。
> 拆分顺序第 1 位（叶子）。

## 1. 目标形态

```
oakcommon/
  include/oakcommon/   # 公共头（C ABI + 允许直引的纯头工具）
  src/                 # 现有 .cpp 原样迁入
  tests/               # oakcommon_gtest
```

- **纯头工具**（lerp.h、define.h、decibel.h、tohex.h、digit.h、
  memorypool.h、threadsafemap.h、range.h 等无 .cpp 的）：作为
  oakcommon 公共头直接提供给其他模块 include——拆分阶段允许
  （不产生链接依赖）。RIIR 阶段这些会改写成各语言自有实现。
- **config/config.h**：单头配置存取，被全模块引用（10+）。
  它是 Qt 依赖（QSettings 包装），按 §2 冻结 C ABI。

## 2. 冻结 C API（有 .cpp 实现的函数族）

命名前缀 `oakcommon_`。以下按头分组（签名机械规则见 01 §3，
此处冻结函数清单与特殊约定）：

### 2.1 `oakcommon/config.h`（对应 config/config.h）

```c
void oakcommon_config_set(const char *group, const char *key,
                          const char *value_utf8);
int  oakcommon_config_get(const char *group, const char *key,
                          char *buf, int buf_size);          /* 两段式 */
int  oakcommon_config_get_int(const char *group, const char *key,
                              int fallback);
double oakcommon_config_get_double(const char *group, const char *key,
                                   double fallback);
void oakcommon_config_set_int(const char *group, const char *key, int v);
```

### 2.2 `oakcommon/xml.h`（对应 common/xmlutils.h，8 次被引）

```c
/* XMLAttributeLoop/xml_read_next_start_element 的 C 化：
 * 以迭代器句柄包装 QXmlStreamReader */
typedef struct OakCommonXmlReader OakCommonXmlReader;
OakCommonXmlReader *oakcommon_xml_reader_init(const char *utf8, int len);
void oakcommon_xml_reader_free(OakCommonXmlReader *r);
int  oakcommon_xml_read_next_start_element(OakCommonXmlReader *r);
int  oakcommon_xml_reader_name(OakCommonXmlReader *r, char *buf, int n);
int  oakcommon_xml_reader_attr(OakCommonXmlReader *r, const char *attr,
                               char *buf, int n);
int  oakcommon_xml_reader_read_element_text(OakCommonXmlReader *r,
                                            char *buf, int n);
void oakcommon_xml_reader_skip_current(OakCommonXmlReader *r);
```

写出侧 `oakcommon_xml_writer_*`（init_to_string/write_attribute/
write_text_element/free 得字符串，两段式）。

### 2.3 `oakcommon/files.h`（common/filefunctions.h，render 7 次）

```c
int  oakcommon_file_exists(const char *path);                 /* 1/0 */
int  oakcommon_file_size(const char *path);                   /* -1 失败 */
int  oakcommon_file_read_all(const char *path, char *buf, int n); /* 两段式 */
int  oakcommon_file_write_all(const char *path, const char *data, int n);
int  oakcommon_dir_mkpath(const char *path);
int  oakcommon_get_config_path(char *buf, int n);
int  oakcommon_get_temp_path(char *buf, int n);
```

### 2.4 `oakcommon/ocio.h`、`oakcommon/oii.h`、`oakcommon/ffmpeg.h`

（ocioutils/oiioutils/ffmpegutils，按 01 §3 机械 POD 化；
OAK/OIIO/FFmpeg 类型全部句柄化或拍平字段。）

### 2.5 `oakcommon/misc.h`

`jobtime`（`double oakcommon_jobtime_now(void)`）、`current`
（`oakcommon_current_get/set` 线程局部当前对象句柄）。

## 3. 切割点（common 的 12 次反向 include，逐条）

| 现状 | 处理 |
|---|---|
| common → render/ 7 次（播放钟/自动滚动等引 render 类型） | 涉及文件（playbackaudioclock/autoscroll 等）**上移出 oakcommon**：它们不是底层工具，归 oakrender（M7） |
| common → node/ 3 次 | 同上，归 oaknode（M3） |
| common → undo/ 1 次 | 同上，归 oakundo（M2） |
| common → codec/ 1 次 | 同上，归 oakcodec（M5） |
| common → pluginSupport/ 1 次 | 同上，归 oakplugin（M9） |

判据：切完后 `grep -rn '#include "' oakcommon/src | grep -vE
'"(oakcommon|olive/core)'` 为空（只剩 oakcore 与 Qt/系统头）。

## 4. 测试（映射 03 §2）

- config：set/get 往返、int/double fallback、两段式 buf。
- xml：reader 解析样例串、attr/text 读取、skip、writer 产出解析回读。
- files：临时目录建/写/读/尺寸/删除。
- 每函数 1 正常 + 1 错误路径；`oakcommon_debug_alive_count()` 泄漏断言。

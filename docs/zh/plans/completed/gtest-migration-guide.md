# 测试统一到 Google Test — 迁移指引

> 本文指导把仓库里并存的三套测试框架统一收敛到 **Google Test**。
> 面向执行者（DeepSeek Flash 或任何接手代理），自包含，可直接照做。
> 工作分支：`c-abi-migration`。**启动前提：R5（C ABI app 侧迁移）验收完
> 成之后**（R5 期间测试是唯一的回归防线，不在迁移途中换测试框架）；
> 启动后可与 UI 改版计划并行（文件域不相交）。每迁完一个测试二进制
> 立即提交，每步全量 ctest 绿才进下一步。
>
> **ctest 的定位**：统一后 ctest 仍然是唯一的测试**运行入口**
> （`ctest --output-on-failure`），Google Test 是唯一的测试**编写框架**。
> 两者不冲突——用 `gtest_discover_tests()` 让 ctest 按用例粒度发现 gtest 用例。

---

## 1. 现状：三套框架并存

| 框架 | 位置 | 编写方式 | 构建/注册 |
|---|---|---|---|
| **Google Test（目标形态）** | `tests/gtest/*.cpp` | `TEST()/TEST_F()/TEST_P()`，单一 `olive-gtest` 二进制 | `tests/gtest/CMakeLists.txt`，共享 `main.cpp`（QApplication + offscreen + OCIO） |
| 自研 OAK 宏框架 | `tests/timeline/timeline-tests.cpp`、`tests/compositing/compositing-tests.cpp` | `OAK_ADD_TEST(name)` + `OAK_ASSERT(x)` | `tests/CMakeLists.txt` 的 `olive_add_test()` 宏，正则扫宏生成 `main()` |
| 纯 C assert | `engine/tests/oakengine_*_test.cpp`、`core/tests/oakcore_*_test.cpp` | 手写 `main()` + `assert()` | `engine/CMakeLists.txt` 的 `make_oakengine_test()`，每个文件一个独立 ctest 二进制 |

## 2. 为什么统一到 Google Test

1. **断言可读性**：`assert(x)` 失败只说行号；`EXPECT_EQ(a, b)` 打印左右值，
   定位快一个数量级。这正是近几轮 facade 调试里最痛的一点。
2. **fixture 替代手工样板**：纯 C 测试里每个文件都手写 `oakengine_init` /
   `setenv("XDG_*")` / 临时目录 / `oakengine_project_free`，gfixture 的
   `SetUp()/TearDown()`/`SetUpTestSuite()` 一次性收口。
3. **`GTEST_SKIP()`**：GPU/缺资源用例优雅跳过（offscreen OpenGL 不可绘现在
   靠崩/超时区分，不好维护）。
4. **过滤与重复**：`--gtest_filter`、重复运行（压 flaky）、死亡测试。
5. **`assert()` 在 NDEBUG 下被吞**：纯 C 测试一旦开 Release 编译就形同虚设，
   这是统一的硬理由之一。

## 3. 目标结构

```
tests/gtest/            # app 集成测试（已是 gtest，保持不变，按需并入新用例）
engine/tests/           # liboakengine facade 测试，改写为 gtest
  CMakeLists.txt        # 一个 oakengine_gtest 目标 + gtest_discover_tests
core/tests/             # liboakcore 测试，改写为 gtest
  CMakeLists.txt        # 一个 oakcore_gtest 目标 + gtest_discover_tests
tests/timeline/         # 删除 olive_add_test 产物，timeline-tests.cpp 改写为 gtest
tests/compositing/      # 同上
```

- `tests/CMakeLists.txt` 的 `olive_add_test()` 宏与 `tests/testutil.h` 的
  `OAK_ADD_TEST`/`OAK_ASSERT`/`OAK_TEST_END` 宏全部删除。
- `engine/CMakeLists.txt` 的 `make_oakengine_test()` 宏删除。
- 每个新 gtest 二进制经 `gtest_discover_tests(<target>)` 进 ctest；**ctest 总
  用例数不得少于迁移前**（迁移前列一张基线清单核对）。

## 4. 转换配方

### 4.1 OAK_ADD_TEST 宏框架（tests/timeline、tests/compositing）

| 旧 | 新 |
|---|---|
| `OAK_ADD_TEST(name)` | `TEST(SuiteName, name)` |
| `OAK_ASSERT(x)` | `ASSERT_TRUE(x)` |
| `OAK_ASSERT_EQUAL(a, b)` | `ASSERT_EQ(a, b)`（自定义宏会打印左右值，直接换掉） |
| `TIMELINE_TEST_START`（ColorManager::set_up_default_config + Project + Sequence） | `class TimelineTest : public ::testing::Test { void SetUp() override {...} }` |
| `OAK_TEST_END / return OLIVE_TEST_SUCCESS` | 删除（gtest 自动判过） |

例：

```cpp
// 旧
OAK_ADD_TEST(add_track) {
    TIMELINE_TEST_START;
    OAK_ASSERT(sequence.track_list(Track::k_video)->get_track_count() == 1);
}

// 新
TEST_F(TimelineTest, AddTrack) {
    ASSERT_EQ(sequence.track_list(Track::k_video)->get_track_count(), 1);
}
```

### 4.2 纯 C assert（engine/tests、core/tests）

| 旧 | 新 |
|---|---|
| 手写 `int main()` | 删除，链接共享 gtest main |
| `assert(x)` | `ASSERT_TRUE(x)` / `EXPECT_TRUE(x)` |
| `assert(fabs(a-b) < eps)` | `EXPECT_NEAR(a, b, eps)` |
| `assert(strcmp(a, b) == 0)` | `EXPECT_STREQ(a, b)` |
| `make_tmpdir()` + `setenv("XDG_*")` | `SetUpTestSuite()` 里建一次临时目录 |
| 每文件自带 `oakengine_init/shutdown` | 共享 fixture 做（见 §5.2） |

**纯 C ABI 测试照写 C 调用**：gtest 文件是 C++，直接 `#include "oakengine/xxx.h"`
调 `oakengine_*` 函数即可，不需要把被测 API 改成 C++。断言里出现
`OakEngineNode*` 等不透明句柄比较用 `EXPECT_EQ((void*)a, (void*)b)`。

**过渡期技巧（可选，不推荐长期使用）**：文件量太大时可先加一个
`#define assert(x) ASSERT_TRUE(x)` 的兼容头，把 `main()` 删掉挂进 gtest，
再逐文件把 `assert` 换成语义化 `EXPECT_*`。但**最终态不许留 `assert()`**。

### 4.3 已是 Google Test 的（tests/gtest）

不动。新增的 engine/core 用例如需 app 侧对象，可直接加进 `olive-gtest` 目标。

## 5. 落地步骤（按顺序，每步闭环：构建 + 全量 ctest 绿 + 提交）

### 5.1 基线
先跑 `ctest -N` 记录迁移前用例总数，存为 `docs/zh/gtest-migration-baseline.md`
（迁移后对比，总数只增不减）。

### 5.2 共享 fixture/main
- `core/tests/main.cpp`：`RUN_ALL_TESTS` + `SetUpTestSuite` 建 XDG 临时目录。
- `engine/tests/main.cpp`：同上，外加 `oakengine_init(OAKENGINE_INIT_HEADLESS)`，
  `TearDownTestSuite` 调 `oakengine_shutdown()`；`OAK_TEST_SOURCE_DIR` 经
  `target_compile_definitions` 传入（照 `make_oakengine_test` 现有做法）。
- XDG 沙箱**每个二进制一份**，不要每个测试一份（与现状一致，避免并发冲突）。

### 5.3 core/tests（最小、无 Qt，先练手）
逐文件改写 `oakcore_*_test.cpp` 为 gtest，删手写 main；建 `oakcore_gtest` 目标
（链 `oakcore` + `GTest::gtest` + `GTest::gtest_main`），`gtest_discover_tests`。
全量 ctest 绿后提交。

### 5.4 tests/timeline、tests/compositing（OAK 宏框架）
按 §4.1 改写；建独立 gtest 目标或并入合适目标；删除 `olive_add_test` 调用、
`tests/testutil.h` 宏与 `tests/CMakeLists.txt` 中的宏定义。全量 ctest 绿后提交。

### 5.5 engine/tests（facade 测试，量最大）
按 §4.2 改写 `oakengine_*_test.cpp`；建 `oakengine_gtest` 目标（链 `oakengine`
+ Qt + gtest），删 `make_oakengine_test`。全量 ctest 绿后提交。

### 5.6 收尾
- `ctest -N` 对比基线（只增不减）；全量 `--output-on-failure` 绿。
- 全仓库 grep 确认无 `OAK_ADD_TEST`/`OAK_ASSERT`/`make_oakengine_test`/
  `olive_add_test` 残留。
- 更新 `../..` 相关文档与本指引标注"已完成"。

## 6. 注意事项（别踩坑）

1. **GPU/渲染用例**：沿用 `GTEST_SKIP()` 判定（参
   `tests/gtest/render_worker_footage_test.cpp` 的 backend 检查与
   viewer_display_repro_test 的 offscreen 跳过模式），不许靠超时/崩溃区分。
2. **offscreen/OCIO**：需要 QApplication 的用例共享 `tests/gtest/main.cpp` 的
   环境初始化（offscreen QPA + OCIO 配置）；engine/core 的无头用例走
   `oakengine_init(HEADLESS)`，不要重复造 QApplication。
3. **测试数据路径**：`OAK_TEST_SOURCE_DIR` 必须经 CMake 定义传入
   （`tests/demo.mp4` 等），不要硬编码相对路径。
4. **线程/事件**：facade 事件类测试（`oakengine_events_test` 等）依赖
   DirectConnection 同步语义，迁移时保持原用例的线程假设，不要引入
   `QCoreApplication::processEvents` 之外的等待方式。
5. **一次性迁移 vs 渐进**：按 §5 的顺序渐进，**禁止**先删框架再慢慢补测试
   （会造成不可测试的空窗）。每步都必须全量绿。
6. **ctest 仍是入口**：CI/本地都继续用 `ctest --output-on-failure -j$(nproc)`；
   `gtest_discover_tests` 注册后，单个用例可用 `ctest -R <SuiteName.CaseName>`
   或 `./<binary> --gtest_filter=...` 跑。

# 03 · 测试规范（每模块每 C API 必有 Google Test）

> 适用于 M1-M9 全部模块。拆分阶段的测试回答一个问题：**经过双层
> 适配器之后，行为和直连 C++ 时一致**。本文冻结测试结构、覆盖要求
> 与 fixture 模式。

## 1. 结构

- 每个模块一个 gtest 二进制：`oak<mod>/tests/`，目标名
  `oak<mod>_gtest`，链接 `oak<mod>` + `GTest::gtest` +
  `GTest::gtest_main` + Qt（Core/Gui 按需），经
  `gtest_discover_tests()` 注册进 ctest（项目统一约定：Google Test
  编写、ctest 运行）。
- 共享 `main.cpp`：Qt 初始化照 `tests/gtest/main.cpp` 现有模式
  （需要 QApplication 的用 offscreen QPA；纯逻辑的不用建）。
- 需要 engine 全局状态的（EngineCore 单例、ColorManager 配置），
  fixture 的 `SetUpTestSuite` 里 `oakengine_init(OAKENGINE_INIT_HEADLESS)`，
  `TearDownTestSuite` 里 shutdown——照 `engine/tests/` 现有惯例。

## 2. 覆盖要求（硬指标）

1. **每个 C API 函数至少 1 个 TEST**。模块手册的 C API 表逐行对应
   测试用例；模块完成判据包含一张核对表（M 手册附录，打勾）。
2. 每个函数至少覆盖：**正常路径 1 个** + **错误路径 1 个**
   （NULL self / 越界索引 / E_INVALID 参数）。
3. **init/free 配对**：每个 init 测试都用泄漏断言收尾（模块级
   `oak<mod>_debug_alive_count()` 调试计数器——各模块在 capi 实现里
   顺手暴露，测试用它断言"测试前后存活对象数相等"）。
4. **枚举序数一致性**：C 侧 POD/枚举与 C++ 侧枚举的映射（01 §3 表）
   每个映射 1 个 TEST（如 `oakundo` 的 movement mode 0-3 ⇄
   `Timeline::MovementMode`）。
5. **事件/回调**：每个 `set_*_cb`/subscribe 至少 1 个 TEST：触发后
   断言回调被调、payload 正确；反注册后断言不再被调。
6. **所有权**：borrowed 句柄（文档注释标了 `/* borrowed */` 的）
   free 后原对象仍存活，1 个 TEST。

## 3. 双层往返测试（每个模块至少一套）

证明"消费侧适配类 ⇄ C ABI ⇄ 提供侧实现"全链路与原 C++ 行为一致：

```cpp
class UndoStackRoundtripTest : public ::testing::Test {
protected:
	void SetUp() override {
		// 提供侧直接 C++ 操作 + 经 C API 操作，对比可观察状态
	}
};

TEST_F(UndoStackRoundtripTest, PushPopSymmetry) {
	OakUndoStack *h = oakundo_undostack_init(nullptr);
	// 经 C API push 两条命令
	// 断言 can_undo==1、count==2、jump(0) 后 can_undo==0
	// 再经消费侧适配类 olive::UndoStack 包一层做同样操作，断言一致
	oakundo_undostack_free(h);
}
```

规则：往返测试操作的是**真实实现**（非 mock）；对比点选
"可观察状态"（计数、标志位、文本、事件序列），不比较内部指针。

## 4. 禁止

- 禁止 mock 提供侧实现来"证明"适配层正确（那是自欺欺人）。
- 禁止用 `assert()` 裸断言（项目规则：一律 `EXPECT_*/ASSERT_*`）。
- 禁止测试间共享可变全局状态（每个 TEST 自建对象；单例类资源在
  fixture 里清理）。
- GPU/GL 相关用例：沿用 `GTEST_SKIP()` 判定模式（offscreen 不可绘
  就跳过，不许靠超时区分）。

## 5. ctest 基线纪律

- 每模块拆完：`ctest -N` 用例数只增不减；新增模块测试全部绿；
  既有 45 个不许回归。
- flaky 规则沿用（`oak_cli_transcode`/`oakengine_export_test`/
  `olive-gtest` 单独重跑一次，连续两次失败才算回归）。

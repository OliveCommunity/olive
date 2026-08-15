# M13：oakstorage 数据库后端、实时持久化与项目管理器

> 前置：M10（oakstorage 骨架与文件后端）已完成；.ove/otio/fcpxml 后端
> 与全特性序列化器（S1）已落地。本计划把工程存储从"文件 + 手动保存"
> 切换为"数据库 + 实时写穿"，并交付达芬奇式的项目管理器窗口。
>
> 约束（用户已定）：数据库同时支持 PostgreSQL 与 SQLite（sea-orm）；
> **所有改动实时写数据库，无保存按钮**；数据库为默认后端；工程可在
> 数据库与 .ove / .otio / .fcpxml 之间导入导出。

## 0. 形态决策

- **默认后端 = 数据库**。本地默认 SQLite（`oakdb+sqlite:///…/library.db`，
  用户级单一库文件）；`oakdb+pg://…` 连接串接 PostgreSQL（团队/服务器场景）。
  .ove 降级为**导入/导出格式**（交换与备份），不再是日常存储。
- **写穿（write-through），没有保存动作**。每一条 undoable 命令
  redo 成功即触发持久化；undo/redo/jump 同样落库（历史即数据）。
- **单一序列化事实**：落库 payload 与 .ove 同源（oaknode serializer
  全特性 XML，S1 已完成）。数据库不另建节点级关系表——节点级查询
  不是需求，保持 schema 小而稳。
- 接口边界：app/facade 只认识 oakstorage 的会话 API（Rust 直调；
  facade `oakengine_storage_*` 仅当 app 需要 C ABI 时按需新增，只增不改）。

## 1. Schema（PG/SQLite 同构，sea-orm migration 管理）

```
projects(
  id            BIGSERIAL/INTEGER PK,
  uuid          TEXT UNIQUE NOT NULL,      -- 工程 uuid（序列化器同值）
  name          TEXT NOT NULL,
  created_at    TIMESTAMP NOT NULL,
  modified_at   TIMESTAMP NOT NULL,
  schema_ver    INTEGER NOT NULL           -- payload 格式版本（=serializer CURRENT_VERSION）
)
snapshots(
  project_id    BIGINT PK REFERENCES projects(id) ON DELETE CASCADE,
  payload       TEXT NOT NULL,             -- 全特性 XML（gzip 预留：PG BYTEA / SQLite BLOB 备选）
  command_seq   BIGINT NOT NULL,           -- 该快照对应的命令序号
  written_at    TIMESTAMP NOT NULL
)
journal(
  project_id    BIGINT NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
  seq           BIGINT NOT NULL,           -- 单调命令序号（每工程）
  kind          TEXT NOT NULL,             -- 'redo' | 'undo' | 'jump'
  payload       TEXT NOT NULL,             -- 该命令后的全量 XML（见 §2 取舍）
  written_at    TIMESTAMP NOT NULL,
  PRIMARY KEY (project_id, seq)
)
project_meta(                              -- 项目管理器用的派生数据，冗余可重建
  project_id    BIGINT PK REFERENCES projects(id) ON DELETE CASCADE,
  duration_ms   BIGINT, track_count INT, clip_count INT, footage_count INT
)
```

- 全部经 sea-orm `DeriveEntityModel` + migration，SQLite 开 WAL。
- 连接配置走 oakcommon config（`Storage/Backend`、`Storage/SqlitePath`、
  `Storage/PgUrl`），偏好设置面板可改。

## 2. 写穿策略（核心取舍）

候选 A：每条命令写全量 payload——语义最简单、恢复即最新行，
代价是每次编辑 O(工程大小)。候选 B：命令级增量日志——写入最小，
但要求每条 undo 命令可序列化（当前不具备，工作量大且易错）。

**定案：A 的改良型——同步小写 + 异步大写**：

1. redo/undo/jump 成功后，**同步**写 journal 一行（kind + 全量 payload；
   全量保证任意 seq 点可恢复，不依赖命令可序列化）。
2. snapshots 由后台写线程 latest-wins 合并（每 ~2s 或有新写入即落），
   journal 按 N=500 条或快照成功后截断。
3. 打开工程 = snapshots.payload + 其后的 journal 末行（二者同源，
   取 command_seq 较大者）。
4. 崩溃恢复：journal 永远完整 ⇒ 最坏损失 0 条命令（同步写）。
5. 大工程（>10MB payload）同步写仍可能卡 UI——届时把 journal 写入
   也挪到写线程但保留"未落盘即禁止退出"闸（退出前 flush）。v1 先
   全同步测性能，超 50ms 的写记入日志备查。

## 3. 实时性的接入点

不侵入 oaknode/oaktimeline：在 **facade 的 undo 推送路径**挂钩
（`oakengine_undo_push` / `undo_group_end` / `undo` / `redo` / `jump`
成功后）通知当前工程会话 → 写穿。新建/导入工程时建立
(project ↔ storage session) 绑定；关闭工程解绑。app 不再出现
"保存/另存为脏标记"语义——状态栏的修改标记改为"已写入/写入中"。

## 4. 项目管理器窗口（app）

达芬奇式启动窗 + 菜单 文件→项目管理器：

- 列表：name、modified_at、时长/轨道数（project_meta）、双击打开。
- 新建（命名即建库行并打开）、重命名、复制、删除（确认对话框）。
- 导入：.ove / .otio / .fcpxml → 新库行（走既有后端解析后转库）。
- 导出：选中工程 → .ove / .otio / .fcpxml（经 serializer/oakotio）。
- 启动行为：有库则显示管理器；`--project <path|oakdb-uri>` 或双击
  进入主界面。gpui 实现，数据源走 oakstorage 会话 API（RealEngine
  加 project-store 面）。

## 5. 分期与判据

| 期 | 内容 | 完成判据（可命令验证） |
|----|------|------------------------|
| D1 | sea-orm schema + SQLite 后端（load/save/list/delete/duplicate/rename/export），journal+snapshot 读写 | `cargo test -p oakstorage`：SQLite 库 round-trip 全特性字段比对、journal 截断/恢复测试、meta 派生正确 |
| D2 | 写穿挂钩（facade undo 路径）+ 异步写线程 + 退出 flush | 编辑工程→进程直接 kill -9→重开恢复到最后一条命令（集成测试） |
| D3 | PG 后端（同 schema）+ 连接配置 | `OAK_TEST_PG_URL` 存在时 PG 测试全绿；不存在自动 ignore；SQLite 测试不受 PG 影响 |
| D4 | 项目管理器窗口 + 导入导出 + 启动接线 | app 测试：建/删/复制/重命名/导入 .ove/导出 .ove；两张截图（中英）入 docs |
| D5 | .ove 手动保存语义退役（菜单改"导入/导出"，脏标记改写入状态）+ M10/M12 文档更新 | 全量测试绿；docs 更新 |

依赖：D1→D2→D3 顺序；D4 可在 D1 后并行（窗口用 SQLite）；D5 最后。

## 6. 风险与对策

| 风险 | 对策 |
|------|------|
| 全量 payload 同步写大工程慢 | §2.5 的写线程化退路；journal 截断阈值可配 |
| sea-orm async 与同步 facade 的阻抗 | 后端内嵌私有 current_thread runtime（Cargo.toml 已预留 tokio rt） |
| PG 测试依赖外部服务 | env 门控 ignore，CI 先只跑 SQLite |
| 写穿与 undo 语义错位（组命令） | group_end 才算一条 journal；abort 不写 |
| 旧 .ove 用户数据 | 首次启动提示导入；.ove 读写后端永久保留 |
| 多实例同库并发 | v1 单写者假设 + `PRAGMA busy_timeout`/PG 行锁；多写者留待 M14 |

## 7. 不在本期

- 节点级关系表与库内查询、多写者协作、云同步、payload 压缩/差分、
  命令级增量序列化。

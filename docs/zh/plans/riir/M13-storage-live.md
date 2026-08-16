# M13：oakstorage 数据库后端、实时持久化与项目管理器

> 前置：M10（oakstorage 骨架与文件后端）已完成；.ove/otio/fcpxml 后端
> 与全特性序列化器（S1）已落地。本计划把工程存储从"文件 + 手动保存"
> 切换为"数据库 + 实时写穿"，并交付达芬奇式的项目管理器窗口。
>
> 约束（用户已定）：数据库同时支持 PostgreSQL 与 SQLite（sea-orm）；
> **所有改动实时写数据库，无保存按钮**；数据库为默认后端；工程可在
> 数据库与 .ove / .otio / .fcpxml 之间导入导出；**撤销历史持久化**
> （无限撤销）。

## 0. 形态决策（定稿）

- **按聚合粒度数据库化**，不做全关系型。库只管四件事：工程元信息、
  KV 设置、加载加速快照、节点粒度的命令日志。领域语义（时间线结构、
  连接、关键帧、效果链）全部留在节点 XML 里——**工程状态 = 节点图 +
  settings，没有第三种东西**，因此节点粒度是封闭全集。
- **journal 由 diff 产生，不由命令申报**。每条命令 redo 后把工程在
  内存里重新序列化（S1 的 save 按节点吐 XML），与上一版逐节点比对，
  变化/新增/删除的节点各落一行。零命令侵入：现有与未来的命令类型
  （含调整图层）自动覆盖，正确性与序列化器共用同一事实源。
- **journal 同时就是持久化撤销历史**：回放 = 快照 + 按 seq 顺序把
  identity 为 X 的节点整个替换成 new_xml；撤销到任意点 = 逆序回写
  old_xml。old/new_xml 都是整节点 XML，定位靠 node_identity 主键，
  不碰 XML 内部。
- **快照只加速加载**：默认每 600s（`Storage/SnapshotIntervalSec`，
  可配）脏状态下落一份全量 XML；定期清理只留最近 3 份。快照损坏也
  能从空工程 + 全 journal 重建。
- **崩溃恢复**：journal 同步写 ⇒ kill -9 最坏丢 0 条命令。
- 默认后端 = 数据库（本地 SQLite 单库文件；PG 用连接串）。.ove 降级
  为导入/导出格式。

## 1. Schema（SQLite / PG 同构，sea-orm migration 管理）

```sql
projects(
  id          INTEGER PRIMARY KEY,          -- PG: BIGSERIAL
  uuid        TEXT UNIQUE NOT NULL,
  name        TEXT NOT NULL,
  schema_ver  INTEGER NOT NULL,             -- serializer CURRENT_VERSION
  created_at  TIMESTAMP NOT NULL,
  modified_at TIMESTAMP NOT NULL,
  command_seq BIGINT NOT NULL DEFAULT 0     -- 当前头的命令序号
)

settings(
  project_id  INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
  key         TEXT NOT NULL,
  value       TEXT NOT NULL,
  PRIMARY KEY (project_id, key)
)

snapshots(                                  -- 周期全量快照（定期清理）
  project_id  INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
  command_seq BIGINT NOT NULL,
  payload     TEXT NOT NULL,                -- 全特性 XML（与 .ove 同源）
  written_at  TIMESTAMP NOT NULL,
  PRIMARY KEY (project_id, command_seq)
)

journal(                                    -- 命令日志：每命令每受影响节点一行
  project_id  INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,
  seq         BIGINT NOT NULL,
  node_identity BIGINT NOT NULL,            -- 0 = settings 伪节点
  kind        TEXT NOT NULL,                -- 'redo'|'undo'|'jump'|'group'|'import'
  old_xml     TEXT,                         -- 命令前像（undo 回放；新增为 NULL）
  new_xml     TEXT,                         -- 命令后像（redo 回放；删除为 NULL）
  at          TIMESTAMP NOT NULL,
  PRIMARY KEY (project_id, seq, node_identity)
)
```

- settings 变更以 `node_identity = 0` 的伪节点行入 journal，payload 为
  变更键的 KV 片段。
- 项目管理器统计（轨道/片段/素材数）打开工程时从节点图派生，
  不落库。
- journal 保留窗口：`Storage/JournalRetentionDays`（默认 0 = 全保留；
  行均 KB 级，全保留量级可接受）。窗口外撤销历史失效（文档注明）。

## 2. 运转方式

- **写穿**：redo/undo/jump 成功后一个事务：diff 出的受影响节点各行
  + `command_seq + 1` + `modified_at`。组命令在 `group_end` 提交一次；
  `group_abort` 不写。
- **快照**：后台线程，latest-wins，不卡 UI；写后清理旧快照（留 3 份）。
- **加载**：最新快照（无则空工程）+ 其后 journal 按 seq 重放 new_xml
  → 拼装 `<project>` 交给 serializer::load。
- **导入**（.ove/.otio/.fcpxml → 库）：解析进内存工程 → 一个事务写
  `kind='import'` 的全节点 journal 行（seq=1）。**导出**反向走内存
  序列化，不落库。
- **退出**：无保存；退出前等快照线程 flush（latest-wins 队列排空）。

## 3. 写穿接入点

facade 的 undo 推送路径挂钩（`oakengine_undo_push` / `undo_group_end`
/ `undo` / `redo` / `jump` 成功后）→ 当前工程会话执行 §2 写穿。
新建/导入建立 (project ↔ storage session) 绑定；关闭解绑。状态栏
脏标记改为"已写入/写入中"。

> D2 落地记录（2026-08）：`crates/oakengine/src/storage.rs` 实现绑定表
> （project handle ctx → `{db uri, uuid}`）、写穿（每次 undo 路径成功
> 后对全部绑定工程调 `DatabaseBackend::save`，diff 式，未变工程 no-op）、
> 快照线程（`Storage/SnapshotIntervalSec`，默认 600s，latest-wins，
> 退出 `oakengine_storage_flush` 排空）与 `last_error` 降级。
> **配置默认值**：`Storage/Backend` 的默认值 = `"sqlite"`，`Storage/SqlitePath`
> 的默认值 = `<系统数据目录>/library.db`（`FileFunctions::get_configuration_location`
> 的 macOS Application Support / XDG 位置，尊重 `OAK_CONFIG_DIR`）。
> **启用语义**：`Storage/Backend` 显式为 `"sqlite"`/`"database"` 才启用写穿；
> 键缺失 = "无库配置"（工程不绑定、写穿不触发）——这是 §2"无库配置优雅
> 降级"的默认形态，保证 headless 消费者（oak-cli）与测试进程永远不写
> 用户的真实库。app 侧（D4/D5）在启动时显式设置该配置即可启用。

## 4. 项目管理器窗口（app）

达芬奇式启动窗 + 菜单 文件→项目管理器：

- 列表：name、modified_at、时长/轨道/片段/素材统计、双击打开。
- 新建 / 重命名 / 复制 / 删除（确认对话框）。
- 导入 .ove/.otio/.fcpxml 为新库行；选中工程导出为 .ove/.otio/.fcpxml。
- 数据源走 oakstorage 会话 API（Rust 直调；需要 C ABI 时 facade 只增）。

## 5. 分期与判据

| 期 | 内容 | 完成判据 |
|----|------|----------|
| D1 | sea-orm schema + SQLite 后端（save/load/快照/重放/截断/list/delete/duplicate/rename/export） | `cargo test -p oakstorage`：全特性 round-trip 字段比对、快照+journal 重放、撤销到任意点、截断、管理 API、导入导出 |
| D2 | diff 写穿挂钩（facade undo 路径）+ 快照线程 + 退出 flush | 编辑工程 → kill -9 → 重开恢复到最后一条命令；撤销历史跨会话可用（集成测试） |
| D3 | PG 后端（同 schema）+ 连接配置 | `OAK_TEST_PG_URL` 存在时 PG 测试全绿，否则自动 ignore |
| D4 | 项目管理器窗口 + 导入导出 + 启动接线 | app 测试：建/删/复制/重命名/导入/导出；中英截图入 docs |
| D5 | .ove 手动保存语义退役（菜单改导入/导出，脏标记改写入状态）+ 文档更新 | 全量测试绿 |

依赖：D1→D2→D3；D4 可在 D1 后并行；D5 最后。

## 6. 风险与对策

| 风险 | 对策 |
|------|------|
| 大工程每命令一次内存序列化 | 微秒-低毫秒级；超阈值记日志，必要时增量序列化优化 |
| sea-orm async ↔ 同步 facade | 后端内嵌私有 current_thread runtime |
| PG 测试依赖外部服务 | env 门控 ignore，CI 先只跑 SQLite |
| 多实例同库并发 | v1 单写者（busy_timeout/PG 行锁）；多写者 M14 |
| 快照线程与退出竞态 | 退出前 flush 闸门 |

## 7. 不在本期

- 全关系型节点表、命令级语义序列化、多写者协作、云同步、payload
  压缩、库内节点级查询。

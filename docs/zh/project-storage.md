# 工程存储架构

[English](../project-storage.md)

Oak 的工程持久化在**数据库**（默认 SQLite，支持 PostgreSQL），采用
**写穿持久化**：每一次编辑都即时落库——没有保存按钮，也没有可丢失
的数据。.ove XML 文件、OTIO、FCPXML 是导入/导出格式，不是日常存储。

## 设计原则

- **按聚合粒度持久化，不做全关系型映射。** 数据库只存四样东西：
  工程元信息、KV 设置、周期全量快照、节点粒度的命令日志。领域语义
  （时间线结构、节点连接、关键帧、效果链）全部留在每个节点的 XML
  payload 里。工程 = 节点图 + settings，没有第三种东西——节点粒度
  因此是封闭全集。
- **单一序列化事实。** 库里的节点 XML 与 .ove 序列化器
  （`oak-node::serializer`）产出的是同一份文档。新功能（比如调整图层）
  只需要扩展 XML schema，数据库 schema 永远不变。
- **journal 由 diff 产生，不靠命令申报。** 每条 undoable 命令成功后，
  在内存里重新序列化工程并与上一状态逐节点比对；变化/新增/删除的
  节点各落一行。现有和未来的命令类型自动覆盖。
- **journal 就是持久化撤销历史。** 回放按命令序应用各节点最新像；
  回退按逆序应用旧像。撤销跨会话存活。

## Schema（SQLite / PostgreSQL，sea-orm）

```sql
projects(id PK, uuid UNIQUE, name, schema_ver, created_at, modified_at, command_seq)
settings(project_id FK, key, value, PK(project_id, key))
snapshots(project_id FK, command_seq, payload, written_at, PK(project_id, command_seq))
journal(project_id FK, seq, node_identity, kind, old_xml, new_xml, at,
        PK(project_id, seq, node_identity))
```

- `snapshots.payload` 是全工程 XML，脏状态下每
  `Storage/SnapshotIntervalSec`（默认 600 秒）写一份，只留最近 3 份。
  它只是加载加速器——journal 自己就能从零重建工程。
- `journal` 行是整节点前后像：新增节点 `old_xml` 为 NULL，删除节点
  `new_xml` 为 NULL。加载 = 取最新快照，其后按 `seq` 顺序把每个节点
  替换为最新像；回退到命令 N = 逆序回写 `old_xml`。
  `node_identity = 0` 是 settings 伪节点。
- 每条命令一个同步事务（journal 行 + `command_seq + 1`），
  `kill -9` 丢 0 条命令。
- `Storage/JournalRetentionDays`（默认 0 = 全保留）限定撤销窗口；
  每行仅 KB 级。

## 时间线的表示

时间线是节点 XML 内部的引用链：

```
sequence ──<tracklists>──▶ tracklist ──<tracks>──▶ track ──<blocks>──▶ clip ──<footage>──▶ footage
```

clip 行自带时间线区间（`<range in out/>`）、媒体偏移（`<media_in>`）
和素材引用；效果链是效果节点 XML 里的连接记录。加载时两阶段重连
这些 identity（见 `oak-node::serializer`），所以不需要任何连接表。
时间线编辑映射为少数节点行：移动 clip 触及它的 track 和 clip 本身；
分割新增一个节点、更新两个；ripple 编辑触及受影响的 track 并删除
被移除的 clip。

## 写穿流程

1. facade 的 undo 推送（`oakengine_undo_push`、group_end、
   undo/redo/jump）成功；
2. 工程在内存里重新序列化（微秒到低毫秒级）并与上一状态 diff；
3. 一个事务写入变化节点行、`command_seq + 1`、`modified_at`；
4. 后台线程 latest-wins 写快照；退出前排空队列。

## 导入 / 导出

- 导入：.ove / .otio / .fcpxml 由既有 oak-storage 后端解析后，以
  `kind = 'import'` 的 journal 行写为新工程行。
- 导出：内存序列化经 ove-xml 或 otio 后端写出；除当前状态外不读写
  数据库。

## 配置

库的选择与行为全部由 `Storage` 配置组驱动（写穿由
`crates/oakengine/src/storage.rs` 读取）：

- `Storage/Backend` — `"sqlite"`（默认值）、`"database"` 或 `"pg"`
  启用写穿；其它值（如 `"off"`）禁用。键缺失 = 无库配置，工程不
  绑定库（headless 消费者与测试进程永不写用户真实库）。
- `Storage/SqlitePath` — SQLite 库文件；默认
  `<系统数据目录>/library.db`（尊重 `OAK_CONFIG_DIR`）。
- `Storage/PgUrl` — PostgreSQL 连接串，`Backend = "pg"` 时使用：
  `user:pass@host:5432/dbname`（libpq URL 形式，带
  `postgres://`/`postgresql://` 前缀也会被剥掉）。解析出的库 URI 为
  `oakdb+pg://<PgUrl>`。
- `Storage/SnapshotIntervalSec`（默认 600）、`Storage/JournalRetentionDays`
  （默认 0 = 全保留）见上。

## 多写者与平台

v1 假设单写者（SQLite `busy_timeout`，PG 行锁）；多写者协作是后续
工作（M14）。默认数据库是用户级单一 SQLite 文件；PostgreSQL 通过
`Storage/Backend = "pg"` + `Storage/PgUrl` 选择，或直接用
`oakdb+pg://` 连接串 URI。

数据库测试：`cargo test -p oak-storage` 全绿无需 PostgreSQL——SQLite
套件常驻运行；PG 套件（`tests/database_pg_test.rs`）在设置了
`OAK_TEST_PG_URL`（如 `postgres://user:pass@host:5432/db`）时连接真实
PG 全量运行，未设置则跳过并打印说明。该 URL 应指向专用测试库：每个
测试会重置四张表。

另见：[M10 oak-storage 手册](plans/completed/riir/M10-oak-storage.md)、
[M13 写穿计划](plans/completed/riir/M13-storage-live.md)、
[工程文件格式参考](project-file-reference.md)。

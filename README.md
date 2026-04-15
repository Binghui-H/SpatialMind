# SpatialMind

**基于 Qt5 + PostGIS + LLM 的 AI 空间分析助手**

用自然语言提问，自动生成 PostGIS 空间查询并在地图上高亮显示结果。支持多轮对话、空间上下文记忆、RAG 少样本增强和结果二次解读。

![Qt](https://img.shields.io/badge/Qt-6.x-green)
![PostGIS](https://img.shields.io/badge/PostGIS-3.x-blue)
![License](https://img.shields.io/badge/license-MIT-orange)

---

## 功能特性

- **自然语言 → GIS 查询**：输入"找距地铁 5 号线最近的 10 所学校"，自动生成并执行 PostGIS SQL
- **地图可视化**：查询结果实时渲染在 MapLibre GL 矢量地图上（天地图底图）
- **多轮对话**：记住上下文，支持"这附近"、"最近的"等指代性表达
- **空间上下文**：自动计算上次结果的地理中心，注入 LLM 的空间焦点
- **RAG 少样本增强**：历史成功 SQL 自动缓存，相似查询时注入 prompt 提升准确率
- **结果二次解读**：LLM 对查询结果生成自然语言分析与建议
- **SQL 自动修正**：执行失败时自动将错误反馈给 LLM 并重试
- **历史持久化**：查询历史和 SQL 缓存写入本地，重启后可恢复

---

## 效果示意

```
你：  找距离地铁5号线最近的10所学校
AI：  正在查询：距离5号线500米内的学校……
      共找到10个结果，包括：和平区第二幼儿园、天津二中…
      （地图高亮）
      在地铁5号线沿线共找到10所学校，其中5所为中小学，
      建议优先考虑和平区第二幼儿园，位置最为便利。

你：  这附近还有医院吗？
AI：  正在查询：以上次结果中心为基准的周边医院……
```

---

## 技术架构

```
用户输入 (QLineEdit)
    ↓
MainWindow（纯 UI 层）
    ↓ submitUserInput()
ConversationManager（编排核心）
    ├─ RAG 注入    ← SQLCache（2-gram 相似度检索）
    ├─ 空间上下文  ← 上次结果 GeoJSON 中心坐标
    ↓ chat(messages, tools)
LLMClient ──────────────────→ LLM API（OpenAI 兼容）
                                  ↓ tool_call: spatial_query
GISEngine ──────────────────→ PostgreSQL + PostGIS
    ↓ GeoJSON
MapBridge ──────────────────→ MapLibre GL (QWebEngineView)
    ↓ 渲染完成
SQLCache（写入成功 SQL）
QueryHistory（写入查询历史）
LLMClient.interpretAsync() → 结果二次解读
```

---

## 环境要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| Qt | 5.x | 需含 WebEngine、Concurrent、Sql、WebChannel |
| PostgreSQL | 13+ | 推荐 Docker 部署 |
| PostGIS | 3.x | 天津 OSM 数据（osm2pgsql 导入） |
| osm2pgsql | 任意 | 用于导入 OpenStreetMap 数据 |
| LLM API | — | OpenAI 兼容接口（DeepSeek / GLM / GPT 等） |

---

## 快速开始

### 1. 克隆项目

```bash
git clone https://github.com/你的用户名/SpatialMind.git
cd SpatialMind
```

### 2. 准备数据库

```bash
# 用 Docker 启动 PostgreSQL + PostGIS
docker run -d \
  --name postgis \
  -e POSTGRES_PASSWORD=yourpassword \
  -p 5434:5432 \
  postgis/postgis:15-3.4

# 导入天津 OSM 数据（先从 geofabrik.de 下载 .osm.pbf）
osm2pgsql -d tianjin -H localhost -P 5434 -U postgres \
  --slim -G --hstore tianjin-latest.osm.pbf
```

### 3. 配置 LLM API Key

在系统环境变量（或 Qt Creator 运行配置）中设置：

```
DS_API_KEY=你的API密钥
```

> Qt Creator 设置路径：Projects → Run → Environment → 添加变量

### 4. 配置 prompt.json

复制 `prompt.example.json` 为 `prompt.json`（已加入 `.gitignore`，不会上传）：

```json
{
  "version":     "1.1",
  "base_url":    "https://api.deepseek.com",
  "model":       "deepseek-chat",
  "api_key_env": "DS_API_KEY",
  "system":      "你是 SpatialMind，一个天津 GIS 空间分析助手..."
}
```

支持切换到任意 OpenAI 兼容接口：修改 `base_url` 和 `model` 即可，无需改代码。

### 5. 构建运行

在 Qt Creator 中打开 `SpatialMind.pro`，选择 Qt 5 Kit，点击运行即可。

---

## 项目结构

```
SpatialMind/
├── main.cpp
├── MainWindow.{h,cpp}          # 纯 UI 层
├── ConversationManager.{h,cpp} # 业务编排核心
├── LLMClient.{h,cpp}           # LLM 网络层
├── GISEngine.{h,cpp}           # PostGIS 数据层
├── MapBridge.{h,cpp}           # C++ ↔ JS 桥接
├── QueryHistory.{h,cpp}        # 查询历史持久化
├── SQLCache.{h,cpp}            # RAG SQL 缓存
├── SettingsDialog.{h,cpp}      # 数据库配置对话框
├── web/
│   └── map.html                # MapLibre GL 地图页
├── promptProject/
│   ├── prompt.example.json     # prompt 配置模板（上传）
│   └── prompt.json             # 实际配置（gitignore，不上传）
├── SpatialMind.pro
└── README.md
```

---

## 核心实现说明

### Tool Calling 格式

遵循 OpenAI Function Calling 规范，关键约束：

- `assistant` 的 `tool_calls` 消息必须原样存入历史，才能通过 DeepSeek 校验
- `tool` 消息的 `tool_call_id` 必须与 `assistant` 返回的完全一致
- 调用工具时 `content` 字段必须为 `null`
- GLM 需强制设置 `stream: false`

### RAG 相似度算法

```cpp
// 分词：中文单字 + 英文单词
QSet<QString> tokenize(const QString &text);

// 相似度 = 两个查询 tokenize 集合的交集大小
int similarity(const QString &a, const QString &b);

// 检索最相似的 top-3 历史 SQL 注入 prompt
QList<SQLCacheEntry> findSimilar(const QString &query, int topN);
```

### PostGIS 关键写法

```sql
-- 坐标系：存储 EPSG:3857，输出必须转 4326
ST_AsGeoJSON(ST_Transform(way, 4326))::json

-- 距离过滤（球面距离，单位米）
ST_DWithin(
  ST_Transform(way, 4326)::geography,
  ST_SetSRID(ST_MakePoint(经度, 纬度), 4326)::geography,
  500
)

-- 最近 N 个（不要用 DWithin，用 ORDER BY + LIMIT）
ORDER BY ST_Distance(
  ST_Transform(way, 4326)::geography,
  ST_SetSRID(ST_MakePoint(经度, 纬度), 4326)::geography
) LIMIT 10
```

---

## 待完善

- [ ] 地图标记点击弹窗（Feature 详情）
- [ ] 左侧历史面板样式优化
- [ ] RAG 相似度算法升级（BM25 / 向量嵌入）
- [ ] 多次 SQL 自动修正上限配置
- [ ] 支持更多查询类型（路径规划、面积统计等）
- [ ] 名称搜索命中率优化

---

## 许可证

MIT License

---

## 致谢

- [MapLibre GL JS](https://maplibre.org/) — 开源矢量地图渲染
- [PostGIS](https://postgis.net/) — 空间数据库扩展
- [OpenStreetMap](https://www.openstreetmap.org/) — 开放地理数据
- [天地图](https://www.tianditu.gov.cn/) — 中文底图服务

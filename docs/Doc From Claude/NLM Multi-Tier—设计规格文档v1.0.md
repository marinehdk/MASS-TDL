# NLM Multi-Tier Knowledge System — 设计规格文档 v1.0

**置信度：🟢 High**（基于 OG-RAG arxiv:2412.15235、ACL 2025 Hierarchical RAG、Dynamic Taxonomy Construction OpenReview、Graph RAG in the Wild 四个独立来源交叉验证）

------

## 1. 设计目标与约束

| 约束             | 值                                                           |
| ---------------- | ------------------------------------------------------------ |
| 每笔记本来源上限 | 300（Pro）                                                   |
| 笔记本总数上限   | 500（Pro，实测）                                             |
| 域数量目标       | **5–15 个**（Graph RAG in the Wild 生产实测）                |
| 触发方式         | 主会话 Claude 和后台 Agent **完全一致**（均通过 bash invoke） |
| 新域创建门槛     | 来源积压 ≥ 20 条（OG-RAG 粒度原则）                          |

------

## 2. 系统架构（三层）

```
┌──────────────────────────────────────────────────────────────┐
│  L3  GLOBAL · {Domain} · Reference                           │
│      跨项目通用知识，手动管理，长期稳定                          │
└────────────────────────────┬─────────────────────────────────┘
                             │ 蒸馏输入
┌────────────────────────────▼─────────────────────────────────┐
│  L2  META · {Project} · Synthesis                            │
│      跨域综合，来源=各子本 Briefing Doc，跨域查询在此            │
└──────┬──────────────┬──────────────┬────────────────┬────────┘
       │蒸馏输入       │蒸馏输入       │蒸馏输入         │蒸馏输入
┌──────▼──────┐ ┌──────▼──────┐ ┌────▼──────┐  ┌──────▼──────┐
│L1 DOMAIN·   │ │L1 DOMAIN·   │ │L1 PROJ·   │  │L1 DOMAIN·   │
│Navigation·  │ │Regulations· │ │MASS-L3·   │  │...          │
│Research     │ │Research     │ │Local      │  │             │
│(专域深挖)    │ │(专域深挖)    │ │(项目特定)  │  │             │
└─────────────┘ └─────────────┘ └───────────┘  └─────────────┘
```

------

## 3. 笔记本命名规范

**格式：`{SCOPE} · {Name} · {Type}`**

| SCOPE    | 用途                           | Type 值     |
| -------- | ------------------------------ | ----------- |
| `PROJ`   | 当前项目特有知识，不跨项目共享 | `Local`     |
| `DOMAIN` | 单一技术领域深挖，可跨项目共享 | `Research`  |
| `META`   | 跨域综合，来源为蒸馏文档       | `Synthesis` |
| `GLOBAL` | 全局通用参考，长期维护         | `Reference` |

**命名示例（MASS-L3 项目）：**

```
PROJ · MASS-L3 · Local
DOMAIN · Navigation Algorithms · Research
DOMAIN · Maritime Regulations · Research  
DOMAIN · Propulsion Systems · Research
META · ASV Research · Synthesis
GLOBAL · Maritime Engineering · Reference
```

**规则：**

- Name 段 ≤ 25 字符，英文 TitleCase 或中文简短短语
- 禁止日期后缀（用 `last_distilled` 字段追踪）
- 禁止版本号后缀（笔记本是活文档）

------

## 4. 完整 Config Schema

```json
{
  "version": 2,
  "project_name": "MASS-L3",
  "notebooks": {
    "local": {
      "id": "<notebook-id>",
      "name": "PROJ · MASS-L3 · Local",
      "source_count": 0
    },
    "synthesis": {
      "id": "<notebook-id>",
      "name": "META · ASV Research · Synthesis",
      "source_count": 0,
      "last_distilled": null
    },
    "global": {
      "id": "<notebook-id>",
      "name": "GLOBAL · Maritime Engineering · Reference"
    },
    "domains": {
      "navigation_algorithms": {
        "id": "<notebook-id>",
        "name": "DOMAIN · Navigation Algorithms · Research",
        "description": "路径规划、避碰、COLREGS、感知融合、控制律",
        "keywords": ["path planning", "collision avoidance", "COLREGS", "LiDAR", "SLAM", "control"],
        "source_count": 0,
        "last_distilled": null
      }
    }
  },
  "routing": {
    "domain_match_threshold": 0.25,
    "domain_merge_overlap": 0.40,
    "new_domain_min_sources": 20,
    "max_domains": 15,
    "distill_trigger_count": 270
  }
}
```

------

## 5. 域粒度控制策略

**来源：OG-RAG + Dynamic Taxonomy Construction（OpenReview）**

粒度原则：**一个域 = 一组可被同一批来源回答的问题类型**，而非"话题类别"。

### 5.1 新域创建三重门闸

```
请求创建新域时，依序检查：

Gate 1 — 积压量检验（source_queue < 20）
  → 不足 20 条来源 → 路由到 local notebook，暂缓建域
  → 通过 → 进入 Gate 2

Gate 2 — 关键词重叠检验（overlap ≥ 40% 与任一现有域）
  → 重叠过高 → 路由到最近域 + 建议更新该域 keywords
  → 通过 → 进入 Gate 3

Gate 3 — 总域数上限（total_domains ≥ 15）
  → 超限 → 路由到 synthesis notebook + 标记 ⚠ 请求人工review
  → 通过 → 创建新域
```

### 5.2 域合并触发条件

每次 `nlm-research` 执行后自动检查：

```
若存在两个域 A 和 B 满足：
  keyword_overlap(A, B) > 40%
  AND combined_source_count(A, B) < 200

→ 输出 merge_suggestion：
  "建议将 {B} 合并入 {A}，执行：nlm-setup --merge-domain B --into A"
```

### 5.3 域拆分触发条件

```
若域 A 满足：
  source_count > 200
  AND 近 10 次 ask 中 >60% 查询只命中其中一个子关键词群

→ 输出 split_suggestion：
  "建议将 {A} 拆分，执行：nlm-setup --split-domain A"
```

------

## 6. `/nlm-ask` 规格

**触发**：主会话 Claude 或后台 Agent，均通过 `bash $INVOKE ask`

```
nlm-ask --query "<text>" [--scope auto|all|local|synthesis|domain:<key>|global]
```

### 查询路由（`--scope auto`，默认）

```
1. classify_domain(query) → domain_key | null
2. 若 domain_key 存在且 source_count > 0：
     并行查询 [domain notebook, local notebook]
     domain 答案主导技术细节
     local 答案补充项目上下文
3. 若 domain_key 不存在或 source_count == 0：
     查询 [synthesis notebook, local notebook]
     + 输出提示："该域无沉淀来源，建议执行 /nlm-research"
4. 跨域综合类问题（classify 返回 null）：
     查询 [synthesis notebook, global notebook]
```

**输出字段：**

```json
{
  "answer": "...",
  "answered_by": ["DOMAIN · Navigation Algorithms · Research", "PROJ · MASS-L3 · Local"],
  "confidence": "high|medium|low",
  "suggest_research": false
}
```

**设计约束：**

- **绝不写入任何笔记本**
- 无来源时明确返回 `suggest_research: true`，不捏造答案

------

## 7. `/nlm-research` 规格

**触发**：主会话 Claude 或后台 Agent，均通过 `bash $INVOKE research`

```
nlm-research --topic "<text>" --depth fast|deep
             [--target auto|local|synthesis|domain:<key>]
             [--max-import N] [--min-relevance 0.1]
```

### 来源路由（`--target auto`，默认）

```
1. classify_domain(topic) → domain_key | NEW:<suggested> | null
2. domain_key 存在 → 路由到该域笔记本，导入来源
3. NEW:<suggested> → 执行三重门闸检验：
     通过全部 → 暂停，请求用户确认创建
                 确认后：nlm-setup --create-domain "<suggested>"
                 写入 config，继续导入
     任意失败 → 路由到 local 或 synthesis（见门闸逻辑）
4. null（跨域综合）→ 路由到 synthesis notebook
```

### 知识沉淀触发

```
导入完成后检查目标笔记本：
  source_count > distill_trigger_count(270)？
  YES → 输出 distillation_required: true
        提示："请在 NotebookLM 中为 {notebook_name} 生成 Briefing Doc，
               下载后执行 nlm-add --target synthesis <file_path>"
```

**输出字段：**

```json
{
  "report": "...",
  "target_notebook": "DOMAIN · Navigation Algorithms · Research",
  "sources_imported": 8,
  "notebook_source_count": 53,
  "distillation_required": false,
  "domain_suggestion": null,
  "merge_suggestion": null,
  "split_suggestion": null
}
```

------

## 8. `classify_domain.sh` 规格

**无外部 LLM 调用，纯本地关键词匹配，适合后台 Agent 静默执行。**

```bash
输入: topic_text, config_path
输出: domain_key | "NEW:<inferred_name>" | "null"

算法：
  1. 提取 topic_text 中的名词短语（简单分词，停用词过滤）
  2. 对每个 domain_notebook：
       score = |topic_keywords ∩ domain.keywords| / |topic_keywords|
  3. max_score > 0.25 → 返回对应 domain_key
  4. 0.1 < max_score ≤ 0.25 → 返回 "local"（低置信，入项目本）
  5. max_score ≤ 0.1 → 推断新域名，返回 "NEW:<inferred_name>"
     推断规则：取 topic_text 前 2-3 个高频技术名词组合
```

------

## 9. `nlm-setup` 新子命令规格

```bash
# 创建域笔记本
nlm-setup --create-domain "Navigation Algorithms" \
          --keywords "path planning,collision avoidance,COLREGS"
# → 创建 NotebookLM 笔记本，名称：DOMAIN · Navigation Algorithms · Research
# → 写入 config.notebooks.domains.navigation_algorithms

# 创建综合笔记本
nlm-setup --create-synthesis "ASV Research"
# → 创建笔记本，名称：META · ASV Research · Synthesis

# 合并域
nlm-setup --merge-domain regulations_imo --into maritime_regulations
# → 将来源迁移，删除旧笔记本，更新 config

# 拆分域（交互式，需人工介入）
nlm-setup --split-domain navigation_algorithms
# → 引导用户命名两个子域，重新分配来源
```

------



Target Tracker：动态注意力机制判断威胁 
COLREGs Engine：多船避碰，避免频繁避碰，提升效率
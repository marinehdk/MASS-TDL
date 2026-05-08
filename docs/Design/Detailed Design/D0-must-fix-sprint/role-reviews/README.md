# D0 Role-Hat Review Sessions

D0 sprint 关闭后遗留 4 个独立 role-hat 评审。每个文件是一个**完整的新会话提示词**，直接粘贴到新 Claude 对话即可开始。

## 执行顺序

```
CCS-hat ──────────────────────────────────────────────→ ✅ 独立
M7-hat  ──────────────────────────────────────────────→ ✅ 独立

法务-hat ──────────────────────────────────────────────→ ✅
                                                           │
                                                           ↓
                                                       M4-hat（需要法务-hat 结论作为输入）
```

- **CCS-hat** 和 **M7-hat** 互相独立，可以同时开两个新对话并行评审
- **法务-hat** 必须先完成，M4-hat 才能做最终方案选定
- 建议顺序：先开 CCS + M7 + 法务三个会话，法务结论出来后再开 M4

## 文件列表

| 文件 | 角色 | 核心问题 | 前置依赖 | 影响 deadline |
|---|---|---|---|---|
| [01-legal-hat-rfc009.md](01-legal-hat-rfc009.md) | 法务-hat | MOOS-IvP GPL v3 商业闭源合规 | 无 | 5/13（D2.1 起点） |
| [02-ccs-hat-memo-d45.md](02-ccs-hat-memo-d45.md) | CCS-hat | RUN-001 数据替代 + D4.5 降级合规 | 无 | 5/13 HAZID kickoff |
| [03-m7-hat-effort-split.md](03-m7-hat-effort-split.md) | M7-hat | 9pw M7 排期可行性 | 无 | 7/20（D3.7 起点） |
| [04-m4-hat-rfc009-decision.md](04-m4-hat-rfc009-decision.md) | M4-hat | IvP 实现路径最终选定 | 法务-hat 01 完成 | 6/16（D2.1 起点） |

## Sign-off 归档

各 hat 完成后，将 sign-off 段内容粘贴回对应文件的 sign-off 节：

| Sign-off 目标文件 | Sign-off 节位置 |
|---|---|
| `docs/Design/Cross-Team Alignment/RFC-009-IvP-Implementation-Path.md` | `## 法务-hat Sign-off` + `## M4-hat Sign-off` |
| `docs/Design/HAZID/RUN-001-fcb-data-substitute-memo.md` | `## §6 CCS-Hat Sign-off` |
| `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md` | D4.5 修订声明 sign-off（搜索 `D4.5 修订声明`） |
| `docs/Design/Detailed Design/M7-Safety-Supervisor/02-effort-split-v2.1.md` | `## §6 M7-hat Sign-off` |

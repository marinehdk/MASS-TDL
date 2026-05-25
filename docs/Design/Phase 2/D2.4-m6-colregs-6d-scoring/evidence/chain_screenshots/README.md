# chain_screenshots/ — D2.4 colregs_chain[5] 可视化截图

## 用途

此目录存放 DEMO-2 P0 强制证据：M6 `colregs_chain[5]` 在 SIL 前端（Foxglove + `ColregsRationaleTree` 组件）的 Playwright 截图。

截图用于证明以下 DoD gate 通过：
- `colregs_chain[5]` 在 C++ `ColregsReasonerNode` 中被正确序列化并发布
- 前端 SAT-2 面板能正确渲染 5 层推理链（layer=1 ODD → layer=2 规则集 → layer=3 会遇分类 → layer=4 责任分配 → layer=5 时机）
- `chain` 为空时（无活跃目标）前端渲染"无会遇"状态而不崩溃

## 存放内容

- **至少 10 张截图**，覆盖 Rule 13/14/15/16/17 各 ≥1 场景
- 命名格式：`chain_{scenario_id}_{layer_label}.png`
  - 示例：`chain_imazu-01-ho_all5layers.png`
  - 示例：`chain_ou-crossing-give-way-01_layer3.png`
- 每张截图须包含完整的 `ColregsRationaleTree` 展开视图（5 层全展开），可接受浏览器整屏截图

## 当前状态

⏳ **待填入** — 等待 T10 任务（Playwright batch 截图脚本）执行后填充。

T10 任务涉及文件：`tools/sil/batch_runner_d24.py`（含 Playwright 调用逻辑），由 D2.4 plan T10 定义。

DEMO-2 强制截图需在 2026-07-31 前 commit 到此目录。

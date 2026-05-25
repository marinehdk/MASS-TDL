# coverage_heatmap_d2.4.html — 生成说明（占位）

## 产物描述

`coverage_heatmap_d2.4.html`：D2.4 50 场景评分覆盖率热图，1100-cell 交叉矩阵（11 Rule × 6 维度 × 场景来源），基于 `batch_scoring_summary.arrow` 生成。

## 生成方法

```bash
# 1. 确认 Arrow 文件存在
ls evidence/batch_scoring_summary.arrow

# 2. 运行分析脚本（D2.4 plan T11 产物）
python3 tools/sil/generate_coverage_heatmap.py \
    --arrow evidence/batch_scoring_summary.arrow \
    --out   evidence/coverage_heatmap_d2.4.html

# 脚本依赖：pyarrow, plotly (pip install pyarrow plotly)
```

脚本产出：
- x 轴：6D 维度（`safe_passage`, `rule_compliance`, `action_timing`, `action_magnitude`, `collision_avoidance`, `situation_awareness`）
- y 轴：11 Rule × 场景来源（imazu22 / ou_mode_d2.4 / ais_derived / synthetic_d2.4）
- 颜色：单元格颜色 = 该组合场景的平均 `quality_score`；白色 = 未覆盖

## 当前状态

⏳ **待生成** — `batch_scoring_summary.arrow` 已存在，等待场景分析脚本（`tools/sil/generate_coverage_heatmap.py`）实现并运行。

目标：DEMO-2（2026-07-31）前生成并 commit 此 HTML。

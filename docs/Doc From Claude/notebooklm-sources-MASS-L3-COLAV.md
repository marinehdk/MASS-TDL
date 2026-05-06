# NotebookLM 来源清单 · MASS-L3-COLAV设计
> 来源：《战术决策层避碰架构调研备忘 v1.1》（2026-04-27）  
> 目标笔记本：https://notebooklm.google.com/notebook/8d64174a-ad62-4e75-b2f8-6336f1c01805  
> 总计：**48 个来源**（NotebookLM 限额 50 个，剩余 2 个槽位）

---

## 操作方法

1. 打开笔记本 → 点击左侧「**+**」或「Add source」
2. 选择「**Website**」
3. 逐条粘贴下方 URL → 点击 Insert
4. PDF 链接同样选「Website」，NotebookLM 会自动识别

---

## A · 工业参考实现（14 个）

### A1 · Kongsberg（4 个）
```
https://www.gebco.net/sites/default/files/documents/lee_04_2_2017.pdf
https://www.kongsberg.com/newsroom/stories/2017/9/autonomous-obstacle-avoidance/
https://www.kongsberg.com/maritime/news-and-events/news-archive/2015/new-research-project-to-investigate-sensor-fusion-and-collision-avoidance-for/
https://www.kongsberg.com/maritime/news-and-events/news-archive/2016/automated-ships-ltd-and-kongsberg-to-build-first-unmanned-and-fully-autonomous/
```

### A2 · Wärtsilä（5 个）
```
https://www.wartsila.com/voyage?id=492
https://www.wartsila.com/voyage/integrated-vessel-control-systems/navigation/smartquay
https://www.wartsila.com/media/news/13-01-2021-ship-control-redefined-wartsila-smartmove-suite-sets-sail-with-the-american-steamship-company-3253273
https://www.wartsila.com/insights/article/the-evolving-intelligence-behind-smart-navigation
https://www.wartsila.com/insights/article/simulating-the-future-of-maritime
```

### A3 · Avikus（3 个）
```
https://www.koreaherald.com/article/10711851
https://avikus.ai/hubfs/45548415/HiNAS_Brochure_0823.pdf?hsLang=en
https://avikus.ai/en-us/press/hd-hyundais-avikus-supplies-autonomous-navigation-solutions-to-a-large-fleet-of-30-vessels
```

### A4 · Sea Machines（4 个）
```
https://sea-machines.com/products/sm300-autonomous-command-control/
https://sea-machines.com/sm300-ng/
https://sea-machines.com/sea-machines-sm300-system-onboard-foss-tug-earns-abs-approval/
https://www.workboat.com/bluewater/sea-machines-completes-world-s-first-1-000-nautical-mile-autonomous-voyage
```
> ⚠️ 备注：海军 ALPV 页面可能需要另行查找（`workboat.com` sea-machines ALPV）

---

## B · 学术文献（11 个）

> **说明**：DOI 链接会跳转到期刊页面，若有订阅可获全文；无订阅则仅索引摘要，仍有参考价值。

```
https://doi.org/10.3389/frobt.2020.00011
```
> Eriksen et al. (2020) · Hybrid Collision Avoidance · **本调研核心学术基线**

```
https://doi.org/10.3389/frobt.2021.739013
```
> Burmeister & Constapel (2021) · Autonomous Collision Avoidance Survey

```
https://doi.org/10.1016/j.ssci.2019.09.018
```
> Huang et al. (2020) · Ship collision avoidance methods · Safety Science

```
https://doi.org/10.1002/rob.20370
```
> Benjamin et al. (2010) · Nested autonomy / MOOS-IvP · Backseat Driver 奠基

```
https://doi.org/10.1109/LRA.2018.2801881
```
> Chiang & Tapia (2018) · COLREG-RRT

```
https://www.sciencedirect.com/science/article/pii/S0029801824005948
```
> Veitch et al. (2024) · ROC 接管时间窗人因研究（20s/60s）· **TBD-06 关闭主证据**

```
https://www.mdpi.com/2077-1312/13/8/1570
```
> MDPI JMSE 13(8):1570 (2025) · 自主避碰性能测试综述

```
https://www.mdpi.com/2077-1312/13/7/1246
```
> MDPI JMSE 13(7):1246 (2025) · MPC + COLREGs 16 类场景

```
https://www.cambridge.org/core/journals/journal-of-navigation/article/abs/path-planning-and-collision-avoidance-technologies-for-maritime-autonomous-surface-ships-a-review-of-colregs-compliance-algorithmic-trends-and-the-navigationgpt-framework/964649D8A39D86D58004B14CAD8A3C39
```
> Cambridge JoN (2026) · MASS 路径规划与避碰综述

```
https://www.sciencedirect.com/science/article/abs/pii/S0029801825016725
```
> He et al. (2025) · COLREGs MPC 受限水域实船验证

```
https://www.nature.com/articles/s41598-025-93635-9
```
> Reza et al. (2025) · RL + NMPC + 数字孪生（TBD-07 关闭）

---

## C · 水动力建模文献（3 个）

```
https://doi.org/10.1007/s00773-014-0293-y
```
> Yasukawa & Yoshimura (2015) · MMG 标准方法 · **FCB MPC 预测模型基础**

```
https://www.sciencedirect.com/science/article/abs/pii/S0029801822027032
```
> Xue et al. (2022) · GP Data-Driven MPC

```
https://kth.diva-portal.org/smash/get/diva2:1299209/FULLTEXT01.pdf
```
> KTH (2019) · Robust MPC for Marine Vessels · PDF 全文

---

## D · CCS / 船级社规范（10 个）

```
https://www.ccs.org.cn/ccswz/specialDetail?id=202503120375512073
```
> CCS《智能船舶规范》(2025) 2025-04-01 生效公告

```
https://m.thepaper.cn/newsDetail_forward_30640809
```
> 《智能船舶规范》(2025) 修订要点解读（含 N/Nx/R1/R2/A1/A2/A3 + III 类 CBS）

```
https://www.xindemarinenews.com/topic/zhinenhangyun/2020/0917/23622.html
```
> 信德海事网 · CCS R1/R2/A1/A2/A3 定义

```
http://wap.eworldship.com/index.php/eworldship/news/article?id=163502
```
> 国际船舶网 · CCS 智能船舶规范全面解读（含配套指南清单）

```
http://www.cansi.org.cn/cms/document/8496.html
```
> 中国水运报 · CCS 智能机舱检验指南 2017

```
https://www.dnv.com/maritime/autonomous-remotely-operated-ships/regulatory/
```
> DNV 自主船认证监管页（含 AiP 描述）

```
https://www.dnv.com/maritime/autonomous-remotely-operated-ships/aros-class-notation/
```
> DNV AROS 类标志公开文档（goal-based 框架）

```
https://standards.globalspec.com/std/13057765/dnvgl-cg-0264
```
> DNV-CG-0264 章节结构（GlobalSpec 摘要）

```
https://www.intertekinform.com/en-us/standards/bv-nr681-2022-1303869_saig_bv_bv_3169824/
```
> BV NR681 USV 适用范围

```
https://erules.veristar.com/dy/data/bv/pdf/681-NR_2022-07.pdf
```
> BV NR681 规范 PDF 全文（July 2022）

---

## E · IMO / 监管机构（4 个）

```
https://www.imo.org/en/mediacentre/hottopics/pages/autonomous-shipping.aspx
```
> IMO 自主航运专题

```
https://transport.ec.europa.eu/transport-modes/maritime/maritime-autonomous-ships-and-shipping_en
```
> EU VTMIS + MASS 监管框架

```
https://www.imo.org/en/mediacentre/meetingsummaries/pages/msc-110th-session.aspx
```
> IMO MSC 110 路线图分析

```
https://arxiv.org/html/2508.00543
```
> arxiv 2508.00543 · MASS-ROC 认证框架（STPA + EMSA RBAT）

---

## F · CONOPS / 方法论（3 个）

```
https://maritimesafetyinnovationlab.org/wp-content/uploads/2020/09/ABS-Advisory-on-Autonomous-Functionality.pdf
```
> ABS Advisory on Autonomous Functionality §4 · CONOPS 模板（PDF）

```
https://safety4sea.com/concept-of-operations-document-for-autonomous-systems-and-its-purpose/
```
> SAFETY4SEA · ABS CONOPS 内容要点

```
https://zenodo.org/records/6792507
```
> DTU + Danish Maritime · CONOPS Methodology（Zenodo 开放获取）

---

## G · 开源工具 / 学术仿真（3 个）

```
https://oceanai.mit.edu/ivpman/pmwiki/pmwiki.php?n=Helm.HelmDesignIntro
```
> MOOS-IvP 官方文档 · Helm Design Intro · Backseat Driver 范式原典

---

## 添加顺序建议

按重要性优先添加：
1. **先加 B（学术文献）** → 内容密度高，NotebookLM 的 AI 分析效果最好
2. **再加 D（CCS/规范）** → 合规依据，中文内容可正常索引
3. **再加 A（工业案例）** → 补充工程背景
4. **最后加 E/F/G** → 方法论支撑

---

## 注意事项

| 项目 | 说明 |
|------|------|
| 配额 | 48 个来源，留 2 个槽位备用 |
| 付费期刊 | DOI 跳转后若无订阅，NotebookLM 仅可读摘要；可改为上传已下载的 PDF |
| 中文网页 | CCS / 信德 / 国际船舶网等中文来源可正常添加，NotebookLM 支持中文 |
| PDF 直链 | GEBCO PDF、ABS Advisory PDF、KTH PDF、BV NR681 PDF 可直接添加为 Website URL |
| Yasukawa 2016 | *J. Ship Research* 60(4) 无公开 DOI 链接，建议手动上传 PDF 至笔记本 |

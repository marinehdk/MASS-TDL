# D2.1 · Evidence 目录 · 产物状态

| 属性 | 值 |
|---|---|
| 更新日期 | 2026-05-22 |
| 状态 | 🟡 设计完成，验证待执行 |

---

## 已完成产物

| 产物 | 路径 | 状态 | 备注 |
|---|---|---|---|
| D2.1-spec.md | `docs/Design/Phase 2/D2.1-m1-odd-hardening/D2.1-spec.md` | ✅ | v0.1，2026-05-21，含 3 候选方案 + 方案 A 选定 + 接口契约 + FMEDA 表 + DoD checklist |
| D2.1-plan.md | `docs/Design/Phase 2/D2.1-m1-odd-hardening/D2.1-plan.md` | ✅ | 6 并行流（Stream A-F）+ 完整代码蓝图 + 回归门控条件 |
| M1-FMEDA-v0.1.md | `docs/Design/Safety/FMEDA/M1-FMEDA-v0.1.md` | ✅ | 11 失效模式，覆盖 5/5 子模块，λSD/λSU/λDD/λDU + CCF，SIL 2 Route 1H |

---

## 待归档验证证据

以下项目对应 DoD checklist 中尚未执行的验证步骤，文件路径为预期落点。

| 证据文件 | 对应 DoD 项 | 状态 |
|---|---|---|
| `evidence/colcon-build.log` | colcon build 零警告成功 | ⏳ 待执行 |
| `evidence/test-results.xml` | 70 旧测试通过 + ≥30 新测试全通过 | ⏳ 待执行 |
| `evidence/lcov-report/` | M1 分支覆盖率 ≥ 95% | ⏳ 待执行 |
| `evidence/sanitizer.log` | ASan/UBSan clean（零 heap error + UB） | ⏳ 待执行 |
| `evidence/clang-tidy.log` | PATH-S 合规（无 exception / malloc on control path / 函数 ≤40 行 / 圈复杂度 ≤8） | ⏳ 待执行 |

# Mock Publishers (DEMO-1 fallback)

迁移自 `archive/sil_v0/`（2026-05-20）。

## 包

| 包 | 用途 | 生命周期 |
|---|---|---|
| sil_mock_publisher | SAT + ODD stub + R14 head-on 轨迹 | D1.3b → D2.4（D2.5 起由真 M1/M2 替代）|
| l3_external_mock_publisher | 11 跨层 topic（L1/L2/Fusion/Checker/Reflex/Override）| DEMO-1 fallback；真节点启动失败时启用 |

## 使用

```bash
ros2 run l3_external_mock_publisher external_mock_publisher
ros2 launch sil_mock_publisher sil_mock.launch.py
```

## 频率验证

```bash
ros2 topic hz /fusion/own_ship_state     # ~50 Hz
ros2 topic hz /fusion/tracked_targets    # ~2 Hz
ros2 topic hz /l1/voyage_task            # ~1 Hz
ros2 topic hz /fusion/environment_state   # ~0.2 Hz
```

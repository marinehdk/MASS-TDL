# Per v1.1.2 §15.1 + §12.2 自适应触发策略
builtin_interfaces/Time stamp

string source_module                     # 数据来源模块名
l3_msgs/SAT1Data sat1                    # 现状（持续刷新）
l3_msgs/SAT2Data sat2                    # 推理（按需触发）
l3_msgs/SAT3Data sat3                    # 预测（TDL 压缩时优先推送）

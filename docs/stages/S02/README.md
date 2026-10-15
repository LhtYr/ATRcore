# S02：数据契约、参数加载与边界剖面

状态：已完成并获用户审核通过。本页根据 S02 已留存的交付记录整理，历史数值以 [S02 自检报告](S02_自检报告.md) 为准。

本步建立四维状态/导数、五项外部输入和部件代数量数据结构，实现 19 键事务式参数加载、CLI 优先级、常值/延迟斜坡以及一次采样的整步保持接口。失败配置不会部分生效；请求流量原样保留，物理保护仍由后续部件处理。

参数单位、必填项、覆盖顺序和样本说明见 [参数契约](参数契约.md)。在 `D:/Acode/C/ATR/ATRcore/` 编译后执行：

```powershell
./build/main.exe --config config/delayed_ramp.cfg --sample 2
./build/char.exe contracts-check
./build/char.exe params-check
./build/char.exe boundary-check
```

S02 验收结果：契约 8 项、参数 60 项、边界 19 项全部通过；五项输入指定时刻的最大绝对误差均为 0；依赖重编译 14/14；27 条命令退出码符合预期。见 [命令记录](../../../results/S02/commands.csv) 和 [边界读回日志](../../../results/S02/16-boundary.log)。

这些检查验证输入层与保持接口，未验证发动机物理结果或真实 RK4 接线。后续累积工程的完整复现方式见 [S03 说明](../S03/README.md)。

[阶段目录](../README.md) · [下一步 S03](../S03/README.md)

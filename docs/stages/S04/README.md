# S04：Gas 数据、固定物性与 sigmoid 数值支持

2026-10-15。S03 已获用户审核通过；S04 自检通过，等待审核。变更、命令、退出码和误差详见 [S04 自检报告](S04_自检报告.md)。

- `gas_build_from_effective_flows` 接收有效 LOX/燃料来源流量、总压和总温，以 SI 单位建立 Gas；流量只相加一次，静温记账为总温的 0.81 倍，静压等于总压。失败不改输出。有效零流量合法；请求流量保护留给 GasGenerator。
- `gas_properties_turbine_calibrated` 分别保存截图 Cp=2860 与涡轮 gamma=1.18、R=380、Cp=2491.111111…，并记录来源。
- `numeric_sigmoid` 使用稳定 exp 分支，拒绝非有限输入，允许有限极值自然舍入为 0/1。C_exp 仅在工具中作数值参考，不链接进主程序。

质量来源不代表未反应出口组分；总静参数只是截图记账；两套 Cp 不构成统一热力学物性。本步没有 LOX 焓温反算或反应能量闭合。

在 PowerShell 中完整复现：

```powershell
Set-Location D:/Acode/C/ATR/ATRcore
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/verify_s04.ps1
$LASTEXITCODE
```

快速核查（父目录 results/S04 已由正式脚本建立）：

```powershell
$env:PATH='C:\msys64\clang64\bin;C:\msys64\usr\bin;'+$env:PATH
make all debug
make check
./build/char.exe gas-check
./build/char.exe numeric-check results/S04/numeric.csv
./build/char_debug.exe numeric-check results/S04/numeric_debug.csv
./build/char.exe numeric-compare results/S04/numeric.csv results/S04/numeric_debug.csv
```

正式结果：27 条命令符合预期；Gas 37/37、numeric 17/17、既有回归 122/122，另有构建冒烟和跨构建 CSV 对比 4/4。正常范围 sigmoid 与 C_exp 最大绝对差 2.2204460492503131e-16。

证据：[命令清单](../../../results/S04/commands.csv)、[验证总日志](../../../results/S04/verification.log)、[数值 CSV](../../../results/S04/numeric.csv)、[跨构建对比](../../../results/S04/17a-csv-compare.log)。CSV 为数学函数检查数据，不是发动机性能数据。本步无图片；作图入口、字体及后续 CSV + Python 流程继续保留。

**门控：收到明确 S04 放行后才进入 S05。** ELM 系数、前向累计及燃气发生器实现仍留 S05；主程序目前仍只检查边界输入。

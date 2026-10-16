# S05：LOX 燃气发生器固定 ELM

2026-10-15。S04 已获用户审核通过；S05 自检通过，待审核。未进入 S06，未暂存、提交或推送 Git。详细命令、文件清单和数值结果见 [自检报告](S05_自检报告.md)。

本步实现三输入、50 节点、250 个冻结系数的纯 C11 温度推理。输入顺序是燃烧室给定总压 Pa、LOX 请求流量 kg/s、燃料请求流量 kg/s；ELM 矩阵列序为归一化 LOX、燃料、压力。两路流量分别执行 2e-5/1e-5 kg/s 下限，原始请求保持不变。总流量只相加一次，总压直传；静温/静压和双口径 Cp 沿用 S04。

`gas_generator_evaluate` 返回完整 Eval，不修改参数或 View；`gas_generator_apply_state` 只复制成功终态快照。零状态是未评估；NaN/Inf、非正压力、错误参数和无效温度均明确失败。Params 的系数指针指向只读数组，本版本只接受冻结参数合同，不新增配置键；`legacy_fuel_calibration` 是不参与计算的元数据。View 按值保留全部诊断，不增加系统动态状态。

必须区分两种范围：归一化区间不是燃烧有效包线，输出反归一化尺度也不是温度限幅。

- 中点 p=3.6e6、W_ox=2.05、Wg=5.1：T4=1354.5654798959306 K，成功并标记输出超尺度。
- 原初始化 p=101325、W_ox=0.0001、Wg=0.0004：原始拟合 T4=-582.01694942315805 K，返回 GG_INVALID_OUTPUT。失败诊断中的其他出口字段不可消费。
- 两请求为零、p=3.6e6：有效 W4=3e-5 kg/s，T4≈1074.04144 K。这是源码下限延拓，不代表熄火或真实断供。

在 PowerShell 中完整复现：

```powershell
Set-Location D:/Acode/C/ATR/ATRcore
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/verify_s05.ps1
$LASTEXITCODE
```

实际退出 0：37 条命令符合预期，新增 1107 项断言、既有 176 项全部通过，另有构建冒烟；250 系数逐值核对、24+2 项依赖重编译通过。

独立运行（自定义输出父目录须存在）：

```powershell
$env:PATH='C:\msys64\clang64\bin;C:\msys64\usr\bin;'+$env:PATH
make all debug
make check
./build/char.exe gg-check tools/fixtures/gg_appendix_a.csv results/S05/gg_check.csv
./build/char.exe gg-point 3600000 2.05 5.1 results/S05/gg_point.csv
./build/char.exe gg-scan results/S05/gg_scan.csv
make plot PLOT_SCRIPT=plotting/plot_char.py 'PLOT_ARGS=--csv results/S05/gg_scan.csv --outdir results/S05/figures'
```

本机新建了工作区 `D:/Acode/C/ATR/.venv`，Make 会优先选择其 Python。其他环境可使用可用的 CPython 3.12 建立 venv，再执行：

```powershell
python -m venv ../.venv
../.venv/Scripts/python.exe -m pip install -r plotting/requirements.txt
```

Python 仅读取 C CSV 作图；编译和数值检查无需 Python。`make plot` 默认系统绘图入口仍待后续集成，通过上面的 PLOT_SCRIPT 显式选择部件绘图；`make run` 仍只打印边界，系统串接留 S08。

证据与图：

- [命令/退出码](../../../results/S05/commands.csv)、[系数核对表](../../../results/S05/coefficient-audit.csv)。
- [154 个样本逐层汇总](../../../results/S05/gg_check.csv)、[7700 行节点明细](../../../results/S05/gg_check.csv.nodes.csv)。
- [1323 点特性扫描](../../../results/S05/gg_scan.csv)、[温度分布图](../../../results/S05/figures/gg_scan_temperature.png)、[燃料截面图](../../../results/S05/figures/gg_scan_fuel_slices.png)。

特性 CSV 有 24 列，包含 requested/effective 流量、下限增加量、出口、归一化量、双 Cp、来源、flags/status；失败单点返回 1，不新写成功 CSV。检查报告同时保留失败样本诊断，不能直接作为有效出口曲线。节点 CSV 按样本、节点 0–49 记录 z/H、独立参考、旧 C_exp、加权项与升序累计。全部实数按 %.17g 序列化。

状态码：0 未评估、1 成功、2 非法输入、3 非法参数、4 非法输出。标志位：1 燃料下限、2 LOX 下限、4 输入外推、8 输出超尺度、16 两套 Cp 不同。

**等待明确 S05 审核通过后进入 S06。** 以上是固定公式的软件复现，不是 CEA 重算或燃烧试验精度验证。

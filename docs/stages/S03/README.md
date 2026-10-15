# ATRcore — S03 表驱动 CSV 日志器

S01、S02 已获用户审核通过；S03 已实现并完成本地自检，等待审核。未进入 S04。
冻结依据为工作区根目录《ATRcore_重构计划_待审核.md》。本页原为项目根目录 README，现归档为 S03 阶段说明；下文命令和代码路径均相对于 ATRcore 工程根目录。
当前证据见 [S03自检报告](S03_自检报告.md) 和 [命令记录](../../../results/S03/commands.csv)；历史证据保留在 results/S01、results/S02。全部阶段文档见 [阶段目录](../README.md)。
按 2026-10-15 最新执行指令启用 CSV + Python 后处理，覆盖蓝本中旧的“剔除 Python”约定；冻结蓝本不改写。

在 VS Code 中打开本目录（ATRcore），使用 MSYS2 Clang64 工具链。
默认安装路径 C:/msys64；若安装位置不同，调整 .vscode/tasks.json 和 launch.json 中的路径。
安装/启用 clangd 与 CodeLLDB（本机已检测到），按 F5 会先执行 Make: debug。
Windows 实际调试文件为 build/main_debug.exe，其他平台为 build/main_debug。
根目录 compile_flags.txt 为 clangd 提供固定工作目录，避免无编译数据库时
把 -Iinclude 错误解释为 src/include；不依赖 Bear。

PowerShell 环境与构建命令：

```powershell
$env:PATH = 'C:\msys64\clang64\bin;C:\msys64\usr\bin;' + $env:PATH
Set-Location D:/Acode/C/ATR/ATRcore
make all
make debug
make check
make run
make help
make clean
```

MSYS2 Clang64 终端可直接执行相同 make 命令。
release: -std=c11 -Wall -Wextra -Wshadow -MMD -MP -O3 -march=native；
debug: 相同公共标志及 -O0 -g3。显式链接库仅 -lm。
严格诊断可用 `make all debug EXTRA_CFLAGS="-pedantic-errors -Werror"`，更换标志前先 `make clean`。
all 构建主入口与 C 工具；debug 构建 main_debug 与 char_debug；char 只构建 release C 工具。
check 运行构建冒烟和独立的 contracts-check、params-check、boundary-check、logger-check（87 项 S02 断言及 35 项 S03 断言）。
无参数 main 读取默认边界并打印五项 SI 输入，返回 0；尚未执行物理仿真。
配置错误、未知参数及未知 char 子命令返回非零。

```powershell
./build/main.exe --config config/delayed_ramp.cfg --sample 2
./build/main.exe --set input.Wg=5.7 --config config/in_range.cfg
./build/char.exe contracts-check
./build/char.exe params-check
./build/char.exe boundary-check
./build/char.exe logger-check
./build/char_debug.exe check
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/verify_s03.ps1
```

完整键表、必填值、单位、优先级和斜坡规则见 [参数契约](../S02/参数契约.md)。
配置加载是事务式的：先完整解析/校验，再提交；错误不留下部分生效的配置。
参数自检在 build 内写入并删除单个临时文件，需在工程根目录串行执行，勿并发运行两份 params-check。
S02 检查的零阶保持是一次采样后的值拷贝消费接口；实际 RK4 接线在 S08 验证。

S03 Logger 使用只读列定义表，输出 80 列 CSV；首列记录终态 t+h，另列记录整步输入采样时间 t。
原始积分腔压与保护后腔压、截图 Cp 与涡轮 Cp、气动功率与轴功率分别记录；五次求值的迭代标志各自保留。
全部操作返回状态码；I/O 错误不能作为成功结束，调用者必须检查 flush/close。
完整格式、单位与调用约定见 [CSV约定](CSV约定.md)。

`logger-check` 默认写入 results/logger_fixture.csv 并用 C 读回；可给一个路径参数覆盖（父目录须存在）。
该文件是合成序列化夹具，故意使用不同的原始/保护后字段来检测错列，**不是发动机仿真结果**。
main 仍只打印边界快照，不伪造物理观测；正式求解与 Logger 接线属于 S08。

源文件清单只来自 mk/app.mk、component.mk、solver.mk、tools.mk。
component 源清单仍为空；部件头文件只定义供 SystemAlgebraic 使用的数据类型。
solver 源清单仅有 BoundaryProfile/BoundaryDefaults；src/param_file.c 和 src/Logger.c 由 app.mk 声明并与 C 工具共享。
没有引入 ELM、Gas 适配计算、涡轮/轴系计算或 RK4/Solver 积分实现。
本阶段依赖验证：include/Logger.h 触发 4/4 个对象重编译，src/logger_columns.def 触发 2/2 个对象重编译。
clean 只删除 build，不删除 results 中的自检证据。

绘图解释器优先顺序：上级工作区 .venv（Scripts/python.exe、bin/python），
本目录 .venv，然后 PATH 上 python/python3；可用 `make plot PYTHON=完整路径` 覆盖。
本阶段提供 `make plot`、`make run-plot` 和两个 Python 命令入口；run-plot 即使并行 make 也先完成 run。
plot.py/plot_char.py 当前明确返回 2，待相应 CSV 阶段放行后接入渲染，不宣称已实现完整后处理。
后续仅从 C 输出的 CSV 画图，C 工具负责数值 PASS/FAIL。
字体 TimesNewRoman.ttf、SimSun.ttf 按参考工程原样复制，setup_style 与其字体顺序一致。
本阶段无物理仿真 CSV 或图片；合成 CSV 的写入/读回和参数/边界检查全部由 C 判定。
results/S03/logger_fixture.csv、logger_fixture_debug.csv 为本次留存的合成数据，二者逐字节一致。
results/S03/commands.csv、dependencies.csv 为命令/依赖元数据。

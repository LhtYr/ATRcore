# S01：纯 C11 构建框架

状态：已完成并获用户审核通过。本页根据 S01 已留存的交付记录整理，历史数值以 [S01 自检报告](S01_自检报告.md) 为准。

本步建立 ATRcore 目录、分层 Makefile、最小 C 主入口/检查入口、自动头文件依赖、clangd 与 VS Code 调试配置，以及 CSV+Python 后处理入口和字体。此时尚无物理模块或模型 CSV。

在 `D:/Acode/C/ATR/ATRcore/` 执行构建：

```powershell
$env:PATH = 'C:\msys64\clang64\bin;C:\msys64\usr\bin;' + $env:PATH
make all
make debug
make help
```

编译基线为 `clang -std=c11 -Wall -Wextra -Wshadow -MMD -MP`，显式链接库仅 `-lm`。release 使用 `-O3 -march=native`，debug 使用 `-O0 -g3`；clean 仅移除 build。工具链和后续累积用法见 [当前 S03 说明](../S03/README.md)。

S01 验收时的 32 条命令退出码均符合预期；构建冒烟误差为 0，依赖重编译 3/3，严格编译及 LLDB 断点检查通过。证据见 [命令记录](../../../results/S01/commands.csv) 与 [依赖记录](../../../results/S01/dependencies.csv)。这些是 S01 当时的结果，不代表当前源码仍停留在 S01。

[阶段目录](../README.md) · [下一步 S02](../S02/README.md)

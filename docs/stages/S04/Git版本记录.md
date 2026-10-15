# S04 本地 Git 版本记录

日期：2026-10-15。分支：master。身份：鹧鸪天 <LHT2518@outlook.com>（仅本地仓库配置）。

建立 Git 历史时，S04 已开发完成，仓库尚无提交。因此首条提交是依据阶段证据事后重建的 S04 前源码基线，使用实际提交时间；不是声称开发前已存在这个提交。

首条提交：871230cfc747ee8594b9509bddfce65027024c59，题为「S04 前初始状态（依据阶段记录重建）」。收录 168 个文件，保留 S03 完成后的文档归档结构。六个构建/源码文件恢复后与历史 SHA256 匹配：Makefile、mk/tools.mk、include/build_info.h、include/component/Gas.h、include/utils/numeric_utils.h、tools/char_main.c。mk/component.mk 和阶段索引按记录反向恢复；进度暂存提取现有文件保留的 S03 原文。这三项没有文档整理后的历史指纹，不宣称整个旧目录已经获得独立的历史字节认证。

在 .git/stage-history/pre-s04 独立目录执行以下命令，退出码均为 0：

- make all debug 'EXTRA_CFLAGS=-pedantic-errors -Werror'
- make check
- ./build/char_debug.exe check

S03 原有 122 项断言和构建冒烟通过。文档保留当时待审核记录，后续授权见当前阶段索引及进度暂存。

第二条提交题为「S04 任务完成：Gas 与数值支持及校验证据」，收录实际当前 S04 完成状态，以及本版本记录和重建校验证据。S04 自检通过不等于用户已审核放行；尚未进入 S05。

证据目录：[重建记录](../../../results/S04/git-history/reconstruction.csv)、[操作前工作区指纹](../../../results/S04/git-history/s04-worktree-before.csv)、[基线编译日志](../../../results/S04/git-history/pre-s04-build.log)、[基线 C 校验](../../../results/S04/git-history/pre-s04-check.log)、[基线 debug 校验](../../../results/S04/git-history/pre-s04-debug-check.log)。S04 原有报告及源码指纹未改动。

所有项目源码、文档、配置、字体和校验结果纳入版本；现有 .gitignore 继续忽略 build/ 和 __pycache__/。未执行 reset/revert，未切换或覆盖当前工作区内容，未修改远程或 push。

查看结果：git log --oneline -2；git status --short --branch。查看第一条提交详情：git show --no-patch --format=fuller HEAD~1。
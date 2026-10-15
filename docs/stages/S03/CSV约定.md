# S03 CSV 日志约定（v1）

日志器仅消费已完成的快照，不调用部件求值函数、不推进积分、不作物理保护、不改写状态或输入。主程序的物理求解与日志接线属于 S08。当前 `char logger-check` 产生的是**合成序列化夹具**，不是发动机仿真结果。

## 格式与列定义

默认 80 列，首行为 ASCII 列名，后续全部为数值；逗号分隔、LF 换行、无 BOM、无附加注释行、无尾随逗号。double 使用 `%.17g`，整数状态/位掩码以精确可表示的数值输出。当前工具链为 binary64 double，C 独立读回对夹具要求精确相等。一般物理比较仍按冻结蓝本各量的容差执行，不能把序列化零误差当作物理精度。

完整列顺序、单位和只读取值表达式位于 `src/logger_columns.def`；可由 `SimulationLogger_columns` 获得只读描述表。生产代码只维护这一份列定义；`tools/logger_expected.h` 是独立的验收期望表，不能从生产描述表自动生成。

| 列组 | 数量 | 内容与单位 |
| --- | --- | --- |
| 时间 | 4 | t_s、input_sampled_at_s、step_start_s、step_h_s，均 s |
| 外部输入 | 5 | p_chamber_Pa、W_ox_requested_kg_s、Wg_requested_kg_s、p_out_Pa、T_load_N_m |
| 状态及压力观测 | 6 | theta_mech_rad、omega_mech_rad_s；两列 raw_Pa 和两列 protected_Pa |
| 燃发器快照与出口 | 12 | gg_p_chamber_Pa、两路 gg_requested、两路 effective、W4、总/静压温、O/F、F/O |
| ELM 归一化 | 4 | nomO2、nomWf、nomConP、nomConT；无量纲，不截断 |
| 物性、燃发器诊断 | 7 | 两套 Cp、gamma、R、property_source、gg_flags、gg_status |
| 涡轮观测 | 11 | 三段流量 kg/s、T6_total/T6_ideal K、效率/速比无量纲、P_turb/P_shaft W、T_turb N·m、turbine_flags |
| 导数与求值状态 | 5 | 角度/转速导数 rad/s、rad/s²；两压力导数 Pa/s；eval_status |
| 迭代诊断 | 26 | iteration_enabled；k1/k2/k3/k4/final 各自 iterations、converged、limit_hit、delta_omega_rad_s、delta_pressure_Pa |

维度字段在列名中注明单位；无量纲使用描述表单位 `1`，诊断单位为 `bool/count/enum/bitmask`。隐层 50 个 z/H 暂保留在 Eval 供 S05 检查，不进入默认 CSV。需要调整默认列时应同时更新格式文档和独立契约检查。

## 时间、压力和功率口径

调用者提供 `step_start_s=t`、`step_h_s=h`，日志器的首列始终为 `t+h`；初始/诊断快照以 h=0 表示。边界快照必须在 t 采样，`input_sampled_at_s=t` 单独保留。合法时间有限、非负；正 h 必须使浮点终止时刻严格大于起点，溢出、负值或采样时刻不匹配都拒绝。记录终态时，不再沿用参考 main 中“step 后仍用 t 标记”的容易混淆写法；此处按冻结蓝本 2.5 的 t+h 约定实现。

`p_plenum_raw_Pa/p_downstream_raw_Pa` 直接来自 SystemState，允许记录有限的负值或低于 p_floor 的值用于诊断，不投影状态。`*_protected_Pa` 来自最终 TurbineEval。日志器不计算保护后压力；后续模型必须显式提供。GG 输入快照也独立记录，便于核对实际串接参数，日志器不自动使其等于外部输入。

P_turb 为模型气动功率，P_shaft 为 T_turb×omega_mech 口径的轴功率，具体计算在 S06。日志只抄录各自字段，不假定低速保护时两者等于同一机械效率倍数，也不重复乘效率。cp_source=截图口径，cp_for_turbine/gamma/R=固定标定口径；不合并两套物性。

## 诊断与错误

五次求值的迭代诊断分别保留，避免只看 final 就丢失前面子步的上限命中。iteration_enabled=0 时所有阶段诊断为 0，其中 converged=0 表示“不适用”，不表示本步失败；查看 enabled 和 eval_status 共同解释。启用时次数须非负、布尔量须为 0/1、增量须非负；converged 与 limit_hit 不能同时为 1。Logger 不重新判断收敛阈值，也不将记录成功解释成模型成功，实际迭代算法在 S08 验证。

EvalStatus 数值：0 未评估、1 成功、2 非法输入、3 部件失败、4 未收敛。GasGeneratorStatus 数值：0 未评估、1 成功、2 非法输入、3 非法参数、4 非法输出。property_source：0 未设置、1 TURBINE_CALIBRATED。gg_flags 的位 0–4 依次为燃料下限、LOX 下限、输入外推、输出超温标、Cp 双口径差异。turbine_flags 原样保留，具体物理位定义随 S06 公开，不在 S03 捏造保护算法。

数值有限的失败状态可作为诊断快照写出，并保持失败状态码。任何被输出列包含 NaN/Inf 时，整行在写入前拒绝；不替换成 0，也不把其当成合法模型结果。

所有公开写入操作返回 LoggerStatus。首个错误持续保留，后续写入被阻止；close 即使已有错误也执行资源释放，返回错误。行先在内存完整格式化，再提交给输出端，因此参数/数值校验失败不会留下半行；真实 I/O 失败仍可能产生部分字节，调用者必须检查 flush 和 close，不能把已有文件当成成功完成证据。rows_written 只表示输出端已接收的行数，不能替代 flush/close 成功。

默认使用 fopen/fwrite/fflush/fclose，均为 C11。Logger 不创建目录；Make 预建 results，自定义父目录由调用环境预建。路径以 `wb` 打开会覆盖已有文件。调用者应初始化 `SimulationLogger logger={0}`，活动 logger 不允许重复初始化；关闭后可重新初始化。

要求当前 LC_NUMERIC 小数点为 `.`；程序启动缺省 C locale 满足此条件。Logger 仅检查、不修改进程 locale。若调用方切换至逗号小数点，将返回 LOGGER_BAD_LOCALE。上下文在记录时须保持有效且不被并发修改；当前 Logger 不提供线程同步。

`LoggerSink` 是可替换的输出接口，write/flush/close 返回 1 成功或 0 失败；使 C 检查可以确定性注入 I/O 故障，不依赖磁盘耗尽、/dev/full 或 Windows API。故障检查区分真实 fopen 失败与注入的写入/刷新/关闭失败，不声称已做磁盘硬件故障试验。

## 复现与下游读取

在 ATRcore 根目录执行 `make check`，或 `./build/char.exe logger-check results/S03/logger_fixture.csv`（父目录须事先存在）。默认 logger-check 写入 results/logger_fixture.csv。检查用具先写两条合成快照，再由独立 C 解析器验证表头、列数、完整数值、末尾 EOF 和源数据不变；Python 不参与判定。

合成夹具故意包含不符合物理守恒的不同数值，用于发现取错字段；例如 raw/protected 腔压、两套功率、外部/GG 输入都可不相等。不得将此夹具绘成发动机性能曲线。S03 无作图验收要求，make plot/run-plot 入口继续保留，正式部件与系统图按后续阶段接入。

未来 Python 可沿用参考程序按列名读取的方式（例如 NumPy genfromtxt 的 names=True），按本工程的 `t_s` 和带单位列名取数，不再使用参考电驱列名。C 检查及其退出码始终承担数值验收。

# 更新说明 (UPDATE.md)

本文档记录 `CATPart 实体 BOM 生成工具` 在近期迭代中的主要问题修复与细节调整。

本项目是一个用于解析单个 `.CATPart` 多实体（Multi-Body）零件、提取基础几何属性（体积、表面积、重心）并汇总生成 Excel 明细表的小工具。

---

## V04（v4.0.0）

### 1. CAA Worker 隐藏实体与警告/状态语义对齐 (`Source/Worker/CAAStandalonePartWorker.cpp`)

- **修正隐藏实体（NoShow）分支的日志冲突**
  - **问题**：检测到 `hiddenCount > 0` 时，隐藏实体分支内部会先输出一行 `failures=1`，而主函数在收尾阶段又会输出最终的 `failures=N` 汇总，导致同一份日志中出现两个互相矛盾的 `failures` 字段，容易被前端判定为解析异常。
  - **调整**：隐藏实体分支只记录 `hidden_body_count=N names=...` 并累加 `failures`，`failures=` 字段统一由主函数收口输出一次。

- **主惯性矩校验补充 `inertiaOk` 前置判断**
  - **问题**：V03 在任一实体的惯性矩不可用时跳过该项校验，仍可能按体积和表面积归并。
  - **调整**：`nearInertia()` 在任一实体 `!inertiaOk` 时直接返回 `false`；惯性测量失败的实体按单件成组，并在状态列标记 `inertia_warning`，保证不会因缺失数据发生错误归并。

- **警告细分计数与组合状态标记**
  - 新增输出 `warning_breakdown tiny=... empty_body=... area_warning=... render_fallback=... inertia_fallback=...`，使各类警告可独立统计。
  - 引入 `composeWarningStatus()` 组合状态标记函数，当单个实体同时存在多种警告时（如表面积异常叠加渲染回退），状态列会拼接成组合标识而非互相覆盖。
  - `empty_body` 实例同步写入 `Instances_*.csv`，确保明细表与《异常与警告明细》工作表的记录能够一一对应。

- **`Entity_Analysis_*.csv` 补充质量与惯性数据**
  - 在原有列之后追加输出 `density_kg_m3,unit_mass_g,total_mass_g,inertia_eig1,inertia_eig2,inertia_eig3`，便于脱离 Excel 直接核对密度换算与主惯性矩结果。

- **后台心跳线程与可控测试钩子**
  - 新增独立心跳线程，每 `2000ms` 以及每个关键阶段（会话建立、文档打开、测量、渲染、导出）立即刷新 `heartbeat_<wid>.txt`，前端根据心跳判断后台是否仍有响应。心跳不表示几何计算已推进，任务仍受总时限约束。
  - 新增环境变量测试钩子：`CAA_FORCE_INERTIA_FAIL`（强制惯性测量失败）、`CAA_FORCE_RENDER_FAIL`（强制渲染回退）、`CAA_SIMULATE_DELAY_MS`（注入延时），便于在不构造异常模型的前提下复现各类降级路径。

---

### 2. 自定义密度参数全链路打通与双引擎一致性 (`Source/UI/pipeline.cpp`, `Source/UI/xlsx_builder.cpp`, `Source/Export/build_filtered_bom.mjs`, `Runtime/Export/build_filtered_bom.mjs`)

- **问题**：命令行 `/c` 模式与界面输入的自定义密度此前只在部分环节生效——Node.js 导出脚本未接收到密度参数，原生 C++ 生成器也未获得该值，导致两种引擎产出的表格在材料行与重量列上不一致。

- **调整**：
  - `pipeline.cpp` 调用 Node.js 导出脚本时补传第 3 个位置参数（`j.density`）；调用原生生成器 `BuildFilteredBomXlsxNative` 时同样传入 `j.density`。
  - 双引擎在 `customDensity > 0` 时，均在《材料密度》工作表第 21 行写入「自定义密度」条目，并同步调整主表公式引用与缓存值，保证公式重算结果与静态缓存一致。

---

### 3. 异常与警告明细工作表与前端分项提示 (`Source/UI/xlsx_builder.cpp`, `Source/Export/build_filtered_bom.mjs`, `Source/UI/pipeline.cpp`)

- **新增《异常与警告明细》工作表**
  - 双引擎在检测到存在状态既非 `ok`、也非 `tiny_volume` 的异常实例时，额外生成一张《异常与警告明细》工作表，逐条记录实体名称、状态标识与异常原因；无异常时该工作表不生成，保持输出文件精简。

- **前端按异常类型分项提示**
  - **问题**：旧版一律将渲染回退、惯性测量失败等警告笼统归类为「微小实体已筛除」，提示信息与实际情况不符。
  - **调整**：`pipeline.cpp` 解析 Worker 输出的 `warning_breakdown`，按 `tiny` / `empty_body` / `area_warning` / `render_warning` / `inertia_warning` 分别给出对应提示文案。

- **校验逻辑放行组合状态**
  - `validateRows()` 改用 `isValidGroupStatus()` 校验状态列，允许组合状态标记通过，避免因多警告拼接导致整行被判为非法而丢弃。

---

### 4. 超时、取消与进程启动鲁棒性 (`Source/UI/pipeline.cpp`, `Source/UI/xlsx_builder.h`, `Source/UI/xlsx_builder.cpp`)

- **超时策略统一为 30 分钟并优先采信心跳**
  - `WORKER_TOTAL_TIMEOUT_MS` 与 `TASK_TOTAL_TIMEOUT_MS` 统一调整为 `1800000`（30 分钟）。
  - 无响应判定改为优先监听 `heartbeat_*.txt` 的修改时间与活跃状态，日志长时间无输出不再直接等同于进程卡死。

- **修复 `launch()` 的句柄与挂起进程遗留问题**
  - Job 绑定或线程恢复失败时，终止并回收已创建的子进程。Job 配置不可用时记录诊断信息。

- **原生导出支持中断检查**
  - `BuildFilteredBomXlsxNative` 新增 `cancelEvent` 与 `deadlineTick` 参数，在写入过程中分段检查取消信号与超时时间。
  - `pipeline.cpp` 在把生成结果拷贝到目标路径前再次校验取消信号，在提交前收到取消信号时中止输出。

---

## V3 主要改动

### 1. CAA 几何提取与渲染模块 (`Source/Worker/CAAStandalonePartWorker.cpp`)

- **增加主惯性矩辅助校验，减少异形件误归并**
  - **问题**：旧版本仅通过「体积 + 表面积」容差判断两个实体是否为同规格零件。理论上若不同形状的实体恰好体积与表面积相近，可能被误合并为同一行。
  - **调整**：在体积（相对/绝对容差 `1e-6`）与表面积（容差 `1e-4`）比对的基础上，补充调用 `CATICGMDynMassProperties3D::GetInertiaMatrix()` 与 `CATMath3x3Matrix::EigenVectors()` 提取绕重心的三个主惯性矩特征值（排序后按 `1e-3` 容差比对）。由于主惯性矩具有平移与旋转不变性，可在不影响常规对称件、标准紧固件归并的前提下，降低异形件误合并的概率。

- **补充 `CATTry` / `CATCatch` 异常捕获与日志记录**
  - **问题**：面对部分拓扑存在缺陷或退化面的模型时，CGM 测量或离屏渲染接口可能抛出 CAA 异常，导致 Worker 进程直接中断且缺少排查信息。
  - **调整**：在 `measureBody` 与 `testRep` 中补充了 `CATTry` / `CATCatch(CATError, err)` 捕获块，记录错误编号（`GetMsgId`）、简短提示（`GetNLSMessage`）及源码位置。
  - **降级处理**：离屏渲染若首次失败会自动重试一次；若仍失败则生成一张空白占位图并计入警告（`area_warning`），尽量保证后续表格能够正常生成，同时确保异常分支下 `CATI3DGeoVisu`、`CATVisManager` 与 `CAT3DViewpoint` 按顺序释放。

- **表面积回退计算记录与空几何体（Empty Body）跳过**
  - **调整**：当整体 `GetWetArea()` 返回异常而退回逐面累加模式时，在 `Instances_*.csv` 中保留有符号面积和（`area_raw_signed_mm2`）、负面积面片数（`negative_face_count`）及计算状态标识（`area_condition`），便于事后核对异常几何。
  - 对于不包含有效三维实体的空 Body，记录 `empty_body` 日志后直接跳过，不再计入有效实例行。

- **调整 COM 智能指针析构顺序**
  - **问题**：旧版本在主函数末尾执行 `CATDocumentServices::Remove(*doc)` 与 `Delete_Session()` 时，部分局部 `_var` 智能指针尚未离开作用域，偶发退出阶段访问已释放文档对象的问题，且存在重复清理代码。
  - **调整**：通过内层作用域块让 `CATIPrtPart_var`、Body 列表及实例数组在关闭文档与会话前先行析构，并合并了末尾的清理逻辑。

- **精简编译依赖与整理构建脚本**
  - 从 `IdentityCard.xml` 与 `Imakefile.mk` 中移除了后台批处理未实际使用的 `SketcherInterfaces`、`MechanicalModelerUI`、`ApplicationFrame` 模块。
  - 在 `Source/Worker/` 下补充了 `build_worker.ps1` 与 `build_worker.bat`，便于在配置好 VS2012 与 CATIA CAA RADE 环境的机器上复现编译。

---

### 2. 界面调度与导出模块 (`Source/UI/`)

- **绑定 Windows Job Object 防止子进程残留 (`pipeline.cpp`)**
  - **问题**：此前若在运行中途关闭界面或遭遇进程卡死，后台的 `CATSTART.exe` 或 `CAAStandalonePartWorker.exe` 可能继续驻留。
  - **调整**：启动子进程时绑定带 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 标志的 Job Object，使子进程随主控句柄关闭一并终止。

- **支持手动取消任务与超时退出 (`BomModern.cpp` / `pipeline.cpp`)**
  - **调整**：运行期间支持点击「取消任务」按钮发送中断信号，并在窗口关闭时增加确认提示。
  - 增加了基础超时检查（单次任务总时长上限 30 分钟、日志连续 3 分钟无输出视为无响应、外部脚本导出上限 2 分钟），避免异常模型导致程序无限期挂起。

- **修复临时目录多层子文件夹清理不完整的问题 (`pipeline.cpp`)**
  - **问题**：每次任务在 `%TEMP%\CAA_BOM_V3_*` 下隔离运行 `CATTemp` 与 `CATReport` 时，CATIA 会在 `cat_temp` 下创建二级子目录（如 `CNext01.roll`），旧版单层清理逻辑无法将其删除，导致任务结束后提示临时缓存未完全清理。
  - **调整**：改为递归遍历删除子目录树（`removeDirTree`），并在删除前重置文件只读属性。

- **命令行 `/c` 静默调用模式补充可选密度参数 (`BomModern.cpp` / `pipeline.cpp`)**
  - 主程序支持通过 `/c` 参数在无界面模式下运行：
    ```bat
    实体BOM生成.exe /c "<输入CATPart路径>" "<输出目录路径>" [600|800|1200] [材料密度kg/m3]
    ```
  - 退出码约定：`0` 正常完成、`1` 运行失败或取消、`2` 参数格式错误、`3` 检测到隐藏状态（NoShow）实体中止导出。

- **统一构建脚本与原生 C++ OpenXML 导出 (`xlsx_builder.cpp` / `build.bat`)**
  - 修复了 `Source/UI/build.bat` 漏编译 `xlsx_builder.cpp` 及编码兼容性问题。
  - 当未部署外部 Node.js 运行目录时，程序默认使用内置的 C++ OpenXML 模块（`xlsx_builder.cpp`）配合 `Runtime/Export/templates/` 下的 XML 模板直接生成 `.xlsx` 文件，减小分发包体积。

---

## 历史版本简记

- **V2**：
  - 将写死的运行路径改为基于程序所在目录动态查找；
  - 增加对隐藏实体（NoShow）的检查与提示，避免误将隐藏特征计入重量；
  - 优先采用 `GetWetArea()` 测量表面积，整理了 15 列 Excel 表格布局与常用材料密度参考表；
  - 补充了原生 C++ OpenXML 导出逻辑。
- **V1**：
  - 初始测试版本，实现基础的多实体体积/面积/重心测量、微小体过滤、离屏截图与表格生成。

---

## 已知局限与说明

1. **同规格归并规则的局限性**：当前的体积、表面积与主惯性矩比对仅属于启发式几何特征匹配，并非严格的拓扑全等证明；对于极特殊构造下三项数值均在容差内的不同零件，仍建议结合预览图人工复核。
2. **仅限单零件多实体（`.CATPart`）**：本工具针对单个 `.CATPart` 内的多实体（Solid Bodies）统计设计，不直接处理 `.CATProduct` 装配体树的层级与实例变换矩阵。
3. **编译与运行环境限制**：CAA Worker 基于 CATIA V5-6R2016 (B26) x64 与 MSVC 2012 (v110) 编译；若在其他大版本的 CATIA 环境下使用，建议根据目标版本的 SDK 重新编译 Worker。

如在使用或编译过程中遇到问题，欢迎提出指正。

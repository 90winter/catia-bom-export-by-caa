# 更新说明 (UPDATE.md)

本文档记录 `CATPart 实体 BOM 生成工具` 在近期迭代中的主要问题修复与细节调整。

本项目是一个用于解析单个 `.CATPart` 多实体（Multi-Body）零件、提取基础几何属性（体积、表面积、重心）并汇总生成 Excel 明细表的小工具。V3 版本主要针对此前使用过程中暴露出的异常退出、后台进程残留、偶发异形件误合并以及临时目录清理不彻底等问题做了针对性修补。

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

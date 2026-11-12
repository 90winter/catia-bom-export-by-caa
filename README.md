# CATPart 实体 BOM 生成工具 V04

这是一个基于 **CATIA V5 CAA C++** 与 **Win32** 的辅助小工具，用于在脱机/无界面模式下读取单个 `.CATPart` 文件中的多实体（Multi-Body）几何数据，统计体积、重心与数量，并导出带预览缩略图的 Excel 明细表（`.xlsx`）。

[下载 V04 Windows 便携版](https://github.com/90winter/catia-bom-export-by-caa/releases/tag/v4.0.0) · [查看更新说明](./UPDATE.md)

---

## 一、 主要功能

1. **多实体几何属性测量**
   - 遍历 `.CATPart` 中的顶层 Solid Bodies，通过 CGM 接口测量每个实体的体积、表面积、重心坐标与主惯性矩。
2. **隐藏实体（NoShow）前置检查**
   - 导出前先检查模型内各实体及结果体是否处于隐藏（NoShow / Invisible）状态。若发现隐藏实体，程序将中止导出并提示对应的实体名称（返回码 `3`），避免漏算或误算。
3. **微小实体过滤**
   - 对体积 `< 1.0 mm³` 的微小几何体自动筛除，并单独记录在《微小几何体筛除记录》工作表中备查。
4. **相同规格实体归并**
   - 根据体积、表面积与重心主惯性矩容差，将模型内规格相同的对称件或紧固件归并为同一行，统计单台用量与加权质心。
5. **离屏预览图渲染**
   - 通过 2x 超采样离屏渲染生成白底实体缩略图（可选 `600×600`、`800×800`、`1200×1200`），并嵌入到生成的 Excel 表格中。
6. **Excel 明细表导出**
   - 内置原生 C++ OpenXML 写入模块（`xlsx_builder.cpp`），无需安装 Office 或 Node.js 即可生成包含《实体BOM》、《材料密度》和《微小几何体筛除记录》的 `.xlsx` 文件。
   - 在《实体BOM》工作表中支持下拉切换常见材料牌号，通过表格公式自动重算质量与总成质心。

---

## 二、 环境要求与使用方法

### 运行环境要求
- **操作系统**：Windows 7 / 10 / 11（64 位）
- **CATIA 环境**：本机需安装 64 位 CATIA V5（默认支持常见安装路径自动查找，如 B26 等；若安装在非默认位置，可配置环境变量 `CATSTART_PATH` 指向本机的 `CATSTART.exe`）。

### 1. 图形界面运行
双击运行 `实体BOM生成.exe`：
1. 选择需要分析的 `.CATPart` 文件及输出目录；
2. 选择缩略图分辨率（默认 `800 × 800`）；
3. 点击 **「开始生成实体 BOM」**，等待分析完成即可在输出目录查看生成的 `.xlsx` 文件；运行过程中可点击 **「取消任务」** 中止执行。

### 2. 命令行静默调用
支持在批处理或脚本中通过 `/c` 参数静默调用：
```bat
实体BOM生成.exe /c "<输入CATPart路径>" "<输出目录路径>" [600|800|1200] [材料密度kg/m3]
```
或使用命令行测试程序：
```bat
pipeline_test.exe "<输入CATPart路径>" "<输出目录路径>" [600|800|1200] [材料密度kg/m3]
```

**返回码说明：**
- `0`：正常导出完成
- `1`：执行失败、文件不存在、超时或中途取消
- `2`：命令行参数格式有误
- `3`：检测到隐藏状态（NoShow）实体而中止导出

---

## 三、 目录结构

```text
├── 实体BOM生成.exe                  # Win32 界面主程序（支持 /c 命令行模式）
├── pipeline_test.exe                # 命令行测试入口
├── README.md                        # 项目说明
├── UPDATE.md                        # 版本改动与修复记录
├── Runtime/
│   ├── CATEnv/                      # CATIA 运行环境模板
│   ├── Export/
│   │   ├── build_filtered_bom.mjs   # 可选的 Node.js 导出脚本
│   │   └── templates/               # Excel 样式与材料密度表 XML 模板
│   └── win_b64/code/bin/            # CAA 几何计算 Worker 与 VC++ 运行库
└── Source/
    ├── Export/                      # 导出模板与脚本源码
    ├── UI/                          # 界面、进程管道控制与 C++ OpenXML 导出源码
    └── Worker/                      # CAA C++ 几何测量与渲染 Worker 源码及构建脚本
```

---

## 四、 编译说明

### 1. 编译界面与主控程序 (`Source\UI`)
需安装 **Visual Studio 2012 (v110 x64)**：
```bat
cd Source\UI
build.bat
```
或使用 PowerShell：
```powershell
powershell -ExecutionPolicy Bypass -File Source\UI\build.ps1
```

### 2. 编译 CAA Worker (`Source\Worker`)
需安装 **Visual Studio 2012 (x64)** 及对应版本的 **CATIA CAA RADE (`mkmk` / `tck_init`)**：
```powershell
powershell -ExecutionPolicy Bypass -File Source\Worker\build_worker.ps1 -CatiaDir "C:\Program Files\Dassault Systemes\B26" -TckProfile "V5_6R2016_B26"
```
编译生成的 `CAAStandalonePartWorker.exe` 会自动复制到 `Runtime\win_b64\code\bin\`。

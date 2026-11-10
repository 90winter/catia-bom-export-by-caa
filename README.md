# CATIA BOM导出 by CAA

在 Windows 上使用 CAA 分析 CATIA V5 的多实体 CATPart，并导出带缩略图的 Excel BOM。程序测量体积、表面积和质心，按几何测量值归并重复实体，并单独记录体积小于 1 mm³ 的实体。Excel 内置材料密度表，更改材质后可重算质量和总成质心。

## 运行

1. 从 [Releases](https://github.com/90winter/catia-bom-export-by-caa/releases) 下载并解压公开便携版。需要 Windows 64 位和 CATIA V5-6R2016（B26）或更高版本的 64 位 CATIA。
2. 双击 `实体BOM生成.exe`，选择自己的 `.CATPart`、输出目录和预览图分辨率。
3. 点击「开始生成实体 BOM」。工作簿由原生 C++ OpenXML 引擎生成。

如果 CATIA 安装在自定义路径，可将环境变量 `CATSTART_PATH` 指向 `CATSTART.exe`。请在导出前检查模型的显示状态，程序发现隐藏实体时会中止并列出其名称。

命令行入口支持 600、800 和 1200 像素的预览图。

```bat
实体BOM生成.exe /c "<输入CATPart路径>" "<输出目录路径>" [600|800|1200]
```

## 源码

- `Source/UI` 包含 Win32 界面、任务调度和原生 Excel 导出器。可在安装 Visual Studio 2012 C++ 工具链的环境下运行 `Source/UI/build.ps1`。
- `Source/Worker` 包含 CAA 几何分析和离屏渲染程序。构建需要对应版本的 CATIA CAA 开发环境。
- `Source/Export` 包含可选 Node.js 导出脚本与模板。公开便携版使用原生引擎。

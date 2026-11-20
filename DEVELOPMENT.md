# 从源码构建《魔夜读本》

## 用玩家发布包重编

在 Windows x64 上准备 Python 3.12、Zig 0.13.0 和 .NET Framework 4.8。系统自带的 Windows PowerShell 5.1 用于读取安装器的托管资源。将同版玩家 ZIP 保存在源码目录外，在仓库根目录运行以下命令。示例假定 Zig 和 Python 已在 PATH 中，下载文件在仓库的上一级。

```powershell
.\build-package.ps1 -ZigPath (Get-Command zig.exe).Source -Python (Get-Command python.exe).Source -PlayerZip '..\魔夜读本-Windows全游戏版.zip'
```

脚本先从 ZIP 取出剧情数据、词索引、字体和词典，按 `corpus/release-data.json` 逐个核对 SHA-256。随后从本仓库的 C++ 和 C# 源码编译游戏内读本、学习窗口和图形安装器。此方式可以在没有安装游戏、EBWin4 或 Sudachi 的电脑上构建。最终玩家 ZIP 位于 `dist/魔夜读本-Windows全游戏版.zip`。

## 测试

构建命令会运行 GDI、原生数据、D3D9 离屏阅读帧及 StudyBook 回归。D3D9 测试需要 Windows 图形会话。全量数据解码覆盖 24,134 条剧情记录。读本测试覆盖查词、收藏、批注、导出和随附小学馆辞书。

测试图形安装器时，把下面的路径换成自己持有的受支持 `WoH.exe`，在 Windows PowerShell 运行。脚本把主程序复制到单独的测试目录，安装和卸载只在该副本中进行。原生安装脚本测试使用 `backups/WoH.exe` 作为同一版本的测试输入。

```powershell
.\installer-tests\Test-SetupWindow.ps1 -GameExe '游戏副本\WoH.exe'
.\installer-tests\Test-Installer.ps1
```

## 维护学习数据

只修改程序时，使用玩家发布包即可。需要重新生成语料或调整词条时，按 `corpus/manifest.json` 确认输入的数据版本。三份完整原始输入放在本机的 `corpus/steam-ja.ctd`、`corpus/sumisora-steam-zh.txt`、`corpus/anki-jlpt-n1-n2.json`。分句、读音和词卡的编辑数据在 `layout-audit/manual-plans.json` 和 `data-prep/` 中。

安装 `requirements-build.txt` 中固定版本的 Sudachi 依赖。将 Noto Serif SC Medium 放入 `assets/fonts/NotoSerifSC-Medium.ttf`，将读本内置辞书放入 `assets/dictionaries/shogakukan.zip`，将玩家包的可选词典放入 `dist/mahoyo-ja-zh-optional.zip`。这些发布数据也可从同版玩家 ZIP 的数据准备步骤取得。准备好原始输入后，在构建命令中省略 `-PlayerZip`，即可重建语料和词索引，并逐键与原表比对。

重建小学馆索引使用 `tools/pack_shogakukan.py <CATALOG路径> --ebwin <EBWin4.exe路径>`。该工具校验四个辞书原文件，保留其原始字节，再生成读本离线使用的查词索引。Tomoshi 可选包由 `tools/pack_optional_dictionary.py` 生成。学习数据随 MOD 分发，各项素材的版权及许可见[来源说明](license-research.md)。

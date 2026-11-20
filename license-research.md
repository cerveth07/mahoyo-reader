# 《魔夜读本》数据来源与发布范围

2026-11-20，项目负责人确认全量剧情原文、澄空译文、Anki 词条和小学馆《中日・日中統合辞書》完整辞书数据均可随《魔夜读本》公开分发。此范围限于随本 MOD 提供的学习数据。程序的 MIT 许可只适用于项目自写代码。

日文原文来自 TYPE-MOON《魔法使之夜》Steam 版。中文采用**澄空学园 PC 汉化的 Steam 移植文本**，收录经项目校订的译句。N1/N2 词条来自 egg rolls《JLPT N1–N5 一万词》的 Anki 牌组。各项素材保留原权利人的版权。

数据来源固定为 [WoH_CHS 文本](https://github.com/kurikomoe/WoH_CHS/blob/6328b99d37c9d9e89cb4a9fbac83f60789d599a9/patches/data/woh_chs_fixed.txt)，commit `6328b99d37c9d9e89cb4a9fbac83f60789d599a9`。署名链：澄空学园旧 PC 版 → ruje0504 / 孤松夜影 Switch 移植 → KurikoMoe Steam 移植。第一章 1116 条按原始 CTD 编号与日文精确对齐，无缺失、空译文或官中回退。

| 来源 | 查到的内容 | 对这个项目的意义 |
| --- | --- | --- |
| [重制版移植原发布公告](https://mahotsukaicn.github.io/2022/09/18/MainPost/) | 发布者说明基于旧 PC 澄空译文移植，补丁免费，转载需注明发布页；同时说明难以联系原汉化负责人，并列有禁止打包进游戏、直播录播及字体商用等限制。 | 该公告记录汉化补丁的最初发布条件。 |
| [Steam 移植项目 WoH_CHS](https://github.com/kurikomoe/WoH_CHS) | README 说明它搬运 Switch 澄空汉化，并保留游戏开发商和原汉化组的版权声明。本次查看的根目录未见 LICENSE。 | 公开仓库可以作为移植线索，不能据此认定译文或其中全部代码可按开源许可证自由再发。 |
| [2015 年澄空发布帖的 Bangumi 转载](https://bgm.tv/subject/topic/6266) | 转载保存了汉化人员表和注明出处、保留全部内容的转载要求，并指向当年的澄空原帖。 | 这是历史转载证据，尚不足以确认允许抽取、修改译文并采用新的开源许可。 |

上表保留了 2026-11-17 的公开来源核对记录。本 MOD 的分发范围以文首 2026-11-20 的项目确认记录为准。

自写代码的许可见 LICENSE。MinHook 的版权告示和许可见 [MinHook 许可](licenses/MinHook-LICENSE.txt)。玩家需自备 Steam 游戏。

剧情译文随安装包提供 Noto Serif SC Medium。字体及其随附版权告示适用 SIL Open Font License 1.1，见 [Noto Serif SC 字体许可](licenses/NotoSerifSC-OFL.txt)。Windows 已安装字体仅作为界面字体使用。

## 离线词典 · 2026-11-18

可选的 `mahoyo-ja-zh-optional.zip` 使用 Tomoshi 开放版的 JMdict 词形与简体中文义项。两张源表 `entries` 和 `zh_defs` 的许可为 CC BY-SA 4.0。加工后的词表沿用该许可，包内的说明记录了上游版本、筛选方法、作者与 EDRDG 归属，并附上上游的 LICENSE 和 NOTICE。[上游许可与来源](https://github.com/tomoshi-app/tomoshi-dict-data)

项目负责人于 2026-11-20 确认，小学馆《中日・日中統合辞書》完整辞书已获准随本 MOD 分发。安装包保留 CATALOG、LANGUAGE.ebz、WELCOME 和 ZHONG_RI/START.ebz 四个原文件，并附读本用查词索引。原辞书内容版权归小学馆及相关权利人。EBWin4 仅用于制作索引，玩家可直接用读本离线查阅释义。[小学馆的著作物利用说明](https://www.shogakukan.co.jp/picture/) · [EBWin4 使用手册](https://ebstudio.info/manual/EBWin4/EBWin4.html)

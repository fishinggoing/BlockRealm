# 方境 / BlockRealm 素材来源与许可

记录日期：2026-10-14。累计素材支出 **人民币 0 元**，预算上限 100 元。没有采购、订阅素材服务或使用来源不明的贴图、模型、音效。

## 原创方块 PBR 纹理

`Tools/generate_assets.py` 使用 NumPy 数学噪声、周期距离场及 Pillow 图形绘制，在本机生成 512 × 512 PNG。没有下载照片、扫描材质或第三方纹理库，也没有调用外部图像生成服务。

确定性种子 `42137`。周期频域噪声与环绕绘制使纹理可重复平铺；法线来自周期高度场的导数，粗糙度来自原始高度/颗粒参数。原始色彩贴图使用 sRGB，法线与粗糙度使用线性采样。

| 纹理组 | 原创表面内容 | Unreal 材质 |
| --- | --- | --- |
| Grass | 青绿草毯、细叶片、浅色草茎 | `/Game/BlockRealm/Materials/M_Grass` |
| Dirt | 褐色泥土、细颗粒、小石粒 | `/Game/BlockRealm/Materials/M_Dirt` |
| Stone | 蓝灰岩石、非规则裂隙、矿物层纹 | `/Game/BlockRealm/Materials/M_Stone` |
| Sand | 暖色细沙、浅沙丘波纹、细小颗粒 | `/Game/BlockRealm/Materials/M_Sand` |
| Wood | 深浅相间的木质纵纹 | `/Game/BlockRealm/Materials/M_Wood` |
| Leaves | 原创多层绿叶轮廓与叶脉 | `/Game/BlockRealm/Materials/M_Leaves` |
| WoodEnd | 周期年轮与径向裂纹，木块水平面使用 | `M_Wood` 的附加纹理 |
| Glass | 青色玻璃的轻微表面变化 | `/Game/BlockRealm/Materials/M_Glass` |
| Water | 青蓝湖水、两层周期波纹 | `/Game/BlockRealm/Materials/M_Water` |
| Glow | 琥珀色矿石与暖白发光裂隙 | `/Game/BlockRealm/Materials/M_Glow` |

每组包含 `RawAssets/Textures/T_<Name>_BC.png`、`T_<Name>_N.png`、`T_<Name>_R.png`，共 30 张图。文件字节哈希、分辨率和边界测量保存在 `RawAssets/asset-manifest.json`。色彩联络图为 `RawAssets/Previews/TextureContactSheet.png`，只用于制作核验，不导入游戏。

以上程序图形和材质定义专为本项目原创制作，原文件和生成器随项目保留，可随本项目使用、修改及分发，不含需另行购买授权的图像素材。费用 **0 元**。

## 原创音效

同一生成器以数学合成、滤波噪声、正弦谐波和原创稀疏短乐句生成 PCM16、48 kHz、立体声 WAV。没有录音、采样包、外部音乐作品或外部音频服务。

| 文件 | 用途 | 时长 |
| --- | --- | ---: |
| AmbientValley.wav | 轻风、溪水感噪声、鸟鸣与稀疏风铃；24 秒周期循环 | 24.00 s |
| MineSoil.wav | 草、泥、沙挖掘的短促松软颗粒声 | 0.24 s |
| MineStone.wav | 岩石与发光石挖掘的短脆响 | 0.30 s |
| MineWood.wav | 木材挖掘的温暖敲击声 | 0.28 s |
| PlaceBlock.wav | 放置方块的轻敲声 | 0.22 s |
| SelectSlot.wav | 快捷栏切换反馈 | 0.13 s |
| UIConfirm.wav | 菜单按钮确认 | 0.18 s |
| SaveConfirm.wav | 保存成功的原创上行三音 | 0.70 s |
| FootstepGrass.wav | 可选草地脚步声 | 0.16 s |

原文件在 `RawAssets/Audio/`，导入后位于 `/Game/BlockRealm/Audio/<文件主名>`。只有 AmbientValley 的 SoundWave 开启 looping。每个短音效首尾采样归零，所有文件无削波；音量、时长、采样数量与 SHA-256 在素材清单中记录。原声音随本项目使用、修改及分发。费用 **0 元**。

## 国内官方字体：小米 MiSans Regular

供应方为小米。字体及许可来自此前已取得授权的素材工作区；2026-10-13 已从小米官方网站下载并记录来源和字节哈希。本项目只更改本地文件名，复制后重新比对 SHA-256，字体内部名称、字形、表格与版权均未修改，也未做字形子集化。仓库不包含其他项目的源码或验证游戏。

| 本项目保留文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `RawAssets/Fonts/MiSans-Regular.ttf` | 8,122,324 bytes | `9c120f0a849bc0aa5048daae2a3c0f6eecd828b5b33fce682a9622833f5feea6` |
| `RawAssets/Fonts/MiSans-License-Official.pdf` | 79,535 bytes | `4a93a27cd2bd81b3b5ecfd0a853144a876fa26938a93a68443c67d74172fcb86` |
| `RawAssets/Fonts/COPYRIGHT.txt` | 原版权通知与嵌入署名说明 | 与许可一同保留 |

官方来源：

- [小米 MiSans 官网](https://hyperos.mi.com/font/zh/)
- [官方下载页](https://hyperos.mi.com/font/zh/download/)
- [官方 FAQ](https://hyperos.mi.com/font/zh/faq/)
- [官方 MiSans 字体压缩包](https://hyperos.mi.com/font-download/MiSans.zip)
- [官方完整知识产权许可](https://hyperos.mi.com/font-download/MiSans%E5%AD%97%E4%BD%93%E7%9F%A5%E8%AF%86%E4%BA%A7%E6%9D%83%E8%AE%B8%E5%8F%AF%E5%8D%8F%E8%AE%AE.pdf)

官方 FAQ 允许免费商用和应用内嵌入。完整许可第 2 条提供免许可费授权，并允许分发使用该字体制作的应用；要求在软件内标明字体来源、保留版权和许可，禁止改编字体或将字体作为独立字体商品分发。使用与分发须遵守所附完整许可，不将本页概述作为许可替代文件。

游戏内请保留署名 **“字体：MiSans（小米科技有限责任公司）”** 或 **“Font: MiSans (Xiaomi)”**。交付含字体的游戏时，把官方许可 PDF、COPYRIGHT.txt 与本来源记录复制至发行目录 `Licenses/`，与游戏一起保留。

字体内嵌版权：

> Copyright © 2020-2025 Beijing Xiaomi Mobile Software Co.,Ltd. All Rights Reserved.

字体覆盖 29,571 个 Unicode 映射点，本项目关键中文标题、操作、方块名称、数字及英文缩写的 69 个不同字符已核验，无缺字。导入字面为 `/Game/BlockRealm/UI/FF_MiSans`，运行时复合字体为 `/Game/BlockRealm/UI/F_MiSans`。价格 **0 元**。

## 成本台账

| 类别 | 来源 | 支出（人民币） |
| --- | --- | ---: |
| 30 张 PBR 纹理和材质定义 | 本机原创数学图形 | 0 |
| 9 个音效 | 本机原创数学合成 | 0 |
| MiSans Regular 与许可 | 国内小米官方免费字体，原件复用 | 0 |
| 天空、雾与灯光 | Unreal 原生组件及本项目参数 | 0 |
| 合计 | | **0** |

复现纹理和音效：在项目根目录执行 `python Tools/generate_assets.py`。官方字体是独立保留的原始授权文件，生成器不会生成或修改它。

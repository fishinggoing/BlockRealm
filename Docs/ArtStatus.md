# 方境 / BlockRealm 美术与内容状态

2026-10-14：原始素材与 Unreal 内容导入脚本已制作完成。太阳旋转轴错误与过高大气系数已修正，`EditorDaylight` 的六张正常曝光截图确认地形、贴图与方向阴影恢复可见；运行日志读取太阳为 `P=-34 Y=40 R=0`、32,000 lux、物理曝光开启。随后 `FinalNoGlobalPrecache/01_Title.png` 确认标题按钮不再显示加载状态。当前远缘仍有可见的地形范围边界与孤立叶块，现按主代理要求调整高度雾作为过渡起点。**新雾参数尚待主代理串行导入与实际截图验证，最终打包验证另行完成。**

## 已完成

- 原创 512 × 512 PBR 纹理 10 组、共 30 张；六种主要地表各具草茎、土粒、岩裂、沙粒、木纹、绿叶细节。
- WoodEnd 年轮纹理用于木块顶部与底部；Grass 使用草皮顶部、泥土侧面和绿色边缘。
- 世界坐标面向投影，常规材质每 100 cm 重复。贪心合并网格无需重新缩放 UV；长面不会把一张纹理拉伸到整个区块。法线依照面方向转入世界空间。
- Glass 是带边缘与菲涅耳变化的青色透明玻璃；Water 使用 360 cm 世界尺度的缓慢周期波纹、透明青蓝色和菲涅耳变化；Glow 是受正常光照的琥珀岩块加高亮暖色矿脉。
- 9 个原创 WAV，含 24 秒轻风、鸟鸣与风铃循环及挖掘/放置/UI/保存反馈。费用 0 元。
- 小米官方 MiSans 原始字体与完整许可复制保存，哈希与上一项目官方原件一致，关键中文缺字数为 0。
- `Tools/build_content.py` 创建 `/Game/BlockRealm/Maps/L_Wilds`，只放环境组件和 PlayerStart，体素地形由 GameMode 在运行时生成。

## 实际画面复核与交付范围

目视检查来源为 `Deliverables/Validation/EditorDaylight/01_Title.png` 至 `06_Exploration.png`，另核对后续 `FinalNoGlobalPrecache/01_Title.png`。画面是程序原创贴图配合阶梯地形与方块树冠的风格：草皮、泥土侧面、沙粒和近处叶片轮廓可辨认，常规面有明暗变化，方向阴影可见。树冠下与背光面很暗，远缘能看见重复的方块树形与部分孤立叶块。

| 截图 | 已能证实的内容 | 当前构图或证据范围 |
| --- | --- | --- |
| 01_Title | 中文标题、菜单、明亮草坡、泥土侧面、沙岸与方向阴影 | EditorDaylight 首图仍显示加载文案；后续标题图已修正。远缘过渡仍需复验 |
| 02_Mining | 可见沙粒贴图、挖空后的内壁及目标轮廓 | 镜头过近，沙面几乎占满画面 |
| 03_Building | 沙块复原后的表面、轮廓与快捷栏 | 与挖掘图视角相同，尚未展示更丰富的建筑作品 |
| 04_Pause | 中文暂停菜单、保存提示与画质选项可见 | 背景仍是近处沙面 |
| 05_Graphics | 游戏 HUD 显示当前渲染状态 | 该图实际已关闭设置菜单，不能作为画质菜单的交付截图 |
| 06_Exploration | 近处叶片纹理、右侧青蓝水面、岸边阴影 | 树叶挡住大部分景观，需主代理调整拍摄位置 |

`M_Water` 脚本包含时间驱动 UV、低幅折射与菲涅耳透明度；单张探索截图仅确认青蓝水面和可见波纹，未确认动态表现与多层透明排序。玻璃和发光石在本轮画面中没有独立近景证据，WoodEnd 年轮、各材质法线与粗糙度差异也未逐项拍摄。应按这些实际范围描述成品，不把材质图存在等同于全部视觉效果通过。

本轮图片确认中文界面与 MiSans 署名可读。截图中的 FPS 与 DLSS 标签属于该次运行状态，不能仅凭图片声称各画质模式或任意硬件性能均验证通过。

## 国内与原创素材支出

| 素材 | 来源 | 支出（人民币） |
| --- | --- | ---: |
| 30 张 512 × 512 PBR 纹理、材质定义 | 本机程序原创，保留生成器与原文件 | 0 |
| 9 个 WAV | 本机数学合成原创，保留原文件 | 0 |
| MiSans Regular、版权与完整许可 | 国内小米官方免费字体原件复用 | 0 |
| 天空、雾、灯光 | Unreal 原生组件与本项目设定 | 0 |
| 合计 | 详细来源、哈希和许可见 `Docs/AssetSources.md` | **0** |

## 地图美术设定

晨间暖阳：方向光 32,000 lux，pitch -34°、yaw 40°，轻微暖色，1.65° 光源角度，Atmosphere Sun Light 开启。太阳方向照亮西南标题镜头所见的表面。天空大气采用 Rayleigh 散射系数 .033100、Mie .003000，接近 UE 原生地球默认值；冷色实时 SkyLight 强度 1.4。新高度雾密度 **.022**、起始距离 **26 m**、最大不透明度 **.95**，体积消光倍率 .50，体积雾距离保持 **160 m**。这组参数用于远缘过渡，保留近处纹理，是待主代理导入与截图验证的保守起点；上列已检查截图仍使用旧雾参数 .008 / 48 m / .65。

后处理固定为物理手动曝光：ISO 100、1/100 s、f/5.6、曝光补偿 +0.20 stops；有效手动 EV100 约 11.4147，比原参数 12.2939 提升约 0.88 档亮度。轻微 bloom 与 vignette，关闭运动模糊。编辑器观察位置 `(-2600,-2600,5000)`，yaw 45°、pitch -24°，适合主代理在出生点上方设定标题相机。最终游戏镜头以 GameMode 中的标题/玩家相机为准。

### 本机 UE 源码核对

`SkyAtmosphereComponent.cpp` 的原生地球默认值为 `RayleighScatteringScale=.033100`、`MieScatteringScale=.003996`。这两个属性表示散射系数的量级，不是从 1.0 起算的无量纲倍率。旧脚本的 1.05/.65 分别约为默认值的 32/163 倍，使大气严重消光；此次只修环境组件与曝光，既有材质图和版本标记保持不变。

黑屏的直接运行证据来自主代理的 `EditorExposureProbe/runtime.log:2133`：太阳实际 `P=40 Y=0 R=-34`，预期则为 `P=-34 Y=40 R=0`。Unreal Python 的 `Rotator` 使用 native make 函数的 roll/pitch/yaw 参数顺序，不能沿用 C++ `FRotator` 的 pitch/yaw/roll 位置参数。`build_map.spawn` 与编辑器预览相机现使用 `unreal.Rotator(pitch=..., yaw=..., roll=...)`；导入报告的 `sun_rotation` 读取 actor 实际旋转，并声明轴顺序。`EditorDaylight/runtime.log:2130–2131` 已确认修正后的旋转、32,000 lux 与原物理曝光设定。新高度雾报告同样读取组件实际密度、起始距离、最大不透明度与体积雾距离，不以输入常量代替读数。

`PostProcessEyeAdaptation.cpp` 中手动物理相机采用 `EV100=log2(fstop² × shutter_inverse_seconds × 100 / ISO)`，曝光补偿以 `2^bias` 乘亮度。缓存预曝光 CVar `r.EyeAdaptation.CachedLightingPreExposure` 默认 4，对应安全曝光范围 `[CVar-12,CVar+8]=[-8,12]`。本项目建议主代理在 Config 的 `[ConsoleVariables]` 设为 **8**，对应 `[-4,16]`；该 CVar 调整 Lumen 和实时天光的缓存亮度范围，不代替相机曝光。

截图必须等待 `Preparing Shaders` 清零，并再给实时 SkyLight/Lumen 几秒稳定时间。首张预览仍显示 160 个准备中的着色器，不能作为完成状态下的天光最终结果。

## 导入与构建接口

生成原始资源：

```powershell
python Tools/generate_assets.py
```

编辑器目标编译成功后，主代理执行：

```powershell
./Tools/Build.ps1 -Stage Content
```

导入进程必须是完整 `UnrealEditor.exe`，使用 `-ExecutePythonScript=... -nullrhi -unattended -nop4 -nosplash`，以 `Start-Process -WindowStyle Hidden` 隐藏窗口。不得改为 Python commandlet；FontFactory 需要 Slate。

脚本从 `__file__` 查找项目根目录。启动即写入 success=false 的新报告，防止误读旧成功结果；每阶段更新 `Deliverables/Validation/content-import.json`，异常保留 traceback，成功时记录素材、材质编译错误数组、字体和地图设定。`finally` 必定调用 `request_exit()`，该函数通过 `unreal.SystemLibrary.quit_editor()` 请求延迟关闭完整编辑器。

材质图不清空、不调用 DeleteAllMaterialExpressions。成功的材质有 `BlockRealmMaterialVersion=BR.PBR.World100.v1` 元数据，重跑时保留原图；未完成的旧图只追加与连接新节点。Unreal 5.8.3 的 `MaterialEditingLibrary.recompile_material` 返回的错误数组非空即视为导入失败。

中文字体依赖主代理提供的 `unreal.BlockRealmContentLibrary.configure_chinese_font(UFont,UFontFace)`，把 F_MiSans 配置为 Runtime Composite Font、Face Inline、Regular 默认字面。

## 静态核验

已执行 Python 语法编译、30 张纹理分辨率与哈希核验、9 个 WAV 的头部/帧数/哈希/峰值检查、短音效首尾归零检查，以及 MiSans cmap 与字体原件哈希核验。预览联络图已目视检查。

纹理使用周期噪声与环绕图形，整体边界变化与普通相邻像素变化接近；木材端面改用周期径向坐标，避免法线在平铺接缝反转。所有 WAV 均无削波，AmbientValley 循环边界最大差 14 个 PCM16 单位，其余音效边界为零。

## 主代理最终核验

以实际 content-import.json 和运行截图确认全部 9 个材质、地图、字体、音效正常导入，检查首次进入游戏曝光、草侧面、透明水/玻璃排序与发光石 bloom；在正常 DX12/SM6 GPU 环境确认 Shader 编译成功。`-nullrhi` 内容导入提供资产与脚本验证，不能代替 GPU 画面验证。

含字体的打包交付应附 `Licenses/MiSans-License-Official.pdf`、`COPYRIGHT.txt`、`AssetSources.md`，游戏 UI 应显示 MiSans 署名。完整来源与支出见 `Docs/AssetSources.md`。

## 最终版本画面复核：ReleaseFinal1080

2026-10-14：已逐张目视检查 `Deliverables/Validation/ReleaseFinal1080/01_Title.png` 至 `06_Exploration.png`，均为 1920 × 1080。对应 `runtime.json` 为 `success=true`，60 项通过、0 项失败；报告确认 `DLSS Quality · 66.7%`、DLSS CVar 开启、Reflex 开启、帧生成关闭。该次默认启动日志确认 `r.RayTracing=1`、`r.Lumen.HardwareRayTracing=1`、太阳 32,000 lux、`P=-34 Y=40 R=0`，以及 ISO 100、1/100 s、f/5.6、bias +.20 的物理曝光。

| 最终截图 | 实际可见结果 |
| --- | --- |
| 01_Title | 正常“开始旅程”按钮与就绪提示；完整的近中景草坡、泥土侧面、方块树冠和沙岸；此构图没有上一轮明显的孤立叶块或未完成近景 |
| 02_Mining | 草皮细纹理、挖空后的泥土内壁、目标轮廓、木材纵纹及背景坡面；前景距离足够，不再被单一沙面占满 |
| 03_Building | 独立放置的草块可同时看见草顶、泥土侧面和绿色边缘；与挖掘图的地形变化可辨认 |
| 04_Pause | 完整中文暂停菜单、保存提示和 DLSS 质量选中状态；草地背景可辨认，未在截图前关闭菜单 |
| 05_Graphics | 完整画质选项与 DLSS 质量选中状态，菜单保留在截图中；上一轮抓到游戏 HUD 的时序问题已修正 |
| 06_Exploration | 宽阔沙岸、青蓝水面、浅水处可见的沙地轮廓、远处阶梯山丘与树林；镜头已离开近处树叶，能够观察景观整体 |

新高度雾参数 .022 / 26 m / .95 与扩大的视距已随最终版本进行截图复核，体积雾距离保持 160 m。近处草、泥、沙纹理没有被明显白雾覆盖；探索图可见天空与远景的蓝灰过渡。远地平线仍呈较平直的蓝色过渡，树冠下与背光面仍很暗；这些图仅证明所拍镜头中的实际表现，不据此声称任意视角均没有加载范围边界。

本轮目视验收证实以上可见贴图、方向阴影、水面、中文界面及菜单状态。**玻璃与 Glow 发光石没有专门近景验证**；木材端面年轮、动态水纹随时间的表现、多层透明排序以及各材质法线与粗糙度的独立差异，也未在这六张静态图中逐项验证。运行检查通过与上述视觉范围分别据实记录。

素材来源与支出保持不变：本机原创 30 张 PBR 纹理、9 个原创合成 WAV、国内小米官方 MiSans 原件及 Unreal 原生环境组件，累计支出 **人民币 0 元**。本节为最终版本记录，前文“待主代理导入与截图验证”和旧截图构图问题属于此前阶段。

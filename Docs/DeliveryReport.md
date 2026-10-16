# 方境 / BlockRealm 交付与验证状态

**2026-10-15 最新 Git / ZIP 交付见 [ReleaseV11](ReleaseV11.md)，本次新 ZIP 包含当前正式存档及单独的傍晚洞穴示例。** 下文未更新的 ZIP 状态属于历史记录。仓库不包含其他项目或独立验证项目。

**最新 V11 状态以 [V11 实测记录](V11Validation.md) 为准，操作见 [V11 指南](V11Guide.md)。** 本页以下 V10e 构建、229 项检查和角色补光参数是历史记录。V11 已更换像素材质、背包布局，增加手持、摔伤和生物，并取消角色随身灯，不能用 V10e 光影结果替代新包验证。

2026-10-15 更新。UE **5.8.3** 原创单人方块探索建造原型；素材支出 **0 元**，使用国内小米 MiSans 与本项目原创纹理 / 声音。当前 `ExpandedV10e_1080` 为 **229/229 通过、0 失败、20 张 1080p 图**；锁角光影脚本 **21/21**、SkyOff 对照 **2/2** 通过，氧气、炉口燃烧和完整床的关键截图已审核。采用硬件 Lumen Surface Cache 兼容路径，真正 Hit Lighting 首帧崩溃与综合运行中的 VSM 告警仍未解决。本轮没有新 ZIP。

## 交付入口

- 独立游戏：`Deliverables/Windows/BlockRealm.exe`。保留完整 Windows 文件夹，不能只复制一个 EXE；不需要安装 Unreal Editor。若缺少 Visual C++ 运行库，安装目录附带 `Engine/Extras/Redist/en-us/vc_redist.x64.exe`。
- 当前为 Windows 64 位 Development、DirectX 12 / Shader Model 6。现有 `Deliverables/BlockRealm-Windows.zip` 与同名 `.verification.json` 属于早期基线，不能代表 V9d 或之后的源码和游戏目录。
- 工程与源码：`BlockRealm.uproject`，源码在 `Source/`，原始素材在 `RawAssets/`，导入资产在 `Content/`；可迁移的 MCP Bridge 源码在 `Plugins/UE_MCP_Bridge/`。DLSS 等第三方插件按 README 安装。
- V10e 构建与综合证据：`Deliverables/Validation/ExpandedV10eBuild/`、`ExpandedV10e_1080/`；锁角光影 `LightingV10e/`，有效 SkyOff 对照 `LightingV10eSkyOff/`。Content 未重新导入，复用 `ExpandedV10dBuild/content-import.json` 的九个 v3 材质 / MPC 1000 / 300 成功结果；旧目录全部保留。

当前交付为 `Deliverables/Windows/` 完整目录，已随 V10e 构建更新。

## V10e 构建与综合运行

`Deliverables/Validation/ExpandedV10eBuild/editor-build.log` 为 `Result: Succeeded`；`package-build.log` 为 `BUILD SUCCESSFUL` / `ExitCode=0`，BuildCookRun **177.92 秒**，完成 Cook / Stage / Archive。Content 没有重导：复用 `ExpandedV10dBuild/content-import.json`，该报告为 `success=true`，九个 `BR.PBR.World100.v3` 材质的 `compiler_errors` 均为空，`MPC_VoxelSky` 为白天 1000 / 夜间 300。

`ExpandedV10e_1080/runtime.json` 为 `success=true`、**229/229 通过、0 失败**，生成 **20 张 1920×1080 截图**，完整流程 **117.59 秒**；日志为 `BR_EXPANSION elapsed=32.85 seconds stages=53`。实际步行 520、水中 220 cm/s，徒手岩石 1.1 秒；回读 **DLSS Quality 66.7%、Reflex 开、帧生成关**。**1,503 帧**短采样平均 **119.98 FPS**、P95 **8.34 ms**，限帧 120，不能推断剩余性能余量，也不代表长期压力或稳定性测试。

当前配置显式选择 **硬件 Lumen Surface Cache 兼容路径**并保留 DLSS。运行探针检查 RHI 光追能力、真正运行时 RT、视图 RT 许可、GI show flags、最终混合视图的 Lumen GI / Ray Lighting 模式及相关 CVar，兼容分支必须满足 `HitLighting.Allowed=0` 和最终 Surface Cache；不能只凭配置或硬件支持标记宣称实际生效。MPC 还检查 cooked 资源、每世界实例和有限范围参数。

水下呼吸已接入 `BlockRealmPlayerActions` 与 HUD：相机所在权威体素为 Water 时消耗 15 秒氧气，只有脚部涉水不消耗；露头约 3 秒完全恢复，耗尽后每秒 2 点溺水伤害。E 背包与 Esc 暂停冻结氧气和溺水计时；呼吸条、剩余秒数与低氧 / 溺水警告已接线。Reset、读档与重生将氧气和浸水标记重置，氧气及溺水计时不写入 JSON。V10e 保持跨 Tick 采样基准修正后的真实 Tick 检查通过。

当前源码 / Content 状态如下：

| 部位 | V10e 当前状态 |
| --- | --- |
| Lumen | 硬件光追与 Lumen GI 保留，显式 Surface Cache 兼容模式；真正 Hit Lighting 没有通过。 |
| 环境 / 角色补光 | 全局无阴影 `AmbientFillLight` 已删除；洞穴投影点光强度 6 / 半径 2000 cm，洞口 1.3；室外投影点光白天 0.25 / 夜间 1.2 / 半径 1800 cm，约 0.18 秒平滑时间常数。 |
| 辉光石、火把 / 熔炉 | 使用有限范围投影局部光；矿脉锚点移到暴露面的空气侧。实际遮蔽和性能需结合固定镜头 / 运行判断。 |
| 水 / 玻璃 / 树叶 | 双面世界法线修复已导入九个 v3 材质，编译错误为空；透明排序仍依赖 UE。 |
| 体素天空补偿 | Grass / Dirt / Stone / Sand / Wood / Leaves 六材质使用逐列遮蔽天空量；每世界 MPC 白天 1000、夜间 300，并平滑混合颜色。它是表面 / 环境补偿，不是 Lumen GI。 |

天空遮蔽从含相邻区块 halo 的纯数据快照按全高竖列计算，木材 / 岩石等实体阻断，Leaves 透过率 0.75、Glass 1、Water 0.9。可见面相邻格的天空量量化到 0..15，写入顶点 R，体素 A=0；默认静态 Engine Cube 顶点 A=1 不启用补偿，避免共享材质使设施自行变亮。不同天空级别分开贪心合并。V10d 起碰撞生成独立为同一块快照上的 solid-only 贪心过程，不让材质或天空分档改变碰撞三角形；自检比较完整网格与专用碰撞网格逐项一致。天空测试按整数级别回读，避免快速浮点运算的末位差误报。

设施 HUD 在 600 cm 内显示作物 / 熔炉 / 床的名称、状态与按键提示，查询沿用真实交互的可见外形与体素遮挡。V10d 加入并在 V10e 保持通过的三条断言为水中放置胶囊横向净空、炉口第二批烧制截图实际瞄准、休息截图实际瞄准。`19_UnderwaterBreath.png` 的低氧画面、`20_FurnaceBurning.png` 的真实第二批炉口燃烧，以及 `17_RestPoint.png` 的完整床与休息提示均已视觉审核通过；休息图无死亡提示，致命伤在后续 Tick 执行。

## V10e 固定机位光影与视觉审核

旧探针会被真实鼠标带偏视角。V10e 仅在 GameMode 光影探针进入时单次锁定移动 / 观察输入，避免 Ignore API 堆栈逐 Tick 增长；清空旋转输入，每 Tick 重设目标角，并将 yaw / pitch 偏差均不超过 0.2° 纳入 `success`，普通游戏输入不改。

`LightingV10e/` 原脚本 **21/21 PASS**，覆盖洞穴多方向 / 禁灯、地表 / 禁灯、洞口、夜间、黎明、黄昏及木 / 石 / 玻璃 / 树叶屋顶。全部 JSON 为 `success=true`、`probe_view_locked=true`、`pending_chunks=0`，实际 `render_path=hwrt_surface_cache_compatibility`。`LightingV10eSkyOff/` 的 SurfaceLighting 与 RoofWoodNoFillLighting 原脚本各通过一次，合计 **2/2 PASS**；与正常图匹配相机、sky=0 和玩家灯状态，未因 Fatal 或 VSM 触发脚本失败。这不等于综合运行日志没有 VSM。

光影视觉结论保持：近洞可导航，远洞仍黑；晨昏前景偏暗，夜间树冠与土墙远景偏暗。有效禁灯 / SkyOff 对照只适用于对应固定机位、夹具和裁剪，不能外推到任意建筑，不能称全面清晰或零漏光。

V10e `runtime.log` 仍有 **`[VSM] Non-Nanite Marking Job Queue overflow`**；历史关闭 coarse 页标记的试验也未消除，当前仍未修复，不能称全部日志干净。

## V10d 综合与失败对照（历史记录）

`ExpandedV10d_1080` 曾为 **229/229 通过、0 失败、20 张 1920×1080 图**，总计 **117.68 秒**、扩展 **32.92 秒 / 53 阶段**；1,501 帧平均 **119.93 FPS**、P95 **8.34 ms**。这些是旧包短采样，不与 V10e 数据混用；V10d 成功的九个 v3 材质 / MPC 1000 / 300 导入报告继续由 V10e 复用。

`LightingV10d` 正常 21 场景脚本全部通过，但旧探针没有锁角。两个 `LightingV10dSkyOff` 不能记为通过：Surface 的进程 / JSON / 截图正常，仍因 VSM 告警被脚本判失败；RoofWoodNoFill 实际 yaw 181.018° / pitch 15.996°，偏离要求的 180° / −8°，机位门禁失败。原 JSON 均为 `success=true`，不代表外部脚本验收通过；这些旧 SkyOff 图不用于同镜头遮光或因果比较，旧证据全部保留。

## V10c 候选诊断（未通过）

`ExpandedV10cCoarse0Candidate` 和 `ExpandedV10cMip1Candidate` 均未通过：水天空量的运行浮点值与常量约差 5.96×10⁻⁸，超出原 10⁻⁸ 容差；视觉天空分档还改变了固体碰撞三角形细分，连带世界自检失败。Mip1 候选另有实体替换水断言失败。这些不是最终通过记录。V10d 已改为整数天空级别验证并独立生成 solid-only 碰撞，相关世界自检和水中放置回归已通过；VSM 告警仍存在。

历史候选固定机位比较用于选择当前天空默认值：白天 1000 的树荫 ROI 平均值由关闭补偿时的 86 提高到 99.02，暗像素（≤16）由 17.88% 降到 5.77%；夜间 300 的候选 ROI 纯黑由 28.21% 降到 4.21%，但暗像素仍约 60%，不能称夜景全面清晰。木屋禁角色灯的同机位天空 0 / 400 / 1000 对照 ROI 均约 0.0006、MAE 约 0.000973，近似不变；这仅支持该夹具与该裁剪，不能证明通用零漏光或替代 V10e 锁角对照。

## V10 / V10b 诊断与中间回归（历史构建）

V10 首次强制 Hit Lighting 在 RTX 4060 Laptop、NVIDIA 566.24 上创建首帧 RTPSO collection 时崩溃。单独尝试 SER=0、EnableMaterials=0、PersistentSBT=0、AsyncCompile=0 或 onethread 均未绕开；只有关闭 `HitLighting.Allowed` 可运行。`LightingV10HitOff/runtime.json` 的 `hit_lighting_allowed=0`、`hit_lighting_requirements_met=false`，唯一失败项为 Hit Lighting 要求，因此 `success=false`；它是诊断证据。底层原因尚不能归结为单一驱动或引擎问题。

V10b 四场景试运行 `LightingV10bPilot` 全部成功，但洞穴 3 / 1400 cm 偏暗。`LightingV10bBrightPilot` 的 6 / 2000 cm 对照给出洞穴 ROI 平均 49.29、暗像素（≤16）32.57%、纯黑 19.71%，木屋未过曝；当前默认洞穴 6 / 2000 cm、洞口 1.3 来源于此轮调节。这些局部结果不代替 V10e 的全部场景。

`ExpandedV10b_1080/runtime.json` 为 **226/226 通过、0 失败**、**19 张 1920×1080 截图**，完整流程 **117.62 秒**；扩展阶段日志为 **32.63 秒 / 53 阶段**。新增水下检查经历真实 15 秒耗尽、露头恢复、E / Esc 冻结、伤害与重置，不能继续套用初版生产流程“约十秒”。回读 DLSS Quality 66.7%、Reflex 开、帧生成关；1,275 帧短采样平均 **101.86 FPS**、P95 **18.47 ms**。该中间包已被当前 V10e 替换，数据不作为当前包性能保证，也不代表长期压力测试。

## 已集成内容与操作

确定性地形、树木、水域、地下洞穴与辉光石矿脉；100 cm 方块、16×16×64 区块、异步贪心网格与碰撞、按玩家位置加载 / 卸载。默认视距半径 5，请求 121 个区块；进入游戏后的滞后保留上限 169，准备阶段分离的初始固定区域最多另有 9 个区块。

步行、跳跃、加速、飞行、按材质计时的持续挖掘、连续放置、水中升降与出水，八类可放置材料、九格快捷栏及库存。中文标题 / 暂停 / 图形菜单和背包配方界面；生命、饥饿、木镐 / 石镐耐久、食物恢复、玩家近战、怪物掉落拾取、火把、昼夜、农业、熔炉、床与重生点均已接入，V9d 综合运行报告覆盖这些基础闭环。

WASD 移动，鼠标观察，Space 跳跃，Shift 加速；左键按住挖掘，右键点击或按住放置，选中火把时右键安装火把；中键攻击、C 进食、E 背包与配方；Q / R 切换工具，数字 1–9 / 滚轮切换方块槽并切回方块放置；F 飞行（Space 上升 / Ctrl 下降），F5 保存，Esc 关闭背包或暂停。水中 Space 上浮、Ctrl 下潜。

| 按键 | 轻量生产与营地规则 |
| --- | --- |
| V | 瞄准草地 / 泥土上表面，消耗 1 食物种植；正常生长 120 秒，成熟后瞄准作物再按 V 收获 3 食物。 |
| T | 瞄准地面上表面，消耗 8 岩石建熔炉；瞄准炉子投入 1 沙块 + 1 木材，8 秒烧成 1 玻璃，再按 T 领取。领取成品的同次按键不补料。 |
| B | 消耗 12 木板在连续两格空地上建床，朝向跟随玩家水平视角；安全时瞄准床设置重生点。夜间休息跳至清晨并恢复 6 生命，玩家或床附近 8 米内有活怪物时拒绝休息。 |

背包与暂停菜单冻结怪物、昼夜、作物与熔炉。床失效或床旁没有安全空间时，死亡重生回落世界出生点。农业、熔炉与床属于简化实现；床和炉失去支撑后的材料回收、更多作物和烧制配方尚未实现。

F5、返回标题和菜单退出保存；写入采用同目录临时文件与原子替换，坏档留在标题并保留原件，保存失败保留会话。version 1 JSON 保留世界修改、玩家位置 / 视角 / 九格库存 / 选槽，以可选 `survival`、`placedTorches`、`placedTorchSupports`、`crops`、`furnaces`、`restPoints`、`respawnPoint` 保存生存与扩展状态；旧档缺失新增字段时采用默认值。V9d 已覆盖 F5 写盘、暂停下状态比较、返回标题清理和同一进程继续恢复，尚未提供关闭游戏进程后逐字段比较的完整证据。飞行请求、背包开关、瞬时速度、动作进度及 V10 氧气 / 溺水计时不保存。

素材继续使用原创 30 张 512² PBR 纹理、九个合成音效与官方 MiSans。V9d 的 51 个导入资产记录保留；V10d 导入在其基础上增加天空 MPC，九个 v3 材质均无编译错误。动态太阳、天空、天光、高度 / 体积雾、虚拟阴影与硬件 Lumen 保留，体素天空表面补偿另行标明；官方 DLSS 超分辨率提供质量、平衡、性能三档，Reflex 在支持时开启。帧生成、多帧生成和光线重建关闭。

## V9d 综合运行验证（历史基线）

参考设备：RTX 4060 Laptop 8 GB、NVIDIA 驱动 566.24、约 16 GB 系统内存；MSVC 14.44。V9d 当时已完成 Editor 构建和 Windows Development 打包，入口为 `Deliverables/Windows/BlockRealm.exe`；以下结果不证明正在制作的 V10 新包。

`./Tools/Validate.ps1 -FreshWorld -OutputName ExpandedV9d_1080` 的 `runtime.json` 为 **202/202 通过、0 失败**，完整流程约 91.58 秒，生成 **18 张 1920×1080 截图**；结束回读 **DLSS Quality 66.7%、Reflex 开、帧生成关**。1,492 帧短采样平均 **119.22 FPS**、P95 **8.74 ms**，限帧 120。采样属于回归流程，不能当作长期压力测试、固定镜头三档对比或完整游戏性能保证。

覆盖实际引擎输入和 HUD hitbox 的开始 / 暂停 / 继续、放置与库存、跳跃脚下搭建、飞行与重力恢复、水中升降 / 填水 / 出水、计时挖掘与取消、远处流式碰撞 / 卸载、背包门控、战斗 / 食物 / 掉落、火把放置 / 回收、农业、熔炉、床与安全重生、昼夜冻结及扩展存档。战斗使用固定生成的真实活怪物，夜间截图关闭自然生成，未长期观察自然刷怪分布。作物与熔炉的生产断言使用缩短周期等待真实 Tick；普通 120 秒 / 8 秒的状态捕获和恢复另有覆盖，不代表已完成长时间连续生产测试。

关闭背包后的动作门控已修复：菜单点击已释放时立即允许重新移动、挖掘与放置；鼠标仍按住时等待释放，避免菜单点击误触世界操作。新增存档检查在同一游戏进程中执行，不将返回标题再继续称为跨进程验证。

## V9d 光影记录（LightingV9 参数时间点）

五组 `runtime.json` 均为 `success=true`、`pending_chunks=0`，图形回读均为 DLSS Quality 66.7%、Reflex 开、帧生成关：

| 固定场景 | 报告中的状态 |
| --- | --- |
| cave，`X=-34 Y=-128 Z=15` | 洞穴填光开，室外填光关；截图可辨墙面、台阶和顶部结构。 |
| surface | 洞穴填光关，室外填光开，室外点光强度约 1.2；太阳 32000 lux。 |
| night / dawn / dusk | 洞穴填光关，室外填光开，室外点光强度 5；太阳 7000 lux。 |

当时角色洞穴点光无阴影、Unitless 强度 6.0、半径 9800 cm、衰减指数 0.95，洞口降至 2.1；Lumen 天光漏光为 0.010、距离 2400 cm。昼夜 Actor 的无阴影环境方向光白天 2200、夜间 850，用于提高背光区域可见性。五张截图证明这些位置和时刻的可见性与状态切换，仍可见部分很暗的树冠凹缝。全局无阴影方向光和局部无阴影点光可能穿透实体屋顶 / 墙体，以上报告没有封闭建筑遮蔽、漏光或全时段过渡的专项断言，不能据此宣称光影问题已全部解决。

## 历史验证（不外推为 V9d 新功能证据）

以下构建、Cook、60 项检查与性能数据属于 **2026-10-14 基线**：Editor 和 Windows Development 构建成功，Cook 为 0 errors / 0 warnings，日志为 `package-verified-console.log`。

| 历史独立运行 | 输出与检查 | 实际渲染 |
| --- | --- | --- |
| ReleaseFinal1080，默认参数 | 1920×1080；60 通过 / 0 失败；六张截图 | DLSS Quality，RT 与硬件 Lumen 保留。 |
| ReleaseTSRFallback，启动期 `r.NGX.Enable=0` | 1920×1080；60 通过 / 0 失败；六张截图 | 官方 DLSS-SR 不可用，原生 TSR 100%，RT 保留。 |

历史运行通过真实 HUD hitbox 和引擎输入检查开始、挖放、库存、F5 后保持 Lit、暂停冻结 / 恢复飞行、图形按钮、远处地形碰撞、旧区块卸载、保留范围及返回标题继续。世界数据自检检查负坐标、边界编辑、序列化、原子保存与非法输入；坏档检查使用隔离路径。DLSS 日志实际创建 NGX feature，输出均为 1920×1080：

| 历史模式 | 回读比例 | 实际输入 |
| --- | --- | --- |
| Quality | 66.7% | 1281×721 |
| Balanced | 58.0% | 1114×627 |
| Performance | 50.0% | 960×540 |
| TSR | 100% | 原生输出分辨率；AA=4。 |

历史默认运行短采样 1,504 帧，平均约 **120.01 FPS**、P95 约 **8.33 ms**；真正禁用 DLSS 的 TSR 运行短采样 1,169 帧，平均约 **93.32 FPS**、P95 约 **11.66 ms**。帧率上限 120，旧黑画面与 ShaderComplexity 的帧率没有用于这些结论。

历史交互修复 `InteractionFixV2_1080` 为 **97 项通过 / 0 失败**，已覆盖跳跃搭建、飞行、水中移动和计时挖掘。`CaveLighting_V6/UndergroundLighting.png` 是 V6 地下可见性记录。这些历史报告继续保留；生存与生产的新增证据始于 V9d，当前运行结果见 V10e。

## 兼容处理及验证边界

独立包在参考设备的默认全局 PSO 预热路径曾启动崩溃。配置使用 `r.PSOPrecache.GlobalShaders=0` 后完成运行验收；RT 和硬件 Lumen 保留，正常按需创建 PSO 与材质 / 组件预缓存保留。该兼容绕过的底层驱动 / 着色器根因没有完全定位，首次遇到某些效果仍可能卡顿，详细历史证据见 `Docs/DLSSIntegration.md`。

当前仍是单人原型。尚无红石、流体模拟、联机、其他维度、工作台、完整物品 / 生物体系和 Minecraft 的全部内容与规则；不能按已完成其大部分玩法交付。世界高度固定 64 方块，矿物复用 Glow，五个基础配方在背包内直接执行。

自动检查通过合成引擎输入执行，不能替代真实桌面鼠标捕获、Alt-Tab、窗口缩放和全部按键的系统性实机测试。持续高速跨区块、100,000 编辑、大量点光 / 玻璃排序、内存峰值、磁盘满 / 断电、Shipping、第二台机器及跨进程逐字段恢复仍未专项验证。

## 复现与许可

沿用现有 PowerShell 7 工具，在项目根目录执行 `./Tools/Build.ps1 -Stage All`；独立检查为 `./Tools/Validate.ps1 -FreshWorld -OutputName MyRun`。`./Tools/Pack-Portable.ps1` 是既有压缩核验工具，本轮没有运行它生成新 ZIP。构建 / 编辑器 / 游戏验证串行，编译并行最多两个 action；不修改 MCP，不新增外部工作流。既有分工使用 **gpt-6.1-sol / max**，主代理负责集成和最终验证。

正式存档在 Windows 目录内 `BlockRealm/Saved/Worlds/BlockRealmWorld.json`；迁移前请备份。本轮运行前后 SHA-256 均为 `81392A380234E6E3AC7B663CEF6E344592EDA46DE6333762D7181857DA9C8E38`，仅证明隔离验证未改变该正式文件，不证明跨进程逐字段恢复。`-BlockRealmSmoke` 使用独立 `Saved/ValidationWorld`，光影探针使用 `Saved/LightingProbeWorld`。历史便携包排除全部运行 Saved 与 PDB，本轮未重新压缩。当前目录附带 Visual C++ 运行库、MiSans 官方完整许可和版权、素材来源、NGX 许可、Streamline MIT notices 与 NVIDIA RTX SDK EULA；官方插件恢复来源与 SHA-256 继续按 README 说明执行。

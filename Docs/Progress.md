# 实施状态

2026-10-15：按用户最新要求提交当前 Git 工作并生成含当前存档的 V11 ZIP，傍晚洞穴示例另附；交付清单、核验方式和已有验证范围见 [V11 Git 与 ZIP 交付](ReleaseV11.md)。

2026-10-15 V11 最新实现、用户纠正及最终验证见 [V11 实测记录](V11Validation.md)。背包按槽位与悬停说明调整，角色随身补光取消；下文保留各历史版本的过程。

2026-10-14：用户授权开始。第一版为原创高画质方块沙盒《方境 / BlockRealm》。以下第一版条目记录历史基线，2026-10-15 当前集成状态见文末。

- 环境：UE 5.8.3、MSVC 14.44、16 GB RAM、RTX 4060 Laptop。
- 已有：已验证的旧项目构建与打包方法、官方 MiSans、DLSS 4.5 UE5.8 插件 v8.8.0。
- 已完成：Git初始化与bootstrap提交4235e7b；世界、玩家/HUD、美术和DLSS四个工作包；首次Editor编译（98 actions）通过。
- 主代理修正：存档加载等待碰撞、暂停冻结并恢复移动模式、菜单输入堆栈重置、保存失败保留会话。已加入隔离运行检查及六张截图。
- 已验证：首个独立Windows包构建成功；1080p的DLSS Quality/Balanced/Performance确有NGX feature创建与对应输入尺寸，FG保持关闭；隔离玩法与存档检查可运行。
- 集成修正：F5清除UE调试绑定；ForceRes保证真实1080p；暂停重复保存/失败提示；坏存档在标题拒绝加载且保留原件；更远距离流式检查加入目的地碰撞和旧区块卸载。
- 画面修正：运行日志证实Python Rotator位置参数顺序导致太阳实际P=40/Y=0/R=-34、位于地平线下；已改为命名轴参数，重新导入实际P=-34/Y=40/R=0，保持32000lux与物理曝光。
- 渲染兼容：独立包全局PSO预热启动失败；固化r.PSOPrecache.GlobalShaders=0后保留RT/硬件Lumen并完成实际场景，底层根因尚未完全定位。
- 视距与截图：默认半径5（121请求区块、滞后上限169）；雾密度.022、起始26m；菜单截图分tick，探索镜头等待目的地碰撞与任务完成。
- 最终独立运行：ReleaseFinal1080，1920×1080，60通过/0失败，三档真实NGX feature，Reflex开/FG关；正常Lit短采样约120.01FPS，P95 8.33ms，限帧120，非完整基准。
- 真正DLSS不可用回退：ReleaseTSRFallback启动期r.NGX.Enable=0，官方DLSS-SR=0，60通过/0失败，原生TSR100%，RT保留；约93.32FPS/P95 11.66ms短采样。
- 第一版基线制作与独立验收完成：源码、素材与插件许可已保留；当时交付为 Windows 目录、便携 ZIP 及同名 verification.json（以 success 与 SHA-256 确认压缩核验）、工程和 DeliveryReport。历史 ZIP 不含运行 Saved 或 PDB，也不代表 2026-10-15 新增源码。本轮使用 Windows 目录且没有新 ZIP；旧黑图和 ShaderComplexity 数据不用于性能结论。
- 约束：全部子代理 gpt-6.1-sol / max；不修改 MCP，不新增外部工作流；国内素材预算 ≤100 元。
- 具体契约见 Architecture.md。子代理写入各自文件，最终结果落盘。不得同时启动多个 UE 编辑器或 cook。

## 2026-10-14 交互修复验收（历史记录）

试玩反馈的四项问题已修复：权威体素 DDA 目标与局部同步碰撞解决放置失败，右键按住会在胶囊清开后重试；F 只按边沿切换且关闭后恢复步行，异常 Flying 会被纠正；水中使用独立移动模式（陆地 520、游泳 220、冲刺游泳 300 cm/s）；左键按材料耗时挖掘，石头为 1.10 秒，松开、换目标、切槽或暂停会取消并清除进度。

当时 `Deliverables/Validation/InteractionFixV2_1080/` 的独立包回归为 **97 项通过 / 0 失败**，1920×1080、DLSS Quality、平均短采样 119.68 FPS、P95 8.34 ms；覆盖跳跃脚下搭建、F 开关和意外 Flying、水中上下移动/替换水/跳上一格高岸、长按挖掘及取消。该历史运行的正式存档前后 SHA-256 均为 `5B15371AC969781B3B669AF2BC5199E2067A6A89EC52BC55764584D29BE5CC42`，不外推为之后运行的存档哈希。自动输入仍不替代真实桌面鼠标捕获和 Alt-Tab 实机测试。

## 2026-10-15 V9d 已验证基线（历史时间点）

V9d 已增加确定性地下洞穴、辉光石矿脉、局部洞穴补光，生命 / 饥饿、木镐 / 石镐耐久、背包与五个基础配方；战斗 / 食物、怪物掉落自动拾取、火把、昼夜、农业、熔炉、床与重生点，以及完整扩展存档字段均已接入。

综合玩法证据为 `Deliverables/Validation/ExpandedV9d_1080/`：**202/202 通过、0 失败**，18 张 1920×1080 截图；DLSS Quality 66.7%、Reflex 开、帧生成关。1,492 帧短采样平均约 119.22 FPS、P95 8.74 ms，限帧 120，不代表长期压力表现。

固定镜头视觉证据为 `Deliverables/Validation/LightingV9/`：洞穴、地表、夜间、黎明、黄昏五个场景均为 `success=true`、`pending_chunks=0`。洞穴截图可辨墙面、台阶和顶部结构，地表背光可见，但部分树冠凹缝仍很暗。下述参数只描述 V9d / LightingV9 时间点：Lumen 天光漏光 0.010 / 2400 cm；角色洞穴填光 6.0 / 9800 cm / 0.95 衰减指数；洞口强度 2.1；白天 / 夜间环境方向光 2200 / 850。无阴影填光可能穿透屋顶和墙体，五组探针未验证封闭建筑遮蔽或全时段光照过渡；后续参数调整需更新构建与证据。

操作说明已同步：WASD 移动；左键按住挖掘，右键放置方块或当前选中的火把；中键攻击，C 进食，E 背包 / 合成，Q / R 切换工具，数字 1–9 / 滚轮选方块，V 种植 / 收获，T 放置 / 操作熔炉，B 建床 / 休息，F 飞行，F5 保存，Esc 关闭背包或暂停，Space 跳跃 / 上浮，Ctrl 下潜 / 飞行下降，Shift 加速。

V9d 已覆盖实际输入下的种植 / 收获、烧制 / 领取、怪物威胁阻止休息、安全床旁重生，及背包 / 暂停冻结。存档证据覆盖 F5 写盘、返回标题清理 Actor 和同一进程继续恢复生存、火把、作物、熔炉、床朝向与重生点；尚未进行关闭进程后逐字段比较。生产断言缩短周期并等待真实 Tick，正常时长为作物 120 秒、熔炉 8 秒。

当前交付仍为 `Deliverables/Windows/` 完整目录，本轮没有新 ZIP；现有 ZIP 与 verification.json 是早期基线。素材累计费用 **0 元**，既有 MiSans、原创素材及 NVIDIA 官方插件许可与恢复来源继续保留。

范围仍为单人原型，没有红石、流体模拟、联机、其他维度、完整物品与生物体系，以及 Minecraft 的全部内容与规则；农业、熔炉和床是本轮可玩的轻量闭环，不等同于完整 Minecraft 内容。

## 2026-10-15 V9d 修复记录（参数时间点）

- 光影：地下远景补光由 7600 cm / 1.10 调为 9800 cm / 0.95，并提高到 6.0；洞口使用 2.1 的低强度过渡。新增昼夜驱动的无阴影环境方向光，白天 2200、夜间 850，提高背光区域可见性，仍需检验其对封闭室内和阴影层次的影响。
- Lumen：稳定曝光体积加入 0.010 的蓝白天光漏光，完整漏光距离 2400 cm；这是一组调光参数，不能作为屋顶遮光已受控的证据。
- 交互：关闭 E 背包时按键门控按鼠标真实释放状态立即重置，解决关闭背包后一帧无法挖放的问题。
- 验证：编辑器编译、Windows Cook/Stage/Archive 成功；202 项综合 smoke、5 个固定镜头光影探针均通过；没有生成 ZIP。桌面焦点 / 鼠标捕获、跨进程存档、长时生产、大量点光及大型世界压力仍需专项验证。

## 2026-10-15 V10 / V10b 记录（历史构建）

- V10 Editor / Windows 打包成功，但真正 Hit Lighting 在 RTX 4060 Laptop / 驱动 566.24 上创建首帧 RTPSO collection 时崩溃。SER=0、EnableMaterials=0、PersistentSBT=0、AsyncCompile=0、onethread 的单项尝试均未绕开；只有关闭 `HitLighting.Allowed` 可运行。`LightingV10HitOff/runtime.json` 唯一失败项为 Hit Lighting 要求，`success=false`，不作为该功能通过证据。首轮洞穴 0.9 / 1400 cm、洞口 0.35 参数已被后续调整替换。
- V10b 实现显式硬件 Lumen Surface Cache 兼容分支，保留硬件光追 / DLSS；移除全局无阴影 Ambient 方向光，角色、矿脉、火把和熔炉使用有限范围投影光。四场景 `LightingV10bPilot` 均成功；洞穴 3 / 1400 cm 偏暗，`LightingV10bBrightPilot` 改为 6 / 2000 cm 后洞穴 ROI 平均 49.29、暗像素（≤16）32.57%、纯黑 19.71%，木屋未过曝。洞穴 6 / 2000 cm、洞口 1.3 已成为当前默认值。
- 中间包 `ExpandedV10b_1080` 实测 **226/226 通过、0 失败**，**19 张 1920×1080 图**，总计 **117.62 秒**；扩展阶段日志为 **32.63 秒 / 53 阶段**，不是原生产阶段约十秒。新增 24 项真实 Tick 检查覆盖 15 秒耗氧、露头约 3 秒恢复、耗尽后每秒 2 点伤害、E / Esc 冻结与 Reset / 读档 / 重生重置，氧气瞬态不落存档。
- V10b 回读 DLSS Quality 66.7%、Reflex 开、帧生成关；1,275 帧短采样平均 101.86 FPS、P95 18.47 ms。这是已被替换的中间包，不能作为当前 V10e 性能或长期压力结论。

## 2026-10-15 V10d 历史基线（综合通过，SkyOff 未通过）

- 新增全高体素竖列天空遮蔽：木材 / 岩石等实体阻断，Leaves 透过率 0.75、Glass 1、Water 0.9；可见面相邻格的天空量量化到 0..15，写入顶点 R，体素 A=0。不同天空级别不合并为同一个视觉贪心面；普通 Engine Cube 默认 A=1 排除补偿。
- 草地、泥土、岩石、沙、木材、树叶六材质使用天空表面补偿；每世界 `MPC_VoxelSky` 随昼夜平滑更新，白天 1000、夜间 300。它是表面 / 环境补偿，不是 Lumen GI；透明玻璃、水和 Glow 不加入该补偿。
- `RunMeshSelfTests` 已接入 `RunSaveSelfTests`，覆盖不透明屋顶、树叶 / 水 / 玻璃透过、贪心合并边界、seam / halo、一致顶点标记、移除屋顶后的新快照。V10c 候选曾因天空浮点容差和视觉分档改变碰撞细分而失败，Mip1 还出现替换水断言失败；V10d 已改整数天空级别验证，并将完整网格碰撞与即时碰撞统一为独立 solid-only 贪心过程，相关世界自检与水中放置回归已通过。
- 真实 RT、最终混合视图 / GI 和每世界 MPC 检查已增强，呼吸检查修正跨 Tick 采样基准。HUD 增加 600 cm 设施名称 / 状态提示；休息截图先保留画面、后续帧再测致命伤，新增 `20_FurnaceBurning.png`。水中放置胶囊净空、炉口烧制与休息截图实际瞄准三条新增断言均通过；该时间点关键画面尚未审核，最终审核见下文 V10e。
- `ExpandedV10dBuild/editor-build.log` 成功；fresh `content-import.json` 为 `success=true`、九个 v3 材质无编译错误、MPC 已为 1000 / 300；`package-build.log` 为 `BUILD SUCCESSFUL` / `ExitCode=0`。`ExpandedV10d_1080/runtime.json` 为 **229/229 通过、0 失败、20 张 1920×1080 图**，117.68 秒，扩展阶段 **32.92 秒 / 53 阶段**；步行 520、水中 220 cm/s、徒手石 1.1 秒。DLSS Quality 66.7%、Reflex 开、FG 关，1,501 帧平均 **119.93 FPS / P95 8.34 ms**，限帧 120，仅为短采样，不推断余量或长期稳定性。
- `LightingV10d` 正常 21 场景脚本全部通过；SkyOff Surface 因 VSM 告警失败，RoofWoodNoFill 因实际 yaw 181.018° / pitch 15.996° 偏离 180° / −8° 失败，两项虽有正常进程 / JSON / 截图也不记为脚本通过或同镜头比较。该时间点尚未完成关键画面审核与锁角补验；最终结果见下文 V10e。
- 历史候选固定机位中，白天 1000 的树荫 ROI 暗像素（≤16）由关闭补偿时的 17.88% 降到 5.77%；夜间 300 纯黑从 28.21% 降到 4.21%，但暗像素仍约 60%。木屋禁灯在天空 0 / 400 / 1000 的固定机位 ROI 近似不变，只说明该夹具，不能保证全面清晰或零漏光。V10d 综合日志仍有 VSM Non-Nanite Marking Job Queue overflow，历史关闭 coarse 标记也未消除；告警未修复，不能称日志全干净。
- 当前明确采用硬件 Lumen Surface Cache 兼容路径，真正 Hit Lighting 的上述崩溃仍是限制。历史 V9d / V10b 报告与参数继续保留；本轮没有新 ZIP、素材支出 0 元，不修改 MCP、不新增工作流。

## 2026-10-15 V10e 最终回归与锁角验收

- 真实鼠标可在旧探针等待时带偏镜头。修正仅在 GameMode 光影探针分支执行：进入时单次锁定移动 / 观察输入，避免 Ignore API 堆栈逐 Tick 增长；每 Tick 清空旋转输入并重设目标角，yaw / pitch 误差均不超过 0.2° 才成功，普通游戏输入不改。
- `ExpandedV10eBuild/editor-build.log` 为 `Result: Succeeded`；`package-build.log` 为 `BUILD SUCCESSFUL` / `ExitCode=0`，BuildCookRun 177.92 秒。Content 没有重导，复用 `ExpandedV10dBuild/content-import.json`：success=true、九个 v3 材质编译错误为空、MPC 白天 1000 / 夜间 300。
- `ExpandedV10e_1080` 实测 **229/229 通过、0 失败、20 张 1920×1080 图**，**117.59 秒**；扩展日志 **32.85 秒 / 53 阶段**。步行 520、水中 220 cm/s、徒手岩石 1.1 秒；DLSS Quality 66.7%、Reflex 开、FG 关。1,503 帧短采样平均 **119.98 FPS / P95 8.34 ms**，限帧 120，不能推断性能余量、长期压力或稳定性。
- `LightingV10e` 原脚本 **21/21 PASS**，全部 JSON success=true、probe_view_locked=true、pending_chunks=0，实际路径为 hwrt_surface_cache_compatibility。`LightingV10eSkyOff` 的 SurfaceLighting 与 RoofWoodNoFillLighting 原脚本 **2/2 PASS**，机位、sky=0 与禁灯状态匹配，未因 Fatal 或 VSM 触发脚本失败；D 的旧 SkyOff 失败证据保留，不用于同镜头因果比较。
- 新增三条水中放置 / 炉口 / 休息瞄准断言继续通过。`19_UnderwaterBreath.png` 低氧、`20_FurnaceBurning.png` 真实第二批烧制炉口，以及 `17_RestPoint.png` 完整床与休息提示均已视觉审核，床图无死亡提示。
- 光影视觉结论保持为近洞可导航、远洞仍黑，晨昏前景和夜间树冠 / 土墙远景仍偏暗；有效固定机位禁灯 / SkyOff 只说明对应夹具与裁剪，不能保证全面清晰或任意建筑零漏光。天空 1000 / 300 为表面 / 环境补偿，不是 Lumen GI。
- 综合 `runtime.log` 仍有 VSM Non-Nanite Marking Job Queue overflow，告警尚未修复，光影脚本通过不等于综合日志全干净。真正 Hit Lighting 首帧崩溃仍是限制，当前显式采用硬件 Lumen Surface Cache 兼容路径并保留 RT / DLSS。
- 正式存档前后 SHA-256 相同，详见 DeliveryReport；仅证明隔离运行未改变正式文件，不证明跨进程逐字段恢复。当前交付为 `Deliverables/Windows/` 完整目录、Development / DX12 SM6；素材支出 0 元，本轮没有新 ZIP、不修改 MCP、不新增工作流。单人农业 / 炉 / 床闭环仍不能等同于 Minecraft 大部分玩法。

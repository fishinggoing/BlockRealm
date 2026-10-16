# 方境 / BlockRealm — 第一版接口契约

V11 当前实现、范围与验证见 [V11 实测记录](V11Validation.md)；本页第一版契约及后续版本记录保留作为历史。

UE 5.8.3，Windows 单人方块建造探索。工程路径由开发者自行选择；构建工具通过参数或 `UE_ENGINE_ROOT` / `UE5_ROOT` 找到引擎。MCP Bridge 的可迁移源码和状态位置见 [MCP](MCP.md)。

## 文件所有权
- 主代理：工程配置、Build.cs/Target.cs、BlockTypes.h、BlockRealmGameMode、工具链适配、插件安装、构建、集成与交付。
- world：BlockWorld.h/.cpp、BlockChunk.h/.cpp、VoxelData.h/.cpp；不得启动 UE 或构建。
- gameplay：BlockRealmCharacter.h/.cpp、BlockRealmHUD.h/.cpp；不得编辑 GameMode、公共类型或启动 UE。
- presentation：RawAssets、Tools/generate_assets.py、Tools/build_content.py、Docs/AssetSources.md；不得运行 UE 或编辑 Source/Config。
- dlss：BlockRenderSettings.h/.cpp、Docs/DLSSIntegration.md；告知主代理需要的模块依赖，不修改 Build.cs。

试玩反馈修复轮沿用以上工程与工具：character_controls_fix 独占 Character 与新增 MovementComponent；world_interaction_fix 独占 World/Chunk/VoxelData；hud_interaction_fix 独占 HUD；movement_diagnosis 只读检查。根代理集成 GameMode、既有隔离 smoke 的交互回归、文档、构建与交付。全部为 gpt-6.1-sol / max，实施文件无交叉所有权。

## 公共数据
`EBlockType`: Air=0, Grass=1, Dirt=2, Stone=3, Sand=4, Wood=5, Leaves=6, Glass=7, Water=8, Glow=9。
方块边长 100 cm；16×16 水平、64 垂直的区块，海平面约 15。地形种子 42137，默认加载半径 5（121 区块），滞后保留距离 6；游戏运行中保留上限 169。初始碰撞仍等待 3×3。
`FWorldSaveState` 包含 Position、Rotation、SelectedSlot、Inventory（9 项，对应 Grass 到 Glow）。

## ABlockWorld（world 负责定义实现）
- `EBlockType GetBlock(const FIntVector&) const`，含未加载区块的确定性基础地形与修改查询。
- `bool TraceBlocks(const FVector& Origin, const FVector& Direction, float MaxDistance, FIntVector& OutBlock, FIntVector& OutAdjacent) const`，权威体素 DDA，跳过空气和水，返回命中块及其面邻格；玩家目标不依赖异步视觉网格。
- `bool SetBlock(const FIntVector&, EBlockType)`，更新编辑数据及边界邻块网格，禁止 Water 放置破坏碰撞。
- `static FIntVector WorldToBlock(const FVector&)`，负数向下取整；`static FVector BlockToWorld(const FIntVector&)` 返回中心。
- `FVector GetSpawnLocation() const` 返回适合站立的出生点。
- `void RequestStreaming(const FVector&)`；`bool IsInitialTerrainReady() const`。
- `int32 GetLoadedChunkCount() const`、`int32 GetPendingChunkCount() const`、`int64 GetVisibleFaceCount() const`、`int32 GetSeed() const`。
- `bool SaveWorld(const FWorldSaveState&)`、`bool LoadWorld(FWorldSaveState&)`、`FString GetSavePath() const`。
- `void ResetWorld()` 清空修改和存档，重新生成。
- `bool RunSelfTests(TArray<FString>& Failures)` 检查真实数据逻辑、负坐标、跨区块编辑、序列化等。
- BeginPlay 自动加载 `/Game/BlockRealm/Materials/M_Grass` 等 9 个材质，自动生成出生区块。区块工作后台计算，主线程创建/更新 PMC，线程任务不持有 UObject。
- `-BlockRealmSmoke` 时所有读写隔离至 Saved/ValidationWorld，保护正式存档。

## ABlockRealmCharacter（gameplay 负责定义实现）
- `void SetWorld(ABlockWorld*)`，`ABlockWorld* GetBlockWorld() const`（避免与 UObject::GetWorld 冲突）。
- `bool MineTarget()`、`bool PlaceTarget()`、`void SelectSlot(int32)`。
- `bool HasTarget() const`、`FIntVector GetTargetBlock() const`、`EBlockType GetSelectedType() const`、`int32 GetSelectedSlot() const`、`int32 GetInventoryCount(int32) const`。
- `FWorldSaveState CaptureSaveState() const`、`void RestoreSaveState(const FWorldSaveState&)`。
- WASD、鼠标视角、Space 跳跃、Shift 加速、左键挖掘、右键放置、数字 1–9、滚轮、F 飞行、Esc 暂停、F5 保存。背包初始每类 64，挖掘增加、放置减少；Water 不作为可放置物，槽位 7 的 Water 可以改为不可选并在 HUD 显示。
- 用射线检测挖放目标，禁止放进玩家胶囊。菜单时禁用动作，通过 GameMode 查询是否允许输入。
- 按住左键按材料计时；`IsMining/GetMiningProgress/GetMiningDuration` 供 HUD 绘制。右键点击/按住与短碰撞重试用于跳跃脚下建造，成功后才扣库存；允许实体方块替换水。编辑后同步更新局部碰撞，视觉仍后台更新。
- 显式飞行意图只由 F 边沿改变，关闭回重力，未请求的 Flying 自动纠正。水体多点采样与专用 Custom Movement 支持减速、上浮/下潜与跳岸，`IsInWater` 供 HUD 显示。`CancelGameplayInput` 用于菜单/失焦，`ResetGameplayState` 用于新开始/读档；暂停保留飞行意图但取消挖放。
- 正常试玩要求视口真实焦点；只有既有 `-BlockRealmSmoke` 允许后台合成引擎输入。GameMode 禁用 Development 默认作弊飞行命令。自动回归不等同于真实桌面鼠标和 Alt-Tab 验证。

## ABlockRealmGameMode（主代理实现，声明会先给出）
- `ABlockWorld* GetWorldActor() const`、`ABlockRealmCharacter* GetPlayerCharacter() const`。
- `bool IsMenuOpen() const`、`bool IsPausedMenu() const`、`bool IsAdventureActive() const`。
- `void StartAdventure()`、`void TogglePauseMenu()`、`bool SaveAdventure()`、`void ReturnToTitle()`、`void QuitAdventure()`。保存失败必须保留当前会话；读取存档后等待出生碰撞区域就绪再释放角色。
- `void SetUpscaleMode(int32)`、`int32 GetUpscaleMode() const`、`FString GetGraphicsStatus() const`、`FString GetNotice() const`。
- 模式 0=TSR、1=DLSS Quality、2=Balanced、3=Performance。默认支持时 Quality，其他硬件 TSR。

## ABlockRealmHUD（gameplay）
- Canvas 中文字体 `/Game/BlockRealm/UI/F_MiSans`，可参考旧工程字体加载和 HUD hitbox。
- 标题、开始/继续、退出；暂停页继续、保存、返回标题；图形模式切换；游戏内准星、方块目标、9 槽快捷栏、操作说明、存档提示、FPS/区块/显卡功能状态。
- 按钮 hitbox 调用 GameMode 的公共方法。

## FBlockRenderSettings（dlss）
- 纯 C++ 工具，`static bool IsDLSSSupported()`、`static void ApplyMode(int32)`、`static FString GetStatus(int32)`。
- 支持检测必须来自正式插件；不要仅依据 CVar 的存在认定 RTX 功能可用。
- 使用项目级 DLSS/Streamline 插件。默认不启用帧生成；先验证真实超分辨率。用户 GPU RTX 4060 Laptop，不支持多帧生成。

## 资源约定（presentation）
- 原创程序生成 PBR 方块材质，颜色按类型固定；草土、岩石、沙、木、叶、玻璃、水、发光石外观可辨。
- 地图 `/Game/BlockRealm/Maps/L_Wilds`：方向光、天空大气、天光、高度雾、曝光稳定、合理的后处理。动态世界由 GameMode 生成。
- 复用官方小米 MiSans 字体及许可，不采购，素材累计 0 元。
- 导入脚本输出 `Deliverables/Validation/content-import.json`，含 success；全编辑器 Python + nullrhi 运行，结尾 request_exit。
- 屏幕 1080p 可玩；材质风格为有质感的原创建造世界，避免只用纯色。

## 验证与交付
主代理串行构建、资源导入、打包。独立运行验证负坐标、编辑、背包、碰撞、区块加载卸载、存档重进与 DLSS，记录截图和帧时间。先交付完整可运行的 Development 技术样板，明确尚未完成的玩法与 Shipping/第二台机器验证。

## 2026-10-15 V10e 增量接口与本轮分工

以上第一版契约和修复轮分工保留为历史约定；本节补充已集成的生存 / 生产接口与本轮变更，不覆盖原九格方块枚举、区块尺寸或 version 1 存档。

- 生存与生产：Character 统一库存、工具与按键，`BlockRealmPlayerActions` 管攻击 / 食物 / 拾取 / 呼吸，`BlockRealmDayNight` 管昼夜，Crop / Furnace / RestPoint / Torch 为独立放置实体。GameMode 负责状态门控、实例清理 / 恢复与重生点；version 1 使用可选 `survival`、`placedTorches` / `placedTorchSupports`、`crops`、`furnaces`、`restPoints`、`respawnPoint`，旧档缺失时采用默认值。
- V10 呼吸接口：`GetBreathSeconds()`、`GetBreathPercent()`、`IsHeadUnderwater()` 供 HUD 与验证读取；相机格 Water 触发 15 秒耗氧，露头约 3 秒恢复，耗尽后每秒 2 点伤害。环境呼吸直接查询 GameMode 活动状态，E / Esc 冻结，不使用鼠标动作就绪门禁。`ResetState` 在新旅程、读档和重生时重置呼吸；氧气、浸水标记、溺水计时不加入存档。
- 当前光影接口：配置和 GameMode 后处理显式选择硬件 Lumen Surface Cache 兼容模式，保留硬件光追 / DLSS。Character 以权威体素屋顶 / 树冠采样计算洞穴、室外点光目标并平滑，公开目标强度与采样状态供探针回读；当前洞穴 6 / 2000 cm、洞口 1.3。全局无阴影 Ambient 灯移除，角色 / 矿脉 / 火把 / 熔炉使用有限范围投影光，矿脉锚点在暴露面的空气侧。Water / Glass / Leaves 双面世界法线修复随九个 v3 材质导入。
- V10d 天空接口：`FVoxelData` 在 worker 快照 / halo 的全高竖列中计算天空透过，木材 / 岩石等实体阻断、Leaves 0.75、Glass 1、Water 0.9。可见面相邻格天空量量化至 0..15 写入顶点 R，A=0 标识体素；视觉贪心合并键包含天空级别。碰撞从同一快照单独执行 solid-only 贪心过程，完整网格与即时碰撞使用相同逻辑，不受材质或天空细分影响，不增加视觉统计。Grass / Dirt / Stone / Sand / Wood / Leaves 六材质使用这项表面补偿，默认 Engine Cube A=1 排除。DayNight 加载 `/Game/BlockRealm/Materials/MPC_VoxelSky.MPC_VoxelSky` 并更新每世界集合实例，白天强度 1000、夜间 300，颜色和强度随太阳高度平滑混合；这不是 Lumen GI。
- 验证接口：`RunMeshSelfTests` 由 `RunSaveSelfTests` 调入现有世界自检，覆盖整数天空级别、透过、贪心分面、屋顶新旧快照、seam / halo，并逐项比较完整网格与专用碰撞网格。V10c 候选的浮点容差与视觉分档干扰碰撞问题已修正，相关世界自检在 V10e 继续通过。光影探针回读真实运行时 RT、视图 RT、GI show flags、最终混合 Lumen / Ray Lighting 和 MPC，每个渲染分支按显式配置验收；呼吸时间差使用已完成 Actions Tick 的采样时刻。
- HUD / 截图接口：600 cm 设施查询复用 Crop / Furnace / RestPoint 的实际外形与体素遮挡，只读取名称 / 状态。扩展 smoke 新增炉口第二批烧制截图和实际断言，休息截图保留完整画面后跨帧再测致命伤；水中放置夹具新增胶囊横向净空断言。V10e 的 19 低氧、20 炉口第二批燃烧和 17 完整床 / 休息提示均已视觉审核，床图无死亡提示；水中放置胶囊横向净空、炉口与休息实际瞄准三条新增断言继续通过。
- 本轮分工：水下呼吸工作包交付 PlayerActions、HUD 和 ExpansionSmoke 变更；根代理负责光影调整、公共接口集成、Content 核验及串行 Editor / Package / 运行验收。文档工作包独占 README、DeliveryReport、Progress、WorldStatus、GameplayStatus、PlayerActions、Architecture、ExpansionValidation；各实现文件避免重叠编辑。沿用 gpt-6.1-sol / max，不修改 MCP、不新增工作流、不自动生成 ZIP。
- 历史兼容问题：V10 真正 Hit Lighting 在 RTX 4060 Laptop / 驱动 566.24 上首帧 RTPSO collection 创建崩溃，单项开关诊断未绕开。`LightingV10HitOff` 仍因 Hit Lighting 要求而 `success=false`；当前兼容模式不等于真正 Hit Lighting 通过。
- V10e 探针修正：旧 SkyOff 已暴露真实鼠标带偏等待镜头，RoofWoodNoFill 实际角度不符要求。仅在探针进入时单次锁定输入（避免 Ignore API 堆栈逐 Tick 增长），每 Tick 清空旋转输入并重设目标角，将 yaw / pitch 差值均≤0.2° 纳入成功条件；普通游戏输入不改。`LightingV10e` 原脚本 21/21 PASS，全部 JSON success=true、probe_view_locked=true、pending_chunks=0，实际路径 hwrt_surface_cache_compatibility；`LightingV10eSkyOff` 的 SurfaceLighting 与 RoofWoodNoFillLighting 原脚本 2/2 PASS，机位、sky=0 与禁灯状态匹配。V10d 正常 21 场脚本通过，但旧 SkyOff 因 VSM / 机位门禁未通过，旧图不作同镜头因果比较，证据保留。
- 当前构建与运行：`ExpandedV10eBuild/` Editor 成功、Windows 打包 BUILD SUCCESSFUL / ExitCode=0，BuildCookRun 177.92 秒。Content 未重导，复用 `ExpandedV10dBuild/content-import.json` 的 success=true、九个 v3 材质无编译错误、MPC 1000 / 300 结果。`ExpandedV10e_1080` 实测 229/229、0 失败、20 图、117.59 秒，扩展阶段 32.85 秒 / 53 阶段；1,503 帧平均 119.98 FPS / P95 8.34 ms，DLSS Quality 66.7%、Reflex 开、FG 关，限帧 120，仅为短采样，不推断余量、长期压力或稳定性。
- 当前证据边界：近洞可导航、远洞仍黑，晨昏前景及夜间树冠 / 土墙远景偏暗；固定机位夹具与裁剪不构成通用零漏光证明。综合日志仍有 VSM Non-Nanite Marking Job Queue overflow，光影脚本通过不等于综合日志干净。历史 V10d 为 229/229、20 图、117.68 秒、扩展 32.92 秒 / 53 阶段，1,501 帧平均 119.93 FPS / P95 8.34 ms；旧版本数据保留。

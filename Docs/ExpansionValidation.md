# 扩展玩法运行验收

2026-10-15 更新。`BlockRealmExpansionSmoke.cpp` 在 `TickExtendedSmoke()` 完成后执行，复用 `SmokeTime`、`SmokeElapsed`、`BR_CHECK` 和最终报告。仅在 `ValidationWorld` 隔离存档目录与活动游戏状态中启用。当前包含生产 / 营地与水下呼吸 **53 个阶段**，有独立 **55 秒**上限，并保留总 harness 超时；地面、标题地形、继续游戏各最多额外等 5 秒，短时生长 / 烧制就绪最多等 2 秒。

当前流程经历真实 15 秒氧气耗尽、约 3 秒露头恢复，以及有氧 / 耗尽时的 E / Esc 冻结等待，不能再用早期“常规约十秒”描述。当前 V10e 日志为 `BR_EXPANSION elapsed=32.85 seconds stages=53`，完整综合运行为 117.59 秒；V10d 的 32.92 秒 / 117.68 秒与 V10b 的 32.63 秒 / 117.62 秒另保留为历史记录。V10e 综合实测 229/229 通过、0 失败，生成 20 张 1080p 图；锁角光影原脚本 21/21、SkyOff 2/2 通过，氧气、炉口燃烧与完整床关键图均已视觉审核。

本说明最初随 V8 扩展工作包提交，当时只有源码与静态检查、尚未构建运行；“只新增此 cpp / 未运行”属于该历史提交状态。当前扩展已接入既有 GameMode、Character、Actor 和验证报告；V9d / V10b 运行记录见下文，不再把最初的未验收状态作为当前结论。

## 实际接口

- Crop：`GetBlockPosition()`、`GetSupportBlock()`、`GetGrowthSeconds()`、`GetGrowthDurationSeconds()`、`IsMature()`、`IsHarvested()`、`FindAimedCrop()`、`bool SetGrowthDurationSeconds(float)`。
- Furnace：`GetCell()`、`GetInputSand()`、`GetFuelWood()`、`GetOutputGlass()`、`GetCookProgress()`、`GetCookDurationSeconds()`、`IsBurning()`、`FindAimedFurnace()`、`bool SetCookDurationSeconds(float)`。
- RestPoint：`GetBlockPosition()`、`GetHeading()`、`GetSecondBlockPosition()`、`OccupiesCell()`、`FindSafeRespawn(FVector&)`、`FindAimedRestPoint()`。
- Food 和火把：真实 PlayerActions 的 `GetFoodCount()`，角色 Capture→Restore 准备库存；纯数据 `TryAddTorches()` 检查大批添加失败原子性。
- 呼吸：真实 PlayerActions 的 `GetBreathSeconds()`、`GetBreathPercent()`、`IsHeadUnderwater()`、`ResetState()`；相机和权威 `GetBlock` 确认 Water / Air，角色 / 组件 / HUD 均持续真实 Tick。

## 覆盖

| 操作 | 运行断言 |
| --- | --- |
| V 无效种植 | 固定石平台顶面拒绝种植，食物不变。 |
| V 种植与重复 | 改为 Grass 后实际 V 产生新苗、只扣食物 1；瞄准未成熟苗拒绝收获；绕过窄植株瞄准同一土壤格，重复种植失败且不扣第二份。 |
| 真实短时生长 | 新苗来自实际 V 的默认 0 年龄；公开调短时长到 0.4 秒保留既有比例，然后等待真实 Tick 成熟。没有直接赋年龄、调用成熟状态或手工 Tick。 |
| T 放置/投料 | 顶面实际 T 只生成一座炉并扣 Stone 8；瞄准炉后实际 T 同时投入 Sand 1、Wood 1。 |
| E、Esc | 在实际界面内等待超过短生产周期；作物年龄、炉输入/输出/进度保持完全不变，炉火关闭；V/T/B 不消耗资源。退出 E 后实际 Delta 推进作阳性对照。 |
| 烧制/领取 | 调短炉周期到 0.4 秒保持比例；真实 Tick 双输入各扣 1、产 Glass 1。实际 T 完整领取，不同时补料。第二次 T 不重复产物，只在空输出时启动下一份普通 8 秒配方。 |
| 烧制截图 | 第二批正常 8 秒烧制中，从炉口侧面瞄准，实际 `IsBurning`、进度范围和 600 cm 瞄准查询通过后请求 `20_FurnaceBurning.png`，跨帧保留镜头再拍作物。 |
| V 收获 | 真正成熟后实际 V 得食物 3、移除作物；同一收获视角再次 V 不重复到账。 |
| B 建床 | 从南侧向北瞄准顶面，实际 B 创建 Heading=1 的 +Y 双格床，只扣 CraftedPlanks 12。 |
| B 威胁与睡眠 | 实际 living Mob 位于 8 m 内时不选重生点、不跳夜、不回血；移除威胁后实际 B 选择床，夜间 0.02→0.32 并回血，不重复扣木板。 |
| 休息截图 | 确认实际床仍可瞄准且晨间休息提示有效，保留完整床视角请求 `17_RestPoint.png`；下一次 Tick 再执行致命伤，避免截图帧已变为重生现场。 |
| 死亡重生 | `UGameplayStatics::ApplyDamage` 对真正角色造成致命伤，角色在床旁合法 Air+Air+实体地面的邻格恢复生命，且保存的床锚点继续有效。 |
| 火把满栏掉落 | 用真实墙面火把拆掉墙支撑，保留脚下平台；生成实际 TorchDrop。六槽已满时拾取失败，掉落与库存都保留。腾出两份容量后实际扫描只入库 1，并删除标记；之后仍有容量，重复扫描不能再到账。 |
| 大批添加原子性 | 纯工具数据只有 1 份剩余容量时请求 `TryAddTorches(64)`，失败且全部工具字段不变，覆盖内部可能先填一份后失败的路径。 |
| 单文件/继续 | 再实际 V 种一株普通 120 秒幼苗，在暂停时实际 F5。真实世界 JSON 保存年龄、炉库存/进度、床方向、选中重生锚点且只有一个文件；实际 HUD 标题清理所有扩展 Actor，再开始/继续恢复。 |
| 脚部涉水 / 头部浸水 | 搜索真实深水与开放 Air 水面，固定相机高度并暂时关闭移动组件 Tick 以隔离浮力；角色、PlayerActions、HUD 仍真实 Tick。脚在 Water、相机在 Air 时保持满 15 秒；相机进入 Water 后按每活动秒 1 秒消耗，未耗尽不扣血。 |
| E / Esc 与动作门禁 | 有氧时分别实际打开 E / Esc 等待并比较储备与生命，关闭后继续耗氧。耗尽后每次在菜单内等待超过 1 秒，生命与伤害计时冻结且退出不补扣；故意保持鼠标动作门禁关闭时，环境呼吸仍继续直到真实耗尽。 |
| 溺水 / 露头恢复 | 不缩短 15 秒储备、不直接赋氧气、不手工 Tick；耗尽后的真实 1 秒准确扣 2 生命。相机移到 Air 立即停止伤害，约 3 秒回满，脚仍在 Water；再次浸水重新耗氧。 |
| Reset / 读档 / 重生 | `ResetState` 清除浸水并恢复 15 秒，后续 Tick 重新采样；真实 `RestoreSaveState` 重置呼吸，致命 `ApplyDamage` 触发合法位置重生与满氧，后续 Tick 无残留溺水伤害。氧气瞬态不加入 JSON。 |

继续后作物年龄与炉进度允许最多 0.35 秒的正常活动推进。炉若恰好完成一份，输入、燃料、产物与进度必须符合同一个真实配方事务；暂停下磁盘比较只容许 0.0001 秒误差。所有材料、食物、数量、方向与选中床精确比较。

V10d 起呼吸时间差以两端最近一次已完成 Actions Tick 的边界为准，即 `CompletedActionsTime = SmokeElapsed - GetWorld()->GetDeltaSeconds()`；入水消耗与露头恢复使用同一基准，避免 GameMode 与 DuringPhysics 组件跨 Tick 顺序和长帧造成错误时间比较。该修正不通过缩短储备或手工推进来绕过真实时间。

## 截图

沿用 `-BRValidationDir=...`，默认 `Saved/Validation`：`15_Farming.png` 为成熟收获前场景，`16_Furnace.png` 为实际完成的炉，`17_RestPoint.png` 为成功休息后的床，`18_ExpansionContinue.png` 为继续恢复后的作物，`19_UnderwaterBreath.png` 在真实耗氧至不高于 3.5 秒时请求；`20_FurnaceBurning.png` 在真实第二批生产时从炉口侧面请求。截图跨帧保留，镜头改变 / 致命伤放在后续 Tick。V10e 相应断言继续通过；19 低氧、20 真实第二批炉口燃烧，以及 17 完整床 / 休息提示均已视觉审核，床图无死亡提示。编号不表示实际拍摄顺序。

## 具名运行证据与当前边界

| 构建 | 已确认结果 | 适用范围 |
| --- | --- | --- |
| `ExpandedV9d_1080`（历史） | 202/202 通过、0 失败，18 张 1920×1080 图。 | 生产、营地、火把与同一进程 F5 / 标题继续恢复；没有水下呼吸。 |
| `ExpandedV10b_1080`（中间旧包） | 226/226 通过、0 失败，19 张 1920×1080 图；总计 117.62 秒，扩展阶段 32.63 秒 / 53 阶段。 | 新增 24 项呼吸检查已真实运行；显式硬件 Lumen Surface Cache 兼容构建。 |
| V10c 候选（历史失败） | Coarse0 与 Mip1 均未通过世界 / 天空 / 碰撞自检，Mip1 另有实体替换水失败。 | 不作为通过证据；V10d 改整数天空级别和独立 solid-only 碰撞后，相关检查已通过。 |
| V10d（历史综合基线） | `ExpandedV10d_1080` 为 229/229 通过、0 失败，20 张 1920×1080 图；总计 117.68 秒，扩展阶段 32.92 秒 / 53 阶段。 | 跨 Tick 水下、放置胶囊净空、炉口 / 休息截图断言通过；正常 21 场光影脚本通过，但 SkyOff 因告警 / 机位未通过。 |
| V10e（当前） | `ExpandedV10e_1080` 为 229/229 通过、0 失败，20 张 1920×1080 图；总计 117.59 秒，扩展阶段 32.85 秒 / 53 阶段。 | 原 21 场锁角光影脚本与两个 SkyOff 全通过，probe_view_locked=true、pending_chunks=0；三条新增放置 / 炉口 / 休息瞄准断言通过，关键图已审核。Content 未重导，复用 V10d 九材质 / MPC 1000 / 300。 |

V10b 回读 DLSS Quality 66.7%、Reflex 开、帧生成关；1,275 帧平均 101.86 FPS、P95 18.47 ms 是被当前 V10e 替换前的短时采样，不代表当前包或长期压力表现。真正 Hit Lighting 在 RTX 4060 Laptop / 驱动 566.24 上创建首帧 RTPSO collection 时崩溃，兼容模式通过不证明该路径通过。

V10e Editor 与 Windows 打包成功，BuildCookRun 177.92 秒；Content 未重导，复用 `ExpandedV10dBuild/content-import.json` 的 `success=true`、九个 v3 材质编译错误为空、MPC 1000 / 300 结果。默认白天 1000 / 夜间 300 的全高竖列天空表面补偿不是 Lumen GI；有效禁灯比较也只对应固定机位夹具与裁剪。V10e 回读 DLSS Quality 66.7%、Reflex 开、FG 关，1,503 帧平均 119.98 FPS、P95 8.34 ms，限帧 120，仅为短采样，不推断余量、长期压力或稳定性。综合日志仍有 VSM Non-Nanite Marking Job Queue overflow；21+2 光影原脚本通过不等于全部日志干净。旧 D SkyOff 因告警 / 偏机位未通过，JSON 成功和图片存在不代表脚本门禁通过，旧图不用于同镜头因果比较。

生产断言把作物 / 熔炉周期调到 0.4 秒并等待真实 Tick，正常 120 秒 / 8 秒状态另作保存比较；这不等于长期生产压力。返回标题继续仍发生在同一进程内，尚无关闭进程后逐字段恢复的专项证据。自动输入也不替代真实桌面鼠标捕获、Alt-Tab 和全部输入设备测试。本轮没有生成新 ZIP，素材支出 0 元。

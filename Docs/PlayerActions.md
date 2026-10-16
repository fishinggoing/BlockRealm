# 玩家战斗、食物、呼吸与掉落组件

2026-10-15 更新。`UBlockRealmPlayerActionsComponent` 已由 `ABlockRealmCharacter` 创建并接入中键 / C 输入、采集回调、HUD 与世界存档。当前 `ExpandedV10e_1080` 实测 **229/229 通过、0 失败、20 张 1080p 图**，含跨 Tick 采样修正后的水下检查。锁角光影原脚本 **21/21**、SkyOff **2/2** 通过，氧气、炉口燃烧与完整床关键图已审核。采用显式硬件 Lumen Surface Cache 兼容模式，真正 Hit Lighting 与 VSM 告警仍是限制。

## 战斗、食物与拾取

- 中键调用 `TryAttack()`：从第一人称相机向控制器视线发出 350 cm 的 `ECC_Visibility` 射线，只伤害首先命中的存活 `ABlockRealmMob`；权威体素 `TraceBlocks` 再排除前方实体墙，避免尚未更新的区块碰撞造成穿墙攻击。允许的挥击立即进入 0.45 秒冷却，挥空 / 被挡同样计冷却；返回 `true` 只表示确实造成伤害。
- 徒手 / 火把伤害 4，木镐 6，石镐 9；只有实际命中的镐消耗 1 耐久。死亡广播可能同步销毁怪物，组件在 `ApplyDamage` 后不再访问该怪物指针。
- C 调用 `TryEat()`：初始 6 份食物，每次消耗 1，最多恢复 28 饥饿与 4 生命；两项均满或食物不足时不消耗。角色负责钳制生命 / 饥饿上限。
- 成功采集后，角色只调用一次 `OnBlockMined(Type)`；每累计 4 块树叶得到 1 食物，其他方块不推进计数。食物容量为 1,000,000。
- 活动 Tick 每约 0.15 秒扫描玩家中心 125 cm 内拾取物：带 `BlockRealmDrop` 且有中文岩石 / 辉光石类型标签的 Actor 各计 1 方块；`BlockRealmTorchDrop` 计 1 火把，`BlockRealmFoodDrop` 计 1 食物。未知类型留在场景中。接收库存完整入库后先删除拾取标签，再销毁 Actor，避免销毁延迟导致重复到账；容量不足时保留掉落。

## 门控与组件 Tick

攻击、进食、树叶奖励和拾取使用 `CanAct(Player)`：有效且存活的角色、背包关闭、`Character::CanUsePlayerActions()` 为真。角色进一步要求活动冒险、有效输入焦点与鼠标释放后的动作就绪状态。中键与 C 的角色包装函数只转发组件；门禁在组件入口重新检查。

呼吸有独立门禁。组件直接查询 `ABlockRealmGameMode::IsAdventureActive()`，并检查有效存活玩家、背包关闭、有效 BlockWorld 与正的有限 Tick 时间。它不依赖 `CanUsePlayerActions()` 或鼠标焦点：E 背包 / Esc 暂停冻结环境呼吸，单纯失焦或动作门禁未就绪不会冻结水下耗氧 / 溺水伤害。

`TickComponent` 先递减攻击冷却、命中反馈与拾取扫描计时，再处理呼吸，最后按动作门禁决定是否扫描拾取。呼吸的 E 冻结规则不代表组件全部计时器采用同一门禁。

`TryConsumeFood(Count)` / `TryAddFood(Count)` 是库存事务入口，只校验正数、现有数量和容量，不负责输入或菜单门控；种植、收获等调用方先完成各自的游戏状态检查。`SetFoodCount` 与 `ResetState` 用于生命周期恢复，不受玩家动作门禁限制。

## 水下呼吸（V10e 已运行验证）

头部浸水取第一人称相机位置的权威 Water 格，缺少相机组件时使用 `GetActorEyesViewPoint`，不使用身体涉水比例或材质颜色。仅脚部在水中不扣氧气；头部在水中每活动秒扣 1 秒氧气，满量 **15 秒**。氧气耗尽之后每累计 **1 秒造成 2 点溺水伤害**；跨越耗尽时只累计耗尽后的时间，不提前扣血。有限长帧以算术累计处理伤害次数，并限制伤害不超过当前生命；致命伤触发同步重生重置后不再回写旧计时。

相机露头后立即清空溺水计时，按每秒 5 秒储备的速度恢复，完全耗尽时约 **3 秒**恢复到满量。E 背包、Esc 暂停或非活动冒险不推进氧气和溺水计时。`ResetState()` 恢复 15 秒、清空伤害计时和浸水标记；新旅程、读档与死亡重生经角色重置路径调用。下一次有效 Tick 再采样当前相机格。

HUD 在头部入水或氧气未满时显示呼吸条和剩余秒数；水下剩余不高于 25% 时显示低氧警告，耗尽时显示溺水 / 上浮提示，露头恢复时显示恢复提示。呼吸秒数、浸水标记和溺水计时均为瞬态，不增加存档字段或旁路文件。

## 已接入的 Character 与 HUD 接口

角色持有 `UPROPERTY` 组件，在构造函数创建 `PlayerActions`；中键 / C 分别调用 `TryAttack()` / `TryEat()`，成功挖掘入库后调用 `OnBlockMined`。现有公开接口包括：

| Character 接口 | 当前用途 |
| --- | --- |
| `CanUsePlayerActions()` | 活动游戏、焦点、背包关闭与输入就绪判断。 |
| `NotifyPlayerAction(Notice)` | 转发中文操作提示。 |
| `AddBlockItem(Type, Count)` / `AddTorchItem(Count)` | 全量加入有效库存，成功才允许移除掉落；不接受 Air / Water 方块。 |
| `RestoreSurvivalHunger(Amount)` / `RestoreSurvivalHealth(Amount)` | 恢复生存数值并限制上限。 |
| `WearSelectedTool()` | 实际命中后消耗镐耐久。 |
| `GetPlayerActions()` | 向 HUD、玩法与隔离验证提供组件。 |

HUD 与验证从组件读取：

| 组件接口 | 含义 |
| --- | --- |
| `GetFoodCount()` | 当前可用食物。 |
| `GetHitFeedback()` / `GetHitFeedbackAlpha()` | 最近命中 / 击败提示及剩余比例，0.8 秒后清空。 |
| `GetAttackCooldownRemaining()` / `GetAttackCooldownPercent()` | 攻击剩余冷却秒数 / 比例，刚挥击为 1，可攻击为 0。 |
| `GetBreathSeconds()` / `GetBreathPercent()` | 当前 0..15 秒氧气 / 0..1 比例，V10 新增。 |
| `IsHeadUnderwater()` | 上次有效呼吸 Tick 采样的头部 Water 状态，菜单冻结时保持，重置时清除。 |

## 存档与重置

`CaptureFoodCount()` 提供唯一由本组件持久化的食物数量，Character 将其放入世界 JSON 的可选 `survival`，GameMode 沿现有单文件路径保存。读档先经 `ResetGameplayState` / `ResetState`，再用 `SetFoodCount(SavedCount)` 恢复食物；旧档缺失生存字段时保留默认 6。`SetFoodCount` 将数量钳制在 0..1,000,000。

`ResetState` 同时重置食物、未满 4 块的树叶累计、攻击冷却、命中反馈、拾取扫描和呼吸状态。氧气和溺水计时不随存档恢复，也不会继承上次潜水的伤害积压。V10e 已通过同进程存档、读档 / 致命伤重生后的呼吸重置检查；关闭游戏进程后逐字段恢复仍未专项验证。

## 验证状态与历史记录

历史 `Deliverables/Validation/ExpandedV9d_1080/runtime.json` 为 202/202 通过、18 张 1080p 截图，其中覆盖石镐三次真实攻击击杀、逐次耐久、冷却期间拒绝重复伤害、怪物 Stone / Glow 掉落只入库一次、C 进食和满值拒绝消费，以及返回标题继续后的食物恢复。扩展检查还覆盖火把满栏掉落保留、腾出容量后只拾取一次。该报告没有 V10 呼吸功能，不能据此声称水下功能通过。

`Deliverables/Validation/ExpandedV10b_1080/runtime.json` 为 **226/226 通过、0 失败**，19 张 1920×1080 图，117.62 秒；第 19 张为 `19_UnderwaterBreath.png`。新增 24 项检查通过真实 Tick 覆盖脚部涉水、相机入水耗氧、E / Esc 在有氧与耗尽状态的冻结、耗尽后伤害、露头恢复、Reset / 读档 / 致命伤重生重置，并检查鼠标动作门禁关闭时环境呼吸仍推进。扩展阶段实际为 32.63 秒 / 53 阶段，包含真实 15 秒耗尽与约 3 秒恢复，不能再写为“常规约十秒”。

V10b 的 1,275 帧平均 101.86 FPS、P95 18.47 ms 保留为中间旧包数据。当前 `ExpandedV10e_1080/runtime.json` 为 **229/229 通过、0 失败**，20 张 1920×1080 图，总计 117.59 秒，扩展阶段 32.85 秒 / 53 阶段；回读 DLSS Quality 66.7%、Reflex 开、帧生成关。1,503 帧短采样平均 119.98 FPS、P95 8.34 ms，限帧 120，不能推断性能余量、长期压力或稳定性。时间差采样基准已对齐已完成的 Actions Tick，真实水下检查通过。

设施 HUD 沿用 600 cm 实际瞄准查询显示状态。V10d 加入的水中放置胶囊横向净空、炉口烧制和休息实际瞄准断言在 V10e 继续通过；`19_UnderwaterBreath.png` 低氧、`20_FurnaceBurning.png` 真实第二批炉口燃烧，以及先截图后致命伤的 `17_RestPoint.png` 完整床与休息提示均已视觉审核，床图无死亡提示。V10e 综合日志仍有 VSM Non-Nanite Marking Job Queue overflow；正常锁角原脚本 21/21、SkyOff 2/2 通过，不等于日志全干净。V10e 仅修探针镜头锁定，普通游戏动作不改。

历史 V10d 为 229/229、20 图、117.68 秒，1,501 帧平均 119.93 FPS / P95 8.34 ms；其旧 SkyOff 因告警 / 偏机位失败，不用于同镜头因果比较。V10 首轮真正 Hit Lighting 在 RTX 4060 Laptop / 驱动 566.24 上创建首帧 RTPSO collection 时崩溃；`LightingV10HitOff` 诊断仍因该要求而 `success=false`。当前采用显式硬件 Lumen Surface Cache 兼容路径并保留 DLSS / RT，V10b 呼吸结果属于该兼容构建，不证明真正 Hit Lighting 已通过。

组件初稿阶段曾只静态核对接口、伤害提交和标签，并将接线与编译留给主代理；这是早期历史状态。当前接线已完成，后续运行结论以上述具名报告为准。自动合成引擎输入不替代真实桌面焦点、鼠标捕获或 Alt-Tab 测试。

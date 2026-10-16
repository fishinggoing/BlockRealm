# 农业模块接线说明

`BlockRealmCrop.h/.cpp` 是待移入 `Source/BlockRealm` 的正常 Unreal C++ 源码。现阶段只创建三个 Farming 草稿文件，未修改 Source、启动 UE、编译或打包。主代理新增的 `UBlockRealmPlayerActionsComponent::TryAddFood(int32)` 声明与定义已在 Source 核对；无需新增模块依赖、方块类型或掉落 UCLASS。

## 接口

```cpp
ABlockRealmCrop* Crop = ABlockRealmCrop::TryPlant(World, BlockWorld, Support, RestoredGrowthSeconds);
ABlockRealmCrop* Aimed = ABlockRealmCrop::FindAimedCrop(World, BlockWorld, EyeLocation, EyeDirection, 600.f);
bool bHarvested = Aimed && Aimed->TryHarvest(PlayerActions);
```

- `Support` 必须是有效世界范围内的 Grass 或 Dirt；上方 `Cell = Support + FIntVector(0,0,1)` 必须有效且为 Air。世界最高格不能作为支撑。草地/泥土顶面的根部坐标为 `BlockToWorld(Cell) - Up * 50cm`。
- `TryPlant` 失败返回 `nullptr`，不修改食物。成功 deferred spawn 先写入 BlockWorld、Cell、Support 与年龄，再 FinishSpawning。一个 UWorld 最多 256 株，同 Cell 的作物或火把均会拒绝。销毁中和已采收的 Actor 不占作物上限。
- `RestoredGrowthSeconds` 必须有限且在 `[0,120]`；默认新苗年龄为 0。直接未初始化生成 Actor 会在 BeginPlay 移除且不掉食物。
- `FindAimedCrop` 逐个测试当前可见茎、叶、谷穗的窄盒，选择射线最近交点，用权威 `TraceBlocks` 排除交点之前的实体。隐藏、失去支撑、已采收、销毁中或不同 BlockWorld 的作物不会返回。距离单位为厘米，6 米交互距离由角色调用方传入。
- `GetBlockPosition / GetSupportBlock / GetBlockWorld / GetGrowthSeconds / IsMature` 提供管理与存档数据。`IsHarvested` 供保存枚举过滤已领取实体。
- `TryHarvest(Actions)` 要求成熟、支撑仍有效、同 UWorld 的有效 Actions，并遵守菜单/背包冻结状态；调用 `TryAddFood(3)` 完整成功后才标记已采收、移除 Crop Tag、隐藏、停 Tick 并 Destroy。未成熟、库存不能完整添加 3 份、无效支撑等失败都保留食物和作物；重复调用不能再次领取。普通 Destroy 不生成回收掉落。

## 生长与视觉

真实游戏 Delta 累计到默认 120 秒成熟，年龄停在成熟阈值；没有墙钟或直接催熟函数。采用每帧 Tick 避免粗粒度 Tick 把暂停时间带入恢复后的生长。`GameMode::IsAdventureActive() == false` 或该模式玩家 `IsInventoryOpen()` 时生长完全冻结，不查询引擎暂停。无 BlockRealm GameMode 的隔离测试世界遵循 DayNight 行为并允许真实 Delta 生长。

四阶段为 0–40 秒幼苗、40–80 秒长叶、80–120 秒抽茎、120 秒成熟。高度约 14、30、50、81cm，植物范围保持在所在 Air 格内。用 Engine Cube 拼出竖茎、四片上翘叶与成熟后才出现的三个金色谷穗；叶/茎用 `M_Leaves`，谷穗用现有 `M_Sand`，缺失时用 `M_Wood`。无 Glow、灯光、碰撞、重叠事件、导航影响或投影。

`bool SetGrowthDurationSeconds(float Seconds)` 接受有限的 `0 < Seconds <= 120`，拒绝其他值并保持当前生长比例。调短时长不会令未成熟作物立即成熟；默认年龄上限仍不超过 120。隔离短测应以 0 年龄种植、先设置例如 1.2 秒时长，然后等待真实 Tick 成熟。调整仅用于测试配置，不加入玩家操作或存档模式。

## 角色 V 与库存

V 按下且允许游戏输入时，先以 `FindAimedCrop(...,600.f)` 检查作物。如果命中未成熟作物，提示尚未成熟并直接结束本次操作，不花食物、不向后面的土壤种植。成熟作物调用 `TryHarvest(PlayerActions)`，成功提示食物 +3；库存满失败保留作物。

未命中作物时，用现有权威方块射线确定目标/邻格，必须确认命中 Grass/Dirt 的**顶面**：`Adjacent == Support + Up`。检查食物足够后调用 `TryPlant(World,BlockWorld,Support)`；仅成功后 `TryConsumeFood(1)`。若扣除意外失败，直接销毁新苗以回滚，不能调用回收掉落。所有事务在游戏线程上执行。模块内部不从角色取食物、不修改背包。

根代理同时接入 Torch::TryPlace 拒绝 Crop 同 Cell，防止先种后放火把占位。

## 支撑破坏与回收

运行中支撑不再为 Grass/Dirt，或作物 Cell 成为实体，下一次 Tick 生成一份种子/食物退款并移除作物；水不是实体，初始 Water 格仍不能种植。支撑有效性检查在生长冻结时继续执行。世界销毁、未初始化 Actor、正常采收和标题主动清理不会额外掉食物。

回收物为单个 `AStaticMeshActor`，带 `BlockRealmFoodDrop` 和 `BlockRealmDrop` 标签，Owner 为 BlockWorld，禁碰撞/阴影/物理/重力/Tick，300 秒寿命。附近存在 Air 格时选可见空格并直接落在权威地面上；完全填满原列时放在世界最高实体面上。掉落生成过程不修改任何 voxel。

主代理在现有 `PickUpNearbyDrops` 增加食物分支：同样的拾取距离/允许交互条件下调用 `TryAddFood(1)`；完整成功才先移除 FoodDrop 与 Drop 两个标签，再 Destroy，保证单次到账。容量满时保留掉落供稍后拾取。

## 存档与标题

保存前枚举 `ABlockRealmCrop`，过滤有效、非销毁中、`!IsHarvested()` 且 `GetBlockWorld()==WorldActor` 的实体。独立可选 `Crops` 数组保存 Cell + GrowthSeconds，最大 256 条；校验坐标在有效范围、Cell.Z>0、无重复、年龄有限且在 `[0,120]`。缺失数组按空处理，继续兼容旧存档。

恢复时逐条 `TryPlant(World,WorldActor,Saved.Cell-Up,Saved.GrowthSeconds)`，复用土壤/空格/重复/上限/火把检查，不能通过绕过初始化来放 Actor。根决定失效项的加载策略。

标题清理增加 `BlockRealmCrop` 标签；食物回收物已由现有 `BlockRealmDrop` 清理命中。普通清理 Destroy 不触发回收生成。

## 静态检查与后续真实验收

草稿静态核对 UE 5.8 声明、包含顺序、Root 坐标、生成顺序、数量/重复、暂停条件和库存到账顺序。新文件差异检查无空白错误；未运行编译或引擎。根代理已补齐 `Actions::TryAddFood(int32)` 依赖，后续工程构建由根执行。

根真实验收建议覆盖：只能顶面种植且扣 1、未成熟 V 不变、真实短时 Delta 到成熟后 +3、库存满保留/容量接近上限不部分到账、菜单及背包期间年龄不变、墙后无法命中、支撑破坏后只有 1 份 FoodDrop 并可单次拾取、同格火把互斥、保存/重载四阶段与成熟年龄、旧档缺失数组、标题清理无残留。

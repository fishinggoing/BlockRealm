# 熔炉源码草稿

本目录仅含可复制进 `Source/BlockRealm` 的 `ABlockRealmFurnace`。没有修改现有 Source、Config、Tools，没有启动 UE 或执行编译。完成集成后由根代理执行 UHT、编译与真实游戏验证。

## 已实现的约定

- 一座熔炉占一个空气格；下方必须为实体方块，并排除树叶、玻璃。最多 64 座；每种内部物料容量均为 64。
- 用 `BlockRealmFurnace` 标签识别清理对象。已按 `BlockRealmFurnace`、`BlockRealmTorch`、`BlockRealmCrop`、`BlockRealmRestPoint` 标签和 `WorldToBlock(ActorLocation)` 拒绝同格占用。根代理还应补齐类型化的床／休息点双格预约判定，并对其他放置入口做对称检查。
- `CanAddBatch` / `AddBatch` 允许单项为 0，但拒绝负数、全 0、任一项超容量；无部分加入。`TakeGlass` 仅在完整请求可用时扣除，成功返回请求数量，失败返回 0。计数上限先检查减法余量，避免整数加法溢出。
- 一份配方为 1 沙块 + 1 木材 → 1 玻璃，默认需 8 秒。沙、木只在完成这一份时同时扣除；过程中完整保留两项输入。没有沙、没有木或输出满时停炉。
- 只有当前 GameMode 的 `IsAdventureActive()` 为 true、有效玩家属于此体素世界且背包关闭时烧制。暂停、标题、背包期间不累计时间；恢复后从当前进度继续。
- 每次 Tick 用显式有界 while 最多完成 8 份，输入和输出仍分别限制为 64。超长 Delta 的额外时间丢弃，没有后台补算／离线补算队列。
- 运行时进度始终为 `[0, GetCookDurationSeconds())`。默认保存进度为 `[0,8)`；TryPlace 允许恢复有限 `[0,8]`，exact 8 钳为 `std::nextafter(8.f, 0.f)`，下一次有效 Tick 才提交配方。负数、NaN、Infinity、超 8，或没有双输入／输出空间却带正进度，均拒绝整个放置。
- `SetCookDurationSeconds` 仅接受有限 `.01..8` 秒，返回 bool。改时长保留当前完成比例，绝不立即扣料或产物；用于隔离短测，正式游戏保持默认 8 秒。这个测试参数未保存。若测试改变过时长，保存时先恢复 8 秒，以使持久化进度仍按 8 秒解释。
- 支撑消失、改为树叶／玻璃、占用格失去空气状态，或另一带放置标签的 Actor 占同格，Tick 会熄火并 Destroy。第一版没有回收掉落，炉内所有输入、燃料、输出随失效销毁。这是当前版本明确的物料损失行为。
- 所有组件与 Actor 均禁碰撞，不修改权威体素。九块 Engine Cube 石材拼成炉体，+X 面留真实洞口；洞内木堆和火面分别使用 M_Wood / M_Glow，其余使用 M_Stone。
- 暖 PointLight：Unitless，强度 0.7，半径 650，非 inverse-square，falloff 2.3，inverse-exposure blend 1；无阴影、无体积雾。火面及灯仅在允许烧制且配方可推进时显示。
- 瞄准使用每个可见 Engine Cube 的真实本地包围盒，并保留非均匀缩放后的世界距离参数；洞口空隙不是完整格包围盒。每个候选随后使用 `TraceBlocks` 拒绝体素遮挡，包含玻璃遮挡。

## 根代理的最小集成建议

现有角色 `Inventory` 私有，已有 `AddBlockItem`，没有公共扣料事务。建议由角色新增三个小 wrapper，内部完成检查与提交；玩家动作组件只传入瞄准后的熔炉／目标格。不要通过 `CaptureSaveState` / `RestoreSaveState` 修改库存，因为那会重置其他玩法状态。

```cpp
// 建议在 ABlockRealmCharacter 内实现；根代理拥有角色代码。
bool TryPlaceFurnace(const FIntVector& Cell);
bool TryFeedFurnace(ABlockRealmFurnace* Furnace);
int32 TryCollectFurnaceGlass(ABlockRealmFurnace* Furnace);
```

三个 wrapper 先检查 `IsInGameThread()`、`CanUsePlayerActions()`、有效同世界对象；放置还应拒绝与玩家胶囊相交，并先确认顶部瞄准条件。槽位沿用 `int32(EBlockType::Stone/Sand/Wood/Glass) - 1`。

1. 放置：先确认有效石头槽、库存 ≥8，并完成双格／其他放置物检查；调用 `ABlockRealmFurnace::TryPlace(GetWorld(), BlockWorld, Cell)`。只有返回有效对象后才 `Inventory[StoneSlot] -= 8`。Spawn 失败绝不扣石料。
2. 投料：先确认有效沙／木槽各有 ≥1，再调用 `Furnace->AddBatch(1, 1)`。只有成功后在同一游戏线程回调内同时扣除角色沙、木各 1，避免先扣一项后另一项失败。
3. 领取：先读 `GetOutputGlass()`。按现有 `AddBlockItem` 的每槽 1,000,000 上限计算 `Count = Min(OutputGlass, 1000000 - Inventory[GlassSlot])`，拒绝非正数；完整检查有效玻璃槽、库存非负、炉仍有效锚定。保证库存能接收后调用 `TakeGlass(Count)`，仅返回 Count 才提交玻璃槽的 `+= Count`。角色 wrapper 可直接对已验证槽位加数，因此不会在炉已扣产物后遭遇第二次可失败的库存 API。

T 按键建议：先用摄像机原点／控制器方向、现有 700 cm 交互范围调用 `FindAimedFurnace`。命中且有输出时只尝试领取；库存满时保留输出且给提示。本次不继续投料。命中且无输出时尝试 1 沙 + 1 木。未命中炉时使用权威 `TraceBlocks`，只接受 `Adjacent == HitBlock + FIntVector(0,0,1)` 的上表面，成本 8 石创建炉；墙面、底面均拒绝。所有操作开始时取消持续挖掘／放置，避免一次按键叠加原有动作。

标题／新世界清理加入 `BlockRealmFurnace` 标签；如果旧世界在调用 ResetWorld 时仍保留炉 Actor，应先销毁这些放置 Actor。可选 JSON 记录为 `{cell, input_sand, fuel_wood, output_glass, cook_progress}`；没有字段的旧存档视为空数组。解析先完整验证数值、整数性、数量 ≤64、唯一格和进度，再改动实时状态。恢复逐项走 `TryPlace`，不要强行 Spawn 绕过支撑／占位判定。实体位置由 Cell 决定，无需保存视觉 Transform。

## 集成后验证建议

- 原子性：负数、全 0、INT_MAX、沙或木单项超容量、超量领取均失败且全部计数保持不变；单项补料成功；完整量领取正确。
- 配方：7.9 秒不扣物料，跨到 8 秒时双输入各 -1、玻璃 +1、进度归零；没有沙或木不推进；输出 64 不推进；输出 63 完成一份后熄火。
- 有界 Delta：64 沙 + 64 木时输入极大有限 Delta，仅完成 8 份并保持合法半开进度；NaN、Infinity、负 Delta 不产生玻璃。
- 状态门控：标题、暂停、背包保持输入、输出、进度不变且熄火；退出这些界面恢复原进度，期间时间不补算。
- 恢复：0、7.9、exact 8 边界符合上述约定；无料半成品、满输出半成品、非法计数／进度均拒绝创建；读取 getter 再还原保留计数与进度。
- 放置／失效：空气 + 合法下方支撑通过；水格、树叶／玻璃支撑、世界边界、重复格、第 65 座、火把／作物／床占格失败；挖掉支撑或在炉格放入体素后 Tick 销毁且无回收掉落。
- 瞄准／视觉：近处炉石材、火面可命中，空洞边缘按实际 mesh 判定；完整石墙／玻璃墙后炉不可命中；确认禁碰撞、灯参数、闲置熄火。

## 草稿检查记录

已阅读当前 `BlockRealmTorch`、`BlockWorld`、`BlockRealmCharacter`、`BlockRealmGameMode`、`VoxelData` 和 `BlockTypes`；确认三种材质资产存在。静态检查通过：全部声明有对应定义、generated header 顺序正确、没有体素／角色库存写入、烹饪 while 显式有界、灯光参数符合约定、九块石材均在单格内且炉口不被石材填满、文件为 UTF-8 且无尾部空格。本目录恰好为三个被分配文件。没有启动 UE、UHT、编译或运行时测试；这些验证由根代理在集成后执行。

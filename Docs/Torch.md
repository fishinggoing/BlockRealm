# 放置火把实体

`ABlockRealmTorch` 由 `BlockRealmTorch.h/.cpp` 实现，火把不写入体素枚举或方块数据。角色工具选择、材料扣除、回收、存档与恢复由主代理接入；本次实现未启动 UE、编译或运行，以下是接口与静态行为说明，集成结果仍待统一验证。

## 放置与恢复接口

```cpp
ABlockRealmTorch* Torch = ABlockRealmTorch::TryPlace(World, BlockWorld, Cell, Support);
```

成功返回已完成生成的 Actor，失败返回 `nullptr`，不修改库存。调用方只在成功后扣除一根火把。存档用可选 `placedTorches` 与对齐的 `placedTorchSupports` 数组保存 `GetBlockPosition()` 与 `GetSupportBlock()` 的整数坐标，保留墙面、顶面和地板朝向；旧位置记录可由上层搜索六个相邻支撑。恢复时逐条调用同一个 `TryPlace`，因而恢复也执行相同的有效性校验，不绕过重复或数量限制。

- `Cell` 必须处于有效世界坐标范围内，满足 `0 < Z < FVoxelData::WorldHeight`，且权威 `GetBlock(Cell)` 为 Air；Water 与实体格不能作为初始放置位置。
- `Support` 必须在有效坐标范围内，与 Cell 共用一个面，且 `FVoxelData::IsSolid(GetBlock(Support))` 为 true。地基 z=0 可以作为支撑，斜角与间隔格不能作为支撑。
- 一个 `UWorld` 内同一 Cell 最多一根火把，活跃火把总数最多 `MaxPlacedTorches=512`。销毁中的 Actor 不占用该限额。
- 要求主线程调用、有效 `UWorld` 与 `ABlockWorld`，并且后者属于传入的 UWorld；无效输入返回 `nullptr`。

生成采用 deferred spawn，在 BeginPlay 前填入世界、Cell 与 Support；普通未初始化的直接生成 Actor 会自动移除。`GetBlockWorld()` 可用于管理器按体素世界筛选实体。

## 外观与照明

外观复用 `/Engine/BasicShapes/Cube`、现有 `M_Wood` 与 `M_Glow`。木柄约 11×11×66 cm，发光帽约 18×18×18 cm；模型从支撑面朝目标空气格伸出，可装在地板、墙面或顶面。点光与发光帽同坐标。所有可见组件均无碰撞、无重叠事件、不影响导航、无阴影；火把不占用体素格，玩家继续向该格放置实体方块时不会被模型阻挡。

暖色 Movable 点光采用非平方反比衰减、Unitless 强度 1.8、`InverseExposureBlend=1`、1100 cm 衰减半径、2.3 衰减指数；无阴影、无半透明光照、体积雾散射强度为 0。强度不随时间变化，没有闪烁、动画或额外灯光。

Actor 每约 0.5 秒检查权威体素数据：支撑不再是实体、目标格变为实体或关联的体素世界失效时移除。查询不依赖区块是否已加载，也不创建或修改任何体素。目标格后来变为 Water 时继续保留，因为 Water 不属于实体方块。销毁后由 UE 一并移除模型与点光。

## 瞄准回收与掉落接口

```cpp
ABlockRealmTorch* Torch = ABlockRealmTorch::FindAimedTorch(
    World, BlockWorld, EyeLocation, ViewDirection, InteractionReach);
```

函数要求主线程、同属一个 UWorld 的有效体素世界、有限的起点和方向、非零方向，以及有限且大于 0 的距离。它只搜索关联该体素世界、仍有有效支撑且可见的火把，返回范围内最近的一根，否则返回 `nullptr`。

射线分别与木柄和发光帽的实际网格包围盒求交。包围盒随组件旋转和缩放；局部方向不重新归一化，因此求交距离仍以世界厘米计量。检测范围保持模型的窄宽度，不把整格空气或支撑方块当作火把，也不增加碰撞。候选命中前再调用权威 `TraceBlocks`，任何先到达的实体方块都会阻止回收，区块碰撞尚未加载也不能穿墙操作。

角色左键命中火把后，先用 `AddTorchItem(1)` 确认工具栏成功接收，再调用 `Destroy()`；工具栏无空间时保留火把。这次按下不会启动挖掘，需重新点击才能挖支撑。直接 `Destroy()`、普通未初始化生成被拒绝、关卡或体素世界失效都不会生成掉落，避免重复返还。

挖掉支撑或向目标格放入实体方块，会在下次检查时生成一个带 `BlockRealmTorchDrop` 与 `BlockRealmDrop` 标签的轻量 `AStaticMeshActor`，随后移除原火把。每根火把最多生成一个恢复掉落。掉落复用木柄与发光帽材质，木柄约 8×8×36 cm、帽约 14 cm；没有点光、碰撞、物理模拟、导航影响、阴影或 Actor Tick。优先用原目标空气格；被覆盖时搜索六个相邻空气格。可用列内的掉落直接落在权威地板上方，顶面火把也不会留下悬空拾取物；周围都被填满时保留原位置作为回收标记。

`PlayerActions` 在玩家进入拾取范围时识别 `BlockRealmTorchDrop`，成功增加一根火把后移除拾取标签并销毁掉落；工具栏无空间则保留掉落。恢复掉落与其他临时掉落一样，不写入存档，返回标题时清理。

## 集成验收清单

主代理应统一构建并运行以下检查：六个支撑方向的成功放置；Water、已占用格、非法高度、斜角支撑、重复 Cell 与第 513 根被拒绝；成功扣除一次库存、失败不扣除；瞄准木柄或帽可回收，略过窄模型、超距和隔墙均被拒绝；回收成功只返还一次，工具栏无空间时保留火把，持续按住左键不会顺便挖支撑；挖掉支撑或覆盖目标后约 0.5 秒内移除并产生一个恢复掉落，拾取一次后数量正确；保存、退出、恢复后坐标、支撑与朝向一致，旧存档可恢复，损坏记录被安全跳过；洞穴中暖光可读、点光稳定且没有体积雾白斑。

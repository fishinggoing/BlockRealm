# 床与营地重生点草稿

本模块仅包含 `ABlockRealmRestPoint` 实体，源码位于本目录的 `BlockRealmRestPoint.h/.cpp`。角色按键、12 木板成本、怪物威胁、时间跳转、生命恢复和 JSON 存档由根代理接入。草稿不修改 Source、Config、Tools 或体素，不启动 UE、不构建、不打包；编译与运行验收仍由根代理完成。

## 放置与朝向

```cpp
ABlockRealmRestPoint* Bed = ABlockRealmRestPoint::TryPlace(
    GetWorld(), BlockWorld, Cell, Heading);
```

`Cell` 是床脚所在的空气格，Heading 必须是整数 `0..3`，分别朝 `+X/+Y/-X/-Y` 延伸一格。`GetBlockPosition()` 返回床脚格，`GetSecondBlockPosition()` 返回床头格，`GetHeading()` 返回原始合法朝向，`GetBlockWorld()` 返回关联体素世界。`OccupiesCell(Cell)` 同时识别两格，方便 Torch、Crop 与角色放置代码接入。

两个占用格都必须在 `FVoxelData::IsValidPosition` 范围内、Z > 0 且权威 `GetBlock` 为 Air。两格各自下方必须是实体，Leaves、Glass 和 Water 均不能支撑床；Wood 可以，地基 Z=0 可以。已有床的任一格、火把的空气格，以及带 `BlockRealmCrop` 标签且 `WorldToBlock(ActorLocation)` 对应的作物格，均阻止新床生成。一个 UWorld 最多 32 张有效、非销毁中的床。输入世界不匹配、非游戏线程、非法朝向/坐标或生成失败，都返回 `nullptr`。

生成用 deferred spawn，在 BeginPlay 前填好 Cell、Heading 和 BlockWorld。普通未初始化直接生成会自行移除。Actor 原点固定在床脚空气格中心，床体沿本地 +X 向床头伸出，避免只看 Actor 原点时误认主占格。

外观复用 Engine Cube 和现有 M_Wood、M_Sand、M_Stone，由木床架、四腿、床头/脚挡板、砂色床垫/枕头、石色毯子及浅色条带组成。共 13 个立方体部件，整体本地包围范围 `X[-45,145]、Y[-42,42]、Z[-49,18]` cm，完整位于两格空气内，底部距支撑面 1 cm。组件禁碰撞、阴影、重叠事件和导航影响；不添加灯光，不参与体素 DDA。

每 0.5 秒重新检查两个支撑、两个占用格及装饰实体占位。任何失效都 Destroy；不返还材料、不生成掉落、不改体素。带 `BlockRealmRestPoint` 标签，可沿用标题清理循环。

## 瞄准与重生

```cpp
ABlockRealmRestPoint* Bed = ABlockRealmRestPoint::FindAimedRestPoint(
    GetWorld(), BlockWorld, EyeLocation, ViewDirection, InteractionReach);

FVector RespawnLocation;
const bool bHasSafeSpace = Bed && Bed->FindSafeRespawn(RespawnLocation);
```

瞄准依次求射线与每个可见、已注册部件的实际网格包围盒交点，保留旋转/非均匀缩放后的世界距离参数，选择距离范围内最近有效床。它不会把整格空气当作命中。每个候选再通过权威 `TraceBlocks` 排除前方实体，因此区块碰撞未加载也不能穿墙使用。起点/方向/距离异常、方向为零、床失效/隐藏或关联其他 BlockWorld，都返回 `nullptr`。

当前 `ABlockWorld` 没有 `FindSafePlayerLocation`，因此 `FindSafeRespawn` 自行查询体素。它按床朝向检查 8 个床外站立点：两侧各两格、床脚外端、床头外端、床脚两斜角，顺序优先两侧。站立点与其上方必须都是有效 Air，脚下必须为可靠实体支撑，且脚/头格不能被床、火把或作物占用。返回位置的 XY 在该列中心，Z 为脚格底面 +98 cm；适配当前 34 cm 半径、90 cm 半高角色胶囊，也容纳半高不超过 96 cm 的胶囊。始终排除床的两格。

床已失效或 8 点全部不可用时返回 `false`，保留调用方 OutLocation 原值；调用方必须回落 `BlockWorld->GetSpawnLocation()`。该查询不承诺附近区块已加载，不启动 Streaming；死亡传送后应由角色调用 `RequestStreaming(RespawnLocation)`，沿用根的地形准备保护。

## 根代理最小接线

1. 将本目录 h/cpp 复制进 `Source/BlockRealm`，保持文件与 generated.h 名称一致。现有 Engine/Core/CoreUObject 依赖足够。
2. 角色增加 B 对应 `UsePressed()`，先检查当前游戏输入许可；沿用相机/控制视线和 `InteractionReach` 调用 FindAimedRestPoint。
3. 命中床时，检查同世界、非销毁、未死亡且可伤害的 Mob，附近 800 cm 内存在威胁就提示并结束。安全时记录床脚 Cell 为重生锚点。若 GameMode::IsNight()，令其 DayNightActor 时间为 `.32f`（`.28f` 也可）并通过 `RestoreSurvivalHealth` 恢复少量生命；白天仅设置锚点。床模块不读写敌人、角色库存或时钟。
4. 未命中床时，用权威方块 TraceBlocks 确认 `Adjacent == Support + FIntVector(0,0,1)`，只接受支撑顶面。库存至少 12 CraftedPlanks 后调用 TryPlace；仅在返回有效床后扣 12。若扣除异常失败，直接销毁刚生成的床回滚，不能影响已存在床。
5. 按玩家水平朝向映射四个 Heading（例如 `((FloorToInt((UnwindYaw+45)/90)%4)+4)%4`）。放置前可用 `DoesBlockIntersectPlayer` 分别检查两格，避免造床穿过玩家当前位置。
6. Torch::TryPlace、Crop::TryPlant 也需通过 typed `RestPoint->OccupiesCell(Cell)` 拒绝床两格，避免从另一模块往床内放装饰实体。RestPoint 已拒绝现有 Torch 与 Crop；Crop 草稿当前标签/原点经核对兼容这里的 tag + WorldToBlock 查询。
7. 死亡时，在 ResetGameplayState 清空瞬态前保留角色的重生锚点，再找同世界有效床且 GetBlockPosition() 匹配，调用 FindSafeRespawn。没有床或返回 false 时用世界出生点；不要从默认 FVector::ZeroVector 或床内部位置传送。新冒险/真正的 Reset 清除锚点，死亡重置应保留锚点。
8. 保存前枚举床、恢复时调用 TryPlace、返回标题时按 BlockRealmRestPoint 标签清理。继续过滤 GetBlockWorld()==WorldActor 及非销毁状态。

## 可选存档字段建议

```cpp
struct FBlockRealmSavedRestPoint
{
    FIntVector Cell = FIntVector::ZeroValue;
    int32 Heading = 0;
};

// FWorldSaveState 的 optional 数据，字段缺失时保持默认。
TArray<FBlockRealmSavedRestPoint> PlacedRestPoints;
bool bHasRespawnRestPoint = false;
FIntVector RespawnRestPoint = FIntVector::ZeroValue;
```

JSON 建议使用 `placedRestPoints: [[x,y,z,heading], ...]` 与仅在设置时写入的 `respawnRestPoint: [x,y,z]`，不需要修改现有版本号；旧存档缺失字段即空床数组和未设置锚点。只存坐标/朝向，不存 Actor 指针、部件或物理结果。

解析/保存校验床记录最多 32，坐标为有界整数、Z>0、Heading 为整数 0..3，第二格也有效；两格不能与另一个床记录重复。锚点坐标需有界整数且 Z>0。字段存在但格式/范围不合法时沿用当前临时 Record 的原子解析规则，避免半加载世界。支撑被编辑或 Crop/Torch 占用造成恢复失败时，TryPlace 安全拒绝；根决定提示/忽略规则。死亡时再次查床与安全空间，失效锚点永远不会使角色卡入床或墙。

## 静态验证与集成验收

已核对本地 UE5.8 头文件的 SpawnActorDeferred、CreateDefaultSubobject、组件隐藏/光追接口及 FIntVector 算术。FIntVector 没有一元负号，因此反向向量按 XYZ 显式构造。材料文件已存在。外观与八个重生候选在四个 Heading 下均检查不越出两格、不落入床格。未编译/运行 UE，以下验收由根统一执行：

- 四朝向床放置，双支撑缺失、Water/Leaves/Glass 支撑、Water 占格、坐标边界、非法 Heading、床头撞已有床、火把/作物占任一格及第 33 张床被拒绝；失败不扣木板。
- 射线命中床板/枕头/毯子成功，略过实际外形、超距及墙后床失败；无碰撞和阴影，不抢挖掘 DDA。
- 半秒内响应任一支撑移除、任一空气格填实体/Water、装饰占格失效；没有材料退款或掉落。
- 8 站立点全部堵住时 FindSafeRespawn=false 且 OutLocation 未变，死亡回落世界出生点；两空气格不足、支撑非法、其他床/火把/作物占脚或头格均不能重生；放行点在床外且胶囊不会进入地板。
- 无威胁时 B 设置锚点，夜晚跳早晨并少量回血，白天只设锚点；800 cm 内有效威胁阻止睡眠。床被拆除后死亡回落出生点。
- 保存/退标题/重进后坐标、朝向及锚点一致；旧存档加载；坏 optional 字段不改变实时世界；标题清理没有残留床。

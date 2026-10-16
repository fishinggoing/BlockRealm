# Mob 与昼夜最小接入说明

`ABlockRealmDayNight` 负责昼夜时钟与地图灯光，`ABlockRealmMob` 负责追击与攻击；现有 `GameMode` 已接入这些 Actor。Mob 使用 Engine/BasicShapes/Cube 和运行时材质，不需要导入资源。昼夜时钟复用地图已有的 DirectionalLight 与 SkyLight，不创建额外灯源。

## 昼夜 Actor

在已有世界的 `BeginPlay` 中可以直接生成一个时钟：

```cpp
ABlockRealmDayNight* Clock = ABlockRealmDayNight::Spawn(GetWorld(), FTransform::Identity);
Clock->SetTimeOfDay(0.32f); // 0=午夜，0.25=日出，0.5=正午，0.75=日落
Clock->SetWorld(BlockWorld); // 可选，仅保存关联，不访问 GameMode
```

Actor 自己 Tick 并按 `DayLengthSeconds` 推进时间，默认一天为 900 秒；在已接入的 `GameMode` 中，仅在冒险活动且背包关闭时自动推进。也可以关闭 `bAutoAdvance` 后由管理器调用 `Advance(DeltaSeconds)`。`SunLight` 绑定带 `BR_MorningSun` 标签的地图方向光，`SkyLight` 绑定带 `BR_CoolSkyFill` 标签的天空光；没有对应标签时选择该类型中强度最高的灯。缺少灯时记录警告，不生成替代灯。

### 晨昏底光与方向过渡

归一化时间 `t` 的太阳高度为 `A = sin(2π(t - 0.25))`。`IsNight()` 在 `A <= NightAltitudeThreshold` 时返回 true，默认阈值为 `-0.08`，所以正好日出和日落的 `t=0.25/0.75` 不判为夜间。

定义 `S(A; lo, hi)` 为先将 `(A-lo)/(hi-lo)` 限制到 `[0,1]`，再应用 `w*w*(3-2*w)` 的平滑权重。默认光照公式为：

```text
方向光 lux = lerp(7000, 32000, S(A; 0, 0.22))
SkyLight 强度 = lerp(1.0, 1.4, S(A; -0.08, 0.22))
月光方向权重 M = 1 - S(A; -0.08, 0.08)
yaw = SunYawOffsetDegrees + t*360° + M*180°
E = abs(asin(A))，转换为度
照地仰角 = lerp(8°, E, S(E; 8°, 16°))
pitch = -照地仰角
```

唯一方向光的夜间强度同时用作晨昏底光，避免日出、日落时太阳与月光权重同时归零。默认夜间方向光为白天的 21.9%，仍明显暗于白天；角色还会在露天地表启用有限半径的冷色补光。`MinimumLightElevationDegrees` 默认 18°，可配置范围为 1°–30°；在其两倍角度以上保留原太阳高度路径，在地平线附近平滑保持向地面照射。默认晨昏方向光对水平地面的理论直接照度约为 `7000*sin(18°)=2165 lux`，实际像素亮度仍受材质、天空贡献、遮挡和曝光影响。

月光方向在太阳高度 `-0.08..0.08` 内平滑旋转；只要 `IsNight()` 返回 true，就已完全采用月光方向。方向光颜色使用线性 RGB 平滑插值：太阳高度 `0..0.08` 从月光蓝 `(0.56,0.66,1)` 变为晨昏暖色 `(1,0.60,0.37)`，`0.08..0.22` 再变为日光 `(1,0.96,0.90)`。天空光保持与其强度权重一致的颜色过渡。

配置中的非有限或非正强度会退回默认值；白天方向光至少为 4 lux，白天天空光至少为 0.002，夜间强度均限制在对应白天强度的一半以内。时间、时长、夜间阈值和角度也有有限值与范围保护。

### 默认值静态核对

| 时间 | 方向光 lux | SkyLight 强度 | pitch | IsNight |
| --- | ---: | ---: | ---: | --- |
| 0 / 1（午夜） | 7000 | 1.000000 | -90° | true |
| 0.249 | 7000 | 1.060000 | -18° | false |
| 0.25（日出） | 7000 | 1.070000 | -18° | false |
| 0.251 | 7074 | 1.080000 | -18° | false |
| 0.32（默认开局） | 32000 | 1.400000 | -25.2° | false |
| 0.5（正午） | 32000 | 1.400000 | -90° | false |
| 0.749 | 7074 | 1.080000 | -18° | false |
| 0.75（日落） | 7000 | 1.070000 | -18° | false |
| 0.751 | 7000 | 1.060000 | -18° | false |

内联 Python 对默认公式抽样 100001 个时间点，核对了非零有限强度、最低 18° 照地角度、夜间判定与月光方向一致、颜色范围和周期闭合；在晨昏、太阳高度过渡及 18°/36° 角度边界逐步缩小采样间隔，强度、颜色和方向向量差值均趋近零。此检查验证数学公式，不替代 UE 编译或画面验证。

已有光影 probe 接入 `-BRDawnLighting` 与 `-BRDuskLighting`，分别将时钟固定在 0.25 与 0.75，与 Cave/Surface/Night probe 共用采样通路，并使用独立 `LightingProbeWorld` 存档目录。源码与文档静态检查阶段未启动 UE、未构建；晨昏地面亮度、阴影与曝光结果由统一构建后的 probe 和截图确认。

## Mob Actor

夜间或洞穴生成点创建 `ABlockRealmMob` 后设置世界和目标：

```cpp
ABlockRealmMob* Mob = GetWorld()->SpawnActor<ABlockRealmMob>(ABlockRealmMob::StaticClass(), SpawnTransform);
Mob->SetWorld(BlockWorld);
Mob->InitializeTarget(PlayerCharacter); // SetTarget 是同义别名；为空时自动追踪 0 号玩家
```

Mob 没有 AIController 也能直线追击；有 Controller 时会改用 `AddMovementInput`，便于以后接入导航。进入攻击距离后每个冷却周期调用 `UGameplayStatics::ApplyDamage`，因此玩家 Character 需要覆写 `TakeDamage`（或在其 TakeDamage 中转发到自己的 Health API）才能扣血。Mob 本身可用 `ApplyDamage`，死亡后广播 `OnMobDied`，生成带 `BlockRealmDrop` 标签的 Stone 和 Glow 方块拾取物并短暂保留物理碰撞。根代理可以在该事件中把标签/位置转换为库存或 `ABlockWorld::SetBlock` 逻辑。

`IsDead()`、`GetHealth()`、`GetTarget()` 和 `GetBlockWorld()` 可用于生成器和 HUD；无需修改 Character、GameMode、HUD、World、Chunk、VoxelData 或 BlockTypes。

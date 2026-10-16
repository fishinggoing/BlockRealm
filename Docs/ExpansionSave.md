# 农业、床与炉的可选存档

农业、床、炉和床重生点写在现有世界 JSON 中，继续使用 `format: "BlockRealmWorld"`、`version: 1` 和九槽方块库存。原有 `player`、`edits`、可选 `survival`、`placedTorches`、`placedTorchSupports` 保持兼容，不增加旁路文件或新的保存流程。

## 纯值字段契约

这些类型定义在 `BlockTypes.h`，不引用 Actor 或玩法系统头文件：

| 类型 | 字段与默认值 |
| --- | --- |
| `FBlockRealmSavedCrop` | `FIntVector Cell = ZeroValue`，`float GrowthSeconds = 0` |
| `FBlockRealmSavedRestPoint` | `FIntVector Cell = ZeroValue`，`int32 Heading = 0` |
| `FBlockRealmSavedFurnace` | `FIntVector Cell = ZeroValue`，`int32 InputSand/FuelWood/OutputGlass = 0`，`float CookProgress = 0` |

`FWorldSaveState` 增加空数组 `Crops`、`RestPoints`、`Furnaces`，以及 `bool bHasRespawnPoint = false` 和 `FIntVector RespawnPoint = ZeroValue`。`RespawnPoint` 保存床锚格，不是世界空间位置或床的第二格。

## JSON 字段与约束

根字段 `crops`、`restPoints`、`furnaces`、`respawnPoint` 各自可选，独立于 `survival`。即使 `bHasSurvivalState` 为 false，也会校验并保存存在的对象。缺少新数组时使用空数组；缺少 `respawnPoint` 时重生标志为 false、坐标为零。

| 根字段 | 单项内容 | 数量与数值范围 |
| --- | --- | --- |
| `crops` | `{ "cell": [x,y,z], "growthSeconds": n }` | 最多 256；生长时间为有限数值，0..120（含端点） |
| `restPoints` | `{ "cell": [x,y,z], "heading": n }` | 最多 32；朝向为整数，0..3 |
| `furnaces` | `{ "cell": [x,y,z], "inputSand": n, "fuelWood": n, "outputGlass": n, "cookProgress": n }` | 最多 64；三个物品数均为整数，0..64；进度为有限数值，`0 <= n < 8` |
| `respawnPoint` | `[x,y,z]` | 必须指向同一存档 `restPoints` 中的一个床锚格 |

每个对象的示例字段全部必填。所有对象格坐标必须恰好包含三个整数；X/Y 为 `-MaxCoordinate..MaxCoordinate`（当前为 ±1,000,000），Z 为 1..63。

床的第二格按朝向计算：0 为 +X，1 为 +Y，2 为 -X，3 为 -Y。锚格和第二格都必须合法。统一格占用检查拒绝火把、作物、床的两格和炉之间的任何重复或冲突，包括两张床的第二格互相重叠。火把支持格仍按原规则校验为合法相邻格，不作为放置对象占用格。

炉的正进度要求 `inputSand > 0`、`fuelWood > 0`、`outputGlass < 64`；进度为零时允许空炉、等待原料或输出已满。完成一批应由玩法层更新物品数并重置进度，保存层拒绝 `cookProgress >= 8`。解析先校验原始 JSON 数值，再转换为 float，避免极小正进度被转换为零后绕过原料要求；接近 8 的合法值转换后仍保持小于 8。

`FVoxelData` 公开 `MaxSavedCrops`、`MaxSavedRestPoints`、`MaxSavedFurnaces`、`MaxSavedFurnaceStack`、`MaxCropGrowthSeconds` 和 `FurnaceCookSeconds`，供接入层共享同一组上限。

以下完整示例省略 `survival`，仍可保存所有新对象及带支持信息的火把：

```json
{
  "format": "BlockRealmWorld",
  "version": 1,
  "seed": 42137,
  "chunkSize": 16,
  "worldHeight": 64,
  "blockSize": 100,
  "player": {
    "position": [50, 50, 2300],
    "rotation": [0, 0, 0],
    "selectedSlot": 0,
    "inventory": [64, 64, 64, 64, 64, 64, 64, 64, 64]
  },
  "edits": [],
  "placedTorches": [[2, 4, 22]],
  "placedTorchSupports": [[2, 4, 21]],
  "crops": [{ "cell": [4, 4, 22], "growthSeconds": 64.25 }],
  "restPoints": [{ "cell": [6, 4, 22], "heading": 0 }],
  "furnaces": [{
    "cell": [10, 4, 22], "inputSand": 12, "fuelWood": 3,
    "outputGlass": 4, "cookProgress": 1.5
  }],
  "respawnPoint": [6, 4, 22]
}
```

## 兼容与原子性

序列化只在对应新数组非空时写出它，且只在 `bHasRespawnPoint` 为 true 时写出重生坐标。空的新状态不会改变旧存档形状或 `survival` 内容。非空火把及支持信息也可在没有 `survival` 时写出；旧的位置式火把档仍不要求支持数组。

存在但为 null、错误类型、缺少必填字段、非有限数值、整数字段的小数、越界坐标、超出数量上限、重复占格或无效重生引用的数据均被拒绝。显式空的新数组是合法的，与缺少数组具有相同默认结果。

`ParseRecord` 先写局部临时记录，校验新对象、引用和全部原有世界字段成功后才替换 `OutRecord`。加载旧档会清空之前输出记录残留的新数组、重生标志和坐标。`SerializeRecord` 也仅在完整成功后替换 `OutJson`；失败时保留调用者原输出，现有文件原子替换流程无需变更。

## 接入与自测

玩法层捕获对象的纯值数组，恢复时按保存的格坐标、床朝向、物品数和进度重建。农业和炉进度保存为当前值，保存层不推进模拟时间。床被移除后，接入层应清除其重生标志或选择仍存在的床锚后再保存。

`FVoxelData::RunSaveSelfTests` 沿用既有入口，新增纯数据用例覆盖：

- 生存、旧九槽库存、种子与 edits、火把支持、农业、四种床朝向、炉物品数与进度、床锚重生点的完整往返。
- 没有 `survival` 的新对象及火把独立往返，旧档加载清除残留 extras，缺失或显式空数组的默认值。
- 对象数量恰好上限、合法坐标端点、床向内与向外的第二格边界。
- 空值、错类型、缺字段、小数坐标、越界及非有限数值、重复锚格、所有对象类型之间的占格冲突、床第二格之间的冲突。
- 炉进度半开范围、正进度前置条件、临近完成的 float 转换、极小正进度的原料检查，以及非法重生引用。
- 解析和序列化拒绝时原输出保持不变，包括没有 `survival` 的情况及新对象通过后出现损坏 edits 的情况。

此 bounded 实现阶段仅做源码和文档静态检查；没有启动 UE 或构建。纯数据自测由根代理在统一构建后的既有 `BlockWorld.RunSelfTests` 通路执行，不增加新工作流。

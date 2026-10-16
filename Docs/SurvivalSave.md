# 可选生存存档

世界文件继续使用 `format: "BlockRealmWorld"` 和 `version: 1`。原有 seed、尺寸、player 和 edits 字段不变，生存进度与火把位置写在同一份世界 JSON 中，继续由现有整个文件的原子替换保存流程处理。

## 数据结构

`BlockTypes.h` 中的 `FWorldSaveState` 增加纯值字段，不依赖 `BlockRealmSurvival.h`。`FBlockRealmSavedTool` 的 type 使用整数，与玩法工具类型映射为 0=空槽、1=木镐、2=石镐、3=火把。

| 状态字段 | 缺少 survival 时的默认值 | 存档约束 |
| --- | --- | --- |
| bHasSurvivalState | false | 解析到完整有效的 survival 对象才置为 true |
| Health / Hunger | 100 / 100 | 有限数值，0..100 |
| CraftedPlanks | 0 | 整数，0..1,000,000 |
| FoodCount | 6 | 整数，0..1,000,000 |
| SelectedToolSlot | -1 | 整数，-1..5；-1 表示徒手 |
| ToolSlots | 空数组 | survival 存在时必须恰好六槽 |
| TimeOfDay | 0.32 | 有限数值，0..1 |
| PlacedTorches | 空数组 | 最多 512 个互不重复的合法方块坐标 |

新增 JSON 字段位于根对象：

```json
{
  "survival": {
    "health": 74.5,
    "hunger": 62.25,
    "craftedPlanks": 12,
    "foodCount": 4,
    "selectedToolSlot": 0,
    "timeOfDay": 0.625,
    "toolSlots": [
      { "type": 1, "durability": 42, "maxDurability": 59, "quantity": 1 },
      { "type": 2, "durability": 100, "maxDurability": 131, "quantity": 1 },
      { "type": 3, "durability": 0, "maxDurability": 0, "quantity": 8 },
      { "type": 0, "durability": 0, "maxDurability": 0, "quantity": 0 },
      { "type": 0, "durability": 0, "maxDurability": 0, "quantity": 0 },
      { "type": 0, "durability": 0, "maxDurability": 0, "quantity": 0 }
    ]
  },
  "placedTorches": [[16, -17, 22], [-17, 16, 3]]
}
```

这是新增字段示例；完整世界文件仍需原有必填字段。

## 兼容与校验

- 缺少 survival 对象时，玩家生存字段采用默认值，bHasSurvivalState 为 false。旧版九格方块背包存档可直接加载。
- survival 一旦存在，示例中的七个字段全部必填。null、错误类型、字符串代替数值、非有限数值、越界值以及整数字段的小数都被拒绝。
- 每个工具槽的四个字段必填且为整数。type 为 0..3，quantity 为 0..64，当前和最大耐久均为 0..131，当前耐久不得超过最大耐久。
- 空槽的数量、当前耐久和最大耐久必须全部为零。镐必须有数量和剩余耐久。火把必须有数量，允许当前及最大耐久都为零。
- placedTorches 是独立可选数组，即使没有 survival 对象也会读取和严格检查。每项必须恰好三个整数；X/Y 为 -1,000,000..1,000,000，Z 为 1..63。缺少数组时使用空数组；null、重复位置和数量超过 512 均被拒绝。
- 序列化仅在 bHasSurvivalState 为 true 时写 survival 和 placedTorches（空火把数组也会写出）。关闭生存状态时只输出原版字段。玩法恢复应以 bHasSurvivalState 决定是否覆盖默认生存状态，火把位置可独立使用。

`ParseRecord` 只向局部临时记录写入。所有字段及世界 edits 校验成功后，才整体替换调用者的输出记录。损坏 extras 不会部分覆盖玩家、种子或编辑数据。`SerializeRecord` 也仅在完整成功后替换输出 JSON，因此校验失败不会进入原子文件替换。

## 接入与自测

玩法层捕获状态时设置 bHasSurvivalState 并将六个玩法工具槽转换成 `FBlockRealmSavedTool`；恢复时做反向转换。保存数据没有引入玩法头文件、额外旁路文件或新的版本号。

`FVoxelData::RunSaveSelfTests(TArray<FString>& Failures)` 是不访问世界或磁盘的纯数据测试入口，可由 `ABlockWorld::RunSelfTests` 调用。它覆盖：

- 旧档 version 1 往返、extras 省略，以及从旧档加载时替换此前残留的生存值。
- 生命、饥饿、木板、食物、选中槽、六个工具及磨损、昼夜时间、火把、种子和世界 edits 的完整往返。
- 可选火把数组缺失、只有火把数组而没有 survival 的兼容行为。
- 缺字段、错误类型、超出范围、非有限值、工具状态不一致、数量限制及非法火把坐标的拒绝；每次解析拒绝均检查原输出记录未被修改。
- 保存侧拒绝非有限状态、工具耐久不一致和非法火把位置，并保持原输出 JSON。

本次实现只进行了静态检查；测试入口需由集成层接入并在后续 UE 自测中运行。

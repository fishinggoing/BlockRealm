# V11 交付与实测记录

后续 Git 与 ZIP 交付见 [V11 Git 与 ZIP 交付](ReleaseV11.md)。以下“不生成 ZIP”和正式存档哈希描述本次回归结束时的状态；之后用户已继续游玩并授权调整傍晚洞穴位置，再要求提交 Git 和打包 ZIP。

2026-10-15。本轮按用户要求优先处理输出分辨率、中文可读性、背包取用、手持切换、采集工具等级、摔伤和生物。不生成 ZIP；素材新增费用 0 元。

## 本轮实现

- 背包支持真实点击方块、工具、空手和食物；数字 / 滚轮、Q / R 切换，第一人称显示手、袖、方块、镐和火把。
- 徒手可采土、沙、木等；岩石至少木镐、辉光石和完整玻璃至少石镐，采集计时和耐久生效。
- 根据真实最高点到落地的距离计算摔伤，前三格安全；涉水、飞行、传送和读档边界有保护。
- 猪、羊游走、避水、受击逃跑，击败后掉食物并可拾取；敌对生物追击、近战和掉落已接入。
- 原创像素地形材质，TSR 主 / 次渲染比例均 100%，关闭动态分辨率、运动模糊和景深；保留 DLSS 三档。
- UE MCP 已连接本工程，实际执行启动编辑器、PIE、运行时 CVar 回读、截图和关闭编辑器。没有改动 MCP 源码或新增工作流。

## 用户最后纠正

背包按 Minecraft 的槽位、快捷栏和悬停说明安排，不再使用占满屏幕的大段说明卡。移除角色跟随补光；洞穴和夜间根据天空及真实放置的火把、辉光石、燃烧熔炉照明，无光源洞穴允许昏暗。没有把提亮视野作为角色自带能力。

## 验证

Editor 编译和 BuildCookRun 均成功，正式包包含更新后的 Engine / 显示配置。Content 导入报告 `Deliverables/Validation/content-import.json` 为 `success=true`、53 个资产，十个 `BR.Pixel.World100.v4` 材质的编译错误均为空。

V11b 原生启动未指定 ResX / ResY / windowed / fullscreen，仍带 `-unattended`，实际 PNG 与报告均为 **2560×1440**；启动日志确认 per-monitor DPI aware、活动桌面 2560×1440、实际视口 2560×1440。该轮 **265 通过 / 2 失败**：旧 ExtendedSmoke 合成点击硬编码了改版前坐标。已将其改为读取共享的背包按钮边界；仅此回归 C++ 文件变更，以同一 Win64 Development 目标重新编译并将实际游戏 EXE 更新进完整交付目录，Content / 配置保持刚完成的打包结果。最终复跑结果将在下方补齐；V11b 失败报告原样保留。

V11b 的 1440p 背包、右下荧石悬停、水下气泡已实际逐图审核，中文和数量无裁剪，提示框未越过屏幕，生命 / 饥饿 / 呼吸各自成行。悬停图为 `26_V11InventoryTooltip.png`。

此前 V11a 的 1080p 包通过 267 项检查并生成 25 张图，但它仍使用旧背包与角色补光，不能代替最后纠正后的验收。V11a 默认启动的实际输出曾为 1707×960，其目录名 Native1440 不代表通过；该 DPI 问题已由 V11b 实际 2560×1440 结果纠正。

### V11c 最终原生运行

`Deliverables/Validation/ExpandedV11c_Native1440/runtime.json`：**success=true，267/267 通过，0 失败，实际 2560×1440，26 张截图**，完整运行 127.62 秒。此轮未传分辨率或窗口模式参数，最终 TSR 主 / 次比例均为 100%，DLSS 支持检测为 true，质量 / 平衡 / 性能三档实际切换通过，帧生成关闭。

背包真实命中区可装备方块、木镐、空手，空槽拒绝凭空生成物品；合成点击真实扣除原木并增加木板。左键实际击中动物、触发逃跑，同时没有启动地形采集。真实走落平台造成摔伤，菜单 / 背包冻结、食物掉落和拾取、工具等级与存档规则均通过。步行 520 cm/s、水中 220 cm/s，木镐石头采集约 0.572 秒。截图内瞬时 FPS 与旧短片段采样不能作为全游戏性能或长时间稳定性结论。

关键画面：`21_V11Inventory.png`、`26_V11InventoryTooltip.png`、`19_UnderwaterBreath.png`、`22_V11WoodenPickaxe.png`、`23_V11FallDamage.png`、`24_V11Population.png`、`25_V11BareHandsAnimal.png`。

### 最终光影专项

`Deliverables/Validation/LightingV11c/lighting-run-summary.json` 保留五场结果：地表、夜晚、清晨的严格脚本通过；洞穴、黄昏因为真实日志出现 **VSM Non-Nanite Marking Job Queue overflow**，严格脚本未通过。没有删除告警或把它们记为通过。

五个场景的运行时状态断言均成功：两盏角色灯不可见，component / cached / target 强度全部为 **0**，`player_fill_disabled=true`；真实世界火把和荧石光源的组件检查保留。已实际查看洞穴、夜间和黄昏原图：无角色光圈，洞穴远处保持暗，荧石是可见实际光源，露天天空 / 月光照亮地面，HUD 清楚。无光源洞穴昏暗是最后用户明确要求的行为。

VSM 告警仍是当前性能限制，关闭屏幕技术提示只避免遮挡玩家文字，不代表修复。UE5.8 该队列硬编码 128 项，溢出回退较慢路径，不能通过增大阴影页池宣称解决。本轮没有为消告警私自降低局部阴影分辨率。

### MCP 最终复核

最终 Editor 增量编译成功（`ExpandedV11Build/editor-final-build.log`）。通过现有 UE MCP 启动编辑器，确认 PIE 为 playing，回读运行时主 / 次比例 100 / 100、动态分辨率 0、运动模糊 0、景深 0、HitLighting.Allowed 0，并保存 `ExpandedV11Build/MCP-final-PIE-ready.png`。该图是 2560×1392 的编辑器 PIE 视口，**不用于证明独立游戏的 1440p 输出**；独立游戏尺寸由上面的完整 V11c 原生回归证明。随后通过 MCP 结束 PIE 并关闭编辑器。没有修改 MCP 源码、没有新增工作流。

最终交付为 `Deliverables/Windows/` 完整目录；运行文件为其下 `BlockRealm.exe`，实际游戏二进制与本次 Win64 Development 编译结果 SHA-256 相同。文档同步至 `Windows/Docs/`。正式存档哈希保持不变，旧 ZIP 未重建；最终核对记录保存为 `ExpandedV11Build/final-integrity.json`。

## 范围和限制

这是原创单人方块生存原型，不能视为完整 Minecraft 或其大部分系统的复刻。没有多人、无限世界、完整红石、附魔、全套物品 / 生物；生物个体和掉落尚不持久化。背包仍基于本项目的固定库存类型，不具备完整自由拖放堆叠体系。

本机 RTX 4060 Laptop / 驱动 566.24 采用 HWRT Lumen Surface Cache 兼容路径，真正 Hit Lighting 首帧崩溃未解决；VSM 非 Nanite 标记队列告警需要以最终日志为准。关闭屏幕技术黄字不等于解决该告警。

正式玩家存档不参与验证，测试分别使用 ValidationWorld 和 LightingProbeWorld。打包前存档 SHA-256 为 `85476F3C36849CC11988A7194A303F7F780F84B4B9B5F6B55462099A599E79B4`。

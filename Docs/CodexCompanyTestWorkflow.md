# BlockRealm Codex 多轮优化与公司验收工作流

这份手册用于“测试人员在另一台电脑拉取 GitHub 工程，再用 Codex 继续优化和验收”的场景。每一轮只解决一个可验收目标，先建立基线，再修改，再运行同一组回归，最后由主代理集成。不要把文档中的示例路径改成作者电脑上的绝对路径。

项目仓库：<https://github.com/fishinggoing/BlockRealm>

目标版本：Unreal Engine 5.8.x（当前验证版本为 5.8.3）、Windows 64 位、DirectX 12。DLSS 是外部依赖，按 NVIDIA 的许可和下载流程安装；UE MCP Bridge 的可迁移源码已经在 `Plugins/UE_MCP_Bridge/`，使用方式见 [MCP.md](MCP.md)。

## 使用规则

1. 主代理负责架构、分工、合并、最终验证和提交。子代理只负责有明确边界的调查、实现、测试或审查。
2. 若启用子代理，统一使用 `gpt-6.1-sol`、`max` 推理档；同一文件不能由两个实现代理同时编辑。读代码和跑测试可以并行，修改和合并必须串行。
3. 每一轮必须先记录基线提交、引擎版本、显卡、驱动、DLSS 状态、窗口分辨率和测试命令。没有基线就不能声称“优化后通过”。
4. 一轮只处理一个主题。发现无关问题时记录到 backlog，不要顺手扩大改动范围。
5. 不修改 MCP 源码，不新增外部工作流，不把 MCP 的 `Binaries/`、`Intermediate/`、`Saved/` 或本机状态文件提交进 Git。
6. 不写入作者机器的盘符、用户名、工程绝对路径、访问令牌、下载凭据或临时目录。构建工具使用 `-EngineRoot`、`UE_ENGINE_ROOT`、`UE5_ROOT` 和 `UE_AUTOSDK_ROOT`。
7. 不用截图、文档或单次 FPS 替代运行时证据。每个结论至少有命令输出、JSON / 日志和必要的截图。
8. 失败时保留失败证据，恢复到本轮开始的工作树或提交；不要删除失败日志来制造“全绿”。
9. 默认不生成 ZIP。只有交付轮明确要求时才执行 `Tools/Pack-Portable.ps1`。
10. 每轮完成后由主代理执行 `git diff --check`、敏感信息扫描和全量回归，再提交一个能说明主题的 commit。

## 测试人员第一次接管工程

在 PowerShell 7 中执行以下命令。`<ENGINE_ROOT>`、`<AUTOSDK_ROOT>` 和 `<RUN_ID>` 只是本机参数，不要写回源码或文档。

```powershell
git clone https://github.com/fishinggoing/BlockRealm.git
Set-Location BlockRealm
git switch -c codex/company-<RUN_ID>
$env:UE_ENGINE_ROOT = '<ENGINE_ROOT>'
$env:UE_AUTOSDK_ROOT = '<AUTOSDK_ROOT>'   # 没有 AutoSDK 时可省略
git status --short
```

然后安装与 UE 5.8 匹配的 NVIDIA DLSS / Streamline 插件到本机项目的 `Plugins/`。插件压缩包不要提交 Git。确认 `BlockRealm.uproject` 能被编辑器识别后，再连接本机 UE MCP Bridge；连接失败时先按 [MCP.md](MCP.md) 检查，不要修改 MCP 源码绕过问题。

## 每一轮的固定协议

每个主题都按下面七步执行，下面的提示词已经把这七步写进去了：

1. **接管**：读取当前分支、工作树和最近提交，确认没有未授权改动。
2. **基线**：运行本主题相关的现有测试，保存到本机的 `Deliverables/Validation/<RUN_ID>/`；该目录被 Git 忽略。
3. **计划**：写出问题、假设、修改文件、验收条件和回滚点。
4. **实现**：只修改本轮所有权范围内的文件，保留现有接口和存档兼容性。
5. **验证**：先编译，再运行定向测试，最后运行完整 smoke；光影和交互需要 UE MCP 的真实 PIE / CVar / 截图证据。
6. **审查**：扫描固定路径、密钥、生成缓存和无关 diff；检查失败日志是否仍存在。
7. **交付**：只有全部门禁通过才提交；报告 commit、命令、证据文件、已知限制和下一轮 backlog。

## 提示词 00：环境接管与只读体检

把下面整段发给 Codex 主代理。此轮不改代码。

```text
你是 BlockRealm 公司测试环境的主代理。当前工作目录是刚从
https://github.com/fishinggoing/BlockRealm 拉取的工程，工程路径由本机决定。

目标：只读完成环境接管，不修改源码、不修改 MCP、不创建 ZIP、不提交代码。

请依次检查并报告：
1. git branch、HEAD、git status、远程地址、当前是否有未提交改动。
2. BlockRealm.uproject、Source、Content、Config、Tools 和 Plugins/UE_MCP_Bridge 是否存在。
3. UE_ENGINE_ROOT / UE5_ROOT / UE_AUTOSDK_ROOT 是否可用；如果没有，说明需要用户提供哪个参数。
4. 是否安装了 UE5.8 匹配的 DLSS、StreamlineCore、StreamlineReflex、StreamlineDLSSG；缺失时只记录阻塞，不下载未知来源文件。
5. 是否能通过 UE MCP 连接编辑器；连接失败时给出 MCP.md 中对应的检查步骤。
6. 用 git ls-files 和 git check-ignore 检查 Binaries、Intermediate、Saved、DerivedDataCache、Deliverables 和 MCP 编译缓存没有进入版本控制。
7. 扫描源码和文档中的作者机器绝对路径、令牌、私钥和旧项目名称。

输出一个“环境接管报告”，字段必须包括：
repo_commit、branch、engine_detected、dlss_detected、mcp_connected、
generated_files_tracked、secret_scan、blockers、next_action。
所有路径只能使用相对路径或 <ENGINE_ROOT> 这类占位符。
```

## 提示词 01：建立可重复基线

```text
在不修改游戏逻辑的前提下，为当前 BlockRealm 分支建立公司测试基线。

先记录 git HEAD、Windows 版本、CPU/GPU、显卡驱动、内存、UE 版本、DLSS 能力、
窗口分辨率和屏幕百分比。然后按本机条件依次运行：

./Tools/Build.ps1 -Stage Editor -EngineRoot <ENGINE_ROOT>
./Tools/Validate.ps1 -FreshWorld -OutputName <RUN_ID>_Baseline

如果没有可用的 DLSS 插件或 Windows Development 包，不能伪造通过；改用能执行的
Editor / MCP smoke，并把阻塞写入报告。保存 runtime.json、runtime.log、截图和编译日志。

基线报告要列出：通过数、失败数、实际分辨率、DLSS/TSR 模式、Reflex、帧生成、
VSM 告警、Hit Lighting 状态、平均帧时间、P95、内存峰值（若能取得）和未覆盖项目。
此轮只提交测试报告模板或不提交任何文件，不提交运行产物。
```

## 提示词 02：光影、VSM 和地下可见性

```text
本轮只处理 BlockRealm 的光影稳定性和地下可见性。

已知基线：当前使用 HWRT Lumen Surface Cache 兼容路径；Hit Lighting 在参考 RTX
设备上创建 RTPSO 时崩溃；洞穴远处、黄昏和部分夜间远景偏暗；日志可能出现
VSM Non-Nanite Marking Job Queue overflow。

先用现有 Lighting 验证脚本和 UE MCP 固定机位复现问题，保存正常光、禁灯、洞口、
夜晚、黄昏、屋顶遮挡的原图和日志。不要用角色自带补光掩盖问题，不要把关闭屏幕
提示当成修复，也不要简单提高全局亮度或增大阴影页池后宣称解决。

允许修改：BlockRenderSettings、天空/雾/材质参数、局部真实光源、光源生命周期和
光影测试夹具。必须保持无光源洞穴仍然昏暗、真实火把/荧石/熔炉有效、屋顶能遮挡。

验收条件：
1. VSM 告警数量相对基线下降或给出可复现的引擎限制说明；若仍存在，报告不能写“修复”。
2. 五类固定机位全部有 runtime 状态和截图；不能只凭 JSON success=true。
3. 近处、远处、洞口和封闭屋顶分别可辨认且没有角色光圈、明显漏光或过曝。
4. Hit Lighting 不稳定时继续走兼容分支，并明确记录实际 render_path。
完成后运行完整 smoke，提交源码和参数变更，不提交缓存。
```

## 提示词 03：区块流式、碰撞和性能

```text
本轮只处理区块加载、网格/碰撞生成和帧时间。

先用基线流程测量：出生加载、远点传送、持续高速移动、飞行跨多个区块、区块卸载后
返回、连续挖放和 1,000 / 10,000 / 100,000 次编辑。记录主线程帧时间、后台任务数、
待处理区块、内存峰值、碰撞烹制耗时和卡顿位置。

检查 BlockWorld 的 worker、epoch/version 取消、主线程应用预算、同步碰撞更新和活动区块
上限。修改必须保证：旧结果不能覆盖新编辑，未加载区块的数据射线仍与权威数据一致，
玩家不会掉入尚未准备好的碰撞。

验收条件：
1. 远距离加载、卸载、返回和高速移动都有实际碰撞断言。
2. 单帧同步碰撞更新有明确预算，超预算时有可观测日志而不是无界阻塞。
3. 活动区块、网格、碰撞和光源都有可解释的上限或回收策略。
4. 1,000 次编辑通过后，再记录 10,000 / 100,000 次的失败点、帧时间和内存，不能只报告 FPS。
5. 全量 smoke 和世界自检通过。
```

## 提示词 04：存档、迁移和恢复

```text
本轮只处理 SaveWorld/LoadWorld 和玩家/世界数据一致性。

先建立存档字段清单：种子、稀疏方块修改、玩家位置/视角、库存、选槽、工具耐久、
昼夜、火把及支撑、作物、熔炉、床和重生点，以及明确不保存的瞬态。先在一个临时世界
编辑方块、移动、挖掘、放置、种植、烧制、放床、死亡，再保存并关闭进程，重新启动后
逐字段比较。不要把“返回标题后继续”当作跨进程验证。

允许增加版本号迁移、校验和、备份、坏档隔离、保存失败回滚和测试夹具。写入必须继续
使用同目录临时文件和原子替换；磁盘空间不足、替换失败和合法但异常的数值都要有结果。

验收条件：跨进程逐字段恢复通过；旧版本存档可迁移；损坏档不会污染当前世界；保存
失败保留会话和原文件；报告包含每个字段的 before/after，不把瞬态误写入存档。
```

## 提示词 05：背包、物品、挖掘和放置

```text
本轮只处理玩家物品闭环和方块交互。

先用真实鼠标/键盘和 UE MCP 检查：背包打开、槽位点击、快捷栏切换、堆叠、拖拽、拆分、
合并、丢弃、拾取、工具耐久、等级限制、挖掘进度、取消、跳跃脚下放置、胶囊净空、
水边放置、设施占用和失败提示。自动点击坐标必须读取当前控件边界，禁止硬编码旧分辨率。

把物品/方块/工具规则集中到数据驱动定义，交互目标继续由权威 DDA 和碰撞检查共同确认。
扣库存必须发生在成功放置/生成之后，失败不能扣物品；采集必须在成功破坏之后发放掉落。

验收至少覆盖 1080p、1440p、窗口缩放、空槽、满槽、满背包、切换工具、挖掘取消、
跳跃搭建、水中搭建和存档重载。
```

## 提示词 06：生物、生存和设施

```text
本轮只处理生物 AI、生存伤害、掉落和农业/熔炉/床的规则。

先复现猪、羊、敌对生物的生成、游走、避水、受击、追击、死亡、掉落、拾取、离开区域
和重新加载。检查是否有对象越界、重复掉落、无限生成、AI Tick 失控和设施附近阻塞。

然后检查生命、饥饿、氧气、溺水、摔落、死亡、床重生、夜间休息、作物生长、熔炉队列、
燃料和设施失去支撑的行为。背包/暂停冻结规则必须一致，恢复后计时不能跳变。

验收条件：生物数量有上限和距离裁剪；死亡和掉落只发生一次；生物/掉落需要持久化的
字段能跨进程恢复；设施状态有明确的无效化和回收规则；所有失败提示与实际状态一致。
```

## 提示词 07：HUD、输入、音频和可访问性

```text
本轮只处理界面和输入体验，不改变世界规则。

用 1280x720、1920x1080、2560x1440、21:9 和窗口缩放检查标题、暂停、背包、悬停说明、
生命/饥饿/呼吸、挖掘进度、设施提示和错误消息。检查中文字体回退、文本裁剪、颜色对比、
低氧/受伤颜色、鼠标捕获和菜单关闭后的动作门控。

把所有自动化命中区改为运行时控件边界；加入按键重绑定、手柄映射、UI 缩放、色盲友好
颜色和可关闭的屏幕技术提示。补齐挖掘、放置、拾取、游泳、战斗、火把和环境 3D 音效。

验收用真实输入完成开始/暂停/继续、背包切换、鼠标释放、Alt-Tab 后恢复、滚轮/数字键、
重绑定和不同分辨率截图；不得用放大截图掩盖原始视口文字不可读。
```

## 提示词 08：画质档位、DLSS/TSR 和资产管线

```text
本轮只处理渲染档位、资产导入和可重复构建。

核对 TSR、DLSS Quality/Balanced/Performance、Reflex、动态分辨率、运动模糊、景深、
帧生成和光线重建的实际运行时状态。DLSS 不可用时必须明确回退到 TSR，不能只显示菜单文字。

检查 30 张 PBR 纹理、法线/粗糙度、材质编译、透明材质、字体和音频生成脚本是否能在新机器
上重跑。`Tools/generate_assets.py` 只能使用项目字体、环境变量或自动查找系统字体，不得写入
机器绝对路径。

验收保存实际输入分辨率、输出分辨率、ScreenPercentage、渲染路径、显存、着色器编译告警和
每档截图；重新导入后材质错误必须为零。没有第二台显卡时要明确写“未验证”，不能外推性能。
```

## 提示词 09：Minecraft 级功能扩展

只有前面稳定性和存档门禁通过后，才进入本轮。每次只选一个子系统。

```text
本轮实现一个明确的 Minecraft 级扩展：<FLUIDS | WORKBENCH_CONTAINERS |
REDSTONE | ITEMS_TOOLS_ARMOR_ENCHANTMENTS | MOB_ECOLOGY |
WORLD_BIOMES_STRUCTURES_DIMENSIONS | MULTIPLAYER>。

先阅读现有 BlockTypes、VoxelData、BlockWorld、SaveWorld/LoadWorld、输入、HUD 和 smoke，
写出数据模型、线程模型、存档字段、网络边界（若适用）和回滚方案。不要把新功能塞进一个
巨型 GameMode 函数，也不要破坏已有的 DDA、区块 epoch、库存扣除和存档迁移。

实现顺序必须是：最小数据模型 -> 单机权威逻辑 -> UI/输入 -> 存档 -> 性能上限 -> 回归。
先添加针对该系统的失败测试，再添加成功测试。报告必须说明当前只完成了哪个子集，不能把
“有一个演示方块”写成系统完成。
```

## 提示词 10：工程化、CI 和发布门禁

```text
本轮只处理公司可重复测试和发布流程。

从一个全新的临时目录按 README 拉取仓库，使用环境变量或 -EngineRoot 构建，不依赖作者电脑
的盘符。执行 Editor 编译、Content 导入、Cook/Stage（若安装了外部插件）、FreshWorld smoke、
定向光影测试和公开仓库敏感信息扫描。

建立 CI 或本机等价脚本，至少输出：commit、引擎版本、编译结果、Cook 结果、测试通过/失败、
实际分辨率、DLSS/TSR 状态、告警摘要、耗时、内存/显存和证据目录。将失败日志作为附件保留。

确认 Git 不跟踪 Binaries、Intermediate、Saved、DerivedDataCache、Deliverables、MCP 编译缓存、
ZIP、私钥和 token。确认 MCP 只在 Editor/回环接口工作，Shipping 包不暴露编辑器桥接端口。
只有所有必需门禁通过才能打 `v<version>-test` 标签或生成 ZIP。
```

## 最终发布验收提示词

```text
你是最终发布审查代理。不要修改代码，审查当前分支是否可以交付给公司测试。

检查：
1. git status 干净，commit 和远程分支一致。
2. 当前版本的基线、定向测试、完整 smoke、光影专项和性能记录都能定位到文件。
3. 没有把固定路径、令牌、旧项目、构建缓存、运行存档或未授权第三方文件提交到 Git。
4. README、MCP.md、构建脚本、外部 DLSS 安装说明和实际依赖一致。
5. 所有失败项、未验证项和硬件限制都明确列出，没有用历史报告代替当前报告。
6. Windows Development/Shipping、DLSS 不可用回退、第二台机器和长时压力测试的状态分别记录。

输出 RELEASE_GATE.json，字段包括：commit、required_checks、passed、failed、blocked、
known_limits、artifacts、recommended_next_round、release_decision。
release_decision 只能是 PASS、PASS_WITH_LIMITS 或 BLOCKED。
```

## 每轮报告模板

每轮可以在本机 `Deliverables/Validation/<RUN_ID>/report.md` 使用以下结构；运行产物不提交到公开仓库，除非另有授权。

```markdown
# BlockRealm <round> 测试报告

- run_id:
- branch:
- commit_before:
- commit_after:
- operator:
- engine:
- GPU / driver:
- display:
- DLSS / TSR:
- MCP:

## 本轮目标

## 基线证据
- commands:
- logs:
- screenshots:

## 修改范围
- files:
- data/schema changes:
- rollback point:

## 验收结果
- passed:
- failed:
- blocked:
- warnings:

## 已知限制

## 下一轮 backlog

## 提交决定
- PASS / PASS_WITH_LIMITS / BLOCKED
```

## 公司测试的停止条件

- 编译失败、Cook 失败、存档迁移失败、关键回归失败或出现数据损坏时，停止本轮继续扩展。
- 光影出现 Fatal、VSM 告警增加、洞穴遮挡被破坏或角色补光重新出现时，恢复本轮改动并保留证据。
- 测试人员无法复现基线、外部 DLSS/UE 依赖缺失或 MCP 未连接时，只做环境报告，不修改业务代码。
- 任何代理要求把凭据、绝对路径、运行缓存或第三方闭源二进制提交到 Git 时，拒绝该操作并记录原因。

这样每台测试电脑都能从同一个公开 commit 开始，用同一组提示词逐轮推进；每一轮都有独立目标、可回滚点和可审查证据，最终结果不会依赖某一台电脑的临时状态。

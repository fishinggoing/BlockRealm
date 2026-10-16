# BlockRealm 只读集成审查

2026-10-14。审查范围：GameMode/Character/HUD 公共接口、VoxelData 的生成/存档、BlockWorld 的任务与流式加载、BlockChunk 的碰撞应用。本轮只更新此文档，没有改动 Source，没有启动 UE 或编译。以下是静态审查结论；构建和运行验证由主代理完成。链接行号对应本轮复核时源码，后续修改可能改变行号。

当前没有确认的剩余实质问题。此前报告的保存失败丢失进度、暂停时角色下落两项均已复核修复。

## 已复核的修复

### 保存失败时保留当前会话

原触发条件：存档文件被其他进程以不共享删除的方式打开、磁盘空间不足等造成写入失败后，点击“返回标题”或“退出”。旧流程忽略保存结果，可能退出丢失修改，或下一次“继续”用旧盘档覆盖内存中的世界、位置与背包。

现状：[SaveAdventure](./Source/BlockRealm/BlockRealmGameMode.cpp:140) 返回 `bool`，声明与实现一致。[ReturnToTitle](./Source/BlockRealm/BlockRealmGameMode.cpp:149) 在保存失败时立即返回；[QuitAdventure](./Source/BlockRealm/BlockRealmGameMode.cpp:166) 在已开始会话保存失败时也立即返回。失败分支保留当前会话状态并显示提示；底层原子替换继续保护旧盘档。

运行验证建议：对测试存档持有不共享删除的句柄，编辑一个方块并消耗一项背包，分别尝试返回标题和退出；确认失败提示出现，当前会话及进度仍在。释放句柄后，保存和返回标题应成功。本轮未运行该场景。

### 暂停冻结位置，恢复原移动模式

原触发条件：跳跃或下落中打开 Esc 菜单。仅清零速度时，CharacterMovement 会在角色 Tick 前继续积分重力，导致暂停期间逐帧下降。

现状：[TogglePauseMenu](./Source/BlockRealm/BlockRealmGameMode.cpp:121) 在暂停时保存 `MovementMode` 与 `CustomMovementMode` 并设置 `MOVE_None`，恢复时还原保存的模式。HUD 的 Resume 调用此函数。Smoke 已加入[暂停位置冻结](./Source/BlockRealm/BlockRealmGameMode.cpp:298)和[恢复后保持飞行](./Source/BlockRealm/BlockRealmGameMode.cpp:313)检查；本轮只核对代码，没有执行 Smoke。

运行验证建议：分别在跳跃下降与飞行中暂停 5 秒，确认坐标保持，恢复后原移动模式保持。

### 恢复远处存档前保留初始碰撞区块

[LoadWorld](./Source/BlockRealm/BlockWorld.cpp:495) 重置初始就绪状态，并将初始中心移到存档位置。[RequestStreaming](./Source/BlockRealm/BlockWorld.cpp:262) 使用 `bKeepInitialChunks` 保留初始 3×3，在角色到达已准备区域后释放；标题预览位置不会再把这些区块卸载。[ConsumeCompletedMeshes](./Source/BlockRealm/BlockWorld.cpp:425) 对保留的初始区块立即准备碰撞。[StartAdventure](./Source/BlockRealm/BlockRealmGameMode.cpp:89) 先加载存档，等待初始区块及对应网格版本的碰撞就绪，再恢复角色位置与步行。

运行验证建议：在远离出生点的位置保存、退出并重启，确认恢复位置、背包与方块修改，并确认角色不会在恢复后掉入尚未准备碰撞的地形。

## 其余静态审查结论

角色世界访问器使用 `GetBlockWorld()`；GameMode 的 `FWorldSaveState` 字段已有 `BlockTypes.h` 包含。HUD hitbox、输入模拟、JSON 与字体辅助函数使用的 UE5.8 声明均能在引擎头中找到。未发现可确认的新增 API 集成或编译阻断。

世界任务捕获值快照与线程安全 mailbox，不捕获 UObject；结束或失效通过 atomic 接收开关和新 mailbox 隔离旧结果，应用前检查 Epoch、Revision 与 wanted 状态。JSON 先解析临时记录，失败不修改当前世界或输出状态；Windows 原子替换失败时保留旧档。最新完成队列消费路径也已复核，未发现明确的数据竞争、旧结果污染或部分解析写入缺陷。

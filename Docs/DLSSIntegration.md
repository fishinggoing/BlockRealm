# BlockRealm DLSS 集成记录

2026-10-14。目标为 UE 5.8.3、Win64 x64、DX12/SM6 上的真实 DLSS Super Resolution 技术样板。源码接入与官方包核对已完成。最终默认配置的独立 Development 包 `ReleaseFinal1080` 与真正禁用 NGX 的 `ReleaseTSRFallback` 均通过全部 60 项 Smoke 检查，实际输出 1920×1080、保留 RT/HW Lumen 与正常白天画面。默认配置记录三档真实 NGX feature；禁用 NGX 后回退 TSR 100%、AA=4。最终配置关闭全局 PSO 预缓存，是已验证的兼容绕过，底层 PSO 创建错误的根因尚未完全定位。

## 官方包与项目依赖

来源为用户提供的 `the supplied UE5.8 DLSS plugin archive`，没有下载替代 DLL 或改动官方插件源码。

`Plugins/DLSS/DLSS.uplugin` 为 `8.8.0-NGX310.9.1`，`EngineVersion=5.8.0`；包内 ReleaseNotes 第 1 页支持 UE 5.8 至 5.6、Win64 x86_64/WinGDK，Streamline SDK 2.14.1。UE 5.8.3 属于该引擎小版本系列，最终兼容性仍以构建与运行结果确认。

2026-10-14 已核对项目描述文件显式启用 `DLSS`、`StreamlineCore`、`StreamlineDLSSG`、`StreamlineReflex`、`ProceduralMeshComponent`。DLSS 自动依赖 `StreamlineNGXCommon`；Streamline 功能插件自动依赖其 Core/RHI 插件。旧 `Streamline` 插件是兼容入口，会连带启用 DeepDVC，工程采用独立功能插件。NIS、DeepDVC 与 Movie Pipeline 不属于本样板的画面功能。

`BlockRenderSettings.cpp` 需要以下模块，可放在 `PrivateDependencyModuleNames`，因为公共头没有暴露插件或 RHI 类型：

- `DLSSBlueprint`
- `StreamlineBlueprint`
- `StreamlineDLSSGBlueprint`
- `StreamlineReflexBlueprint`
- `RHI`，用于记录实际渲染适配器和驱动。

插件必须随 Development 包正常 stage，其 NGX/Streamline 第三方运行文件由各插件 Build.cs 管理，不手工替换引擎 DLL。本机仅安装项目级插件，避免引擎与项目各加载一份 DLSS。

## 接口与运行行为

`Source/BlockRealm/BlockRenderSettings.h/.cpp` 提供架构约定的三个静态函数。应在引擎 `PostEngineInit` 之后的游戏线程调用，GameMode 的 `BeginPlay` 是合适入口；禁止构造函数/模块早期启动就查询并固定缓存失败结果。

| 模式 | 行为 |
| --- | --- |
| 0 | 关闭 DLSS-SR/RR，设置 AA=TSR、ScreenPercentage=100 |
| 1 | 官方 `UDLSSMode::Quality`，使用插件返回的最佳比例 |
| 2 | 官方 `UDLSSMode::Balanced`，使用插件返回的最佳比例 |
| 3 | 官方 `UDLSSMode::Performance`，使用插件返回的最佳比例 |
| 其他值 | 记录无效模式，回退 TSR |

`IsDLSSSupported()` 必须满足 DLSS 运行模块已经加载且 `UDLSSLibrary::QueryDLSSSupport() == UDLSSSupport::Supported`。CVar 是否存在、显卡名称是否包含 RTX 和当前选择的 UI 按钮都不参与支持判定。

`ApplyMode()` 使用 UE5.8 官方 `GetDLSSModeInformation()` 获取质量比例，再调用 `EnableDLSS()`。`SetDLSSMode()` 已弃用，代码不使用它。显式质量模式的比例在本 API 中不依赖传入屏幕分辨率，因此传 `FVector2D::ZeroVector`；Auto 模式不在本工程接口中。比例必须有限、位于官方范围内且小于 100%，否则回退 TSR，防止把 DLAA 冒充 SR。

DLSS 采用引擎 TAA 的时域路由，开启 `r.TemporalAA.Upscaler` 与 `r.TemporalAA.Upsampling`；关闭动态分辨率以保持菜单质量档位。插件 API 启用后再回读支持与启用状态以及已设置的比例。失败通过官方 `EnableDLSS(false)` 关闭 DLSS、恢复 TSR 100%，不会为了绕过硬件/驱动检查而强设开启 CVar。

CVar 的 int/float 写入及 NIS 关闭明确使用 `IConsoleVariable::Set(Value, ECVF_SetByCommandline)`，与官方 `EnableDLSS()` 对共享时域 CVar 使用的优先级一致，并能覆盖项目设置。此前 `SetByConsole` 高于官方 API 的 `SetByCommandline`，会将 TSR 关闭状态锁住，导致再次选择 DLSS 时被拒绝；现已移除对 `r.NGX.DLSS.Enable` 的手工写入，开启与关闭都交由官方 API。显式 Flags 仍避免 `SetWithCurrentPriority` 的 implicit-priority 告警，值的回读检查与失败回退继续保留。最终 `ReleaseFinal1080` 已通过 TSR→Quality→Balanced→Performance→Quality 的实际菜单切换检查。

每次设置都用 `UStreamlineLibraryDLSSG::SetDLSSGMode(Off)` 明确关闭 FG/MFG，也关闭 DLSS-RR；本样板验证普通渲染帧的超分辨率。Reflex 仅在官方 `QueryReflexSupport()==Supported` 时设为 `Enabled`，不启用 Boost。ReleaseNotes 第 1 页明确 8.8.0 修正了旧版 Reflex 在不支持硬件上误报支持的问题，因此不能替换为旧库或凭 NVIDIA 型号硬开。

`GetStatus()` 显示官方启用状态、回读的比例、Reflex 与帧生成状态；TSR 回退附实际原因。`LogBlockRenderSettings` 为每次选择输出 RequestedMode、AppliedMode、官方枚举名、实际 RHI 适配器/用户驱动版本、ScreenPercentage、Reflex 枚举和 FG 模式。状态文字“DLSS Quality”表示 API 已启用并且时域配置成立，还需要下面的实际渲染检查。

## 驱动要求与当前机器

当前用户设备为 RTX 4060 Laptop，已知用户驱动 566.24。硬件属于 DLSS-SR 和单帧生成可支持的 RTX 40 系列，不属于 MFG 所需的 RTX 50 系列。工程关闭 FG，因此不要求为验证 SR 特意改动 HAGS 或安装驱动。

包内文档有不同适用范围：

| 包内来源 | 原文要求 | 对本机的含义 |
| --- | --- | --- |
| `Getting Started with NVIDIA DLSS 4 for Unreal Engine.pdf`，第 2 页（内容标题为 DLSS 4.5） | NVIDIA RTX GPU；572.16 driver or newer | 566.24 低于整包入门指南要求，不能承诺全部 DLSS 4.5 功能可用 |
| `Documentation/DLSS_Super_Resolution_UE_Plugin_Guide.pdf`，第 3 页 | driver issued after March 3, 2022 (for instance 512.15) | 给出 SR 的基础要求；不能据此推断新包内任意模型/预设一定可用 |
| `Documentation/DLSS_Programming_Guide_Release.pdf`，第 11、52、53、57 页 | SDK/特定功能可能各自要求更高版本；查询 capability 与 driver requirement，不满足则回退 | 本工程依 NGX/DLSS 官方运行时结果决定 SR 可用性 |

因此没有把 572.16 或 512.15 硬编码为 `IsDLSSSupported()` 的门槛。官方 API 返回 `NotSupportedDriverOutOfDate` 时调用 `GetDLSSMinimumDriverVersion()`，显示真实检测驱动与返回的最低版本；最低版本为 0 时诚实显示“插件未报告最低版本”，不杜撰版本。对其他失败分别显示硬件、Windows、构建平台、API 捕获工具或一般 NGX/RHI 初始化原因。

本次没有安装或升级驱动。已完成的 EditorPreview 中，实际适配器为 RTX 4060 Laptop、驱动为 566.24，SR capability 返回 `bNeedsUpdatedDriver=0`、最低版本 470.0，工程官方查询为 `DLSSSupport=Supported`。因此本机这次 SR 路径实际可用；470.0 是此次运行时 capability 报告值，不能替代整包指南或推广为所有预设、功能和机器的统一门槛。其他环境若返回驱动过旧，仍按官方失败结果提示最低版本并回退 TSR。

## 程序网格与硬件 Lumen

DLSS-SR 本身不要求光追。BlockRealm 的动态世界使用 `UProceduralMeshComponent`，其三角形可进入硬件光追场景，所需项目设置为 DX12/SM6、`r.RayTracing=1`、`r.SkinCache.CompileShaders=1`、`r.Lumen.HardwareRayTracing=1` 与 `r.RayTracing.Geometry.ProceduralMeshes=1`。引擎插件源码 `Engine/Plugins/Runtime/ProceduralMeshComponent/.../ProceduralMeshComponent.cpp` 第 35 行定义程序网格开关，第 426 行起提交动态 RT instances。

PMC 不是拥有构建距离场和常规 Lumen mesh cards 的静态网格。`r.GenerateMeshDistanceFields=1` 不会为动态方块世界自动补出这些数据，软件 Lumen 的回退不能与硬件 Lumen 等价。

建议技术样板使用 `r.Lumen.HardwareRayTracing.LightingMode=1`。UE5.8 `LumenHardwareRayTracingCommon.cpp` 的定义：0=Surface Cache；1=GI 与反射的 hit lighting；2=仅反射 hit lighting。模式 1 能在命中动态程序三角形后计算材质与灯光，secondary bounces 仍使用 Surface Cache，因此不是完全解决所有间接光照缺口。它明显增加 GPU 成本，需要主代理记录帧时间后决定默认值，不能用未经测量的 FPS 承诺代替验证。方块编辑更新网格/RT 加速结构也会产生帧尖峰。

## EditorPreview 已完成的实际验证

证据文件：[runtime.json](./Deliverables/Validation/EditorPreview/runtime.json) 与 [runtime.log](./Deliverables/Validation/EditorPreview/runtime.log)。该轮通过 `UnrealEditor.exe -game -BlockRealmSmoke` 执行，是实际渲染运行；独立 Development 包的结果另行记录。

| 已观察到的证据 | 日志位置与结果 |
| --- | --- |
| 实际 DX12 适配器与驱动 | [runtime.log:453](./Deliverables/Validation/EditorPreview/runtime.log:453) 为 `Using Default RHI: D3D12`；第 459 行为 NVIDIA GeForce RTX 4060 Laptop GPU，第 462 行驱动 566.24，第 481 行选择 adapter 0 |
| NGX D3D12 初始化成功 | [runtime.log:1977](./Deliverables/Validation/EditorPreview/runtime.log:1977) 的 `NVSDK_NGX_D3D12_Init_with_ProjectID` 返回 `NVSDK_NGX_Result_Success`，随后 capability 查询也成功 |
| 本机 SR 实际受支持 | [runtime.log:1980](./Deliverables/Validation/EditorPreview/runtime.log:1980) 的 `bNeedsUpdatedDriver=0`，第 1981–1982 行报告 SR 最低驱动 470.0；第 1988 行 `DlssAvailable=1`，第 2003 行 `DLSS-SR=1` |
| Quality 的实际 NGX feature | [runtime.log:2224](./Deliverables/Validation/EditorPreview/runtime.log:2224) 记录 `Creating NGX DLSS Feature`，`SrcRect=950x593`、`DestRect=1423x889`、`NGXPerfQuality=MaxQuality(2)`、`DenoiseMode=Off`。这条证据来自渲染路径，补充了 API 与 CVar 状态 |
| 菜单 TSR/Quality 切换 | [runtime.log:2289](./Deliverables/Validation/EditorPreview/runtime.log:2289) 为 TSR 100%；[runtime.log:2305](./Deliverables/Validation/EditorPreview/runtime.log:2305) 为 Quality 66.700%、`DLSSSupport=Supported`、`ReflexSupport=Supported`、`FG=Off` |

`runtime.json` 的 `success=true`、`failed=[]`，记录 `dlss_supported=true`、`dlss_cvar_enabled=true`、ScreenPercentage≈66.7%，最终状态为 Quality、Reflex 开、帧生成关。Smoke 的鼠标菜单、挖掘/建造、背包、隔离存档重载、暂停位置冻结、恢复飞行与返回标题继续等检查均通过。该文件的状态与测试结果应和上述实际 NGX 日志一起阅读；单独的 CVar 或 Supported 枚举仍不足以说明渲染路径。

这轮采样为 1,373 帧、平均约 119.16 FPS、P95 帧时间约 8.34 ms。性能数值只适用于该预览：启动参数虽然请求 1920×1080，实际 NGX 输出是 1423×889；[runtime.log:2264](./Deliverables/Validation/EditorPreview/runtime.log:2264) 又记录 `Set new viewmode: ShaderComplexity`。因此这些 FPS 不能代表最终 1920×1080、正常 Lit/Lumen 画质下的性能，也不能用于比较三档质量。Quality feature 的证据出现在该视图切换之前，窗口尺寸与视图限制不改变此前已观察到的 DX12/NGX 初始化和 feature 记录。

## 独立包 PSO 诊断与兼容绕过

`Draft1080/runtime.log:1570–1574` 记录 D3D12 计算 PSO 创建失败 `8007000e`，shader hash `2FA1C95F422AE9C2` 对应 `FLumenScreenProbeGatherHardwareRayTracingCS`。真正启动期禁用 NGX 的 `DiagnosticTrueTSR` 仍有计算 PSO 错误，故 NGX 不是该错误的必要条件；目前不能将驱动、预缓存或音频直接定为根因。

最终默认配置在启动期设 `r.PSOPrecache.GlobalShaders=0`。UE5.8 [PSOPrecacheSettings.cpp:17](<UE_ENGINE_ROOT>/Engine/Source/Runtime/Engine/Private/PSOPrecacheSettings.cpp:17) 将它定义为 ReadOnly；[ShaderCompiler.cpp:5121](<UE_ENGINE_ROOT>/Engine/Source/Runtime/Engine/Private/ShaderCompiler/ShaderCompiler.cpp:5121) 在值为 0 时跳过全局计算及已注册全局图形 PSO 的启动预缓存。它不改变已 cook 的 shader、RT/Lumen 开关或 DLSS API，材质/组件预缓存也仍可运行。[PipelineStateCache.cpp:4951](<UE_ENGINE_ROOT>/Engine/Source/Runtime/RHI/Private/PipelineStateCache.cpp:4951) 在 `WITH_EDITOR` 下直接禁用 PSO 预缓存，因此此前 EditorLit 与 Draft1080 虽都打印 `GlobalShaders=1`，仍存在此执行路径差异。

该开关按全局 shader 类别关闭预热，不能仅跳过某几个失败 hash；代价是首次使用这些 PSO 时更可能产生编译卡顿。正常渲染仍在 [PipelineStateCache.cpp:4228](<UE_ENGINE_ROOT>/Engine/Source/Runtime/RHI/Private/PipelineStateCache.cpp:4228) 按需创建计算 PSO；若实际 dispatch 必需的 PSO 仍创建失败，[同文件:727](<UE_ENGINE_ROOT>/Engine/Source/Runtime/RHI/Private/PipelineStateCache.cpp:727) 仍会报 Fatal，所以它不能修复必需 shader 的兼容性问题。此前 `FinalNoGlobalPrecache` 启动参数对照为 58 项通过、0 项失败；最终固化配置与禁用 NGX 的两轮复测均为 60 项通过、0 项失败，验证了此兼容绕过能完成本机 Smoke 场景，仍不将它描述为底层驱动或 shader 根因的修复。

Agility DLL 已核对：独立包 `D3D12Core.dll` 与引擎原始 `AgilitySDK/1.618.5/Binaries/x64` 副本的整体 SHA256 相同；编辑器副本的全部 10 个 PE 节也相同，整体 hash 差异仅位于 PE checksum 与 Authenticode 证书表，两者均为有效 Microsoft 签名。因此未观察到该 DLL 的运行代码差异，不以整体 hash 不同认定损坏。

## 最终默认配置与 TSR 回退验证

最终默认配置的证据为 [ReleaseFinal1080/runtime.json](./Deliverables/Validation/ReleaseFinal1080/runtime.json)、[runtime.log](./Deliverables/Validation/ReleaseFinal1080/runtime.log) 与 [探索截图](./Deliverables/Validation/ReleaseFinal1080/06_Exploration.png)。报告为 `success=true`、60 项通过、0 项失败，实际输出 1920×1080；正常白天、保存后维持 Lit、菜单切换、挖掘/建造、隔离存档与重载、暂停冻结、飞行恢复、远处负坐标碰撞、区块卸载/保留边界及返回标题继续均通过。

[runtime.log:337](./Deliverables/Validation/ReleaseFinal1080/runtime.log:337) 的启动命令没有 PSO 配置覆盖参数；[第 761 行](./Deliverables/Validation/ReleaseFinal1080/runtime.log:761) 确认固化的默认 `r.PSOPrecache.GlobalShaders=0` 生效。第 429、431、763 行分别为 `r.RayTracing=1`、`r.Lumen.HardwareRayTracing=1`、`r.Lumen.HardwareRayTracing.LightingMode=1`。日志未出现此前的计算 PSO 创建失败。实际显卡为 RTX 4060 Laptop、驱动 566.24，SR 官方支持查询为 `Supported`，Reflex 开、FG/RR 关。

| 实际菜单模式 | 回读比例 | 渲染路径证据 |
| --- | --- | --- |
| TSR | 100% | [runtime.log:1793](./Deliverables/Validation/ReleaseFinal1080/runtime.log:1793)，Smoke 同时检查 AA=4 |
| Quality | 66.7% | [runtime.log:1842](./Deliverables/Validation/ReleaseFinal1080/runtime.log:1842)，NGX `MaxQuality(2)`，1281×721→1920×1080 |
| Balanced | 58.0% | [runtime.log:1872](./Deliverables/Validation/ReleaseFinal1080/runtime.log:1872)，NGX `Balanced(1)`，1114×627→1920×1080 |
| Performance | 50.0% | [runtime.log:1902](./Deliverables/Validation/ReleaseFinal1080/runtime.log:1902)，NGX `MaxPerf(0)`，960×540→1920×1080 |
| 恢复 Quality | 66.7% | [runtime.log:1933](./Deliverables/Validation/ReleaseFinal1080/runtime.log:1933)，再次创建 Quality feature |

该轮 1,504 帧的短 Smoke 样本平均约 120.01 FPS、P95 帧时间约 8.33 ms，包含菜单与不同档位，不能据此比较三档质量性能。固定相机与档位的 GPU/帧时间、首次使用 PSO、区块生成和编辑尖峰尚未单独测量；第二台机器与 Shipping 尚未验证。

真正禁用 NGX 的最终证据为 [ReleaseTSRFallback/runtime.json](./Deliverables/Validation/ReleaseTSRFallback/runtime.json) 与 [runtime.log](./Deliverables/Validation/ReleaseTSRFallback/runtime.log)。启动期用 `-ini:Engine:[ConsoleVariables]:r.NGX.Enable=0` 关闭 NGX；[第 1301 行](./Deliverables/Validation/ReleaseTSRFallback/runtime.log:1301) 明确记录 NGX library loading disabled，[第 1302 行](./Deliverables/Validation/ReleaseTSRFallback/runtime.log:1302) 为 `DLSS-SR=0`、`DLSS-RR=0`。第 429、431、763 行仍保留 RT/HW Lumen/LightingMode=1，两轮启动命令均没有 `r.RayTracing=0`。

回退报告为 `success=true`、60 项通过、0 项失败、1920×1080，`dlss_supported=false`、`dlss_cvar_enabled=false`、ScreenPercentage=100%。[runtime.log:1379](./Deliverables/Validation/ReleaseTSRFallback/runtime.log:1379) 记录 `DLSSSupport=NotSupported`、`AppliedMode=0` 与 TSR 100%；Smoke 对 TSR 及各 DLSS 菜单选择均验证 AA=4、100% 回退。状态栏显示 TSR 100% 和 DLSS 初始化失败的通用提示，主动禁用原因由上述日志确认，Reflex 开、FG 关。

官方插件 `r.NGX.Enable.AllowCommandLine` 默认 0；若另用 `-ngxdisable`，须在插件初始化前设为 1，本轮已采用直接启动期关闭 NGX 的有效对照。此前 `DiagnosticTrueTSR` 在关闭 NGX 后仍遇到原 PSO 错误，最终两轮通过的是固化的预缓存兼容绕过，底层根因仍未完全定位。

编辑器内 PIE 还可能受 `r.Editor.Viewport.OverridePIEScreenPercentage` 和插件 PIE 开关影响，交付性能结论优先采用独立包实测。8.8.0 已知 UE5.8 FG 运行/窗口 resize 问题不改变本样板 FG 保持关闭的设计。

## 交付许可文件

交付包应同时附带现有 `Plugins/DLSS/Source/ThirdParty/NGX/LICENSE.txt`、[NVIDIA-STREAMLINE-MIT-NOTICES.txt](./Docs/Licenses/NVIDIA-STREAMLINE-MIT-NOTICES.txt) 与 [NVIDIA RTX SDK EULA.pdf](<./Docs/Licenses/NVIDIA RTX SDK EULA.pdf>)。MIT 声明文件保留 `sl_reflex.h`、`sl_core_types.h`、`sl_helpers.h`、`sl_pcl.h` 四个原始首部块，包含各自版权年份、完整 permission notice 与免责文本，没有将其替换为 NGX 许可。插件及官方 ZIP 未提供独立 Streamline ThirdPartyNotices 文件，此记录不据此推断 SDK 内部第三方项均已覆盖。

EULA PDF 原样提取自官方 `2026.09.15_UE5.8_DLSS4.5Plugin_v8.8.0.zip` 内的 `Documentation/NVIDIA RTX SDK EULA.pdf`，大小 179,031 字节；输出与该 ZIP entry 的 SHA256 均为 `640951DE3A7AF9E5D30628CA7E528A5B815F6902FC879CBA96EB478B072578B8`。随最终包复制及复制后哈希核验由主代理完成。

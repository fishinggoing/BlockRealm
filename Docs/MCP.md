# UE MCP Bridge

本仓库包含 `Plugins/UE_MCP_Bridge` 的源码、模块描述和过滤配置，便于其他开发者下载后继续开发。它只保留可重建的源码；`Binaries/`、`Intermediate/`、生成的 UHT 文件和本机状态文件均被 Git 忽略。

## 路径与状态

- 插件通过 Unreal 的 `FPaths::ProjectDir()` 获取当前工程目录，不依赖作者电脑上的盘符、工程名或固定绝对路径。
- 状态写入当前工程的 `Saved/UE_MCP_Bridge/`，端口按当前工程目录派生；多个工程可以并存。
- `Plugins/UE_MCP_Bridge/.ue-mcp-install.json` 只记录插件来源版本、引擎版本和平台，不记录本机目录。
- 工程自己的 MCP 配置可以放在项目根目录的 `ue-mcp.yml`、`ue-mcp.local.yml` 或按环境命名的配置文件中；这些文件按项目路径查找，仓库不要求固定位置。

## 使用

1. 安装与项目匹配的 Unreal Engine 5.8，并打开 `BlockRealm.uproject`。
2. 在编辑器中启用 **UE MCP Bridge**；如果编辑器提示模块需要重新编译，允许它按当前机器生成 `Binaries/` 和 `Intermediate/`。
3. 让 MCP 客户端连接编辑器显示的本机回环端口。插件默认只监听 `127.0.0.1`。
4. 构建项目时运行：

   ```powershell
   ./Tools/Build.ps1 -Stage Editor -EngineRoot <UE_ENGINE_ROOT>
   ```

   也可以设置 `UE_ENGINE_ROOT` 或 `UE5_ROOT`，省略 `-EngineRoot`。`-ProjectAlias` 仅在某些旧工具不接受当前工程路径时使用，默认不创建路径别名。

MCP 插件依赖编辑器自带的 Python、Editor Scripting、Enhanced Input、Niagara、PCG 等模块；它不把这些引擎模块复制进仓库，也不会把开发者本机的引擎目录提交到 Git。

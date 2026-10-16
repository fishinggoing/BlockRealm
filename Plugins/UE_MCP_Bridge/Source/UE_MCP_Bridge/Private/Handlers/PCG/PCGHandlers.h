#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FPCGHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> ListPCGGraphs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPCGComponents(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreatePCGGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadPCGGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddPCGNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ConnectPCGNodes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DisconnectPCGNodes(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemovePCGNode(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetPCGNodeSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExecutePCGGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SpawnPCGVolume(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadPCGNodeSettings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetPCGComponentDetails(const TSharedPtr<FJsonObject>& Params);

	// v0.7.18 issue #145 - populate MeshEntries on a PCGStaticMeshSpawner node
	static TSharedPtr<FJsonValue> SetStaticMeshSpawnerMeshes(const TSharedPtr<FJsonObject>& Params);

	// v0.7.19 issue #146 - PCG component (re)generation and cleanup
	static TSharedPtr<FJsonValue> ForceRegeneratePCG(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CleanupPCG(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ToggleGraphPCG(const TSharedPtr<FJsonObject>& Params);

	// issue #213 - bulk graph authoring via JSON spec.
	static TSharedPtr<FJsonValue> ImportGraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExportGraph(const TSharedPtr<FJsonObject>& Params);

	// issue #1087 - convert UPCGSettingsInstance nodes to node-owned settings.
	static TSharedPtr<FJsonValue> UnwrapInstanceNodes(const TSharedPtr<FJsonObject>& Params);

	// issue #1244 - PCG Assemblies: export a level to a UPCGDataAsset, and re-export assets from their levels.
	static TSharedPtr<FJsonValue> ExportLevelToAsset(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> UpdateLevelAssets(const TSharedPtr<FJsonObject>& Params);

	// issue #1253 - assign a Subgraph node's graph through SetSubgraph, and override its user parameters.
	static TSharedPtr<FJsonValue> SetSubgraph(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetSubgraphParameters(const TSharedPtr<FJsonObject>& Params);
	static void ExportSubgraphSettings(const class UPCGSettings* Settings, const TSharedPtr<FJsonObject>& Node);
	static bool ImportSubgraphSettings(class UPCGSettings* Settings, const TSharedPtr<FJsonObject>& Node, FString& Error);
};

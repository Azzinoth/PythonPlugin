#pragma once
#include "../SubSystems/FocalEngine/SubSystems/PluginSystem/FEPluginConnector.h"
#include "../SubSystems/EditorAPI/FEEditorAPI.h"
#include "../Public/PythonPluginInterface.h"
using namespace FocalEngine;

class PythonPluginEditor : public FEEditorPluginCore
{
	FEEditorAPI* Editor = nullptr;

	using EngineModuleInterface = PythonPluginInterface;
	// Editor module is loaded after engine module and unloaded before it, so engine module is always available.
	EngineModuleInterface* GetEngineModule() { return PLUGIN_MANAGER.GetPlugin<EngineModuleInterface>(UNIQUE_ID.FromString(EngineModuleInterface::PluginID)); }
public:
	bool OnEditorLoad(FEEditorAPI* EditorAPI) override;
	void OnEditorUnload() override;
	void OnEditorUpdate(double DeltaTime) override;
};
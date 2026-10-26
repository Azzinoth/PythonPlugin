#pragma once
#include "../SubSystems/FocalEngine/SubSystems/PluginSystem/FEPluginConnector.h"
#include "../SubSystems/EditorAPI/FEEditorAPI.h"
#include "../Public/PythonPluginInterface.h"
#include "../PythonPluginEditorLink.h"
using namespace FocalEngine;

class PythonPluginEditor : public FEEditorPluginCore
{
	FEEditorAPI* Editor = nullptr;

	using EngineModuleInterface = PythonPluginInterface;
	// Editor module is loaded after engine module and unloaded before it, so engine module is always available.
	EngineModuleInterface* GetEngineModule() { return PLUGIN_MANAGER.GetPlugin<EngineModuleInterface>(UNIQUE_ID.FromString(EngineModuleInterface::PluginID)); }
	PythonPluginEditorLink* GetEditorLink() { return PLUGIN_MANAGER.GetPlugin<PythonPluginEditorLink>(UNIQUE_ID.FromString(EngineModuleInterface::PluginID)); }

	enum class ConsoleEntryType
	{
		Input,
		Output,
		Value,
		Error,
		Info
	};

	struct ConsoleEntry
	{
		ConsoleEntryType Type = ConsoleEntryType::Output;
		std::string Text;
	};

	FEUUID ConsoleWindowID;
	std::vector<ConsoleEntry> ConsoleEntries;
	std::string ConsoleInput;
	std::vector<std::string> InputHistory;
	// -1 when input is not taken from history.
	int HistoryIndex = -1;
	int PendingHistoryStep = 0;
	bool bInputWasActive = false;
	// Changed by splitter between output and input, 0 until first frame sets default.
	float InputHeight = 0.0f;
	bool bFocusInput = false;
	bool bScrollToBottom = false;

	void RenderConsole();
	void RunConsoleInput();
	void RunScriptFile(const std::string& FilePath);
	void AddConsoleEntry(ConsoleEntryType Type, std::string Text);
	void AddExecutionResult(const PythonPluginInterface::ExecutionResult& Result);
	static int ConsoleInputCallback(ImGuiInputTextCallbackData* Data);
public:
	bool OnEditorLoad(FEEditorAPI* EditorAPI) override;
	void OnEditorUnload() override;
	void OnEditorUpdate(double DeltaTime) override;
};

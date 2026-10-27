#pragma once
// Python.h should be included before engine and standard headers.
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include "SubSystems/FocalEngine/SubSystems/PluginSystem/FEPluginConnector.h"
#include "SubSystems/EditorAPI/FEEditorAPI.h"
#include "Public/PythonPluginInterface.h"
#include "PythonPluginEditorLink.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
using namespace FocalEngine;
namespace py = pybind11;

class PythonPlugin : public FEPluginCore, public PythonPluginInterface, public PythonPluginEditorLink
{
public:
	// Bindings reach plugin through it, it is set while Python is running.
	static PythonPlugin* Instance;

	void OnLoad() override;
	void OnUnload() override;
	void OnUpdate(double DeltaTime) override;
	void OnSceneAdded(FEScene* Scene) override;
	void OnSceneDeleting(FEScene* Scene) override;

	bool IsInitialized() const override;
	std::string GetPythonVersion() const override;
	ExecutionResult RunString(const std::string& Code, bool bIsolatedNamespace = false) override;
	ExecutionResult RunFile(const std::string& FilePath, bool bIsolatedNamespace = false) override;
	ExecutionResult Evaluate(const std::string& Expression) override;
	void ResetNamespace() override;
	bool AddModuleSearchPath(const std::string& DirectoryPath) override;
	bool IsModuleAvailable(const std::string& ModuleName) override;
	bool RegisterNativeFunction(const std::string& OwnerName, const std::string& FunctionName, NativeFunction Function) override;
	void UnregisterNativeFunctions(const std::string& OwnerName) override;

	void SetEditorAPI(FEEditorAPI* Editor) override;
	FEEditorAPI* GetEditorAPI() const;

	enum class CallbackType
	{
		Update,
		SceneAdded,
		SceneDeleting,
		Shutdown,
		Count
	};

	// Called by bindings, GIL should be held.
	int AddCallback(CallbackType Type, py::object Function);
	bool RemoveCallback(int CallbackID);
	py::object CallOnMainThread(py::object Function);
	void WriteOutput(const std::string& Text, bool bErrorStream);
	bool IsMainThread() const;
	std::string CallNativeFunction(const std::string& FunctionName, const std::string& ArgumentsJSON);
	std::vector<std::string> GetNativeFunctionNames() const;
private:
	bool bInitialized = false;
	PyThreadState* MainThreadState = nullptr;
	std::thread::id MainThreadID;
	std::string PythonVersion;
	FEEditorAPI* EditorAPI = nullptr;

	// Python objects below are released before Python is finalized.
	// Namespace of RunnerCode.
	py::object Runner;

	struct Callback
	{
		int ID = 0;
		CallbackType Type = CallbackType::Update;
		py::object Function;
	};
	std::vector<Callback> Callbacks;
	int NextCallbackID = 1;
	// Lets OnUpdate skip taking GIL when there is nothing to call.
	std::atomic<int> CallbackCount[static_cast<size_t>(CallbackType::Count)] = {};

	struct QueuedCall
	{
		py::object Function;
		py::object Future;
	};
	std::mutex QueueMutex;
	std::vector<QueuedCall> Queue;
	std::atomic<bool> bQueueHasCalls = false;
	// Set when plugin starts unloading, after that calls are not queued and callbacks are not added.
	std::atomic<bool> bShuttingDown = false;

	struct NativeFunctionEntry
	{
		std::string OwnerName;
		NativeFunction Function;
	};
	// Used only on main thread. Allocated separately, so entries that owners did not unregister could be leaked on unload,
	// destroying them could run code of modules that are already unloaded.
	std::unique_ptr<std::unordered_map<std::string, NativeFunctionEntry>> NativeFunctions = std::make_unique<std::unordered_map<std::string, NativeFunctionEntry>>();

	// Output of RunString, RunFile and Evaluate goes to result, other output goes to log.
	std::string* CapturedOutput = nullptr;
	std::thread::id CaptureThreadID;
	std::string PendingLogLines[2];

	ExecutionResult Execute(const std::function<py::object(py::dict)>& Run, bool bIsolatedNamespace);
	py::dict GetNamespace(bool bIsolated);
	void CallCallbacks(CallbackType Type, const py::object& Argument);
	void RunQueuedCall(QueuedCall& Call);
	void RunQueuedCalls();
	void CancelQueuedCalls();
	// Returns names of threads that are still running after timeout.
	std::vector<std::string> JoinScriptThreads(double TimeoutInSeconds);
	void FlushPendingLogLines();
	static std::string FormatException(py::error_already_set& Error);
};

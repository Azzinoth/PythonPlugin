#pragma once
// Python.h should be included before engine and standard headers.
#include <pybind11/embed.h>
#include <pybind11/stl.h>
#include "SubSystems/FocalEngine/SubSystems/PluginSystem/FEPluginConnector.h"
#include "SubSystems/EditorAPI/FEEditorAPI.h"
#include "Public/PythonPluginInterface.h"
#include "PythonPluginEditorLink.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
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

	// Output of RunString, RunFile and Evaluate goes to result, other output goes to log.
	std::string* CapturedOutput = nullptr;
	std::thread::id CaptureThreadID;
	std::string PendingLogLines[2];

	ExecutionResult Execute(const std::function<py::object(py::dict)>& Run, bool bIsolatedNamespace);
	py::dict GetNamespace(bool bIsolated);
	void CallCallbacks(CallbackType Type, const py::object& Argument);
	void RunQueuedCall(QueuedCall& Call);
	void RunQueuedCalls();
	void FlushPendingLogLines();
	static std::string FormatException(py::error_already_set& Error);
};

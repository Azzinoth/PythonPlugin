#include "PythonBindings.h"

// DO NOT CHANGE THIS LINE.
// The plugin manager uses it to create and destroy the plugin instance.
REGISTER_PLUGIN(PythonPlugin)

PythonPlugin* PythonPlugin::Instance = nullptr;

namespace
{
	const char* RunnerFileName = "<python plugin runner>";

	// Executes code like interactive Python: value of the last expression is returned as repr().
	// Exceptions are returned as text, SystemExit is caught too, so exit() in a script does not close application.
	const char* RunnerCode = R"(
import ast, importlib.util, textwrap, tokenize, traceback

def _format_error(error):
    if isinstance(error, SyntaxError):
        return "".join(traceback.format_exception_only(type(error), error))
    trace = error.__traceback__
    while trace is not None and trace.tb_frame.f_code.co_filename == RUNNER_FILE_NAME:
        trace = trace.tb_next
    return "".join(traceback.format_exception(type(error), error, trace))

def _execute(code, file_name, namespace):
    tree = ast.parse(textwrap.dedent(code), file_name, "exec")
    last_expression = None
    if tree.body and isinstance(tree.body[-1], ast.Expr):
        last_expression = ast.Expression(tree.body.pop().value)
    exec(compile(tree, file_name, "exec"), namespace)
    if last_expression is None:
        return ""
    value = eval(compile(last_expression, file_name, "eval"), namespace)
    return "" if value is None else repr(value)

def run(code, file_name, namespace):
    try:
        return True, _execute(code, file_name, namespace)
    except BaseException as error:
        return False, _format_error(error)

def run_file(file_path, namespace):
    try:
        with tokenize.open(file_path) as file:
            code = file.read()
        namespace["__file__"] = file_path
        return True, _execute(code, file_path, namespace)
    except BaseException as error:
        return False, _format_error(error)

def evaluate(expression, namespace):
    try:
        return True, repr(eval(compile(textwrap.dedent(expression).strip(), "<expression>", "eval"), namespace))
    except BaseException as error:
        return False, _format_error(error)

def is_module_available(name):
    try:
        return importlib.util.find_spec(name) is not None
    except (ImportError, ValueError):
        return False
)";

	const std::wstring PythonVersionPostfix = std::to_wstring(PY_MAJOR_VERSION) + std::to_wstring(PY_MINOR_VERSION);

	// Runtime is next to Python DLL: in Debug or Release folder of plugin, or next to executable in games.
	std::wstring GetPythonRuntimeDirectory()
	{
		HMODULE PythonModule = GetModuleHandleW((L"python" + PythonVersionPostfix + L".dll").c_str());
		if (PythonModule == nullptr)
			return L"";

		std::wstring Path(32768, L'\0');
		const DWORD Length = GetModuleFileNameW(PythonModule, Path.data(), static_cast<DWORD>(Path.size()));
		Path.resize(Length);

		const size_t LastSeparator = Path.find_last_of(L"\\/");
		return LastSeparator == std::wstring::npos ? L"" : Path.substr(0, LastSeparator + 1);
	}

	std::string ToUTF8(const std::wstring& Text)
	{
		const int Length = WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string Result(Length > 0 ? Length - 1 : 0, '\0');
		WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), -1, Result.data(), Length, nullptr, nullptr);
		return Result;
	}

	const char* GetCallbackTypeName(PythonPlugin::CallbackType Type)
	{
		switch (Type)
		{
			case PythonPlugin::CallbackType::Update: return "update";
			case PythonPlugin::CallbackType::SceneAdded: return "scene added";
			case PythonPlugin::CallbackType::SceneDeleting: return "scene deleting";
			case PythonPlugin::CallbackType::Shutdown: return "shutdown";
			default: return "unknown";
		}
	}
}

void PythonPlugin::OnLoad()
{
	MainThreadID = std::this_thread::get_id();

	if (Py_IsInitialized())
	{
		LOG.Add("PythonPlugin::OnLoad: Python is already initialized by application, plugin could not use its own runtime", "PYTHON_PLUGIN", FE_LOG_ERROR);
		return;
	}

	// Inittab stays after Python is finalized, so after plugin is reloaded existing entry gets function from new DLL.
	bool bModuleRegistered = false;
	for (_inittab* Entry = PyImport_Inittab; Entry->name != nullptr; Entry++)
	{
		if (strcmp(Entry->name, "focalengine") == 0)
		{
			Entry->initfunc = PyInit_focalengine;
			bModuleRegistered = true;
		}
	}

	if (!bModuleRegistered && PyImport_AppendInittab("focalengine", PyInit_focalengine) == -1)
	{
		LOG.Add("PythonPlugin::OnLoad: Failed to register focalengine module", "PYTHON_PLUGIN", FE_LOG_ERROR);
		return;
	}

	const std::wstring RuntimeDirectory = GetPythonRuntimeDirectory();
	if (RuntimeDirectory.empty())
	{
		LOG.Add("PythonPlugin::OnLoad: Folder of Python runtime is not found", "PYTHON_PLUGIN", FE_LOG_ERROR);
		return;
	}

	// Isolated config ignores Python that is installed on computer and its environment variables.
	PyConfig Config;
	PyConfig_InitIsolatedConfig(&Config);
	Config.install_signal_handlers = 0;

	PyStatus Status = PyConfig_SetString(&Config, &Config.home, RuntimeDirectory.c_str());
	if (!PyStatus_Exception(Status))
	{
		Config.module_search_paths_set = 1;
		Status = PyWideStringList_Append(&Config.module_search_paths, (RuntimeDirectory + L"python" + PythonVersionPostfix + L".zip").c_str());
	}

	if (!PyStatus_Exception(Status))
		Status = PyWideStringList_Append(&Config.module_search_paths, RuntimeDirectory.c_str());

	if (!PyStatus_Exception(Status))
		Status = Py_InitializeFromConfig(&Config);
	PyConfig_Clear(&Config);

	if (PyStatus_Exception(Status))
	{
		LOG.Add("PythonPlugin::OnLoad: Python failed to start from \"" + ToUTF8(RuntimeDirectory) + "\": " + std::string(Status.err_msg != nullptr ? Status.err_msg : "unknown error"), "PYTHON_PLUGIN", FE_LOG_ERROR);
		return;
	}

	try
	{
		py::module_ Sys = py::module_::import("sys");
		py::module_ Engine = py::module_::import("focalengine");
		Sys.attr("stdout") = Engine.attr("_OutputStream")(false);
		Sys.attr("stderr") = Engine.attr("_OutputStream")(true);

		py::module_ Builtins = py::module_::import("builtins");
		Runner = py::dict();
		Runner["__builtins__"] = Builtins;
		Runner["RUNNER_FILE_NAME"] = RunnerFileName;
		Builtins.attr("exec")(Builtins.attr("compile")(RunnerCode, RunnerFileName, "exec"), Runner);
	}
	catch (py::error_already_set& Error)
	{
		LOG.Add("PythonPlugin::OnLoad: Python started, but plugin setup failed: " + FormatException(Error), "PYTHON_PLUGIN", FE_LOG_ERROR);
		Runner = py::object();
		Py_FinalizeEx();
		return;
	}

	PythonVersion = Py_GetVersion();
	PythonVersion = PythonVersion.substr(0, PythonVersion.find(' '));

	// GIL is released, so threads started by scripts could run between frames.
	MainThreadState = PyEval_SaveThread();
	bInitialized = true;
	Instance = this;

	LOG.Add("PythonPlugin::OnLoad: Python " + PythonVersion + " started from \"" + ToUTF8(RuntimeDirectory) + "\"", "PYTHON_PLUGIN");
}

void PythonPlugin::OnUnload()
{
	if (bInitialized)
	{
		PyEval_RestoreThread(MainThreadState);
		MainThreadState = nullptr;

		// Main thread will not run queued calls anymore, so threads that wait for them get CancelledError instead of waiting forever,
		// that includes threads that shutdown callbacks wait for.
		CancelQueuedCalls();

		CallCallbacks(CallbackType::Shutdown, py::object());
		Callbacks.clear();
		for (size_t i = 0; i < static_cast<size_t>(CallbackType::Count); i++)
			CallbackCount[i] = 0;

		// Shutdown callbacks could release GIL, for example while joining a thread, so other threads could queue calls meanwhile.
		CancelQueuedCalls();

		Runner = py::object();
		FlushPendingLogLines();

		// Waits for threads of scripts that are not daemon threads.
		if (Py_FinalizeEx() < 0)
			LOG.Add("PythonPlugin::OnUnload: Python reported an error while finalizing", "PYTHON_PLUGIN", FE_LOG_WARNING);

		FlushPendingLogLines();
		bInitialized = false;
		Instance = nullptr;
	}

	EditorAPI = nullptr;
}

void PythonPlugin::OnUpdate(double DeltaTime)
{
	if (!bInitialized)
		return;

	if (!bQueueHasCalls && CallbackCount[static_cast<size_t>(CallbackType::Update)] == 0)
		return;

	py::gil_scoped_acquire Acquire;
	RunQueuedCalls();
	CallCallbacks(CallbackType::Update, py::float_(DeltaTime));
}

void PythonPlugin::OnSceneAdded(FEScene* Scene)
{
	if (!bInitialized || CallbackCount[static_cast<size_t>(CallbackType::SceneAdded)] == 0)
		return;

	py::gil_scoped_acquire Acquire;
	CallCallbacks(CallbackType::SceneAdded, py::cast(PythonScene{ Scene->GetID() }));
}

void PythonPlugin::OnSceneDeleting(FEScene* Scene)
{
	if (!bInitialized || CallbackCount[static_cast<size_t>(CallbackType::SceneDeleting)] == 0)
		return;

	py::gil_scoped_acquire Acquire;
	CallCallbacks(CallbackType::SceneDeleting, py::cast(PythonScene{ Scene->GetID() }));
}

bool PythonPlugin::IsInitialized() const
{
	return bInitialized;
}

std::string PythonPlugin::GetPythonVersion() const
{
	return PythonVersion;
}

PythonPluginInterface::ExecutionResult PythonPlugin::RunString(const std::string& Code, bool bIsolatedNamespace)
{
	return Execute([&](py::dict Namespace) { return Runner["run"](Code, "<string>", Namespace); }, bIsolatedNamespace);
}

PythonPluginInterface::ExecutionResult PythonPlugin::RunFile(const std::string& FilePath, bool bIsolatedNamespace)
{
	return Execute([&](py::dict Namespace) { return Runner["run_file"](FilePath, Namespace); }, bIsolatedNamespace);
}

PythonPluginInterface::ExecutionResult PythonPlugin::Evaluate(const std::string& Expression)
{
	return Execute([&](py::dict Namespace) { return Runner["evaluate"](Expression, Namespace); }, false);
}

PythonPluginInterface::ExecutionResult PythonPlugin::Execute(const std::function<py::object(py::dict)>& Run, bool bIsolatedNamespace)
{
	ExecutionResult Result;
	if (!bInitialized)
	{
		Result.Error = "Python is not initialized";
		return Result;
	}

	py::gil_scoped_acquire Acquire;

	std::string* PreviousCapturedOutput = CapturedOutput;
	const std::thread::id PreviousCaptureThreadID = CaptureThreadID;
	CapturedOutput = &Result.Output;
	CaptureThreadID = std::this_thread::get_id();

	try
	{
		py::tuple RunResult = Run(GetNamespace(bIsolatedNamespace));
		Result.bSuccess = RunResult[0].cast<bool>();
		(Result.bSuccess ? Result.Value : Result.Error) = RunResult[1].cast<std::string>();
	}
	catch (py::error_already_set& Error)
	{
		Result.bSuccess = false;
		Result.Error = FormatException(Error);
	}
	catch (const std::exception& Exception)
	{
		Result.bSuccess = false;
		Result.Error = Exception.what();
	}

	CapturedOutput = PreviousCapturedOutput;
	CaptureThreadID = PreviousCaptureThreadID;
	return Result;
}

py::dict PythonPlugin::GetNamespace(bool bIsolated)
{
	if (!bIsolated)
		return py::module_::import("__main__").attr("__dict__");

	py::dict Namespace;
	Namespace["__name__"] = "__main__";
	Namespace["__builtins__"] = py::module_::import("builtins");
	return Namespace;
}

void PythonPlugin::ResetNamespace()
{
	if (!bInitialized)
		return;

	py::gil_scoped_acquire Acquire;
	try
	{
		py::dict Namespace = GetNamespace(false);
		Namespace.clear();
		Namespace["__name__"] = "__main__";
		Namespace["__builtins__"] = py::module_::import("builtins");
	}
	catch (py::error_already_set& Error)
	{
		LOG.Add("PythonPlugin::ResetNamespace: " + FormatException(Error), "PYTHON_PLUGIN", FE_LOG_ERROR);
	}
}

bool PythonPlugin::AddModuleSearchPath(const std::string& DirectoryPath)
{
	if (!bInitialized || !FILE_SYSTEM.DoesDirectoryExist(DirectoryPath))
		return false;

	py::gil_scoped_acquire Acquire;
	try
	{
		const std::string AbsolutePath = FILE_SYSTEM.GetAbsolutePath(DirectoryPath);
		py::list Path = py::module_::import("sys").attr("path");
		if (!Path.contains(AbsolutePath))
			Path.append(AbsolutePath);

		return true;
	}
	catch (py::error_already_set& Error)
	{
		LOG.Add("PythonPlugin::AddModuleSearchPath: " + FormatException(Error), "PYTHON_PLUGIN", FE_LOG_ERROR);
		return false;
	}
}

bool PythonPlugin::IsModuleAvailable(const std::string& ModuleName)
{
	if (!bInitialized)
		return false;

	py::gil_scoped_acquire Acquire;
	try
	{
		return Runner["is_module_available"](ModuleName).cast<bool>();
	}
	catch (py::error_already_set&)
	{
		return false;
	}
}

void PythonPlugin::SetEditorAPI(FEEditorAPI* Editor)
{
	EditorAPI = Editor;
}

FEEditorAPI* PythonPlugin::GetEditorAPI() const
{
	return EditorAPI;
}

int PythonPlugin::AddCallback(CallbackType Type, py::object Function)
{
	Callback NewCallback;
	NewCallback.ID = NextCallbackID++;
	NewCallback.Type = Type;
	NewCallback.Function = Function;
	Callbacks.push_back(NewCallback);
	CallbackCount[static_cast<size_t>(Type)]++;

	return NewCallback.ID;
}

bool PythonPlugin::RemoveCallback(int CallbackID)
{
	for (size_t i = 0; i < Callbacks.size(); i++)
	{
		if (Callbacks[i].ID != CallbackID)
			continue;

		CallbackCount[static_cast<size_t>(Callbacks[i].Type)]--;
		Callbacks.erase(Callbacks.begin() + i);
		return true;
	}

	return false;
}

void PythonPlugin::CallCallbacks(CallbackType Type, const py::object& Argument)
{
	// Callbacks could add or remove callbacks, so list is copied.
	std::vector<Callback> CallbacksToCall;
	for (size_t i = 0; i < Callbacks.size(); i++)
	{
		if (Callbacks[i].Type == Type)
			CallbacksToCall.push_back(Callbacks[i]);
	}

	for (size_t i = 0; i < CallbacksToCall.size(); i++)
	{
		std::string Error;
		try
		{
			if (Argument)
				CallbacksToCall[i].Function(Argument);
			else
				CallbacksToCall[i].Function();
		}
		catch (py::error_already_set& Exception)
		{
			Error = FormatException(Exception);
		}
		catch (const std::exception& Exception)
		{
			Error = Exception.what();
		}

		if (Error.empty())
			continue;

		// Otherwise update callback with error would fill log every frame.
		RemoveCallback(CallbacksToCall[i].ID);
		LOG.Add("Python " + std::string(GetCallbackTypeName(Type)) + " callback raised an exception and was removed:\n" + Error, "PYTHON", FE_LOG_ERROR);
	}
}

py::object PythonPlugin::CallOnMainThread(py::object Function)
{
	QueuedCall Call;
	Call.Function = Function;
	Call.Future = py::module_::import("concurrent.futures").attr("Future")();

	if (IsMainThread())
	{
		RunQueuedCall(Call);
		return Call.Future;
	}

	std::lock_guard<std::mutex> Lock(QueueMutex);
	Queue.push_back(Call);
	bQueueHasCalls = true;
	return Call.Future;
}

void PythonPlugin::RunQueuedCall(QueuedCall& Call)
{
	try
	{
		if (!Call.Future.attr("set_running_or_notify_cancel")().cast<bool>())
			return;

		try
		{
			Call.Future.attr("set_result")(Call.Function());
		}
		catch (py::error_already_set& Error)
		{
			Call.Future.attr("set_exception")(Error.value());
		}
	}
	catch (py::error_already_set& Error)
	{
		LOG.Add("PythonPlugin::RunQueuedCall: " + FormatException(Error), "PYTHON_PLUGIN", FE_LOG_ERROR);
	}
}

void PythonPlugin::RunQueuedCalls()
{
	std::vector<QueuedCall> CallsToRun;
	{
		std::lock_guard<std::mutex> Lock(QueueMutex);
		CallsToRun.swap(Queue);
		bQueueHasCalls = false;
	}

	for (size_t i = 0; i < CallsToRun.size(); i++)
		RunQueuedCall(CallsToRun[i]);
}

void PythonPlugin::CancelQueuedCalls()
{
	std::vector<QueuedCall> CallsToCancel;
	{
		std::lock_guard<std::mutex> Lock(QueueMutex);
		CallsToCancel.swap(Queue);
		bQueueHasCalls = false;
	}

	for (size_t i = 0; i < CallsToCancel.size(); i++)
	{
		try
		{
			CallsToCancel[i].Future.attr("cancel")();
		}
		catch (py::error_already_set&) {}
	}
}

void PythonPlugin::WriteOutput(const std::string& Text, bool bErrorStream)
{
	if (CapturedOutput != nullptr && std::this_thread::get_id() == CaptureThreadID)
	{
		*CapturedOutput += Text;
		return;
	}

	std::string& PendingLine = PendingLogLines[bErrorStream ? 1 : 0];
	PendingLine += Text;

	size_t LineEnd = PendingLine.find('\n');
	while (LineEnd != std::string::npos)
	{
		LOG.Add(PendingLine.substr(0, LineEnd), "PYTHON", bErrorStream ? FE_LOG_WARNING : FE_LOG_INFO);
		PendingLine.erase(0, LineEnd + 1);
		LineEnd = PendingLine.find('\n');
	}
}

void PythonPlugin::FlushPendingLogLines()
{
	for (size_t i = 0; i < 2; i++)
	{
		if (!PendingLogLines[i].empty())
			LOG.Add(PendingLogLines[i], "PYTHON", i == 1 ? FE_LOG_WARNING : FE_LOG_INFO);
		PendingLogLines[i].clear();
	}
}

bool PythonPlugin::IsMainThread() const
{
	return std::this_thread::get_id() == MainThreadID;
}

std::string PythonPlugin::FormatException(py::error_already_set& Error)
{
	try
	{
		py::object Lines = py::module_::import("traceback").attr("format_exception")(Error.type(), Error.value(), Error.trace());
		return py::str("").attr("join")(Lines).cast<std::string>();
	}
	catch (py::error_already_set&)
	{
		return Error.what();
	}
}

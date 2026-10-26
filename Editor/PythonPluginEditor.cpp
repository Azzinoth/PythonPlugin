#include "PythonPluginEditor.h"

// DO NOT CHANGE THIS LINE.
// The plugin manager uses it to create and destroy the plugin instance.
REGISTER_PLUGIN(PythonPluginEditor)

// Windows, menu items and inspector sections should be added here through Editor, they are removed automatically when module is unloaded.
bool PythonPluginEditor::OnEditorLoad(FEEditorAPI* EditorAPI)
{
	Editor = EditorAPI;

	PythonPluginEditorLink* EditorLink = GetEditorLink();
	if (EditorLink != nullptr)
		EditorLink->SetEditorAPI(Editor);

	// Editor lists windows of plugin in its menu, so console needs no menu item.
	ConsoleWindowID = Editor->AddWindow("PythonConsole", "Python Console", [this]() { RenderConsole(); });
	Editor->AddMenuItem("Run Python Script...", [this]() {
		FILE_SYSTEM.OpenFileDialog({ { "Python script (*.py)", { "py" } } }, [this](const std::string& FilePath) {
			if (!FilePath.empty())
				RunScriptFile(FilePath);
		});
	});

	return true;
}

void PythonPluginEditor::OnEditorUnload()
{
	PythonPluginEditorLink* EditorLink = GetEditorLink();
	if (EditorLink != nullptr)
		EditorLink->SetEditorAPI(nullptr);

	Editor = nullptr;
}

void PythonPluginEditor::OnEditorUpdate(double DeltaTime)
{

}

void PythonPluginEditor::AddConsoleEntry(ConsoleEntryType Type, std::string Text)
{
	while (!Text.empty() && (Text.back() == '\n' || Text.back() == '\r'))
		Text.pop_back();

	if (Text.empty())
		return;

	ConsoleEntries.push_back({ Type, Text });
	bScrollToBottom = true;
}

void PythonPluginEditor::AddExecutionResult(const PythonPluginInterface::ExecutionResult& Result)
{
	AddConsoleEntry(ConsoleEntryType::Output, Result.Output);
	AddConsoleEntry(ConsoleEntryType::Value, Result.Value);
	AddConsoleEntry(ConsoleEntryType::Error, Result.Error);
}

void PythonPluginEditor::RunConsoleInput()
{
	if (ConsoleInput.find_first_not_of(" \t\r\n") == std::string::npos)
		return;

	PythonPluginInterface* Python = GetEngineModule();
	if (Python == nullptr)
		return;

	AddConsoleEntry(ConsoleEntryType::Input, ConsoleInput);
	if (InputHistory.empty() || InputHistory.back() != ConsoleInput)
		InputHistory.push_back(ConsoleInput);
	HistoryIndex = -1;

	const std::string Code = ConsoleInput;
	ConsoleInput.clear();
	AddExecutionResult(Python->RunString(Code));
}

void PythonPluginEditor::RunScriptFile(const std::string& FilePath)
{
	PythonPluginInterface* Python = GetEngineModule();
	if (Python == nullptr || !Python->IsInitialized())
	{
		LOG.Add("PythonPluginEditor::RunScriptFile: Python is not available, \"" + FilePath + "\" was not run", "PYTHON_PLUGIN", FE_LOG_ERROR);
		return;
	}

	Editor->SetWindowVisible(ConsoleWindowID, true);
	AddConsoleEntry(ConsoleEntryType::Info, "Running \"" + FilePath + "\"");
	AddExecutionResult(Python->RunFile(FilePath));
}

int PythonPluginEditor::ConsoleInputCallback(ImGuiInputTextCallbackData* Data)
{
	PythonPluginEditor* Console = static_cast<PythonPluginEditor*>(Data->UserData);
	if (Console->PendingHistoryStep == 0 || Console->InputHistory.empty())
		return 0;

	const int HistorySize = static_cast<int>(Console->InputHistory.size());
	if (Console->PendingHistoryStep < 0)
		Console->HistoryIndex = Console->HistoryIndex == -1 ? HistorySize - 1 : (Console->HistoryIndex > 0 ? Console->HistoryIndex - 1 : 0);
	else if (Console->HistoryIndex != -1)
		Console->HistoryIndex = Console->HistoryIndex + 1 >= HistorySize ? -1 : Console->HistoryIndex + 1;
	Console->PendingHistoryStep = 0;

	Data->DeleteChars(0, Data->BufTextLen);
	if (Console->HistoryIndex != -1)
		Data->InsertChars(0, Console->InputHistory[Console->HistoryIndex].c_str());

	return 0;
}

void PythonPluginEditor::RenderConsole()
{
	PythonPluginInterface* Python = GetEngineModule();
	if (Python == nullptr || !Python->IsInitialized())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Python is not available, log has details.");
		return;
	}

	ImGui::TextDisabled("Python %s", Python->GetPythonVersion().c_str());
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear"))
		ConsoleEntries.clear();

	ImGui::SameLine();
	if (ImGui::SmallButton("Copy"))
	{
		std::string Text;
		for (size_t i = 0; i < ConsoleEntries.size(); i++)
			Text += ConsoleEntries[i].Text + "\n";
		ImGui::SetClipboardText(Text.c_str());
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Reset variables"))
	{
		Python->ResetNamespace();
		AddConsoleEntry(ConsoleEntryType::Info, "Variables were removed.");
	}

	const ImGuiStyle& Style = ImGui::GetStyle();
	const float SplitterHeight = 6.0f;
	// Splitter, row with Run button and spacing between items, everything below output except input.
	const float FooterHeightWithoutInput = SplitterHeight + ImGui::GetFrameHeight() + Style.ItemSpacing.y * 3.0f;
	if (InputHeight <= 0.0f)
		InputHeight = ImGui::GetTextLineHeight() * 4.0f + Style.FramePadding.y * 2.0f;

	// Output and input always keep at least a few lines.
	const float MaxInputHeight = ImGui::GetContentRegionAvail().y - FooterHeightWithoutInput - ImGui::GetTextLineHeightWithSpacing() * 2.0f;
	const float MinInputHeight = ImGui::GetTextLineHeight() + Style.FramePadding.y * 2.0f;
	if (InputHeight > MaxInputHeight)
		InputHeight = MaxInputHeight;
	if (InputHeight < MinInputHeight)
		InputHeight = MinInputHeight;

	if (ImGui::BeginChild("##PythonConsoleOutput", ImVec2(0.0f, -(InputHeight + FooterHeightWithoutInput)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
	{
		for (size_t i = 0; i < ConsoleEntries.size(); i++)
		{
			const ConsoleEntry& Entry = ConsoleEntries[i];
			ImVec4 Color = Style.Colors[ImGuiCol_Text];
			if (Entry.Type == ConsoleEntryType::Input || Entry.Type == ConsoleEntryType::Info)
				Color = Style.Colors[ImGuiCol_TextDisabled];
			else if (Entry.Type == ConsoleEntryType::Value)
				Color = ImVec4(0.5f, 0.85f, 1.0f, 1.0f);
			else if (Entry.Type == ConsoleEntryType::Error)
				Color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);

			ImGui::PushStyleColor(ImGuiCol_Text, Color);
			if (Entry.Type == ConsoleEntryType::Input)
			{
				// Every line of input gets prompt, like in interactive Python.
				size_t LineStart = 0;
				while (LineStart <= Entry.Text.size())
				{
					size_t LineEnd = Entry.Text.find('\n', LineStart);
					if (LineEnd == std::string::npos)
						LineEnd = Entry.Text.size();

					ImGui::TextUnformatted(((LineStart == 0 ? ">>> " : "... ") + Entry.Text.substr(LineStart, LineEnd - LineStart)).c_str());
					LineStart = LineEnd + 1;
				}
			}
			else
			{
				ImGui::TextUnformatted(Entry.Text.c_str());
			}
			ImGui::PopStyleColor();
		}

		if (bScrollToBottom)
			ImGui::SetScrollHereY(1.0f);
		bScrollToBottom = false;
	}
	ImGui::EndChild();

	// Dragging splitter moves border between output and input.
	ImGui::InvisibleButton("##PythonConsoleSplitter", ImVec2(ImGui::GetContentRegionAvail().x, SplitterHeight));
	const bool bSplitterActive = ImGui::IsItemActive();
	const bool bSplitterHovered = ImGui::IsItemHovered();
	if (bSplitterActive)
		InputHeight -= ImGui::GetIO().MouseDelta.y;
	if (bSplitterActive || bSplitterHovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

	const ImVec2 SplitterMin = ImGui::GetItemRectMin();
	const ImVec2 SplitterMax = ImGui::GetItemRectMax();
	const float SplitterCenter = (SplitterMin.y + SplitterMax.y) * 0.5f;
	const ImGuiCol SplitterColor = bSplitterActive ? ImGuiCol_SeparatorActive : (bSplitterHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
	ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(SplitterMin.x, SplitterCenter - 1.0f), ImVec2(SplitterMax.x, SplitterCenter + 1.0f), ImGui::GetColorU32(SplitterColor));

	// History step is applied in callback, because text of active input could be changed only there.
	if (bInputWasActive && ImGui::GetIO().KeyCtrl)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			PendingHistoryStep = -1;
		else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			PendingHistoryStep = 1;
	}

	if (bFocusInput)
		ImGui::SetKeyboardFocusHere();
	bFocusInput = false;

	// In multiline input Enter adds new line, Ctrl+Enter runs code.
	const ImGuiInputTextFlags InputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways;
	bool bRun = ImGui::InputTextMultiline("##PythonConsoleInput", &ConsoleInput, ImVec2(-FLT_MIN, InputHeight), InputFlags, ConsoleInputCallback, this);
	bInputWasActive = ImGui::IsItemActive();

	if (ImGui::Button("Run"))
		bRun = true;
	ImGui::SameLine();
	ImGui::TextDisabled("Ctrl+Enter runs code, Ctrl+Up and Ctrl+Down go through history.");

	if (bRun)
	{
		RunConsoleInput();
		bFocusInput = true;
	}
}

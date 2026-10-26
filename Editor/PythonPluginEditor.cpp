#include "PythonPluginEditor.h"

// DO NOT CHANGE THIS LINE.
// The plugin manager uses it to create and destroy the plugin instance.
REGISTER_PLUGIN(PythonPluginEditor)

// Windows, menu items and inspector sections should be added here through Editor, they are removed automatically when module is unloaded.
bool PythonPluginEditor::OnEditorLoad(FEEditorAPI* EditorAPI)
{
	Editor = EditorAPI;
	return true;
}

void PythonPluginEditor::OnEditorUnload()
{

}

void PythonPluginEditor::OnEditorUpdate(double DeltaTime)
{

}
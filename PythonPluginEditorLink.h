#pragma once

namespace FocalEngine
{
	class FEEditorAPI;
}

// Only for editor module of this plugin, it is not public API.
// Engine module implements focalengine.editor with FEEditorAPI of editor module,
// so Python never holds functions of editor DLL, that would crash after editor module is unloaded.
class PythonPluginEditorLink
{
public:
	virtual ~PythonPluginEditorLink() = default;
	// Editor should stay valid until SetEditorAPI(nullptr) is called.
	virtual void SetEditorAPI(FocalEngine::FEEditorAPI* Editor) = 0;
};

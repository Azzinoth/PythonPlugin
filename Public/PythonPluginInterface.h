#pragma once

// Public API of PythonPlugin, applications include only this header.
// Usage:
//	using PluginInterface = PythonPluginInterface;
//	PluginInterface* Plugin = PLUGIN_MANAGER.GetPlugin<PluginInterface>(UNIQUE_ID.FromString(PluginInterface::PluginID));
//	if (Plugin != nullptr && Plugin->GetInterfaceVersion() == PluginInterface::InterfaceVersion)
//		Plugin->...;
class PythonPluginInterface
{
public:
	// Same as "ID" in PythonPlugin.json.
	static constexpr const char* PluginID = "a1262d03-a32d-4dbe-aa78-faf2e862ce72";

	// Increase when anything in this file changes.
	static constexpr int InterfaceVersion = 1;

	// Order of virtual functions is part of the interface, new functions should be added only at the end.
	virtual ~PythonPluginInterface() = default;
	virtual int GetInterfaceVersion() const { return InterfaceVersion; }
};
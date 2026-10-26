#include "SubSystems/FocalEngine/SubSystems/PluginSystem/FEPluginConnector.h"
#include "Public/PythonPluginInterface.h"
using namespace FocalEngine;

class PythonPlugin : public FEPluginCore, public PythonPluginInterface
{
public:
	void OnLoad() override;
	void OnUnload() override;
	void OnUpdate(double DeltaTime) override;
};
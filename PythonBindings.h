#pragma once
#include "PythonPlugin.h"

// Defined by PYBIND11_MODULE in PythonBindings.cpp, plugin registers it before Python starts.
extern "C" PYBIND11_EXPORT PyObject* PyInit_focalengine();

// Python keeps IDs instead of pointers, so using deleted scene or entity raises an exception instead of crashing.
struct PythonScene
{
	FEUUID ID;
	FEScene* Get() const;
};

struct PythonEntity
{
	FEUUID SceneID;
	FEUUID ID;
	FEEntity* Get() const;
};

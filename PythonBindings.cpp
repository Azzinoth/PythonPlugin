#include "PythonBindings.h"

FEScene* PythonScene::Get() const
{
	FEScene* Scene = SCENE_MANAGER.GetSceneByID(ID);
	if (Scene == nullptr)
		throw std::runtime_error("Scene " + UNIQUE_ID.ToString(ID) + " no longer exists");

	return Scene;
}

FEEntity* PythonEntity::Get() const
{
	FEScene* Scene = SCENE_MANAGER.GetSceneByID(SceneID);
	FEEntity* Entity = Scene != nullptr ? Scene->GetEntity(ID) : nullptr;
	if (Entity == nullptr)
		throw std::runtime_error("Entity " + UNIQUE_ID.ToString(ID) + " no longer exists");

	return Entity;
}

namespace
{
	// Engine is not thread safe, bindings that use it work only on main thread.
	struct MainThreadGuard
	{
		MainThreadGuard()
		{
			if (PythonPlugin::Instance == nullptr || !PythonPlugin::Instance->IsMainThread())
				throw std::runtime_error("Focal Engine could be used only on main thread, focalengine.call_on_main_thread() runs function there");
		}
	};
	using MainThreadOnly = py::call_guard<MainThreadGuard>;

	struct PythonOutputStream
	{
		bool bErrorStream = false;
	};

	PythonPlugin& GetPlugin()
	{
		if (PythonPlugin::Instance == nullptr)
			throw std::runtime_error("PythonPlugin is not loaded");

		return *PythonPlugin::Instance;
	}

	FEEditorAPI& GetEditor()
	{
		FEEditorAPI* Editor = GetPlugin().GetEditorAPI();
		if (Editor == nullptr)
			throw std::runtime_error("Editor is not available, focalengine.editor.is_available() could be used to check it");

		return *Editor;
	}

	PythonEntity ToPython(FEEntity* Entity)
	{
		return PythonEntity{ Entity->GetParentScene()->GetID(), Entity->GetID() };
	}

	std::optional<PythonEntity> ToPythonOrNone(FEEntity* Entity)
	{
		if (Entity == nullptr)
			return std::nullopt;

		return ToPython(Entity);
	}

	std::vector<PythonEntity> ToPython(const std::vector<FEEntity*>& Entities)
	{
		std::vector<PythonEntity> Result;
		for (size_t i = 0; i < Entities.size(); i++)
		{
			if (Entities[i] != nullptr)
				Result.push_back(ToPython(Entities[i]));
		}

		return Result;
	}

	std::optional<PythonScene> ToPythonOrNone(FEScene* Scene)
	{
		if (Scene == nullptr)
			return std::nullopt;

		return PythonScene{ Scene->GetID() };
	}

	std::vector<PythonScene> ToPython(const std::vector<FEScene*>& Scenes)
	{
		std::vector<PythonScene> Result;
		for (size_t i = 0; i < Scenes.size(); i++)
		{
			if (Scenes[i] != nullptr)
				Result.push_back(PythonScene{ Scenes[i]->GetID() });
		}

		return Result;
	}

	FETransformComponent& GetTransform(const PythonEntity& Entity)
	{
		FEEntity* EngineEntity = Entity.Get();
		if (!EngineEntity->HasComponent<FETransformComponent>())
			throw py::value_error("Entity does not have transform component");

		return EngineEntity->GetComponent<FETransformComponent>();
	}

	FEInstancedComponent& GetInstancedComponent(const PythonEntity& Entity)
	{
		FEEntity* EngineEntity = Entity.Get();
		if (!EngineEntity->HasComponent<FEInstancedComponent>())
			throw py::value_error("Entity does not have instanced component");

		return EngineEntity->GetComponent<FEInstancedComponent>();
	}

	void RespawnInstances(const PythonEntity& Entity, const FESpawnInfo& SpawnInfo)
	{
		if (APPLICATION.GetGraphicsMode() == FEGraphicsMode::None)
			throw std::runtime_error("Instances could not be spawned without graphics");

		FEEntity* EngineEntity = Entity.Get();
		INSTANCED_RENDERING_SYSTEM.ClearInstance(EngineEntity);
		INSTANCED_RENDERING_SYSTEM.PopulateInstance(EngineEntity, SpawnInfo);
	}

	std::vector<std::string> GetFlagNames(const FEScene* Scene)
	{
		const std::pair<FESceneFlag, const char*> Flags[] = {
			{ FESceneFlag::Renderable, "Renderable" },
			{ FESceneFlag::Active, "Active" },
			{ FESceneFlag::EditorMode, "EditorMode" },
			{ FESceneFlag::GameMode, "GameMode" },
			{ FESceneFlag::PrefabDescription, "PrefabDescription" }
		};

		std::vector<std::string> Result;
		for (size_t i = 0; i < std::size(Flags); i++)
		{
			if (Scene->HasFlag(Flags[i].first))
				Result.push_back(Flags[i].second);
		}

		return Result;
	}

	FE_COORDINATE_SPACE_TYPE ToSpace(bool bWorld)
	{
		return bWorld ? FE_WORLD_SPACE : FE_LOCAL_SPACE;
	}

	std::string ToString(const glm::vec3& Vector)
	{
		char Buffer[128];
		snprintf(Buffer, sizeof(Buffer), "Vector3(%g, %g, %g)", Vector.x, Vector.y, Vector.z);
		return Buffer;
	}
}

PYBIND11_MODULE(focalengine, Module)
{
	Module.doc() = "Focal Engine API, provided by PythonPlugin. Engine could be used only on main thread.";

	// sys.stdout and sys.stderr of plugin.
	py::class_<PythonOutputStream>(Module, "_OutputStream")
		.def(py::init([](bool bErrorStream) { return PythonOutputStream{ bErrorStream }; }))
		.def("write", [](const PythonOutputStream& Stream, const std::string& Text) {
			if (PythonPlugin::Instance != nullptr)
				PythonPlugin::Instance->WriteOutput(Text, Stream.bErrorStream);
			return Text.size();
		})
		.def("flush", [](const PythonOutputStream&) {})
		.def("isatty", [](const PythonOutputStream&) { return false; })
		.def("writable", [](const PythonOutputStream&) { return true; })
		.def_property_readonly("encoding", [](const PythonOutputStream&) { return "utf-8"; });

	Module.def("log", [](const std::string& Text, const std::string& Level) {
		LOG_SEVERITY Severity = FE_LOG_INFO;
		if (Level == "debug")
			Severity = FE_LOG_DEBUG;
		else if (Level == "warning")
			Severity = FE_LOG_WARNING;
		else if (Level == "error")
			Severity = FE_LOG_ERROR;
		else if (Level != "info")
			throw py::value_error("Level should be \"info\", \"debug\", \"warning\" or \"error\"");

		LOG.Add(Text, "PYTHON", Severity);
	}, py::arg("text"), py::arg("level") = "info", "Adds text to engine log, under PYTHON topic.");

	Module.def("is_main_thread", []() { return GetPlugin().IsMainThread(); });
	Module.def("call_on_main_thread", [](py::function Function) { return GetPlugin().CallOnMainThread(Function); }, py::arg("function"),
		"Calls function without arguments on main thread before next frame, or right away if called on main thread. Returns concurrent.futures.Future with its result.");

	Module.def("add_update_callback", [](py::function Function) { return GetPlugin().AddCallback(PythonPlugin::CallbackType::Update, Function); }, py::arg("function"),
		"Function(delta_time) is called every frame on main thread. Returns ID for remove_callback(). Callback that raises an exception is removed.");
	Module.def("add_scene_added_callback", [](py::function Function) { return GetPlugin().AddCallback(PythonPlugin::CallbackType::SceneAdded, Function); }, py::arg("function"),
		"Function(scene) is called when scene is added.");
	Module.def("add_scene_deleting_callback", [](py::function Function) { return GetPlugin().AddCallback(PythonPlugin::CallbackType::SceneDeleting, Function); }, py::arg("function"),
		"Function(scene) is called before scene is deleted.");
	Module.def("add_shutdown_callback", [](py::function Function) { return GetPlugin().AddCallback(PythonPlugin::CallbackType::Shutdown, Function); }, py::arg("function"),
		"Function() is called before Python is finalized, threads that are not daemon threads should be stopped here. Python waits for them at most 5 seconds, if they are still running it is not finalized.");
	Module.def("remove_callback", [](int CallbackID) { return GetPlugin().RemoveCallback(CallbackID); }, py::arg("callback_id"));

	Module.def("get_graphics_mode", []() { return GraphicsModeToString(APPLICATION.GetGraphicsMode()); }, "\"OnScreen\", \"Offscreen\" or \"None\".");

	py::class_<glm::vec3>(Module, "Vector3")
		.def(py::init<>())
		.def(py::init<float, float, float>(), py::arg("x"), py::arg("y"), py::arg("z"))
		.def(py::init([](const py::sequence& Values) {
			if (py::len(Values) != 3)
				throw py::value_error("Vector3 needs 3 values");

			return glm::vec3(Values[0].cast<float>(), Values[1].cast<float>(), Values[2].cast<float>());
		}), py::arg("values"))
		.def_readwrite("x", &glm::vec3::x)
		.def_readwrite("y", &glm::vec3::y)
		.def_readwrite("z", &glm::vec3::z)
		.def("__iter__", [](const glm::vec3& Vector) { return py::iter(py::make_tuple(Vector.x, Vector.y, Vector.z)); })
		.def("__eq__", [](const glm::vec3& First, const glm::vec3& Second) { return First == Second; }, py::is_operator())
		.def("__repr__", [](const glm::vec3& Vector) { return ToString(Vector); });
	// Tuples and lists could be passed where Vector3 is expected.
	py::implicitly_convertible<py::tuple, glm::vec3>();
	py::implicitly_convertible<py::list, glm::vec3>();

	py::class_<PythonScene>(Module, "Scene")
		.def_property_readonly("id", [](const PythonScene& Scene) { return UNIQUE_ID.ToString(Scene.ID); })
		.def("is_valid", [](const PythonScene& Scene) { return SCENE_MANAGER.GetSceneByID(Scene.ID) != nullptr; }, MainThreadOnly())
		.def("get_name", [](const PythonScene& Scene) { return Scene.Get()->GetName(); }, MainThreadOnly())
		.def("set_name", [](const PythonScene& Scene, const std::string& Name) { Scene.Get()->SetName(Name); }, py::arg("name"), MainThreadOnly())
		.def("get_tag", [](const PythonScene& Scene) { return Scene.Get()->GetTag(); }, MainThreadOnly(),
			"Engine and editor mark their private objects with tags, for example \"ENGINE_PRIVATE_RESOURCE\" or \"EDITOR_PRIVATE_RESOURCE\".")
		.def("get_flags", [](const PythonScene& Scene) { return GetFlagNames(Scene.Get()); }, MainThreadOnly(),
			"List of \"Renderable\", \"Active\", \"EditorMode\", \"GameMode\" and \"PrefabDescription\". Editor game mode runs on a copy of scene with \"GameMode\" flag.")
		.def("get_entity", [](const PythonScene& Scene, const std::string& EntityID) { return ToPythonOrNone(Scene.Get()->GetEntity(UNIQUE_ID.FromString(EntityID))); }, py::arg("id"), MainThreadOnly())
		.def("get_entities", [](const PythonScene& Scene) {
			FEScene* EngineScene = Scene.Get();
			std::vector<PythonEntity> Result;
			const std::vector<FEUUID> EntityIDs = EngineScene->GetEntityIDList();
			for (size_t i = 0; i < EntityIDs.size(); i++)
				Result.push_back(PythonEntity{ Scene.ID, EntityIDs[i] });

			return Result;
		}, MainThreadOnly())
		.def("get_entities_by_name", [](const PythonScene& Scene, const std::string& Name) { return ToPython(Scene.Get()->GetEntityByName(Name)); }, py::arg("name"), MainThreadOnly())
		.def("create_entity", [](const PythonScene& Scene, const std::string& Name) { return ToPython(Scene.Get()->CreateEntity(Name)); }, py::arg("name") = "", MainThreadOnly())
		.def("delete_entity", [](const PythonScene& Scene, const PythonEntity& Entity) {
			if (Entity.SceneID != Scene.ID)
				throw py::value_error("Entity belongs to other scene");

			Scene.Get()->DeleteEntity(Entity.Get());
		}, py::arg("entity"), MainThreadOnly())
		.def("import_asset", [](const PythonScene& Scene, const std::string& FilePath) {
			py::list Result;
			const std::vector<FEObject*> Objects = Scene.Get()->ImportAsset(FilePath);
			for (size_t i = 0; i < Objects.size(); i++)
			{
				if (Objects[i] == nullptr)
					continue;

				py::dict Object;
				Object["id"] = UNIQUE_ID.ToString(Objects[i]->GetID());
				Object["name"] = Objects[i]->GetName();
				Object["type"] = FEObjectTypeToString(Objects[i]->GetType());
				Result.append(Object);
			}

			return Result;
		}, py::arg("file_path"), MainThreadOnly(), "Returns list of {\"id\", \"name\", \"type\"} of objects that were created.")
		.def("__eq__", [](const PythonScene& First, const PythonScene& Second) { return First.ID == Second.ID; }, py::is_operator())
		.def("__hash__", [](const PythonScene& Scene) { return std::hash<FEUUID>()(Scene.ID); })
		.def("__repr__", [](const PythonScene& Scene) { return "Scene(\"" + UNIQUE_ID.ToString(Scene.ID) + "\")"; });

	py::class_<PythonEntity>(Module, "Entity")
		.def_property_readonly("id", [](const PythonEntity& Entity) { return UNIQUE_ID.ToString(Entity.ID); })
		.def_property_readonly("scene", [](const PythonEntity& Entity) { return PythonScene{ Entity.SceneID }; })
		.def("is_valid", [](const PythonEntity& Entity) {
			FEScene* Scene = SCENE_MANAGER.GetSceneByID(Entity.SceneID);
			return Scene != nullptr && Scene->GetEntity(Entity.ID) != nullptr;
		}, MainThreadOnly())
		.def("get_name", [](const PythonEntity& Entity) { return Entity.Get()->GetName(); }, MainThreadOnly())
		.def("set_name", [](const PythonEntity& Entity, const std::string& Name) { Entity.Get()->SetName(Name); }, py::arg("name"), MainThreadOnly())
		.def("get_tag", [](const PythonEntity& Entity) { return Entity.Get()->GetTag(); }, MainThreadOnly(),
			"Engine and editor mark their private objects with tags, for example \"ENGINE_PRIVATE_RESOURCE\" or \"EDITOR_PRIVATE_RESOURCE\".")

		.def("get_position", [](const PythonEntity& Entity, bool bWorld) { return GetTransform(Entity).GetPosition(ToSpace(bWorld)); }, py::arg("world") = false, MainThreadOnly())
		.def("set_position", [](const PythonEntity& Entity, const glm::vec3& Position, bool bWorld) { GetTransform(Entity).SetPosition(Position, ToSpace(bWorld)); }, py::arg("position"), py::arg("world") = false, MainThreadOnly())
		.def("get_rotation", [](const PythonEntity& Entity, bool bWorld) { return GetTransform(Entity).GetRotation(ToSpace(bWorld)); }, py::arg("world") = false, MainThreadOnly(), "Euler angles in degrees.")
		.def("set_rotation", [](const PythonEntity& Entity, const glm::vec3& Rotation, bool bWorld) { GetTransform(Entity).SetRotation(Rotation, ToSpace(bWorld)); }, py::arg("rotation"), py::arg("world") = false, MainThreadOnly(), "Euler angles in degrees.")
		.def("get_scale", [](const PythonEntity& Entity, bool bWorld) { return GetTransform(Entity).GetScale(ToSpace(bWorld)); }, py::arg("world") = false, MainThreadOnly())
		.def("set_scale", [](const PythonEntity& Entity, const glm::vec3& Scale, bool bWorld) { GetTransform(Entity).SetScale(Scale, ToSpace(bWorld)); }, py::arg("scale"), py::arg("world") = false, MainThreadOnly())

		.def("get_parent", [](const PythonEntity& Entity) { return ToPythonOrNone(Entity.Get()->GetParentEntity()); }, MainThreadOnly())
		.def("get_children", [](const PythonEntity& Entity) { return ToPython(Entity.Get()->GetChildEntities()); }, MainThreadOnly())
		.def("attach_to", [](const PythonEntity& Entity, const PythonEntity& Parent, bool bPreserveWorldTransform) { return Entity.Get()->AttachTo(Parent.Get(), bPreserveWorldTransform); },
			py::arg("parent"), py::arg("preserve_world_transform") = true, MainThreadOnly())
		.def("detach", [](const PythonEntity& Entity, bool bPreserveWorldTransform) { return Entity.Get()->Detach(bPreserveWorldTransform); }, py::arg("preserve_world_transform") = true, MainThreadOnly())
		.def("is_child_of", [](const PythonEntity& Entity, const PythonEntity& PotentialParent) { return Entity.Get()->IsChildOf(PotentialParent.Get()); }, py::arg("potential_parent"), MainThreadOnly())

		.def("is_visible", [](const PythonEntity& Entity) { return Entity.Get()->IsVisible(); }, MainThreadOnly())
		.def("set_visible", [](const PythonEntity& Entity, bool bVisible) { Entity.Get()->SetVisible(bVisible); }, py::arg("visible"), MainThreadOnly())

		.def("get_instance_seed", [](const PythonEntity& Entity) { return GetInstancedComponent(Entity).SpawnInfo.Seed; }, MainThreadOnly())
		.def("set_instance_seed", [](const PythonEntity& Entity, int Seed) {
			FEInstancedComponent& InstancedComponent = GetInstancedComponent(Entity);
			FESpawnInfo SpawnInfo = InstancedComponent.SpawnInfo;
			SpawnInfo.Seed = Seed;
			RespawnInstances(Entity, SpawnInfo);
		}, py::arg("seed"), MainThreadOnly(), "Instances are spawned again with new seed.")
		.def("get_instance_count", [](const PythonEntity& Entity) { return GetInstancedComponent(Entity).GetInstanceCount(); }, MainThreadOnly())
		.def("set_instance_count", [](const PythonEntity& Entity, int Count) {
			FEInstancedComponent& InstancedComponent = GetInstancedComponent(Entity);
			FESpawnInfo SpawnInfo = InstancedComponent.SpawnInfo;
			SpawnInfo.ObjectCount = Count;
			RespawnInstances(Entity, SpawnInfo);
		}, py::arg("count"), MainThreadOnly(), "Instances are spawned again with new count.")

		.def("__eq__", [](const PythonEntity& First, const PythonEntity& Second) { return First.SceneID == Second.SceneID && First.ID == Second.ID; }, py::is_operator())
		.def("__hash__", [](const PythonEntity& Entity) { return std::hash<FEUUID>()(Entity.ID); })
		.def("__repr__", [](const PythonEntity& Entity) { return "Entity(\"" + UNIQUE_ID.ToString(Entity.ID) + "\")"; });

	Module.def("get_scene", [](const std::string& SceneID) { return ToPythonOrNone(SCENE_MANAGER.GetSceneByID(UNIQUE_ID.FromString(SceneID))); }, py::arg("id"), MainThreadOnly());
	Module.def("get_scenes", []() { return ToPython(SCENE_MANAGER.GetAllScenes()); }, MainThreadOnly());
	Module.def("get_scenes_by_name", [](const std::string& Name) { return ToPython(SCENE_MANAGER.GetSceneByName(Name)); }, py::arg("name"), MainThreadOnly());
	Module.def("get_starting_scene", []() { return ToPythonOrNone(SCENE_MANAGER.GetStartingScene()); }, MainThreadOnly());

	// Implemented here with FEEditorAPI from editor module, see PythonPluginEditorLink.h.
	py::module_ Editor = Module.def_submodule("editor", "Editor functions, they work only when plugin runs in the editor.");
	py::module_::import("sys").attr("modules")["focalengine.editor"] = Editor;
	Editor.def("is_available", []() { return PythonPlugin::Instance != nullptr && PythonPlugin::Instance->GetEditorAPI() != nullptr; });
	Editor.def("is_project_open", []() { return GetEditor().IsProjectOpen(); }, MainThreadOnly());
	Editor.def("get_focused_scene", []() { return ToPythonOrNone(GetEditor().GetFocusedScene()); }, MainThreadOnly());
	Editor.def("get_selected_entity", []() { return ToPythonOrNone(GetEditor().GetSelectedEntity()); }, MainThreadOnly());
	Editor.def("set_selected_entity", [](const std::optional<PythonEntity>& Entity) { GetEditor().SetSelectedEntity(Entity.has_value() ? Entity->Get() : nullptr); }, py::arg("entity"), MainThreadOnly(),
		"None clears selection.");
	Editor.def("mark_project_modified", []() { GetEditor().MarkProjectModified(); }, MainThreadOnly());
}

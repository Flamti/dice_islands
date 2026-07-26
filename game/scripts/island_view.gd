extends Node3D
## Сборщик карты (SPEC §11.2): по сидам из NetSession генерирует остров каждого
## участника через ядро (локально — генерация детерминирована и не зависит от
## стейта партии), грузит GLB, кладёт оверлей сетки и расставляет острова кольцом.

const GridOverlay := preload("res://scripts/grid_overlay.gd")
const BuildingStatus := preload("res://scripts/building_status.gd")
const ScaffoldTileScene := preload("res://scenes/scaffold_tile.tscn")
const CameraRigScene := preload("res://scenes/world_map.tscn")

const SCAFFOLD_COLOR := Color(0.72, 0.6, 0.38, 0.85)
const POI_STONE_COLOR := Color(0.55, 0.55, 0.6)
const POI_WOOD_COLOR := Color(0.5, 0.33, 0.15)
const POI_MARKER_SIZE := Vector3(0.9, 0.9, 0.9)

const ATLAS_TEXTURE := preload("res://assets/atlas_placeholder.png")
## Путь к конфигу генератора относительно res:// (data/ живёт в корне репо).
const GENERATOR_CONFIG_PATH := "../data/generator.json"
const RING_RADIUS := 60.0
const ISLANDS_GROUP := "islands"
const VIEW_GROUP := "island_view"

## Заглушки зданий: высота коробки. Цвет/прозрачность по статусу — в
## building_status.gd (общая точка соответствия BuildingStatus -> визуал).
const STUB_HEIGHT := 1.6
const STUB_CASTLE_HEIGHT := 3.0
const STUB_FOOTPRINT_SCALE := 0.85 ## доля клетки под коробкой (зазор для читаемости)

var _camera_rig: Node3D
## player_id -> {"node": Node3D, "grid": Dictionary}
var _islands := {}
## player_id -> MultiMeshInstance3D (инстансинг зданий острова)
var _building_nodes := {}
## player_id -> { "cell_x,cell_z": Node3D } для лесов и POI
var _scaffold_nodes := {}
var _poi_nodes := {}


func _ready() -> void:
	add_to_group(VIEW_GROUP)
	# Обзорная камера — отдельный риг, изолированный от логики (SPEC §14, VR).
	_camera_rig = CameraRigScene.instantiate()
	add_child(_camera_rig)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-55, 30, 0)
	add_child(sun)

	var config_text := _load_generator_config()
	var core: Object = ClassDB.instantiate(&"DiceCoreServer")

	var player_ids: Array = NetSession.island_seeds.keys()
	player_ids.sort()
	var own_position := Vector3.ZERO
	for index in player_ids.size():
		var player_id: int = player_ids[index]
		var island_position := _ring_position(index, player_ids.size())
		var island := _build_island(core, player_id, int(NetSession.island_seeds[player_id]), config_text)
		if island == null:
			continue
		island.position = island_position
		island.name = "Island%d" % player_id
		add_child(island)
		island.add_to_group(ISLANDS_GROUP) # членство в группе — только внутри дерева
		if player_id == NetSession.my_player_id():
			own_position = island_position
			# Коллизия для кликов стройки нужна только на своём острове.
			_create_collision(island)

	_camera_rig.focus(own_position)

	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	NetSession.island_updated.connect(_on_island_updated)
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


## Информация об острове игрока для панели стройки: {"node", "grid"} или {}.
func island_info(player_id: int) -> Dictionary:
	return _islands.get(player_id, {})


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	_sync_buildings(turn_state.get("buildings", []))
	# Леса и POI динамичны (платформы, добыча) — обновляем из снапшота.
	for player in turn_state.get("players", []):
		_sync_scaffolds(int(player["id"]), player.get("scaffolds", []))
		_sync_pois(int(player["id"]), player.get("pois", []))


## Синхронизация зданий: один MultiMeshInstance3D на остров (инстансинг
## однотипных заглушек — перф-правка SPEC §14). Буфер инстансов пересобирается
## из снапшота; трансформ и цвет — на инстанс.
func _sync_buildings(buildings: Array) -> void:
	var by_island := {}
	for building in buildings:
		var pid: int = building["player_id"]
		if not by_island.has(pid):
			by_island[pid] = []
		by_island[pid].append(building)
	for pid in _islands:
		_rebuild_building_multimesh(int(pid), by_island.get(pid, []))


func _rebuild_building_multimesh(player_id: int, buildings: Array) -> void:
	var info: Dictionary = _islands.get(player_id, {})
	if info.is_empty():
		return
	var mmi: MultiMeshInstance3D = _building_nodes.get(player_id)
	if mmi == null:
		mmi = MultiMeshInstance3D.new()
		var mm := MultiMesh.new()
		mm.transform_format = MultiMesh.TRANSFORM_3D
		mm.use_colors = true
		mm.mesh = BoxMesh.new()
		mmi.multimesh = mm
		var material := StandardMaterial3D.new()
		material.vertex_color_use_as_albedo = true
		mmi.material_override = material
		(info["node"] as Node3D).add_child(mmi)
		_building_nodes[player_id] = mmi
	var mm: MultiMesh = mmi.multimesh
	var grid: Dictionary = info["grid"]
	mm.instance_count = buildings.size()
	for i in buildings.size():
		var b: Dictionary = buildings[i]
		var height := STUB_CASTLE_HEIGHT if b["type"] == "castle" else STUB_HEIGHT
		var cx: int = b["cell_x"]
		var cz: int = b["cell_z"]
		var cell: float = grid["cell_size"]
		var heights: PackedFloat32Array = grid["heights"]
		var cells_x: int = grid["cells_x"]
		var surface_y: float = heights[cz * cells_x + cx]
		var basis := Basis().scaled(Vector3(
			b["size_x"] * cell * STUB_FOOTPRINT_SCALE, height, b["size_z"] * cell * STUB_FOOTPRINT_SCALE))
		var origin := Vector3(
			grid["origin_x"] + (cx + b["size_x"] * 0.5) * cell,
			surface_y + height * 0.5,
			grid["origin_z"] + (cz + b["size_z"] * 0.5) * cell)
		mm.set_instance_transform(i, Transform3D(basis, origin))
		mm.set_instance_color(i, BuildingStatus.color_for(b["type"], int(b["status"])))


## Клетки-леса острова игрока (SPEC §11.4). Для своего острова добавляем
## коллизию, чтобы на лесах можно было строить кликом.
func _sync_scaffolds(player_id: int, scaffolds: Array) -> void:
	var info: Dictionary = _islands.get(player_id, {})
	if info.is_empty():
		return
	var grid: Dictionary = info["grid"]
	var nodes: Dictionary = _scaffold_nodes.get(player_id, {})
	var alive := {}
	var own := player_id == NetSession.my_player_id()
	for cell in scaffolds:
		var key := "%d,%d" % [int(cell["cell_x"]), int(cell["cell_z"])]
		alive[key] = true
		if nodes.has(key):
			continue
		var tile := ScaffoldTileScene.instantiate()
		(info["node"] as Node3D).add_child(tile)
		tile.position = _cell_center(grid, int(cell["cell_x"]), int(cell["cell_z"]), 1, 1)
		var material := StandardMaterial3D.new()
		material.albedo_color = SCAFFOLD_COLOR
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		(tile.get_node("Mesh") as MeshInstance3D).material_override = material
		# Коллизия под клик стройки нужна только на своём острове.
		(tile.get_node("Collision") as CollisionShape3D).disabled = not own
		nodes[key] = tile
	for key in nodes.keys():
		if not alive.has(key):
			nodes[key].queue_free()
			nodes.erase(key)
	_scaffold_nodes[player_id] = nodes


## POI-маркеры острова игрока из снапшота (исчезают при добыче).
func _sync_pois(player_id: int, pois: Array) -> void:
	var info: Dictionary = _islands.get(player_id, {})
	if info.is_empty():
		return
	var grid: Dictionary = info["grid"]
	var nodes: Dictionary = _poi_nodes.get(player_id, {})
	var alive := {}
	for poi in pois:
		var key := "%d,%d" % [int(poi["cell_x"]), int(poi["cell_z"])]
		alive[key] = true
		if nodes.has(key):
			continue
		var marker := MeshInstance3D.new()
		var box := BoxMesh.new()
		box.size = POI_MARKER_SIZE
		marker.mesh = box
		var material := StandardMaterial3D.new()
		material.albedo_color = POI_STONE_COLOR if poi["kind"] == "stone" else POI_WOOD_COLOR
		marker.material_override = material
		var pos := _cell_center(grid, int(poi["cell_x"]), int(poi["cell_z"]), 1, 1)
		marker.position = pos + Vector3(0, POI_MARKER_SIZE.y * 0.5, 0)
		(info["node"] as Node3D).add_child(marker)
		nodes[key] = marker
	for key in nodes.keys():
		if not alive.has(key):
			nodes[key].queue_free()
			nodes.erase(key)
	_poi_nodes[player_id] = nodes


## Центр области клеток (cx,cz)+(size) на поверхности острова.
func _cell_center(grid: Dictionary, cx: int, cz: int, size_x: int, size_z: int) -> Vector3:
	var cell: float = grid["cell_size"]
	var heights: PackedFloat32Array = grid["heights"]
	var cells_x: int = grid["cells_x"]
	var surface_y: float = heights[cz * cells_x + cx]
	return Vector3(
		grid["origin_x"] + (cx + size_x * 0.5) * cell,
		surface_y,
		grid["origin_z"] + (cz + size_z * 0.5) * cell
	)


func _create_collision(island: Node3D) -> void:
	for child in island.find_children("*", "MeshInstance3D", true, false):
		(child as MeshInstance3D).create_trimesh_collision()


func _ring_position(index: int, count: int) -> Vector3:
	if count <= 1:
		return Vector3.ZERO
	var angle := TAU * index / count
	return Vector3(cos(angle), 0.0, sin(angle)) * RING_RADIUS


func _load_generator_config() -> String:
	var path := ProjectSettings.globalize_path("res://").path_join(GENERATOR_CONFIG_PATH)
	if not FileAccess.file_exists(path):
		push_warning("Конфиг генератора не найден (%s), используются дефолты ядра" % path)
		return ""
	return FileAccess.get_file_as_string(path)


func _build_island(core: Object, player_id: int, seed_value: int, config_text: String) -> Node3D:
	var result: Dictionary = core.generate_island(seed_value, config_text)
	if not result["ok"]:
		push_error("Генерация острова отклонена ядром: %s" % result["reason"])
		return null

	var mesh_root := _mesh_from_glb(result["glb"])
	if mesh_root == null:
		return null

	var island := Node3D.new()
	island.add_child(mesh_root)
	_apply_atlas_material(mesh_root)

	var overlay: Node3D = GridOverlay.new()
	overlay.setup(result["grid"])
	island.add_child(overlay)
	_islands[player_id] = {"node": island, "grid": result["grid"], "mesh": mesh_root}
	return island


## Загрузка меша острова из GLB-байтов (первичная и после деструкции).
func _mesh_from_glb(glb: PackedByteArray) -> Node:
	var gltf := GLTFDocument.new()
	var gltf_state := GLTFState.new()
	var err := gltf.append_from_buffer(glb, "", gltf_state)
	if err != OK:
		push_error("GLB не разобран (код %d)" % err)
		return null
	var mesh_root: Node = gltf.generate_scene(gltf_state)
	if mesh_root == null:
		push_error("GLB не дал сцены")
	return mesh_root


## Ре-полигонизация меша острова после деструкции ландшафта (SPEC §11.5).
func _on_island_updated(update: Dictionary) -> void:
	var player_id := int(update["player"])
	var info: Dictionary = _islands.get(player_id, {})
	if info.is_empty():
		return
	var new_mesh := _mesh_from_glb(update["glb"])
	if new_mesh == null:
		return
	var island: Node3D = info["node"]
	if info.get("mesh") != null and is_instance_valid(info["mesh"]):
		(info["mesh"] as Node).queue_free()
	island.add_child(new_mesh)
	_apply_atlas_material(new_mesh)
	info["mesh"] = new_mesh
	_islands[player_id] = info
	# Свой остров — обновляем коллизию под клики стройки.
	if player_id == NetSession.my_player_id():
		_create_collision(new_mesh)


func _apply_atlas_material(node: Node) -> void:
	if node is MeshInstance3D:
		var material := StandardMaterial3D.new()
		material.albedo_texture = ATLAS_TEXTURE
		material.roughness = 1.0
		(node as MeshInstance3D).material_override = material
	for child in node.get_children():
		_apply_atlas_material(child)

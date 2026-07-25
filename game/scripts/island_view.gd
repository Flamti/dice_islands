extends Node3D
## Сборщик карты (SPEC §11.2): по сидам из NetSession генерирует остров каждого
## участника через ядро (локально — генерация детерминирована и не зависит от
## стейта партии), грузит GLB, кладёт оверлей сетки и расставляет острова кольцом.

const GridOverlay := preload("res://scripts/grid_overlay.gd")
const BuildingStubScene := preload("res://scenes/building_stub.tscn")
const BuildingStatus := preload("res://scripts/building_status.gd")
const ScaffoldTileScene := preload("res://scenes/scaffold_tile.tscn")

const SCAFFOLD_COLOR := Color(0.72, 0.6, 0.38, 0.85)
const POI_STONE_COLOR := Color(0.55, 0.55, 0.6)
const POI_WOOD_COLOR := Color(0.5, 0.33, 0.15)
const POI_MARKER_SIZE := Vector3(0.9, 0.9, 0.9)

const ATLAS_TEXTURE := preload("res://assets/atlas_placeholder.png")
## Путь к конфигу генератора относительно res:// (data/ живёт в корне репо).
const GENERATOR_CONFIG_PATH := "../data/generator.json"
const RING_RADIUS := 60.0
const CAMERA_OFFSET := Vector3(0, 34, 28)
const ISLANDS_GROUP := "islands"
const VIEW_GROUP := "island_view"

## Заглушки зданий: высота коробки. Цвет/прозрачность по статусу — в
## building_status.gd (общая точка соответствия BuildingStatus -> визуал).
const STUB_HEIGHT := 1.6
const STUB_CASTLE_HEIGHT := 3.0
const STUB_FOOTPRINT_SCALE := 0.85 ## доля клетки под коробкой (зазор для читаемости)

var _camera: Camera3D
## player_id -> {"node": Node3D, "grid": Dictionary}
var _islands := {}
## id здания -> MeshInstance3D
var _building_nodes := {}
## player_id -> { "cell_x,cell_z": Node3D } для лесов и POI
var _scaffold_nodes := {}
var _poi_nodes := {}


func _ready() -> void:
	add_to_group(VIEW_GROUP)
	_camera = Camera3D.new()
	add_child(_camera)

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

	_camera.look_at_from_position(own_position + CAMERA_OFFSET, own_position, Vector3.UP)

	NetSession.turn_state_changed.connect(_on_turn_state_changed)
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


## Синхронизация заглушек зданий со снапшотом хода.
func _sync_buildings(buildings: Array) -> void:
	var alive_ids := {}
	for building in buildings:
		var id: int = building["id"]
		alive_ids[id] = true
		var info: Dictionary = _islands.get(building["player_id"], {})
		if info.is_empty():
			continue
		var stub: MeshInstance3D = _building_nodes.get(id)
		if stub == null:
			stub = BuildingStubScene.instantiate()
			(info["node"] as Node3D).add_child(stub)
			_building_nodes[id] = stub
		_place_stub(stub, building, info["grid"])
	for id in _building_nodes.keys():
		if not alive_ids.has(id):
			_building_nodes[id].queue_free()
			_building_nodes.erase(id)


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


func _place_stub(stub: MeshInstance3D, building: Dictionary, grid: Dictionary) -> void:
	var cell: float = grid["cell_size"]
	var heights: PackedFloat32Array = grid["heights"]
	var cells_x: int = grid["cells_x"]
	var cx: int = building["cell_x"]
	var cz: int = building["cell_z"]
	var height := STUB_CASTLE_HEIGHT if building["type"] == "castle" else STUB_HEIGHT
	var surface_y: float = heights[cz * cells_x + cx]
	stub.scale = Vector3(
		building["size_x"] * cell * STUB_FOOTPRINT_SCALE,
		height,
		building["size_z"] * cell * STUB_FOOTPRINT_SCALE
	)
	stub.position = Vector3(
		grid["origin_x"] + (cx + building["size_x"] * 0.5) * cell,
		surface_y + height * 0.5,
		grid["origin_z"] + (cz + building["size_z"] * 0.5) * cell
	)
	var status: int = building["status"]
	var material := StandardMaterial3D.new()
	material.albedo_color = BuildingStatus.color_for(building["type"], status)
	if BuildingStatus.is_translucent(status):
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	stub.material_override = material


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

	var gltf := GLTFDocument.new()
	var gltf_state := GLTFState.new()
	var err := gltf.append_from_buffer(result["glb"], "", gltf_state)
	if err != OK:
		push_error("GLB не разобран (код %d)" % err)
		return null
	var mesh_root: Node = gltf.generate_scene(gltf_state)
	if mesh_root == null:
		push_error("GLB не дал сцены")
		return null

	var island := Node3D.new()
	island.add_child(mesh_root)
	_apply_atlas_material(mesh_root)

	var overlay: Node3D = GridOverlay.new()
	overlay.setup(result["grid"])
	island.add_child(overlay)
	_islands[player_id] = {"node": island, "grid": result["grid"]}
	return island


func _apply_atlas_material(node: Node) -> void:
	if node is MeshInstance3D:
		var material := StandardMaterial3D.new()
		material.albedo_texture = ATLAS_TEXTURE
		material.roughness = 1.0
		(node as MeshInstance3D).material_override = material
	for child in node.get_children():
		_apply_atlas_material(child)

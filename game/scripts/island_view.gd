extends Node3D
## Сборщик карты (SPEC §11.2): по сидам из NetSession генерирует остров каждого
## участника через ядро (локально — генерация детерминирована и не зависит от
## стейта партии), грузит GLB, кладёт оверлей сетки и расставляет острова кольцом.

const GridOverlay := preload("res://scripts/grid_overlay.gd")

const ATLAS_TEXTURE := preload("res://assets/atlas_placeholder.png")
## Путь к конфигу генератора относительно res:// (data/ живёт в корне репо).
const GENERATOR_CONFIG_PATH := "../data/generator.json"
const RING_RADIUS := 60.0
const CAMERA_OFFSET := Vector3(0, 34, 28)
const ISLANDS_GROUP := "islands"

var _camera: Camera3D


func _ready() -> void:
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
		var island := _build_island(core, int(NetSession.island_seeds[player_id]), config_text)
		if island == null:
			continue
		island.position = island_position
		island.name = "Island%d" % player_id
		add_child(island)
		island.add_to_group(ISLANDS_GROUP) # членство в группе — только внутри дерева
		if player_id == NetSession.my_player_id():
			own_position = island_position

	_camera.look_at_from_position(own_position + CAMERA_OFFSET, own_position, Vector3.UP)


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


func _build_island(core: Object, seed_value: int, config_text: String) -> Node3D:
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
	return island


func _apply_atlas_material(node: Node) -> void:
	if node is MeshInstance3D:
		var material := StandardMaterial3D.new()
		material.albedo_texture = ATLAS_TEXTURE
		material.roughness = 1.0
		(node as MeshInstance3D).material_override = material
	for child in node.get_children():
		_apply_atlas_material(child)

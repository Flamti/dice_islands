extends Node3D
## Проигрывание боя (SPEC §9.3): бойцы-капсулы движутся по логу кадров из ядра
## на острове защитника. Только визуализация — исход уже посчитан хостом.

const TICK_RATE := 20.0 ## тиков/с симуляции (совпадает с data/combat.json)
const PLAYBACK_SPEED := 2.0 ## ускорение проигрывания для краткости
const CAPSULE_HEIGHT := 1.2
const CAPSULE_RADIUS := 0.28
const ATTACKER_COLOR := Color(0.85, 0.3, 0.25)
const DEFENDER_COLOR := Color(0.3, 0.5, 0.85)
const ISLANDS_GROUP := "island_view"

var _frames: Array = []
var _grid: Dictionary = {}
var _island: Node3D
var _capsules := {} ## unit id -> MeshInstance3D
var _play_time := 0.0
var _frame_idx := 0
var _active := false


func _ready() -> void:
	NetSession.battle_ready.connect(_on_battle_ready)


func _on_battle_ready(battle: Dictionary) -> void:
	# Остров защитника: играем бой в его локальном пространстве.
	var views := get_tree().get_nodes_in_group(ISLANDS_GROUP)
	if views.is_empty():
		return
	var info: Dictionary = views[0].island_info(int(battle["defender"]))
	if info.is_empty():
		return
	_clear()
	_island = info["node"]
	_grid = info["grid"]
	_frames = battle.get("frames", [])
	_play_time = 0.0
	_frame_idx = 0
	_active = not _frames.is_empty()


func _process(delta: float) -> void:
	if not _active:
		return
	_play_time += delta * PLAYBACK_SPEED
	var current_tick := _play_time * TICK_RATE

	# Находим кадр, чей tick <= current_tick (кадры по возрастанию tick).
	while _frame_idx + 1 < _frames.size() and float(_frames[_frame_idx + 1]["tick"]) <= current_tick:
		_frame_idx += 1
	_render_frame(_frames[_frame_idx])

	if _frame_idx >= _frames.size() - 1:
		# Досмотрели последний кадр — гасим бой через короткую паузу.
		if _play_time * TICK_RATE > float(_frames[_frames.size() - 1]["tick"]) + TICK_RATE:
			_clear()


func _render_frame(frame: Dictionary) -> void:
	var alive := {}
	for unit in frame["units"]:
		var id: int = unit["id"]
		alive[id] = true
		var capsule: MeshInstance3D = _capsules.get(id)
		if capsule == null:
			capsule = _make_capsule(bool(unit["attacker"]))
			_island.add_child(capsule)
			_capsules[id] = capsule
		capsule.position = _cell_to_local(float(unit["x"]), float(unit["z"]))
	for id in _capsules.keys():
		if not alive.has(id):
			_capsules[id].queue_free()
			_capsules.erase(id)


func _make_capsule(attacker: bool) -> MeshInstance3D:
	var capsule := MeshInstance3D.new()
	var mesh := CapsuleMesh.new()
	mesh.height = CAPSULE_HEIGHT
	mesh.radius = CAPSULE_RADIUS
	capsule.mesh = mesh
	var material := StandardMaterial3D.new()
	material.albedo_color = ATTACKER_COLOR if attacker else DEFENDER_COLOR
	capsule.material_override = material
	return capsule


## Позиция бойца (в клеточных координатах) -> локальные координаты острова.
func _cell_to_local(cell_x: float, cell_z: float) -> Vector3:
	var cell: float = _grid["cell_size"]
	var cells_x: int = _grid["cells_x"]
	var heights: PackedFloat32Array = _grid["heights"]
	var ix := clampi(int(floor(cell_x)), 0, cells_x - 1)
	var iz := clampi(int(floor(cell_z)), 0, int(_grid["cells_z"]) - 1)
	var y: float = heights[iz * cells_x + ix]
	return Vector3(
		_grid["origin_x"] + cell_x * cell,
		y + CAPSULE_HEIGHT * 0.5,
		_grid["origin_z"] + cell_z * cell
	)


func _clear() -> void:
	for id in _capsules.keys():
		_capsules[id].queue_free()
	_capsules.clear()
	_frames = []
	_active = false
	_island = null

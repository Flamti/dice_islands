extends PanelContainer
## Режим стройки (этапы 4, 8): выбор здания из data/buildings.json, клик по
## своему острову -> BuildIntent; режимы сноса и добычи POI. Валидация — на
## хосте, панель только собирает намерения и показывает вердикты.

const BUILDINGS_CONFIG_PATH := "../data/buildings.json"
const RAY_LENGTH := 500.0
const PHASE_DEVELOPMENT := 6
## Здания, доступные из панели (порядок кнопок). Замок предразмещён и не строится.
const BUILDABLE_IDS: Array[String] = [
	"farm", "sawmill", "quarry", "market", "workshop", "barracks", "school",
	"university", "warehouse", "mill", "port", "platform", "wall", "tower", "wonder",
]

var _selected_building := "" ## пусто — ничего не выбрано
var _demolish_mode := false
var _harvest_mode := false
var _buttons := {} ## id здания -> Button
var _demolish_button: Button
var _harvest_button: Button
var _status_label: Label


func _ready() -> void:
	_build_ui()
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	NetSession.intent_confirmed.connect(_on_intent_confirmed)
	visible = false


func _build_ui() -> void:
	var box := VBoxContainer.new()
	add_child(box)

	var title := Label.new()
	title.text = "Стройка"
	box.add_child(title)

	var defs := _load_building_defs()
	for id in BUILDABLE_IDS:
		if not defs.has(id):
			continue
		var def: Dictionary = defs[id]
		var button := Button.new()
		button.text = "%s (%s)" % [def.get("name", id), _cost_text(def.get("cost", {}))]
		button.toggle_mode = true
		button.pressed.connect(_on_building_pressed.bind(id))
		box.add_child(button)
		_buttons[id] = button

	_demolish_button = Button.new()
	_demolish_button.text = "Снести (молоток, возврат 50%)"
	_demolish_button.toggle_mode = true
	_demolish_button.pressed.connect(_on_demolish_pressed)
	box.add_child(_demolish_button)

	_harvest_button = Button.new()
	_harvest_button.text = "Добыть POI (молоток)"
	_harvest_button.toggle_mode = true
	_harvest_button.pressed.connect(_on_harvest_pressed)
	box.add_child(_harvest_button)

	_status_label = Label.new()
	_status_label.text = "Выберите здание и кликните по своему острову"
	box.add_child(_status_label)


func _load_building_defs() -> Dictionary:
	var path := ProjectSettings.globalize_path("res://").path_join(BUILDINGS_CONFIG_PATH)
	if not FileAccess.file_exists(path):
		push_warning("Не найден %s — панель стройки пуста" % path)
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed == null or not parsed is Dictionary:
		push_error("Битый buildings.json")
		return {}
	return parsed.get("buildings", {})


func _cost_text(cost: Dictionary) -> String:
	var parts: Array[String] = []
	if cost.get("wood", 0) > 0:
		parts.append("%dД" % int(cost["wood"]))
	if cost.get("stone", 0) > 0:
		parts.append("%dК" % int(cost["stone"]))
	if cost.get("gold", 0) > 0:
		parts.append("%dЗ" % int(cost["gold"]))
	parts.append("1М")
	return " ".join(parts)


# --- Выбор режима ------------------------------------------------------------


func _on_building_pressed(id: String) -> void:
	_demolish_mode = false
	_harvest_mode = false
	_demolish_button.set_pressed_no_signal(false)
	_harvest_button.set_pressed_no_signal(false)
	_selected_building = "" if _selected_building == id else id
	for button_id in _buttons:
		_buttons[button_id].set_pressed_no_signal(button_id == _selected_building)
	_status_label.text = "Клик по острову — стройка: %s" % _selected_building \
			if _selected_building != "" else "Выбор снят"


func _on_demolish_pressed() -> void:
	_clear_building_selection()
	_harvest_mode = false
	_harvest_button.set_pressed_no_signal(false)
	_demolish_mode = _demolish_button.button_pressed
	_status_label.text = "Клик по зданию — снос" if _demolish_mode else "Выбор снят"


func _on_harvest_pressed() -> void:
	_clear_building_selection()
	_demolish_mode = false
	_demolish_button.set_pressed_no_signal(false)
	_harvest_mode = _harvest_button.button_pressed
	_status_label.text = "Клик по POI — добыча" if _harvest_mode else "Выбор снят"


func _clear_building_selection() -> void:
	_selected_building = ""
	for button_id in _buttons:
		_buttons[button_id].set_pressed_no_signal(false)


# --- Клик по острову ---------------------------------------------------------


func _unhandled_input(event: InputEvent) -> void:
	if not visible or (_selected_building == "" and not _demolish_mode and not _harvest_mode):
		return
	if not (event is InputEventMouseButton and event.pressed \
			and event.button_index == MOUSE_BUTTON_LEFT):
		return
	var cell := _pick_cell(event.position)
	if cell.x < 0:
		return
	if _harvest_mode:
		NetSession.submit_intent({"type": "harvest", "payload": {
			"cell_x": str(cell.x), "cell_z": str(cell.y)}})
	elif _demolish_mode:
		NetSession.submit_intent({"type": "demolish", "payload": {
			"cell_x": str(cell.x), "cell_z": str(cell.y)}})
	else:
		NetSession.submit_intent({"type": "build", "payload": {
			"building": _selected_building,
			"cell_x": str(cell.x), "cell_z": str(cell.y)}})
	get_viewport().set_input_as_handled()


## Клетка своего острова под курсором либо (-1, -1).
func _pick_cell(screen_pos: Vector2) -> Vector2i:
	var views := get_tree().get_nodes_in_group("island_view")
	if views.is_empty():
		return Vector2i(-1, -1)
	var info: Dictionary = views[0].island_info(NetSession.my_player_id())
	if info.is_empty():
		return Vector2i(-1, -1)

	var camera := get_viewport().get_camera_3d()
	if camera == null:
		return Vector2i(-1, -1)
	var from := camera.project_ray_origin(screen_pos)
	var to := from + camera.project_ray_normal(screen_pos) * RAY_LENGTH
	var hit: Dictionary = camera.get_world_3d().direct_space_state.intersect_ray(
		PhysicsRayQueryParameters3D.create(from, to))
	if hit.is_empty():
		return Vector2i(-1, -1)

	var island: Node3D = info["node"]
	var grid: Dictionary = info["grid"]
	var local: Vector3 = hit["position"] - island.global_position
	var cell_x := int(floor((local.x - grid["origin_x"]) / grid["cell_size"]))
	var cell_z := int(floor((local.z - grid["origin_z"]) / grid["cell_size"]))
	if cell_x < 0 or cell_x >= int(grid["cells_x"]) or cell_z < 0 or cell_z >= int(grid["cells_z"]):
		return Vector2i(-1, -1)
	return Vector2i(cell_x, cell_z)


# --- Реакция на стейт --------------------------------------------------------


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	visible = turn_state["phase"] == PHASE_DEVELOPMENT


func _on_intent_confirmed(result: Dictionary) -> void:
	if result["type"] not in ["build", "demolish", "harvest"]:
		return
	if result["accepted"]:
		_status_label.text = "Принято: %s" % result["type"]
	else:
		_status_label.text = "Отклонено: %s" % result["reason"]

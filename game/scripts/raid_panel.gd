extends PanelContainer
## Панель рейдов (SPEC §9.4): в фазе Набегов игрок с активным Портом отправляет
## часть гарнизона на здание игрока чужой команды. Валидация — на хосте.

const PHASE_RAIDS := 7
const STATUS_ACTIVE := 1
const STATUS_DESTROYED := 3
const SIDE_NAMES := ["Север", "Восток", "Юг", "Запад"]

var _target_option: OptionButton
var _count_spin: SpinBox
var _side_option: OptionButton
var _send_button: Button
var _status_label: Label
## Параллельно _target_option: индекс -> {"player": int, "building": int}
var _targets: Array = []


func _ready() -> void:
	var box := VBoxContainer.new()
	add_child(box)
	box.add_child(_titled("Набеги"))

	_target_option = OptionButton.new()
	box.add_child(_labeled("Цель", _target_option))
	_count_spin = SpinBox.new()
	_count_spin.min_value = 1
	_count_spin.max_value = 8
	_count_spin.value = 1
	box.add_child(_labeled("Бойцов", _count_spin))
	_side_option = OptionButton.new()
	for name in SIDE_NAMES:
		_side_option.add_item(name)
	box.add_child(_labeled("Высадка", _side_option))

	_send_button = Button.new()
	_send_button.text = "Отправить рейд"
	_send_button.pressed.connect(_on_send_pressed)
	box.add_child(_send_button)

	_status_label = Label.new()
	box.add_child(_status_label)

	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	NetSession.intent_confirmed.connect(_on_intent_confirmed)
	visible = false


func _titled(text: String) -> Label:
	var label := Label.new()
	label.text = text
	return label


func _labeled(text: String, control: Control) -> HBoxContainer:
	var row := HBoxContainer.new()
	var label := Label.new()
	label.text = text + ":"
	row.add_child(label)
	control.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(control)
	return row


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	visible = turn_state["phase"] == PHASE_RAIDS
	if not visible:
		return
	_rebuild_targets(turn_state)


func _rebuild_targets(turn_state: Dictionary) -> void:
	var my_id := NetSession.my_player_id()
	var my_team := -1
	var garrison := 0
	var has_port := false
	for player in turn_state["players"]:
		if player["id"] == my_id:
			my_team = player["team"]
			garrison = int(player["resources"]["swords"])
	for building in turn_state.get("buildings", []):
		if building["player_id"] == my_id and building["type"] == "port" \
				and building["status"] == STATUS_ACTIVE:
			has_port = true

	# Цели: не разрушенные здания игроков чужих команд.
	var team_by_player := {}
	for player in turn_state["players"]:
		team_by_player[player["id"]] = player["team"]

	_targets.clear()
	_target_option.clear()
	for building in turn_state.get("buildings", []):
		var owner: int = building["player_id"]
		if owner == my_id or team_by_player.get(owner, my_team) == my_team:
			continue
		if building["status"] == STATUS_DESTROYED:
			continue
		_targets.append({"player": owner, "building": int(building["id"])})
		_target_option.add_item("Игрок %d: %s" % [owner + 1, building["type"]])

	_count_spin.max_value = max(1, garrison)
	_send_button.disabled = not has_port or _targets.is_empty() or garrison <= 0
	if not has_port:
		_status_label.text = "Нужен активный Порт"
	elif _targets.is_empty():
		_status_label.text = "Нет целей чужих команд"
	else:
		_status_label.text = "Гарнизон: %d" % garrison


func _on_send_pressed() -> void:
	var idx := _target_option.selected
	if idx < 0 or idx >= _targets.size():
		return
	var target: Dictionary = _targets[idx]
	NetSession.submit_intent({"type": "raid", "payload": {
		"target_player": str(target["player"]),
		"target_building": str(target["building"]),
		"count": str(int(_count_spin.value)),
		"side": str(_side_option.selected),
	}})


func _on_intent_confirmed(result: Dictionary) -> void:
	if result["type"] != "raid":
		return
	if result["accepted"]:
		_status_label.text = "Рейд отправлен"
		_send_button.disabled = true
	else:
		_status_label.text = "Отклонено: %s" % result["reason"]

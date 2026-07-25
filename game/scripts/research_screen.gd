extends PanelContainer
## Экран исследований (SPEC §8): три ветки по три узла, открываются
## последовательно. Доступен в фазе Развития при активном Университете.
## Трата культуры уменьшает и баланс, и счёт культурной победы (§3).

const RESEARCH_CONFIG_PATH := "../data/research.json"
const PHASE_DEVELOPMENT := 6
const BRANCH_NAMES := {"economy": "Экономика", "war": "Война", "magic": "Магия"}

var _branches := {} ## branch -> Array[node dict]
var _node_buttons := {} ## node id -> Button
var _status_label: Label
var _unlocked := {} ## node id -> true (изучено)
var _culture := 0
var _available := false


func _ready() -> void:
	_branches = _load_branches()
	_build_ui()
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	NetSession.intent_confirmed.connect(_on_intent_confirmed)
	visible = false


func _load_branches() -> Dictionary:
	var path := ProjectSettings.globalize_path("res://").path_join(RESEARCH_CONFIG_PATH)
	if not FileAccess.file_exists(path):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed == null or not parsed is Dictionary:
		return {}
	return parsed.get("branches", {})


func _build_ui() -> void:
	var box := VBoxContainer.new()
	add_child(box)
	var title := Label.new()
	title.text = "Исследования"
	box.add_child(title)

	var columns := HBoxContainer.new()
	box.add_child(columns)
	for branch in _branches:
		var col := VBoxContainer.new()
		col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var head := Label.new()
		head.text = BRANCH_NAMES.get(branch, branch)
		col.add_child(head)
		for node in _branches[branch]:
			var button := Button.new()
			button.text = "%s (%dК)" % [node.get("name", node["id"]), int(node.get("cost", 0))]
			button.pressed.connect(_on_node_pressed.bind(String(node["id"])))
			col.add_child(button)
			_node_buttons[String(node["id"])] = button
		columns.add_child(col)

	_status_label = Label.new()
	box.add_child(_status_label)


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	var my_id := NetSession.my_player_id()
	for player in turn_state["players"]:
		if player["id"] != my_id:
			continue
		_available = bool(player.get("research_available", false))
		_culture = int(player["resources"]["culture"])
		_unlocked.clear()
		for effect in player.get("research", []):
			_unlocked[String(effect)] = true
	# Экран виден в фазе Развития при активном университете (SPEC §8).
	visible = turn_state["phase"] == PHASE_DEVELOPMENT and _available
	if visible:
		_refresh_buttons()


func _refresh_buttons() -> void:
	for branch in _branches:
		var prev_unlocked := true
		for node in _branches[branch]:
			var id := String(node["id"])
			var cost := int(node.get("cost", 0))
			var owned: bool = _unlocked.has(id)
			var button: Button = _node_buttons[id]
			# Доступен, если предыдущий изучен, сам не изучен и хватает культуры.
			button.disabled = owned or not prev_unlocked or _culture < cost
			button.text = "%s (%dК)%s" % [
				node.get("name", id), cost, " ✓" if owned else ""]
			prev_unlocked = owned
	_status_label.text = "Культура: %d" % _culture


func _on_node_pressed(node_id: String) -> void:
	NetSession.submit_intent({"type": "research", "payload": {"node": node_id}})


func _on_intent_confirmed(result: Dictionary) -> void:
	if result["type"] != "research":
		return
	if result["accepted"]:
		_status_label.text = "Изучено: %s" % result["payload"].get("node", "?")
	else:
		_status_label.text = "Отклонено: %s" % result["reason"]

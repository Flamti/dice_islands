extends PanelContainer
## Полоса собственных ресурсов с капами (SPEC §3). Капы читаются из
## data/dice.json — та же истина, что у ядра.

const DICE_CONFIG_PATH := "../data/dice.json"
## (ключ ресурса, подпись); капы показываются, если заданы в конфиге.
const RESOURCE_ORDER := [
	["wood", "Дерево"], ["stone", "Камень"], ["food", "Еда"], ["gold", "Золото"],
	["hammers", "Молотки"], ["swords", "Мечи"], ["culture", "Культура"],
]

var _label: Label
var _caps := {}


func _ready() -> void:
	_label = Label.new()
	add_child(_label)
	_caps = _load_caps()
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


func _load_caps() -> Dictionary:
	var path := ProjectSettings.globalize_path("res://").path_join(DICE_CONFIG_PATH)
	if not FileAccess.file_exists(path):
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed == null or not parsed is Dictionary:
		return {}
	return parsed.get("caps", {})


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	var my_id := NetSession.my_player_id()
	for player in turn_state["players"]:
		if player["id"] != my_id:
			continue
		var parts: Array[String] = []
		for entry in RESOURCE_ORDER:
			var key: String = entry[0]
			var value := int(player["resources"][key])
			var cap := int(_caps.get(key, 0))
			parts.append("%s %s" % [entry[1], "%d/%d" % [value, cap] if cap > 0 else str(value)])
		_label.text = "  ".join(parts)
		return

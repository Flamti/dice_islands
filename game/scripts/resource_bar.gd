extends PanelContainer
## Полоса собственных ресурсов с капами (SPEC §3). Капы приходят из снапшота
## (эффективные, с учётом складов — этап 8); ядро остаётся единой истиной.

## (ключ ресурса, подпись); капы показываются для еды/дерева/камня.
const RESOURCE_ORDER := [
	["wood", "Дерево"], ["stone", "Камень"], ["food", "Еда"], ["gold", "Золото"],
	["hammers", "Молотки"], ["swords", "Мечи"], ["culture", "Культура"],
]

var _label: Label


func _ready() -> void:
	_label = Label.new()
	add_child(_label)
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	var my_id := NetSession.my_player_id()
	for player in turn_state["players"]:
		if player["id"] != my_id:
			continue
		var caps: Dictionary = player.get("caps", {})
		var parts: Array[String] = []
		for entry in RESOURCE_ORDER:
			var key: String = entry[0]
			var value := int(player["resources"][key])
			var cap := int(caps.get(key, 0))
			parts.append("%s %s" % [entry[1], "%d/%d" % [value, cap] if cap > 0 else str(value)])
		_label.text = "  ".join(parts)
		return

extends PanelContainer
## Панель игроков (перенесена из полосы фаз, этап полировки): по каждому игроку —
## команда, теги (ИИ/выбыл/готов/голод) и ресурсы. Читает снапшоты хода из
## NetSession, ничего не считает сама. Голод берётся из статусов зданий снапшота.

const BuildingStatus := preload("res://scripts/building_status.gd")

var _players_box: VBoxContainer


func _ready() -> void:
	var box := VBoxContainer.new()
	add_child(box)

	var title := Label.new()
	title.text = "Игроки"
	box.add_child(title)

	_players_box = VBoxContainer.new()
	box.add_child(_players_box)

	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	_rebuild_players(turn_state["players"], turn_state.get("buildings", []))


func _rebuild_players(players: Array, buildings: Array) -> void:
	for child in _players_box.get_children():
		child.queue_free()
	var starving_by_player := _count_starving(buildings)
	for player in players:
		var row := Label.new()
		var tags: Array[String] = []
		if player["is_ai"]:
			tags.append("ИИ")
		if not player["alive"]:
			tags.append("выбыл")
		if player["ready"]:
			tags.append("готов")
		var starving := int(starving_by_player.get(player["id"], 0))
		if starving > 0:
			tags.append("голод: %d" % starving)
		var suffix: String = " (%s)" % ", ".join(tags) if not tags.is_empty() else ""
		var res: Dictionary = player["resources"]
		row.text = "Игрок %d | команда %d%s | Д:%d К:%d З:%d Е:%d М:%d" % [
			player["id"] + 1, player["team"], suffix,
			res["wood"], res["stone"], res["gold"], res["food"], res["hammers"],
		]
		_players_box.add_child(row)


## player_id -> число зданий со статусом Starving (SPEC §4 фаза 1).
func _count_starving(buildings: Array) -> Dictionary:
	var result := {}
	for building in buildings:
		if building["status"] == BuildingStatus.STATUS_STARVING:
			var pid: int = building["player_id"]
			result[pid] = int(result.get(pid, 0)) + 1
	return result

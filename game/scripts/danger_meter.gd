extends PanelContainer
## Шкала опасности всех игроков (SPEC §7.1): заполнение видно всем как элемент
## взаимного давления. Читает danger/danger_max из снапшота хода.

var _rows_box: VBoxContainer
var _bars := {} ## player_id -> ProgressBar


func _ready() -> void:
	var box := VBoxContainer.new()
	add_child(box)
	var title := Label.new()
	title.text = "Опасность"
	box.add_child(title)
	_rows_box = VBoxContainer.new()
	box.add_child(_rows_box)

	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	var danger_max := int(turn_state.get("danger_max", 0))
	# Система опасности выключена (нет disasters.json) — панель пуста.
	visible = danger_max > 0
	if not visible:
		return
	for player in turn_state["players"]:
		_update_row(int(player["id"]), int(player["danger"]), danger_max)


func _update_row(player_id: int, danger: int, danger_max: int) -> void:
	var bar: ProgressBar = _bars.get(player_id)
	if bar == null:
		var row := HBoxContainer.new()
		var label := Label.new()
		label.text = "Игрок %d" % (player_id + 1)
		label.custom_minimum_size = Vector2(80, 0)
		row.add_child(label)
		bar = ProgressBar.new()
		bar.max_value = danger_max
		bar.custom_minimum_size = Vector2(160, 0)
		row.add_child(bar)
		_rows_box.add_child(row)
		_bars[player_id] = bar
	bar.max_value = danger_max
	bar.value = danger
	# Ближе к порогу катастрофы — краснее (визуальный сигнал давления).
	var ratio := float(danger) / float(danger_max) if danger_max > 0 else 0.0
	bar.modulate = Color(0.6 + 0.4 * ratio, 0.8 - 0.6 * ratio, 0.3)

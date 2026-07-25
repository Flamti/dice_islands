extends PanelContainer
## Режим наблюдателя (SPEC §10): когда игрок выбыл (замок разрушен), он не
## может действовать — панель сообщает о наблюдении. Действующие панели сами
## скрываются, читая alive из снапшота (здесь — только индикатор статуса).

var _label: Label


func _ready() -> void:
	set_anchors_preset(Control.PRESET_CENTER_TOP)
	position = Vector2(-140, 44)
	_label = Label.new()
	_label.text = "Наблюдение: вы выбыли из партии"
	add_child(_label)
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	visible = false


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	var my_id := NetSession.my_player_id()
	for player in turn_state["players"]:
		if player["id"] == my_id:
			# Выбыл, но партия ещё идёт — показываем индикатор наблюдения.
			visible = not player["alive"] and not turn_state.get("finished", false)
			return

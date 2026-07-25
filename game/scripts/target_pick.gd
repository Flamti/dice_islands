extends PanelContainer
## Мини-решение выбора цели Opponent-катастрофы (Тёмная магия, SPEC §4/§8).
## Появляется, когда приходит target_choice для нашего игрока; таймер 15 с
## считает хост — если не выбрать, цель определится случайно.

var _label: Label
var _button_a: Button
var _button_b: Button


func _ready() -> void:
	var box := VBoxContainer.new()
	add_child(box)
	_label = Label.new()
	_label.text = "Тёмная магия: выбор цели"
	box.add_child(_label)
	var row := HBoxContainer.new()
	box.add_child(row)
	_button_a = Button.new()
	_button_a.pressed.connect(func() -> void: _pick(0))
	row.add_child(_button_a)
	_button_b = Button.new()
	_button_b.pressed.connect(func() -> void: _pick(1))
	row.add_child(_button_b)

	NetSession.party_event.connect(_on_party_event)
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	visible = false


func _on_party_event(event: Dictionary) -> void:
	if event["type"] != "target_choice":
		return
	var payload: Dictionary = event.get("payload", {})
	if int(payload.get("chooser", -1)) != NetSession.my_player_id():
		return
	_button_a.text = "Игрок %d" % (int(payload.get("candidate_a", 0)) + 1)
	_button_b.text = "Игрок %d" % (int(payload.get("candidate_b", 0)) + 1)
	_label.text = "«%s»: выберите цель (15 с)" % payload.get("disaster", "?")
	visible = true


func _pick(index: int) -> void:
	NetSession.pick_target(index)
	visible = false


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	# Хост разрешил выбор (фаза Катастроф прошла) — прячем панель.
	if turn_state.get("phase", -1) != 5:
		visible = false

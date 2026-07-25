extends PanelContainer
## Панель бросков (SPEC §6): свои кубики в фазе Бросков, выбор подмножества
## для переброса (грани с крестами заблокированы), кнопка реролла.
## Все правила считает хост; панель лишь отправляет намерение reroll.

const PHASE_ROLLS := 3
## Подписи ресурсов на гранях (порядок вывода).
const GAIN_LABELS := {
	"wood": "Д", "stone": "К", "food": "Е", "gold": "З",
	"hammers": "М", "swords": "Меч", "culture": "Кул",
}

var _dice_box: HBoxContainer
var _reroll_button: Button
var _info_label: Label
var _die_buttons: Array[CheckBox] = []
var _last_signature := "" ## отпечаток пула — чтобы не пересобирать без нужды


func _ready() -> void:
	var box := VBoxContainer.new()
	add_child(box)

	var title := Label.new()
	title.text = "Броски"
	box.add_child(title)

	_dice_box = HBoxContainer.new()
	box.add_child(_dice_box)

	_reroll_button = Button.new()
	_reroll_button.pressed.connect(_on_reroll_pressed)
	box.add_child(_reroll_button)

	_info_label = Label.new()
	box.add_child(_info_label)

	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	NetSession.intent_confirmed.connect(_on_intent_confirmed)
	visible = false
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	visible = turn_state["phase"] == PHASE_ROLLS
	if not visible:
		_last_signature = ""
		return
	var me := _my_player(turn_state)
	if me.is_empty():
		return
	var dice: Array = me["dice"]
	var rerolls: int = me["rerolls_left"]
	_reroll_button.text = "Перебросить выбранные (осталось %d)" % rerolls
	_reroll_button.disabled = rerolls <= 0

	# Пересборка кнопок только при изменении граней (сохраняет выбор игрока).
	var signature := "%d|%s" % [rerolls, str(dice)]
	if signature == _last_signature:
		return
	_last_signature = signature
	_rebuild_dice(dice)


func _rebuild_dice(dice: Array) -> void:
	for child in _dice_box.get_children():
		child.queue_free()
	_die_buttons.clear()
	for die in dice:
		var button := CheckBox.new()
		button.text = _face_text(die)
		# Блокиратор крестов: грань нельзя выделить для переброса (SPEC §6).
		button.disabled = die["crosses"] > 0
		if die["crosses"] > 0:
			button.tooltip_text = "Крест: грань зафиксирована"
		_dice_box.add_child(button)
		_die_buttons.append(button)


func _face_text(die: Dictionary) -> String:
	var parts: Array[String] = []
	var gain: Dictionary = die["gain"]
	for key in GAIN_LABELS:
		if int(gain.get(key, 0)) > 0:
			parts.append("%d%s" % [int(gain[key]), GAIN_LABELS[key]])
	for i in int(die["crosses"]):
		parts.append("✗")
	if parts.is_empty():
		parts.append("—")
	return "[%s] %s" % [die["type"], "+".join(parts)]


func _on_reroll_pressed() -> void:
	var selected: Array[String] = []
	for i in _die_buttons.size():
		if _die_buttons[i].button_pressed:
			selected.append(str(i))
	if selected.is_empty():
		_info_label.text = "Выберите кубики для переброса"
		return
	NetSession.submit_intent({"type": "reroll", "payload": {"dice": ",".join(selected)}})


func _on_intent_confirmed(result: Dictionary) -> void:
	if result["type"] != "reroll":
		return
	if result["accepted"]:
		_info_label.text = "Переброшено (осталось %s)" % result["payload"].get("rerolls_left", "?")
	else:
		_info_label.text = "Отклонено: %s" % result["reason"]


func _my_player(turn_state: Dictionary) -> Dictionary:
	var my_id := NetSession.my_player_id()
	for player in turn_state["players"]:
		if player["id"] == my_id:
			return player
	return {}

extends PanelContainer
## Панель бросков (SPEC §6): свои кубики в фазе Бросков как 3D-d6 (die3d.gd),
## выбор подмножества для переброса (грани с крестами заблокированы), кнопка
## реролла. Все правила считает хост; панель лишь отправляет намерение reroll.

const PHASE_ROLLS := 3
const Die3DScene := preload("res://scenes/die3d.tscn")
## Путь к конфигу кубиков относительно res:// (data/ живёт в корне репо).
const DICE_CONFIG_PATH := "../data/dice.json"
## Ресурсные ключи граней — для сопоставления выпавшей грани снапшота с конфигом.
const RESOURCE_KEYS := ["wood", "stone", "food", "gold", "hammers", "swords", "culture"]

var _dice_box: HBoxContainer
var _reroll_button: Button
var _info_label: Label
var _dice: Array = [] ## текущие Die3D
var _faces_by_type := {} ## тип кубика -> массив из 6 граней (dice.json)
var _last_signature := "" ## отпечаток пула — чтобы не пересобирать без нужды


func _ready() -> void:
	_faces_by_type = _load_dice_faces()

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


func _load_dice_faces() -> Dictionary:
	var path := ProjectSettings.globalize_path("res://").path_join(DICE_CONFIG_PATH)
	if not FileAccess.file_exists(path):
		push_warning("Не найден %s — 3D-кубики без граней" % path)
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed == null or not parsed is Dictionary:
		push_error("Битый dice.json")
		return {}
	return parsed.get("dice", {})


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

	# Пересборка кубиков только при изменении граней (сохраняет выбор игрока).
	var signature := "%d|%s" % [rerolls, str(dice)]
	if signature == _last_signature:
		return
	_last_signature = signature
	_rebuild_dice(dice)


func _rebuild_dice(dice: Array) -> void:
	for child in _dice_box.get_children():
		child.queue_free()
	_dice.clear()
	for die in dice:
		var d: Control = Die3DScene.instantiate()
		_dice_box.add_child(d) # add_child запускает _ready и строит вьюпорт кубика
		var type: String = die["type"]
		var faces: Array = _faces_by_type.get(type, [])
		d.setup(type, faces)
		var crosses := int(die["crosses"])
		if not faces.is_empty():
			d.show_face(_match_face(faces, die.get("gain", {}), crosses))
		# Блокиратор крестов: грань нельзя выделить для переброса (SPEC §6).
		d.set_disabled(crosses > 0)
		if crosses > 0:
			d.tooltip_text = "Крест: грань зафиксирована"
		_dice.append(d)


## Индекс грани в конфиге, совпадающей с выпавшей (ресурсы + кресты) или 0.
func _match_face(faces: Array, gain: Dictionary, crosses: int) -> int:
	for i in faces.size():
		var f: Dictionary = faces[i]
		if int(f.get("cross", 0)) != crosses:
			continue
		var ok := true
		for key in RESOURCE_KEYS:
			if int(f.get(key, 0)) != int(gain.get(key, 0)):
				ok = false
				break
		if ok:
			return i
	return 0


func _on_reroll_pressed() -> void:
	var selected: Array[String] = []
	for i in _dice.size():
		if _dice[i].selected:
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

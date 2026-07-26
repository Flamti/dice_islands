extends Control
## Полноэкранный экран броска (SPEC §6): показывается поверх карты в фазе Броски.
## Механика Keep/Roll — клик по кубику переносит его между «оставить» и
## «перебросить»; кресты фиксируются в Keep. Всё считает ядро, экран лишь шлёт
## намерение reroll и рисует снапшот. Иконок-ассетов нет — плейсхолдеры (буквы).

const PHASE_ROLLS := 3
const Die3DScene := preload("res://scenes/die3d.tscn")
const PlayerListScene := preload("res://ui/player_list.tscn")

## Пути к конфигам относительно res:// (data/ в корне репо).
const DICE_CONFIG_PATH := "../data/dice.json"
const DISASTERS_CONFIG_PATH := "../data/disasters.json"
const BUILDINGS_CONFIG_PATH := "../data/buildings.json"

const DIM_COLOR := Color(0.03, 0.05, 0.09, 0.82)
const READY_DIAMETER := 116.0
const READY_NORMAL := Color(0.22, 0.45, 0.28)
const READY_HOVER := Color(0.28, 0.55, 0.35)
const READY_PRESSED := Color(0.3, 0.75, 0.4)
const REACHED_COLOR := Color(1.0, 0.55, 0.4)
const DIM_TIER_COLOR := Color(0.55, 0.6, 0.66)

const STATUS_ACTIVE := 1 ## BuildingStatus.STATUS_ACTIVE

## Глифы ресурсов на гранях/в сводке.
const GAIN_LABELS := {
	"wood": "Д", "stone": "К", "food": "Е", "gold": "З",
	"hammers": "М", "swords": "Меч", "culture": "Кул",
}
## Порядок и подписи ресурсов в сводке (как в resource_bar).
const RESOURCE_ORDER := [
	["wood", "Дерево"], ["stone", "Камень"], ["food", "Еда"], ["gold", "Золото"],
	["hammers", "Молотки"], ["swords", "Мечи"], ["culture", "Культура"],
]
const CAPPED := {"wood": true, "stone": true, "food": true}

# --- Данные ---
var _faces_by_type := {} ## тип кубика -> 6 граней (dice.json)
var _building_defs := {} ## id здания -> def (buildings.json)
var _disaster_tiers := {} ## tier -> {"self": name, "opponent": name}
var _thresholds := {} ## tier -> danger value
var _danger_max := 12

# --- Состояние броска ---
var _dice_nodes: Array = [] ## Die3D по индексу пула
var _last_dice: Array = [] ## прошлый пул (для точечной перерисовки)
var _kept := {} ## index -> true (оставленные игроком + кресты)
var _locked := {} ## index -> true (кресты — не двигаются)
var _total_rerolls := 0
var _last_turn := -1

# --- Узлы UI ---
var _turn_label: Label
var _phase_label: Label
var _crosses_label: Label
var _keep_box: HFlowContainer
var _roll_box: HFlowContainer
var _reroll_button: Button
var _info_label: Label
var _danger_box: VBoxContainer
var _resource_box: VBoxContainer
var _sources_label: Label
var _ready_button: Button


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	_faces_by_type = _load_json_dict(DICE_CONFIG_PATH, "dice")
	_building_defs = _load_json_dict(BUILDINGS_CONFIG_PATH, "buildings")
	_load_disasters()
	_build_ui()

	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	NetSession.intent_confirmed.connect(_on_intent_confirmed)
	visible = false
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


# --- Загрузка конфигов --------------------------------------------------------


func _load_json_dict(rel_path: String, key: String) -> Dictionary:
	var path := ProjectSettings.globalize_path("res://").path_join(rel_path)
	if not FileAccess.file_exists(path):
		push_warning("Не найден %s" % path)
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed == null or not parsed is Dictionary:
		push_error("Битый %s" % rel_path)
		return {}
	return parsed.get(key, {})


func _load_disasters() -> void:
	var path := ProjectSettings.globalize_path("res://").path_join(DISASTERS_CONFIG_PATH)
	if not FileAccess.file_exists(path):
		return
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed == null or not parsed is Dictionary:
		return
	_danger_max = int(parsed.get("danger_max", 12))
	for entry in parsed.get("thresholds", []):
		_thresholds[int(entry["tier"])] = int(entry["value"])
	for id in parsed.get("disasters", {}):
		var def: Dictionary = parsed["disasters"][id]
		var tier := int(def.get("tier", 0))
		var target: String = def.get("target", "self")
		if not _disaster_tiers.has(tier):
			_disaster_tiers[tier] = {"self": "—", "opponent": "—"}
		_disaster_tiers[tier][target] = def.get("name", id)


# --- Построение UI ------------------------------------------------------------


func _build_ui() -> void:
	var dim := ColorRect.new()
	dim.color = DIM_COLOR
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	dim.mouse_filter = Control.MOUSE_FILTER_STOP # модально: карта под ним не кликается
	add_child(dim)

	add_child(PlayerListScene.instantiate()) # список игроков (сам себя якорит слева)

	_build_header()
	_build_zones()
	_build_danger_scale()
	_build_resource_summary()
	_build_sources()
	_build_ready_button()


func _build_header() -> void:
	var box := VBoxContainer.new()
	box.set_anchors_preset(Control.PRESET_CENTER_TOP)
	box.offset_left = -160
	box.offset_right = 160
	box.offset_top = 8
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	add_child(box)
	_turn_label = _centered_label(24)
	_phase_label = _centered_label(18)
	_crosses_label = _centered_label(40)
	box.add_child(_turn_label)
	box.add_child(_phase_label)
	box.add_child(_crosses_label)


func _build_zones() -> void:
	var row := HBoxContainer.new()
	row.set_anchors_preset(Control.PRESET_CENTER)
	row.offset_left = -520
	row.offset_right = 520
	row.offset_top = -250
	row.offset_bottom = 150
	row.add_theme_constant_override("separation", 24)
	add_child(row)
	_keep_box = _make_zone(row, "KEEP")
	_roll_box = _make_zone(row, "ROLL")

	_reroll_button = Button.new()
	_reroll_button.set_anchors_preset(Control.PRESET_CENTER)
	_reroll_button.offset_left = 140
	_reroll_button.offset_right = 480
	_reroll_button.offset_top = 168
	_reroll_button.offset_bottom = 208
	_reroll_button.pressed.connect(_on_reroll_pressed)
	add_child(_reroll_button)

	_info_label = Label.new()
	_info_label.set_anchors_preset(Control.PRESET_CENTER)
	_info_label.offset_left = 140
	_info_label.offset_right = 480
	_info_label.offset_top = 214
	_info_label.offset_bottom = 240
	add_child(_info_label)


func _make_zone(parent: HBoxContainer, title: String) -> HFlowContainer:
	var panel := PanelContainer.new()
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	parent.add_child(panel)
	var vbox := VBoxContainer.new()
	panel.add_child(vbox)
	var label := Label.new()
	label.text = title
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.add_theme_font_size_override("font_size", 22)
	vbox.add_child(label)
	var flow := HFlowContainer.new()
	flow.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vbox.add_child(flow)
	return flow


func _build_danger_scale() -> void:
	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_RIGHT_WIDE)
	panel.offset_left = -232
	panel.offset_right = -8
	panel.offset_top = 96
	panel.offset_bottom = -96
	add_child(panel)
	_danger_box = VBoxContainer.new()
	panel.add_child(_danger_box)


func _build_resource_summary() -> void:
	# Левый нижний угол зарезервирован под кнопку «Готов» — сводку ставим правее неё.
	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	panel.offset_left = READY_DIAMETER + 44
	panel.offset_top = -230
	panel.offset_right = READY_DIAMETER + 344
	panel.offset_bottom = -8
	add_child(panel)
	var vbox := VBoxContainer.new()
	panel.add_child(vbox)
	var title := Label.new()
	title.text = "Ресурсы (тек+прогноз / cap)"
	vbox.add_child(title)
	_resource_box = VBoxContainer.new()
	vbox.add_child(_resource_box)


func _build_sources() -> void:
	var panel := PanelContainer.new()
	panel.set_anchors_preset(Control.PRESET_CENTER_BOTTOM)
	panel.offset_left = -260
	panel.offset_right = 60
	panel.offset_top = -96
	panel.offset_bottom = -8
	add_child(panel)
	var vbox := VBoxContainer.new()
	panel.add_child(vbox)
	var title := Label.new()
	title.text = "Источники кубиков (по активным зданиям)"
	vbox.add_child(title)
	_sources_label = Label.new()
	vbox.add_child(_sources_label)


func _build_ready_button() -> void:
	# Кнопка «Готов» — в левом нижнем углу (как в основном HUD).
	_ready_button = Button.new()
	_ready_button.set_anchors_preset(Control.PRESET_BOTTOM_LEFT)
	_ready_button.offset_left = 28
	_ready_button.offset_top = -(READY_DIAMETER + 28)
	_ready_button.offset_right = READY_DIAMETER + 28
	_ready_button.offset_bottom = -28
	_ready_button.toggle_mode = true
	_ready_button.text = "Готов"
	_ready_button.focus_mode = Control.FOCUS_NONE
	_ready_button.add_theme_font_size_override("font_size", 24)
	_round_style(_ready_button, "normal", READY_NORMAL)
	_round_style(_ready_button, "hover", READY_HOVER)
	_round_style(_ready_button, "pressed", READY_PRESSED)
	_round_style(_ready_button, "hover_pressed", READY_PRESSED)
	_ready_button.toggled.connect(func(p: bool) -> void: NetSession.set_phase_ready(p))
	add_child(_ready_button)


func _round_style(button: Button, state: String, color: Color) -> void:
	var style := StyleBoxFlat.new()
	style.bg_color = color
	style.set_corner_radius_all(int(READY_DIAMETER * 0.5))
	button.add_theme_stylebox_override(state, style)


func _centered_label(font_size: int) -> Label:
	var label := Label.new()
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.add_theme_font_size_override("font_size", font_size)
	return label


# --- Реакция на снапшот -------------------------------------------------------


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	visible = turn_state["phase"] == PHASE_ROLLS
	if not visible:
		return
	var turn: int = turn_state["turn"]
	if turn != _last_turn:
		# Новый ход = свежий бросок: сбрасываем выбор и фиксируем максимум рероллов.
		_last_turn = turn
		_kept.clear()
		_ready_button.set_pressed_no_signal(false)
	_turn_label.text = "Ход %d" % turn
	_phase_label.text = "Фаза: Броски"

	var me := _my_player(turn_state)
	if me.is_empty():
		return
	var dice: Array = me["dice"]
	var rerolls_left: int = me["rerolls_left"]
	if turn != _last_turn or _total_rerolls == 0 or rerolls_left > _total_rerolls:
		_total_rerolls = rerolls_left
	_sync_dice(dice)
	_update_reroll_button(rerolls_left)
	_update_header(dice)
	_update_danger(me)
	_update_resources(me, dice)
	_update_sources(turn_state.get("buildings", []))


## Пересборка/обновление кубиков. Полная пересборка при смене числа кубиков/хода;
## иначе точечно обновляем изменившиеся грани (оставленные не перекатываются).
func _sync_dice(dice: Array) -> void:
	if dice.size() != _dice_nodes.size():
		_rebuild_dice(dice)
	else:
		for i in dice.size():
			# Важно: держим в _locked только реально зафиксированные (крест),
			# иначе _locked.has(i) станет true для всех (ключ есть даже при false).
			if int(dice[i]["crosses"]) > 0:
				_locked[i] = true
				_kept[i] = true
			else:
				_locked.erase(i)
			if _face_changed(dice[i], _last_dice[i] if i < _last_dice.size() else {}):
				_apply_die(i, dice[i])
	_last_dice = dice.duplicate(true)
	_place_all()


func _rebuild_dice(dice: Array) -> void:
	for node in _dice_nodes:
		node.queue_free()
	_dice_nodes.clear()
	_locked.clear()
	for i in dice.size():
		var d: Control = Die3DScene.instantiate()
		_roll_box.add_child(d) # временный родитель; _place_all() расставит зоны
		d.selection_toggled.connect(_on_die_clicked)
		d.set_meta("index", i)
		_dice_nodes.append(d)
		_apply_die(i, dice[i])
		if int(dice[i]["crosses"]) > 0:
			_locked[i] = true
			_kept[i] = true


func _apply_die(index: int, die: Dictionary) -> void:
	var d: Control = _dice_nodes[index]
	var type: String = die["type"]
	var faces: Array = _faces_by_type.get(type, [])
	d.setup(type, faces)
	if not faces.is_empty():
		d.show_face(_match_face(faces, die.get("gain", {}), int(die["crosses"])))
	d.set_disabled(int(die["crosses"]) > 0) # крест — нельзя двигать/перебрасывать
	d.tooltip_text = "Крест: зафиксирован" if int(die["crosses"]) > 0 else ""


func _face_changed(a: Dictionary, b: Dictionary) -> bool:
	return int(a.get("crosses", 0)) != int(b.get("crosses", 0)) \
			or str(a.get("gain", {})) != str(b.get("gain", {}))


## Расставить кубики по зонам: Keep (оставленные/кресты) и Roll (перебрасываемые).
func _place_all() -> void:
	for i in _dice_nodes.size():
		var target := _keep_box if (_kept.has(i) or _locked.has(i)) else _roll_box
		var d: Control = _dice_nodes[i]
		if d.get_parent() != target:
			if d.get_parent() != null:
				d.get_parent().remove_child(d)
			target.add_child(d)
		d.selected = false
		d.queue_redraw()


func _on_die_clicked(die: Control) -> void:
	var i := int(die.get_meta("index", -1))
	if i < 0 or _locked.has(i):
		return
	if _kept.has(i):
		_kept.erase(i)
	else:
		_kept[i] = true
	# Перенос выполняем отложенно: менять дерево прямо в обработчике ввода кубика
	# (внутри его же _gui_input) нельзя — Godot прервёт перенос активного узла.
	_place_all.call_deferred()
	_update_reroll_button(_current_rerolls_left())


func _current_rerolls_left() -> int:
	var me := _my_player(NetSession.turn_state)
	return int(me.get("rerolls_left", 0)) if not me.is_empty() else 0


func _roll_indices() -> Array:
	var out: Array = []
	for i in _dice_nodes.size():
		if not _kept.has(i) and not _locked.has(i):
			out.append(i)
	return out


func _update_reroll_button(rerolls_left: int) -> void:
	var used := _total_rerolls - rerolls_left
	var roll_empty := _roll_indices().is_empty()
	if rerolls_left <= 0:
		_reroll_button.text = "Перебросов нет"
	else:
		_reroll_button.text = "Re Roll %d/%d" % [used + 1, _total_rerolls]
	_reroll_button.disabled = rerolls_left <= 0 or roll_empty


func _on_reroll_pressed() -> void:
	var indices := _roll_indices()
	if indices.is_empty():
		_info_label.text = "Нет кубиков для переброса (все оставлены)"
		return
	var as_str: Array[String] = []
	for i in indices:
		as_str.append(str(i))
	NetSession.submit_intent({"type": "reroll", "payload": {"dice": ",".join(as_str)}})


func _on_intent_confirmed(result: Dictionary) -> void:
	if result["type"] != "reroll":
		return
	if result["accepted"]:
		_info_label.text = "Переброшено (осталось %s)" % result["payload"].get("rerolls_left", "?")
	else:
		_info_label.text = "Отклонено: %s" % result["reason"]


# --- Панели снапшота ----------------------------------------------------------


func _update_header(dice: Array) -> void:
	var crosses := 0
	for die in dice:
		crosses += int(die["crosses"])
	_crosses_label.text = "Крестов: %d" % crosses


func _update_danger(me: Dictionary) -> void:
	for child in _danger_box.get_children():
		child.queue_free()
	var danger := int(me.get("danger", 0))
	var header := Label.new()
	header.text = "Опасность %d/%d   Self | Others" % [danger, _danger_max]
	_danger_box.add_child(header)
	# Тиры сверху вниз: высокий тир — выше.
	var tiers := _disaster_tiers.keys()
	tiers.sort()
	tiers.reverse()
	for tier in tiers:
		var value := int(_thresholds.get(tier, 0))
		var reached := danger >= value
		var row := Label.new()
		var names: Dictionary = _disaster_tiers[tier]
		row.text = "≥%d  %s | %s" % [value, names["self"], names["opponent"]]
		row.add_theme_color_override("font_color", REACHED_COLOR if reached else DIM_TIER_COLOR)
		_danger_box.add_child(row)


func _update_resources(me: Dictionary, dice: Array) -> void:
	for child in _resource_box.get_children():
		child.queue_free()
	var resources: Dictionary = me.get("resources", {})
	var caps: Dictionary = me.get("caps", {})
	var income := _projected_income(dice)
	for entry in RESOURCE_ORDER:
		var key: String = entry[0]
		var cur := int(resources.get(key, 0))
		var inc := int(income.get(key, 0))
		var cap := int(caps.get(key, 0))
		var label := Label.new()
		if CAPPED.has(key) and cap > 0:
			label.text = "%s: %d+%d / %d" % [entry[1], cur, inc, cap]
		else:
			label.text = "%s: %d+%d" % [entry[1], cur, inc]
		_resource_box.add_child(label)


## Прогноз дохода = сумма граней всего пула (приближение; точную конвертацию с
## капами делает ядро в фазе Ресурсов).
func _projected_income(dice: Array) -> Dictionary:
	var income := {}
	for die in dice:
		var gain: Dictionary = die.get("gain", {})
		for key in gain:
			income[key] = int(income.get(key, 0)) + int(gain[key])
	return income


func _update_sources(buildings: Array) -> void:
	var my_id := NetSession.my_player_id()
	var counts := {}
	for b in buildings:
		if int(b["player_id"]) != my_id or int(b["status"]) != STATUS_ACTIVE:
			continue
		var type: String = b["type"]
		var def: Dictionary = _building_defs.get(type, {})
		if not def.has("dice"):
			continue
		counts[type] = int(counts.get(type, 0)) + 1
	var parts: Array[String] = []
	for type in counts:
		var name: String = _building_defs[type].get("name", type)
		parts.append("%s×%d" % [name, counts[type]])
	_sources_label.text = "  ".join(parts) if not parts.is_empty() else "—"


# --- Вспомогательное ----------------------------------------------------------


func _match_face(faces: Array, gain: Dictionary, crosses: int) -> int:
	for i in faces.size():
		var f: Dictionary = faces[i]
		if int(f.get("cross", 0)) != crosses:
			continue
		var ok := true
		for key in GAIN_LABELS:
			if int(f.get(key, 0)) != int(gain.get(key, 0)):
				ok = false
				break
		if ok:
			return i
	return 0


func _my_player(turn_state: Dictionary) -> Dictionary:
	if turn_state.is_empty():
		return {}
	var my_id := NetSession.my_player_id()
	for player in turn_state.get("players", []):
		if player["id"] == my_id:
			return player
	return {}

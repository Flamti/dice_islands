extends VBoxContainer
## HUD хода: полоса фаз 0–9, таймер, готовность игроков, кнопка «Готов».
## Читает снапшоты хода из NetSession, сам ничего не считает (логика — в ядре).

signal leave_requested

const BuildingStatus := preload("res://scripts/building_status.gd")

const PHASE_NAMES: Array[String] = [
	"Начало", "Еда", "Доход", "Броски", "Ресурсы",
	"Катастрофы", "Развитие", "Набеги", "Бой", "Проверки",
]
const DECISION_PHASE_HINT := "Фаза-решение: жмите «Готов» или ждите таймер"
const AUTO_PHASE_HINT := "Авто-фаза: хост считает результат"

var _turn_label: Label
var _phase_labels: Array[Label] = []
var _timer_label: Label
var _hint_label: Label
var _players_box: VBoxContainer
var _ready_button: CheckButton
var _last_seen_phase := -1
var _last_seen_turn := -1


func _ready() -> void:
	_build_ui()
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


func _build_ui() -> void:
	_turn_label = Label.new()
	add_child(_turn_label)

	var phases_row := HBoxContainer.new()
	for i in PHASE_NAMES.size():
		var phase_label := Label.new()
		phase_label.text = PHASE_NAMES[i]
		phase_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		phases_row.add_child(phase_label)
		_phase_labels.append(phase_label)
	add_child(phases_row)

	_timer_label = Label.new()
	add_child(_timer_label)

	_hint_label = Label.new()
	add_child(_hint_label)

	_players_box = VBoxContainer.new()
	add_child(_players_box)

	_ready_button = CheckButton.new()
	_ready_button.text = "Готов"
	_ready_button.toggled.connect(func(pressed: bool) -> void: NetSession.set_phase_ready(pressed))
	add_child(_ready_button)

	var leave_button := Button.new()
	leave_button.text = "Покинуть партию"
	leave_button.pressed.connect(func() -> void: leave_requested.emit())
	add_child(leave_button)


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	var turn: int = turn_state["turn"]
	var phase: int = turn_state["phase"]
	var is_decision: bool = turn_state["is_decision"]

	_turn_label.text = "Ход %d" % turn
	for i in _phase_labels.size():
		# Текущая фаза выделяется маркером — без темы и стилей до этапа полировки.
		_phase_labels[i].text = ("[%s]" % PHASE_NAMES[i]) if i == phase else PHASE_NAMES[i]

	var timer_remaining: float = turn_state["timer_remaining_sec"]
	_timer_label.text = "Таймер: %d с" % ceili(timer_remaining) if timer_remaining >= 0.0 else "Таймер: —"
	_hint_label.text = DECISION_PHASE_HINT if is_decision else AUTO_PHASE_HINT

	# Смена фазы (или хода) сбрасывает локальную кнопку готовности.
	if phase != _last_seen_phase or turn != _last_seen_turn:
		_last_seen_phase = phase
		_last_seen_turn = turn
		_ready_button.set_pressed_no_signal(false)
	_ready_button.visible = is_decision

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

extends VBoxContainer
## Верхняя полоса хода: ход, лента фаз 0–9 (текущая выделена), таймер, подсказка,
## кнопки «Готов»/«Покинуть» и host-only тумблер сохранений. Читает снапшоты хода
## из NetSession, сам ничего не считает (логика — в ядре). Список игроков вынесен
## в отдельную панель (player_list.gd), чтобы полоса не наезжала на 3D-вид.

signal leave_requested
signal save_menu_toggled(shown: bool)

const PHASE_NAMES: Array[String] = [
	"Начало", "Еда", "Доход", "Броски", "Ресурсы",
	"Катастрофы", "Развитие", "Набеги", "Бой", "Проверки",
]
const DECISION_PHASE_HINT := "Фаза-решение: жмите «Готов» или ждите таймер"
const AUTO_PHASE_HINT := "Авто-фаза: хост считает результат"
## Подсказки по фазам хода (SPEC §4) — лента подсказок фаз (этап 15).
const PHASE_HINTS: Array[String] = [
	"Активация построек и клеток-лесов; автосейв",
	"Апкип еды: недокормленные здания голодают",
	"Активные здания дают кубики в пул",
	"Бросайте и перебрасывайте кубики (кресты фиксируются)",
	"Грани превращаются в ресурсы; излишки сверх капа сгорают",
	"Кресты идут в шкалу опасности; срабатывают катастрофы",
	"Стройка, снос, исследования, добыча POI, апгрейды",
	"Отправка рейдов через активный Порт",
	"Автосимуляция боёв на островах-целях",
	"Проверка условий победы и выбывания",
]

var _turn_label: Label
var _phase_labels: Array[Label] = []
var _timer_label: Label
var _hint_label: Label
var _save_toggle: CheckButton


func _ready() -> void:
	_build_ui()
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


func _build_ui() -> void:
	# Строка 1: ход | лента фаз | таймер | кнопки.
	var top_row := _passthrough(HBoxContainer.new())
	add_child(top_row)

	_turn_label = _passthrough(Label.new())
	top_row.add_child(_turn_label)

	var phases_row := _passthrough(HBoxContainer.new())
	phases_row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	for i in PHASE_NAMES.size():
		var phase_label := _passthrough(Label.new())
		phase_label.text = PHASE_NAMES[i]
		phase_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		phase_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		phases_row.add_child(phase_label)
		_phase_labels.append(phase_label)
	top_row.add_child(phases_row)

	_timer_label = _passthrough(Label.new())
	top_row.add_child(_timer_label)

	var leave_button := Button.new()
	leave_button.text = "Покинуть"
	leave_button.pressed.connect(func() -> void: leave_requested.emit())
	top_row.add_child(leave_button)

	# Тумблер сохранений — только у хоста (гость не сохраняет/загружает).
	_save_toggle = CheckButton.new()
	_save_toggle.text = "Сохранения"
	_save_toggle.visible = NetSession.is_host
	_save_toggle.toggled.connect(func(pressed: bool) -> void: save_menu_toggled.emit(pressed))
	top_row.add_child(_save_toggle)

	# Строка 2: подсказка фазы.
	_hint_label = _passthrough(Label.new())
	add_child(_hint_label)


## Сделать контрол прозрачным для мыши — чтобы клик по пустой полосе доходил до
## 3D-вида (стройка ловит его в build_mode._unhandled_input).
func _passthrough(control: Control) -> Control:
	control.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return control


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
	var tip: String = PHASE_HINTS[phase] if phase < PHASE_HINTS.size() else ""
	var mode: String = DECISION_PHASE_HINT if is_decision else AUTO_PHASE_HINT
	_hint_label.text = "%s — %s" % [tip, mode]

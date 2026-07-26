extends Button
## Большая круглая кнопка готовности (левый нижний угол). Видна только в
## фазах-решениях, сбрасывается при смене фазы/хода. Дублирует действие «Готов»
## удобной крупной целью клика; правила фаз считает ядро.

const DIAMETER := 128.0
const FONT_SIZE := 26
const PHASE_ROLLS := 3 ## в фазе Броски «Готов» даёт экран броска — фоновую прячем
const NORMAL_COLOR := Color(0.22, 0.45, 0.28)
const HOVER_COLOR := Color(0.28, 0.55, 0.35)
const PRESSED_COLOR := Color(0.3, 0.75, 0.4)

var _last_phase := -1
var _last_turn := -1


func _ready() -> void:
	toggle_mode = true
	text = "Готов"
	focus_mode = Control.FOCUS_NONE
	custom_minimum_size = Vector2(DIAMETER, DIAMETER)
	add_theme_font_size_override("font_size", FONT_SIZE)
	_apply_round_style("normal", NORMAL_COLOR)
	_apply_round_style("hover", HOVER_COLOR)
	_apply_round_style("pressed", PRESSED_COLOR)
	_apply_round_style("hover_pressed", PRESSED_COLOR)

	toggled.connect(func(pressed: bool) -> void: NetSession.set_phase_ready(pressed))
	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	visible = false
	if not NetSession.turn_state.is_empty():
		_on_turn_state_changed(NetSession.turn_state)


func _apply_round_style(state: String, color: Color) -> void:
	var style := StyleBoxFlat.new()
	style.bg_color = color
	style.set_corner_radius_all(int(DIAMETER * 0.5)) # радиус = половина -> круг
	add_theme_stylebox_override(state, style)


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	var phase: int = turn_state["phase"]
	var turn: int = turn_state["turn"]
	# Смена фазы (или хода) сбрасывает локальную готовность.
	if phase != _last_phase or turn != _last_turn:
		_last_phase = phase
		_last_turn = turn
		set_pressed_no_signal(false)
	visible = turn_state["is_decision"] and turn_state["phase"] != PHASE_ROLLS

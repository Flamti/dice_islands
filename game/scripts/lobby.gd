extends Control
## Экран лобби этапа 1. UI создаётся кодом: до этапа полировки сцены
## остаются минимальными, вся логика сети — в синглтоне NetSession.

const SmokeRunner := preload("res://scripts/smoke_runner.gd")
const HudPhase := preload("res://scripts/hud_phase.gd")
const PhaseBarScene := preload("res://ui/phase_bar.tscn")
const IslandScene := preload("res://scenes/island.tscn")
const BuildPanelScene := preload("res://ui/build_panel.tscn")
const DiceTrayScene := preload("res://ui/dice_tray.tscn")
const ResourceBarScene := preload("res://ui/resource_bar.tscn")
const DangerMeterScene := preload("res://ui/danger_meter.tscn")
const EventFeedScene := preload("res://ui/event_feed.tscn")
const BattleViewScene := preload("res://scenes/battle_view.tscn")

const MARGIN_PX := 16
const TIMER_MAX_SEC := 600
const LOG_MIN_HEIGHT_PX := 160
const PORT_MIN := 1024
const PORT_MAX := 65535
const DEFAULT_ADDRESS := "127.0.0.1"
const DEFAULT_PLAYER_NAME := "Игрок"
const DEFAULT_ECHO_MESSAGE := "ping"
const SMOKE_ARG_PREFIX := "--smoke="

# --- Меню ---
var _menu_panel: VBoxContainer
var _name_edit: LineEdit
var _host_port_spin: SpinBox
var _host_password_edit: LineEdit
var _host_slots_spin: SpinBox
var _join_address_edit: LineEdit
var _join_port_spin: SpinBox
var _join_password_edit: LineEdit
var _status_label: Label

# --- Лобби ---
var _lobby_panel: VBoxContainer
var _slots_box: VBoxContainer
var _slot_count_spin: SpinBox
var _rolls_timer_spin: SpinBox
var _dev_timer_spin: SpinBox
var _raids_timer_spin: SpinBox
var _start_button: Button
var _ready_button: CheckButton
var _echo_edit: LineEdit
var _log_view: TextEdit

# --- Партия ---
var _phase_bar: HudPhase
var _island_view: Node3D
var _build_panel: Control
var _dice_tray: Control
var _resource_bar: Control
var _danger_meter: Control
var _event_feed: Control
var _battle_view: Node3D


func _ready() -> void:
	_build_menu_ui()
	_build_lobby_ui()
	_show_menu()

	NetSession.lobby_state_changed.connect(_on_lobby_state_changed)
	NetSession.join_rejected.connect(_on_join_rejected)
	NetSession.intent_confirmed.connect(_on_intent_confirmed)
	NetSession.log_message.connect(_append_log)
	NetSession.connection_lost.connect(_on_connection_lost)
	NetSession.match_started.connect(_on_match_started)

	_maybe_start_smoke()


# --- Построение UI -----------------------------------------------------------


func _build_menu_ui() -> void:
	_menu_panel = VBoxContainer.new()
	_menu_panel.set_anchors_preset(Control.PRESET_FULL_RECT)
	_menu_panel.offset_left = MARGIN_PX
	_menu_panel.offset_top = MARGIN_PX
	_menu_panel.offset_right = -MARGIN_PX
	_menu_panel.offset_bottom = -MARGIN_PX
	add_child(_menu_panel)

	_menu_panel.add_child(_make_title("Dice Islands"))

	_name_edit = _make_labeled_line_edit(_menu_panel, "Имя игрока", DEFAULT_PLAYER_NAME)

	_menu_panel.add_child(_make_title("Создать партию"))
	_host_port_spin = _make_labeled_spin(_menu_panel, "Порт", PORT_MIN, PORT_MAX, NetSession.DEFAULT_PORT)
	_host_password_edit = _make_labeled_line_edit(_menu_panel, "Пароль партии", "")
	_host_slots_spin = _make_labeled_spin(
		_menu_panel, "Число слотов", NetSession.MIN_SLOTS, NetSession.MAX_SLOTS, NetSession.DEFAULT_SLOT_COUNT
	)
	var host_button := Button.new()
	host_button.text = "Создать партию"
	host_button.pressed.connect(_on_host_pressed)
	_menu_panel.add_child(host_button)

	_menu_panel.add_child(_make_title("Присоединиться"))
	_join_address_edit = _make_labeled_line_edit(_menu_panel, "Адрес", DEFAULT_ADDRESS)
	_join_port_spin = _make_labeled_spin(_menu_panel, "Порт", PORT_MIN, PORT_MAX, NetSession.DEFAULT_PORT)
	_join_password_edit = _make_labeled_line_edit(_menu_panel, "Пароль партии", "")
	var join_button := Button.new()
	join_button.text = "Присоединиться"
	join_button.pressed.connect(_on_join_pressed)
	_menu_panel.add_child(join_button)

	_status_label = Label.new()
	_menu_panel.add_child(_status_label)


func _build_lobby_ui() -> void:
	_lobby_panel = VBoxContainer.new()
	_lobby_panel.set_anchors_preset(Control.PRESET_FULL_RECT)
	_lobby_panel.offset_left = MARGIN_PX
	_lobby_panel.offset_top = MARGIN_PX
	_lobby_panel.offset_right = -MARGIN_PX
	_lobby_panel.offset_bottom = -MARGIN_PX
	add_child(_lobby_panel)

	_lobby_panel.add_child(_make_title("Лобби"))

	_slot_count_spin = _make_labeled_spin(
		_lobby_panel, "Число слотов", NetSession.MIN_SLOTS, NetSession.MAX_SLOTS, NetSession.DEFAULT_SLOT_COUNT
	)
	_slot_count_spin.value_changed.connect(func(value: float) -> void:
		NetSession.host_set_slot_count(int(value)))

	_slots_box = VBoxContainer.new()
	_lobby_panel.add_child(_slots_box)

	_rolls_timer_spin = _make_labeled_spin(
		_lobby_panel, "Таймер Бросков, с (0 — без лимита)", 0, TIMER_MAX_SEC,
		int(NetSession.DEFAULT_ROLLS_TIMER_SEC)
	)
	_dev_timer_spin = _make_labeled_spin(
		_lobby_panel, "Таймер Развития, с (0 — без лимита)", 0, TIMER_MAX_SEC,
		int(NetSession.DEFAULT_DEVELOPMENT_TIMER_SEC)
	)
	_raids_timer_spin = _make_labeled_spin(
		_lobby_panel, "Таймер Набегов, с (0 — без лимита)", 0, TIMER_MAX_SEC,
		int(NetSession.DEFAULT_RAIDS_TIMER_SEC)
	)

	_start_button = Button.new()
	_start_button.text = "Начать партию"
	_start_button.pressed.connect(_on_start_match_pressed)
	_lobby_panel.add_child(_start_button)

	_ready_button = CheckButton.new()
	_ready_button.text = "Готов"
	_ready_button.toggled.connect(func(pressed: bool) -> void: NetSession.set_ready(pressed))
	_lobby_panel.add_child(_ready_button)

	var echo_row := HBoxContainer.new()
	_echo_edit = LineEdit.new()
	_echo_edit.text = DEFAULT_ECHO_MESSAGE
	_echo_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	echo_row.add_child(_echo_edit)
	var echo_button := Button.new()
	echo_button.text = "Эхо-тест"
	echo_button.pressed.connect(_on_echo_pressed)
	echo_row.add_child(echo_button)
	_lobby_panel.add_child(echo_row)

	_log_view = TextEdit.new()
	_log_view.editable = false
	_log_view.custom_minimum_size = Vector2(0, LOG_MIN_HEIGHT_PX)
	_log_view.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_lobby_panel.add_child(_log_view)

	var leave_button := Button.new()
	leave_button.text = "Покинуть партию"
	leave_button.pressed.connect(_on_leave_pressed)
	_lobby_panel.add_child(leave_button)


func _make_title(text: String) -> Label:
	var label := Label.new()
	label.text = text
	return label


func _make_labeled_line_edit(parent: Container, label_text: String, initial: String) -> LineEdit:
	var row := HBoxContainer.new()
	var label := Label.new()
	label.text = label_text + ":"
	row.add_child(label)
	var edit := LineEdit.new()
	edit.text = initial
	edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(edit)
	parent.add_child(row)
	return edit


func _make_labeled_spin(parent: Container, label_text: String, minimum: int, maximum: int, initial: int) -> SpinBox:
	var row := HBoxContainer.new()
	var label := Label.new()
	label.text = label_text + ":"
	row.add_child(label)
	var spin := SpinBox.new()
	spin.min_value = minimum
	spin.max_value = maximum
	spin.value = initial
	row.add_child(spin)
	parent.add_child(row)
	return spin


# --- Переключение экранов ----------------------------------------------------


func _show_menu() -> void:
	_menu_panel.visible = true
	_lobby_panel.visible = false


func _show_lobby() -> void:
	_menu_panel.visible = false
	_lobby_panel.visible = true
	_slot_count_spin.get_parent().visible = NetSession.is_host
	_rolls_timer_spin.get_parent().visible = NetSession.is_host
	_dev_timer_spin.get_parent().visible = NetSession.is_host
	_raids_timer_spin.get_parent().visible = NetSession.is_host
	_start_button.visible = NetSession.is_host
	_log_view.text = ""


# --- Обработчики UI ----------------------------------------------------------


func _on_host_pressed() -> void:
	var err := NetSession.host_match(
		int(_host_port_spin.value),
		_host_password_edit.text,
		int(_host_slots_spin.value),
		_name_edit.text,
	)
	if err != OK:
		_status_label.text = "Не удалось создать сервер (код %d)" % err
		return
	_show_lobby()


func _on_join_pressed() -> void:
	var err := NetSession.join_match(
		_join_address_edit.text,
		int(_join_port_spin.value),
		_join_password_edit.text,
		_name_edit.text,
	)
	if err != OK:
		_status_label.text = "Не удалось подключиться (код %d)" % err
		return
	_status_label.text = "Подключение..."


func _on_echo_pressed() -> void:
	NetSession.submit_intent({"type": "echo", "payload": {"message": _echo_edit.text}})


func _on_start_match_pressed() -> void:
	NetSession.host_start_match({
		"rolls_sec": _rolls_timer_spin.value,
		"development_sec": _dev_timer_spin.value,
		"raids_sec": _raids_timer_spin.value,
	})


func _on_leave_pressed() -> void:
	NetSession.leave()
	_show_menu()


# --- Партия ------------------------------------------------------------------


func _on_match_started() -> void:
	_lobby_panel.visible = false
	# Node3D под Control рендерится в общий World3D главного вьюпорта.
	_island_view = IslandScene.instantiate()
	add_child(_island_view)
	_phase_bar = PhaseBarScene.instantiate()
	_phase_bar.leave_requested.connect(_on_match_leave_requested)
	add_child(_phase_bar)
	_build_panel = BuildPanelScene.instantiate()
	add_child(_build_panel)
	_dice_tray = DiceTrayScene.instantiate()
	add_child(_dice_tray)
	_resource_bar = ResourceBarScene.instantiate()
	add_child(_resource_bar)
	_danger_meter = DangerMeterScene.instantiate()
	add_child(_danger_meter)
	_event_feed = EventFeedScene.instantiate()
	add_child(_event_feed)
	_battle_view = BattleViewScene.instantiate()
	add_child(_battle_view)


func _on_match_leave_requested() -> void:
	NetSession.leave()
	_close_match_view()
	_show_menu()


func _close_match_view() -> void:
	if _phase_bar != null:
		_phase_bar.queue_free()
		_phase_bar = null
	if _island_view != null:
		_island_view.queue_free()
		_island_view = null
	if _build_panel != null:
		_build_panel.queue_free()
		_build_panel = null
	if _dice_tray != null:
		_dice_tray.queue_free()
		_dice_tray = null
	if _resource_bar != null:
		_resource_bar.queue_free()
		_resource_bar = null
	if _danger_meter != null:
		_danger_meter.queue_free()
		_danger_meter = null
	if _event_feed != null:
		_event_feed.queue_free()
		_event_feed = null
	if _battle_view != null:
		_battle_view.queue_free()
		_battle_view = null


# --- Реакция на события сети -------------------------------------------------


func _on_lobby_state_changed(state: Dictionary) -> void:
	if not _lobby_panel.visible:
		_show_lobby() # гость попадает в лобби с первым полученным стейтом
	_slot_count_spin.set_value_no_signal(state["slot_count"])
	_rebuild_slots(state)


func _rebuild_slots(state: Dictionary) -> void:
	for child in _slots_box.get_children():
		child.queue_free()
	var slots: Array = state["slots"]
	for i in slots.size():
		_slots_box.add_child(_make_slot_row(i, slots[i]))


func _make_slot_row(slot_index: int, slot: Dictionary) -> HBoxContainer:
	var row := HBoxContainer.new()

	var info := Label.new()
	info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var kind: int = slot["kind"]
	var descr: String
	match kind:
		NetSession.SlotKind.HUMAN:
			descr = "%s%s" % [slot["name"], " [готов]" if slot["ready"] else ""]
		NetSession.SlotKind.AI:
			descr = "%s (ИИ)" % slot["name"]
		_:
			descr = "— пусто —"
	info.text = "Слот %d: %s | команда %d" % [slot_index + 1, descr, slot["team"]]
	row.add_child(info)

	if NetSession.is_host:
		var ai_check := CheckBox.new()
		ai_check.text = "ИИ"
		ai_check.button_pressed = kind == NetSession.SlotKind.AI
		ai_check.disabled = kind == NetSession.SlotKind.HUMAN
		ai_check.toggled.connect(func(pressed: bool) -> void:
			NetSession.host_set_ai(slot_index, pressed))
		row.add_child(ai_check)

		var team_spin := SpinBox.new()
		team_spin.min_value = NetSession.DEFAULT_TEAM
		team_spin.max_value = NetSession.MAX_SLOTS
		team_spin.value = slot["team"]
		team_spin.value_changed.connect(func(value: float) -> void:
			NetSession.host_set_team(slot_index, int(value)))
		row.add_child(team_spin)

	return row


func _on_join_rejected(reason: String) -> void:
	var text: String
	match reason:
		NetSession.REJECT_WRONG_PASSWORD:
			text = "Отказ: неверный пароль"
		NetSession.REJECT_LOBBY_FULL:
			text = "Отказ: лобби заполнено"
		_:
			text = "Отказ: %s" % reason
	_status_label.text = text
	_show_menu()


func _on_intent_confirmed(result: Dictionary) -> void:
	var verdict: String = "принято" if result["accepted"] else "отклонено (%s)" % result["reason"]
	_append_log("Подтверждение хоста: «%s» %s, payload=%s" % [result["type"], verdict, result["payload"]])


func _on_connection_lost() -> void:
	_status_label.text = "Соединение потеряно"
	_close_match_view()
	_show_menu()


func _append_log(text: String) -> void:
	_log_view.text += text + "\n"
	print("[лобби] ", text)


# --- Смоук-режим (автотест сети, запуск из scripts/net_smoke.sh) -------------


func _maybe_start_smoke() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with(SMOKE_ARG_PREFIX):
			var runner := SmokeRunner.new()
			runner.mode = arg.trim_prefix(SMOKE_ARG_PREFIX)
			add_child(runner)
			return

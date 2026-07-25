extends PanelContainer
## Экран итогов (SPEC §10): показывает победившую команду и путь победы,
## когда партия завершена. Данные — из снапшота хода (finished/winner_team).

signal leave_requested

const PATH_NAMES := {"wonder": "Чудо", "military": "Военная", "cultural": "Культурная"}

var _title: Label
var _detail: Label
var _last_path := "" ## путь из последнего события победы


func _ready() -> void:
	var box := VBoxContainer.new()
	add_child(box)
	_title = Label.new()
	box.add_child(_title)
	_detail = Label.new()
	box.add_child(_detail)
	var leave := Button.new()
	leave.text = "В меню"
	leave.pressed.connect(func() -> void: leave_requested.emit())
	box.add_child(leave)

	NetSession.turn_state_changed.connect(_on_turn_state_changed)
	NetSession.party_event.connect(_on_party_event)
	visible = false


func _on_party_event(event: Dictionary) -> void:
	if event["type"] == "victory":
		_last_path = event.get("payload", {}).get("path", "")


func _on_turn_state_changed(turn_state: Dictionary) -> void:
	if not turn_state.get("finished", false):
		return
	var team := int(turn_state.get("winner_team", -1))
	var my_team := _my_team(turn_state)
	var outcome := "Победа!" if team == my_team else "Поражение"
	_title.text = "%s Команда %d победила" % [outcome, team]
	var path: String = PATH_NAMES.get(_last_path, "—")
	_detail.text = "Путь победы: %s" % path
	visible = true


func _my_team(turn_state: Dictionary) -> int:
	var my_id := NetSession.my_player_id()
	for player in turn_state["players"]:
		if player["id"] == my_id:
			return int(player["team"])
	return -1

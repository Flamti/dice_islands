extends Node
## Автосценарии проверки сети этапа 1 (см. scripts/net_smoke.sh).
## Режимы: host — создаёт партию с ИИ и командами, ждёт гостя с эхо-тестом;
## guest — подключается, проверяет стейт, шлёт эхо; badguest — неверный пароль.

const TIMEOUT_SEC := 20.0
const SMOKE_PORT := 34567
const SMOKE_PASSWORD := "smoke-pass"
const WRONG_PASSWORD := "wrong-pass"
const SMOKE_SLOT_COUNT := 4
const AI_SLOT_INDEX := 1
const AI_TEAM := 2
const ECHO_MESSAGE := "ping"
const LOCALHOST := "127.0.0.1"

var mode := ""

# Флаги прогресса host-сценария.
var _guest_joined := false
var _echo_seen := false
# Флаг прогресса guest-сценария.
var _state_checked := false


func _ready() -> void:
	get_tree().create_timer(TIMEOUT_SEC).timeout.connect(_fail.bind("таймаут"))
	match mode:
		"host":
			_run_host()
		"guest":
			_run_guest()
		"badguest":
			_run_badguest()
		_:
			_fail("неизвестный режим «%s»" % mode)


func _run_host() -> void:
	NetSession.log_message.connect(_host_on_log)
	NetSession.lobby_state_changed.connect(_host_on_state)
	var err: Error = NetSession.host_match(SMOKE_PORT, SMOKE_PASSWORD, SMOKE_SLOT_COUNT, "смоук-хост")
	if err != OK:
		_fail("create_server: код %d" % err)
		return
	NetSession.host_set_ai(AI_SLOT_INDEX, true)
	NetSession.host_set_team(AI_SLOT_INDEX, AI_TEAM)
	print("SMOKE_HOST_READY")


func _host_on_log(text: String) -> void:
	if text.contains("«echo»") and text.contains("принято"):
		_echo_seen = true


func _host_on_state(state: Dictionary) -> void:
	var humans := _count_humans(state)
	if humans > 1:
		_guest_joined = true
	# Успех: гость приходил, отправил эхо и штатно вышел.
	if _guest_joined and _echo_seen and humans == 1:
		_pass_and_quit("SMOKE_HOST_OK")


func _run_guest() -> void:
	NetSession.lobby_state_changed.connect(_guest_on_state)
	NetSession.intent_confirmed.connect(_guest_on_intent)
	var err: Error = NetSession.join_match(LOCALHOST, SMOKE_PORT, SMOKE_PASSWORD, "смоук-гость")
	if err != OK:
		_fail("create_client: код %d" % err)


func _guest_on_state(state: Dictionary) -> void:
	if _state_checked:
		return
	# Проверка: хост и ИИ в слотах, команда ИИ назначена хостом.
	var ai_slot: Dictionary = state["slots"][AI_SLOT_INDEX]
	if _count_humans(state) < 2:
		return # наш собственный слот ещё не назначен
	if ai_slot["kind"] != NetSession.SlotKind.AI or ai_slot["team"] != AI_TEAM:
		_fail("стейт лобби не содержит ожидаемого ИИ-слота с командой %d" % AI_TEAM)
		return
	_state_checked = true
	NetSession.set_ready(true)
	NetSession.submit_intent({"type": "echo", "payload": {"message": ECHO_MESSAGE}})


func _guest_on_intent(result: Dictionary) -> void:
	if not _state_checked:
		_fail("подтверждение пришло до проверки стейта")
		return
	if not result["accepted"] or result["payload"].get("message", "") != ECHO_MESSAGE:
		_fail("эхо-ответ искажён: %s" % result)
		return
	NetSession.leave()
	_pass_and_quit("SMOKE_GUEST_OK")


func _run_badguest() -> void:
	NetSession.join_rejected.connect(func(reason: String) -> void:
		if reason == NetSession.REJECT_WRONG_PASSWORD:
			_pass_and_quit("SMOKE_BADGUEST_OK")
		else:
			_fail("неожиданная причина отказа: %s" % reason))
	NetSession.lobby_state_changed.connect(func(_state: Dictionary) -> void:
		_fail("гость с неверным паролем попал в лобби"))
	var err: Error = NetSession.join_match(LOCALHOST, SMOKE_PORT, WRONG_PASSWORD, "смоук-взломщик")
	if err != OK:
		_fail("create_client: код %d" % err)


func _count_humans(state: Dictionary) -> int:
	var humans := 0
	for slot in state["slots"]:
		if slot["kind"] == NetSession.SlotKind.HUMAN:
			humans += 1
	return humans


func _pass_and_quit(marker: String) -> void:
	print(marker)
	get_tree().quit(0)


func _fail(why: String) -> void:
	printerr("SMOKE_%s_FAIL: %s" % [mode.to_upper(), why])
	get_tree().quit(1)

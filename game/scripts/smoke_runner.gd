extends Node
## Автосценарии проверки сети (см. scripts/net_smoke.sh).
## Этап 1: host — лобби с ИИ и командами, ждёт гостя с эхо-тестом;
## guest — подключается, проверяет стейт, шлёт эхо; badguest — неверный пароль.
## Этап 2: mhost + mguest — партия двух людей: барьер PhaseReady держит фазу,
## пока не готовы оба; фазы с таймером завершаются сами.

const TIMEOUT_SEC := 30.0
const SMOKE_PORT := 34567
const SMOKE_PASSWORD := "smoke-pass"
const WRONG_PASSWORD := "wrong-pass"
const SMOKE_SLOT_COUNT := 4
const AI_SLOT_INDEX := 1
const AI_TEAM := 2
const ECHO_MESSAGE := "ping"
const LOCALHOST := "127.0.0.1"

# Параметры матчевого сценария (mhost/mguest).
const MATCH_SLOT_COUNT := 2
const MATCH_ROLLS_TIMER_SEC := 0.0 ## без лимита — проверка барьера готовности
const MATCH_DEV_TIMER_SEC := 2.0 ## проверка автозавершения фазы по таймеру
const MATCH_RAIDS_TIMER_SEC := 1.0
const BARRIER_HOLD_SEC := 1.0 ## сколько гость держит фазу Бросков незавершённой
const PHASE_ROLLS := 3
const TARGET_TURN := 2 ## успех — партия дожила до второго хода
const HOST_PLAYER_ID := 0

var mode := ""

# Флаги прогресса host-сценария.
var _guest_joined := false
var _echo_seen := false
# Флаг прогресса guest-сценария.
var _state_checked := false
# Флаги прогресса матчевого сценария.
var _match_started := false
var _turn_reached := false
var _barrier_wait_started := false
var _barrier_checked := false


func _ready() -> void:
	get_tree().create_timer(TIMEOUT_SEC).timeout.connect(_fail.bind("таймаут"))
	match mode:
		"host":
			_run_host()
		"guest":
			_run_guest()
		"badguest":
			_run_badguest()
		"mhost":
			_run_mhost()
		"mguest":
			_run_mguest()
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


# --- Матчевый сценарий этапа 2 (mhost + mguest) ------------------------------


func _run_mhost() -> void:
	NetSession.lobby_state_changed.connect(_mhost_on_lobby_state)
	NetSession.turn_state_changed.connect(_mhost_on_turn_state)
	var err: Error = NetSession.host_match(SMOKE_PORT, SMOKE_PASSWORD, MATCH_SLOT_COUNT, "смоук-хост")
	if err != OK:
		_fail("create_server: код %d" % err)
		return
	print("SMOKE_MHOST_READY")


func _mhost_on_lobby_state(state: Dictionary) -> void:
	if not _match_started and _count_humans(state) == MATCH_SLOT_COUNT:
		_match_started = NetSession.host_start_match({
			"rolls_sec": MATCH_ROLLS_TIMER_SEC,
			"development_sec": MATCH_DEV_TIMER_SEC,
			"raids_sec": MATCH_RAIDS_TIMER_SEC,
		})
		if not _match_started:
			_fail("host_start_match отклонён")
		return
	# Успех хоста: партия дошла до хода 2, острова с сетками на месте,
	# гость штатно вышел.
	if _turn_reached and _match_started and _count_humans(state) == 1:
		if not _islands_ok(MATCH_SLOT_COUNT):
			_fail("у хоста нет островов с сетками для всех участников")
			return
		_pass_and_quit("SMOKE_MHOST_OK")


func _mhost_on_turn_state(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	# Хост готов в Бросках сразу: барьер должен держать фазу из-за гостя.
	if turn_state["phase"] == PHASE_ROLLS and not _is_player_ready(turn_state, HOST_PLAYER_ID):
		NetSession.set_phase_ready(true)
	if turn_state["turn"] >= TARGET_TURN:
		_turn_reached = true


func _run_mguest() -> void:
	NetSession.turn_state_changed.connect(_mguest_on_turn_state)
	var err: Error = NetSession.join_match(LOCALHOST, SMOKE_PORT, SMOKE_PASSWORD, "смоук-гость")
	if err != OK:
		_fail("create_client: код %d" % err)


func _mguest_on_turn_state(turn_state: Dictionary) -> void:
	if not turn_state.get("active", false):
		return
	var turn: int = turn_state["turn"]
	var phase: int = turn_state["phase"]

	if turn == 1 and phase == PHASE_ROLLS and not _barrier_wait_started:
		# Вход в фазу Бросков: гость молчит BARRIER_HOLD_SEC и проверяет барьер.
		_barrier_wait_started = true
		get_tree().create_timer(BARRIER_HOLD_SEC).timeout.connect(_mguest_check_barrier)
		return
	if turn == 1 and phase > PHASE_ROLLS and not _barrier_checked:
		_fail("фаза Бросков завершилась без готовности гостя (барьер не сработал)")
		return
	if turn >= TARGET_TURN:
		# Этап 3: у каждого участника свой остров с наложенной сеткой.
		if not _islands_ok(MATCH_SLOT_COUNT):
			_fail("у гостя нет островов с сетками для всех участников")
			return
		NetSession.leave()
		_pass_and_quit("SMOKE_MGUEST_OK")


func _mguest_check_barrier() -> void:
	var turn_state: Dictionary = NetSession.turn_state
	if turn_state["turn"] != 1 or turn_state["phase"] != PHASE_ROLLS:
		_fail("барьер не удержал фазу Бросков: %s" % turn_state)
		return
	if not _is_player_ready(turn_state, HOST_PLAYER_ID):
		_fail("готовность хоста не дошла до гостя за %s с" % BARRIER_HOLD_SEC)
		return
	_barrier_checked = true
	NetSession.set_phase_ready(true)


func _is_player_ready(turn_state: Dictionary, player_id: int) -> bool:
	for player in turn_state["players"]:
		if player["id"] == player_id:
			return player["ready"]
	return false


## Острова и сетки всех участников сгенерированы (этап 3).
func _islands_ok(expected: int) -> bool:
	if get_tree().get_nodes_in_group("islands").size() != expected:
		return false
	var grids := get_tree().get_nodes_in_group("island_grids")
	if grids.size() != expected:
		return false
	for grid in grids:
		if grid.buildable_count <= 0 or grid.poi_count <= 0:
			return false
	return true


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

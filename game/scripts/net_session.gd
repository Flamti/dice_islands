extends Node
## Сетевой синглтон (autoload). Host-authoritative лобби по SPEC §12:
## истинный стейт и ядро — у хоста; гости шлют намерения и получают
## подтверждённые изменения стейта.

signal lobby_state_changed(state: Dictionary)
signal join_rejected(reason: String)
signal intent_confirmed(result: Dictionary)
signal log_message(text: String)
signal connection_lost
signal match_started
signal turn_state_changed(turn_state: Dictionary)
## Событие партии из ядра (ход, катастрофа) для ленты событий (SPEC §7).
signal party_event(event: Dictionary)
## Лог боя для проигрывания визуализации (SPEC §9.3).
signal battle_ready(battle: Dictionary)
## Обновление меша острова после деструкции ландшафта (SPEC §11.5).
signal island_updated(update: Dictionary)

const DEFAULT_PORT := 7777
const MIN_SLOTS := 2
const MAX_SLOTS := 16
const DEFAULT_SLOT_COUNT := 4
const DEFAULT_TEAM := 1
const SERVER_PEER_ID := 1 ## фиксированный peer id хоста в High-Level Multiplayer

const REJECT_WRONG_PASSWORD := "wrong_password"
const REJECT_LOBBY_FULL := "lobby_full"

## Разброс производных сидов островов (простое число Кнута для смешивания).
const ISLAND_SEED_STRIDE := 2654435761

## Дефолты таймеров фаз-решений, сек (SPEC §2.3 [баланс]); 0 — без лимита.
const DEFAULT_ROLLS_TIMER_SEC := 60.0
const DEFAULT_DEVELOPMENT_TIMER_SEC := 90.0
const DEFAULT_RAIDS_TIMER_SEC := 45.0
## Период трансляции оставшегося времени таймера гостям (SPEC §12.2).
## ID игрока в партии = индекс слота лобби (см. host_start_match).
const TURN_TIMER_SYNC_SEC := 1.0
## Пауза перед принудительным отключением отклонённого гостя: RPC с причиной
## отказа рассылается при poll в конце кадра — мгновенный кик его обгоняет.
const KICK_DELAY_SEC := 0.5

enum SlotKind { EMPTY, HUMAN, AI }

## Ядро создаётся только у хоста (guest не считает логику вообще).
var _core: Object = null
var is_host := false
## Пароль партии хранится только у хоста и никогда не попадает в стейт лобби.
var _host_password := ""
## Параметры входа гостя: отправляются хосту после установления соединения.
var _pending_join := {}
## Реплицируемый стейт лобби: {"slot_count": int, "slots": Array[Dictionary]}.
var state := {}
## Партия запущена (истина и у хоста, и у гостей после match_started).
var match_active := false
## Последний снапшот хода от ядра (структуру см. DiceCoreServer.get_turn_state).
var turn_state := {}
## Сиды островов участников: player_id -> int. Раздаёт хост при старте партии
## (SPEC §11.1); генерация по сиду детерминирована и выполняется локально.
var island_seeds := {}
var _timer_sync_accum := 0.0


func _ready() -> void:
	multiplayer.peer_connected.connect(_on_peer_connected)
	multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	multiplayer.connected_to_server.connect(_on_connected_to_server)
	multiplayer.connection_failed.connect(_on_connection_failed)
	multiplayer.server_disconnected.connect(_on_server_disconnected)


# --- Публичный API (вызывается из UI) ---------------------------------------


func host_match(port: int, password: String, slot_count: int, host_name: String) -> Error:
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_server(port, MAX_SLOTS)
	if err != OK:
		return err
	multiplayer.multiplayer_peer = peer
	is_host = true
	_core = ClassDB.instantiate(&"DiceCoreServer")
	_host_password = password
	state = _make_empty_state(clampi(slot_count, MIN_SLOTS, MAX_SLOTS))
	_occupy_slot(0, SlotKind.HUMAN, SERVER_PEER_ID, host_name)
	_broadcast_state()
	_broadcast_log("Партия создана (ядро %s)" % core_version())
	return OK


func join_match(address: String, port: int, password: String, player_name: String) -> Error:
	var peer := ENetMultiplayerPeer.new()
	var err := peer.create_client(address, port)
	if err != OK:
		return err
	multiplayer.multiplayer_peer = peer
	is_host = false
	_pending_join = {"password": password, "name": player_name}
	return OK


func leave() -> void:
	if multiplayer.multiplayer_peer != null:
		multiplayer.multiplayer_peer.close()
	multiplayer.multiplayer_peer = null
	is_host = false
	_core = null
	_host_password = ""
	_pending_join = {}
	state = {}
	match_active = false
	turn_state = {}
	island_seeds = {}
	_timer_sync_accum = 0.0


func core_version() -> String:
	return _core.get_core_version() if _core != null else ""


## Отправка намерения хосту. Единственный путь команд в ядро (SPEC §12.1).
func submit_intent(intent: Dictionary) -> void:
	if is_host:
		_host_handle_intent(SERVER_PEER_ID, intent)
	else:
		_rpc_submit_intent.rpc_id(SERVER_PEER_ID, intent)


func set_ready(ready: bool) -> void:
	if is_host:
		_host_set_ready(SERVER_PEER_ID, ready)
	else:
		_rpc_set_ready.rpc_id(SERVER_PEER_ID, ready)


## Барьер фазы-решения (SPEC §12.2): «Готов» внутри партии.
func set_phase_ready(ready: bool) -> void:
	submit_intent({
		"type": "phase_ready",
		"payload": {"ready": "true" if ready else "false"},
	})


# --- Партия ------------------------------------------------------------------


## Старт партии хостом. timers: {"rolls_sec": float, "development_sec": float,
## "raids_sec": float}; 0 — без лимита. Игроки — занятые слоты лобби,
## ID игрока = индекс слота.
func host_start_match(timers: Dictionary) -> bool:
	if not is_host or match_active:
		return false
	# Сиды островов: производные от общего сида партии, по одному на игрока.
	var match_seed := randi()
	island_seeds = {}
	var players: Array = []
	for i in state["slots"].size():
		var slot: Dictionary = state["slots"][i]
		if slot["kind"] == SlotKind.EMPTY:
			continue
		var island_seed := absi(match_seed + i * ISLAND_SEED_STRIDE)
		island_seeds[i] = island_seed
		players.append({
			"id": i,
			"team": slot["team"],
			"is_ai": slot["kind"] == SlotKind.AI,
			"island_seed": island_seed,
		})
	var result: Dictionary = _core.start_match({
		"players": players,
		"timers": timers,
		"match_seed": match_seed,
		"generator_json": _read_data_file("generator.json"),
		"buildings_json": _read_data_file("buildings.json"),
		"dice_json": _read_data_file("dice.json"),
		"disasters_json": _read_data_file("disasters.json"),
		"combat_json": _read_data_file("combat.json"),
		"research_json": _read_data_file("research.json"),
	})
	if not result["ok"]:
		_broadcast_log("Старт партии отклонён ядром: %s" % result["reason"])
		island_seeds = {}
		return false
	match_active = true
	_broadcast_log("Партия началась: игроков %d" % players.size())
	match_started.emit()
	if multiplayer.multiplayer_peer != null:
		_rpc_match_started.rpc(island_seeds)
	_broadcast_turn_state()
	return true


## ID игрока этого клиента (индекс слота) либо -1 до входа в слот.
func my_player_id() -> int:
	return _find_slot_by_peer(multiplayer.get_unique_id())


## Текст файла из data/ в корне репозитория; пустая строка, если файла нет.
func _read_data_file(file_name: String) -> String:
	var path := ProjectSettings.globalize_path("res://").path_join("../data").path_join(file_name)
	if not FileAccess.file_exists(path):
		push_warning("Файл данных не найден: %s" % path)
		return ""
	return FileAccess.get_file_as_string(path)


## Хост тикает ядро каждый кадр; гости получают снапшоты по сети.
func _process(delta: float) -> void:
	if not is_host or not match_active:
		return
	var events: Array = _core.tick(delta)
	for event in events:
		match event["type"]:
			"turn_started", "disaster", "battle", "target_choice", "landscape":
				_broadcast_party_event(event)
	# Логи боёв фазы Боя -> проигрывание визуализации у всех (SPEC §9.3).
	for battle in _core.poll_battles():
		_broadcast_battle(battle)
	# Ре-полигонизация островов после деструкции ландшафта (SPEC §11.5).
	for update in _core.poll_island_updates():
		_broadcast_island_update(update)
	var need_sync := not events.is_empty()
	# Оставшееся время таймера транслируется периодически (SPEC §12.2).
	if float(turn_state.get("timer_remaining_sec", -1.0)) >= 0.0:
		_timer_sync_accum += delta
		if _timer_sync_accum >= TURN_TIMER_SYNC_SEC:
			need_sync = true
	if need_sync:
		_broadcast_turn_state()


# --- Управление лобби (только хост) -----------------------------------------


func host_set_slot_count(slot_count: int) -> void:
	if not is_host:
		return
	var new_count := clampi(slot_count, MIN_SLOTS, MAX_SLOTS)
	var slots: Array = state["slots"]
	while slots.size() > new_count:
		var last: Dictionary = slots[slots.size() - 1]
		if last["kind"] == SlotKind.HUMAN:
			return # нельзя срезать слот с живым игроком
		slots.pop_back()
	while slots.size() < new_count:
		slots.append(_make_empty_slot())
	state["slot_count"] = new_count
	_broadcast_state()


func host_set_ai(slot_index: int, enabled: bool) -> void:
	if not is_host or not _slot_index_valid(slot_index):
		return
	var slot: Dictionary = state["slots"][slot_index]
	if slot["kind"] == SlotKind.HUMAN:
		return
	if enabled:
		_occupy_slot(slot_index, SlotKind.AI, 0, "ИИ %d" % (slot_index + 1))
		state["slots"][slot_index]["ready"] = true # ИИ всегда готов
	else:
		state["slots"][slot_index] = _make_empty_slot()
	_broadcast_state()


func host_set_team(slot_index: int, team: int) -> void:
	if not is_host or not _slot_index_valid(slot_index):
		return
	state["slots"][slot_index]["team"] = maxi(DEFAULT_TEAM, team)
	_broadcast_state()


# --- RPC гость -> хост -------------------------------------------------------


@rpc("any_peer", "call_remote", "reliable")
func _rpc_request_join(password: String, player_name: String) -> void:
	if not is_host:
		return
	var sender := multiplayer.get_remote_sender_id()
	if password != _host_password:
		_broadcast_log("Отклонено подключение (неверный пароль)")
		_rpc_join_rejected.rpc_id(sender, REJECT_WRONG_PASSWORD)
		_kick(sender)
		return
	var slot_index := _find_free_slot()
	if slot_index < 0:
		_rpc_join_rejected.rpc_id(sender, REJECT_LOBBY_FULL)
		_kick(sender)
		return
	_occupy_slot(slot_index, SlotKind.HUMAN, sender, player_name)
	_broadcast_state()
	_broadcast_log("Игрок «%s» занял слот %d" % [player_name, slot_index + 1])


@rpc("any_peer", "call_remote", "reliable")
func _rpc_set_ready(ready: bool) -> void:
	if is_host:
		_host_set_ready(multiplayer.get_remote_sender_id(), ready)


@rpc("any_peer", "call_remote", "reliable")
func _rpc_submit_intent(intent: Dictionary) -> void:
	if is_host:
		_host_handle_intent(multiplayer.get_remote_sender_id(), intent)


# --- RPC хост -> гость -------------------------------------------------------


@rpc("authority", "call_remote", "reliable")
func _rpc_receive_state(new_state: Dictionary) -> void:
	state = new_state
	lobby_state_changed.emit(state)


@rpc("authority", "call_remote", "reliable")
func _rpc_join_rejected(reason: String) -> void:
	join_rejected.emit(reason)
	leave()


@rpc("authority", "call_remote", "reliable")
func _rpc_intent_result(result: Dictionary) -> void:
	intent_confirmed.emit(result)


@rpc("authority", "call_remote", "reliable")
func _rpc_log_line(text: String) -> void:
	log_message.emit(text)


@rpc("authority", "call_remote", "reliable")
func _rpc_match_started(seeds: Dictionary) -> void:
	match_active = true
	island_seeds = seeds
	match_started.emit()


@rpc("authority", "call_remote", "reliable")
func _rpc_receive_turn_state(new_turn_state: Dictionary) -> void:
	turn_state = new_turn_state
	turn_state_changed.emit(turn_state)


@rpc("authority", "call_remote", "reliable")
func _rpc_party_event(event: Dictionary) -> void:
	party_event.emit(event)


@rpc("authority", "call_remote", "reliable")
func _rpc_battle(battle: Dictionary) -> void:
	battle_ready.emit(battle)


@rpc("authority", "call_remote", "reliable")
func _rpc_island_update(update: Dictionary) -> void:
	island_updated.emit(update)


## Барьер мини-решения: выбор цели Тёмной магией (SPEC §8).
func pick_target(pick: int) -> void:
	submit_intent({"type": "target_pick", "payload": {"pick": str(pick)}})


# --- Логика хоста ------------------------------------------------------------


func _host_handle_intent(sender: int, intent: Dictionary) -> void:
	var result: Dictionary = _core.submit_intent(_find_slot_by_peer(sender), intent)
	var verdict: String = "принято" if result["accepted"] else "отклонено (%s)" % result["reason"]
	_broadcast_log("Намерение «%s» от peer %d: %s" % [result["type"], sender, verdict])
	if sender == SERVER_PEER_ID:
		intent_confirmed.emit(result)
	else:
		_rpc_intent_result.rpc_id(sender, result)
	# Принятое намерение меняет стейт хода (готовность и т.п.) — синхронизируем.
	if match_active and result["accepted"]:
		_broadcast_turn_state()


func _broadcast_turn_state() -> void:
	turn_state = _core.get_turn_state()
	_timer_sync_accum = 0.0
	turn_state_changed.emit(turn_state)
	if multiplayer.multiplayer_peer != null:
		_rpc_receive_turn_state.rpc(turn_state)


## Событие ядра (ход, катастрофа) в ленту партии — виден всем (SPEC §7.1).
func _broadcast_party_event(event: Dictionary) -> void:
	party_event.emit(event)
	if multiplayer.multiplayer_peer != null:
		_rpc_party_event.rpc(event)


## Лог боя всем участникам для проигрывания (SPEC §9.3).
func _broadcast_battle(battle: Dictionary) -> void:
	battle_ready.emit(battle)
	if multiplayer.multiplayer_peer != null:
		_rpc_battle.rpc(battle)


## Обновлённый меш острова всем участникам (SPEC §11.5).
func _broadcast_island_update(update: Dictionary) -> void:
	island_updated.emit(update)
	if multiplayer.multiplayer_peer != null:
		_rpc_island_update.rpc(update)


func _host_set_ready(peer_id: int, ready: bool) -> void:
	var slot_index := _find_slot_by_peer(peer_id)
	if slot_index < 0:
		return
	state["slots"][slot_index]["ready"] = ready
	_broadcast_state()


func _broadcast_state() -> void:
	# Сначала RPC, потом локальный сигнал: обработчик сигнала может тут же
	# слать новые RPC (старт партии), и они не должны обгонять стейт лобби.
	if multiplayer.multiplayer_peer != null:
		_rpc_receive_state.rpc(state)
	lobby_state_changed.emit(state)


func _broadcast_log(text: String) -> void:
	log_message.emit(text)
	if multiplayer.multiplayer_peer != null:
		_rpc_log_line.rpc(text)


func _kick(peer_id: int) -> void:
	await get_tree().create_timer(KICK_DELAY_SEC).timeout
	# Гость мог уже отключиться сам, получив причину отказа.
	if multiplayer.multiplayer_peer != null and peer_id in multiplayer.get_peers():
		multiplayer.multiplayer_peer.disconnect_peer(peer_id, false)


# --- Обработчики соединения --------------------------------------------------


func _on_peer_connected(_id: int) -> void:
	# Слот выдаётся только после проверки пароля в _rpc_request_join.
	pass


func _on_peer_disconnected(id: int) -> void:
	if not is_host:
		return
	var slot_index := _find_slot_by_peer(id)
	if slot_index >= 0:
		var player_name: String = state["slots"][slot_index]["name"]
		state["slots"][slot_index] = _make_empty_slot()
		_broadcast_state()
		_broadcast_log("Игрок «%s» покинул партию" % player_name)


func _on_connected_to_server() -> void:
	_rpc_request_join.rpc_id(SERVER_PEER_ID, _pending_join["password"], _pending_join["name"])
	_pending_join = {}


func _on_connection_failed() -> void:
	connection_lost.emit()
	leave()


func _on_server_disconnected() -> void:
	connection_lost.emit()
	leave()


# --- Стейт лобби -------------------------------------------------------------


func _make_empty_state(slot_count: int) -> Dictionary:
	var slots: Array = []
	for i in slot_count:
		slots.append(_make_empty_slot())
	return {"slot_count": slot_count, "slots": slots}


func _make_empty_slot() -> Dictionary:
	return {"kind": SlotKind.EMPTY, "peer_id": 0, "name": "", "team": DEFAULT_TEAM, "ready": false}


func _occupy_slot(slot_index: int, kind: SlotKind, peer_id: int, player_name: String) -> void:
	state["slots"][slot_index] = {
		"kind": kind,
		"peer_id": peer_id,
		"name": player_name,
		"team": DEFAULT_TEAM,
		"ready": false,
	}


func _find_free_slot() -> int:
	for i in state["slots"].size():
		if state["slots"][i]["kind"] == SlotKind.EMPTY:
			return i
	return -1


func _find_slot_by_peer(peer_id: int) -> int:
	if state.is_empty():
		return -1
	for i in state["slots"].size():
		var slot: Dictionary = state["slots"][i]
		if slot["kind"] == SlotKind.HUMAN and slot["peer_id"] == peer_id:
			return i
	return -1


func _slot_index_valid(slot_index: int) -> bool:
	return slot_index >= 0 and slot_index < state["slots"].size()

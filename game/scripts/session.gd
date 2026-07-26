extends PanelContainer
## Менеджер сохранений (SPEC §13): автосейв-кольцо (5) на границе хода +
## ручные слоты хоста. Загрузка поднимает партию из снапшота. Только хост
## сохраняет/загружает; гости продолжают из разосланного снапшота.

const SAVE_DIR := "user://saves"
const AUTOSAVE_COUNT := 5 ## кольцо автосейвов (SPEC §13)
const MANUAL_SLOTS := 3
const LIST_MIN_SIZE := Vector2(248, 200) ## область списка сейвов (скролл)

var _autosave_index := 0
var _status: Label
var _list: VBoxContainer


func _ready() -> void:
	DirAccess.make_dir_recursive_absolute(SAVE_DIR)
	var box := VBoxContainer.new()
	add_child(box)
	var title := Label.new()
	title.text = "Сохранения (хост)"
	box.add_child(title)

	for slot in MANUAL_SLOTS:
		var row := HBoxContainer.new()
		var save_btn := Button.new()
		save_btn.text = "Сохранить в слот %d" % (slot + 1)
		save_btn.pressed.connect(_on_manual_save.bind(slot))
		row.add_child(save_btn)
		box.add_child(row)

	# Список сейвов растёт с числом слотов — прячем в скролл ограниченной высоты.
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = LIST_MIN_SIZE
	box.add_child(scroll)
	_list = VBoxContainer.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_list)
	_status = Label.new()
	box.add_child(_status)

	NetSession.party_event.connect(_on_party_event)
	_refresh_list()


func _on_party_event(event: Dictionary) -> void:
	# Автосейв на границе хода (событие turn_started, только хост).
	if event["type"] == "turn_started" and NetSession.is_host:
		var blob := NetSession.save_snapshot()
		if not blob.is_empty():
			_write("%s/autosave_%d.json" % [SAVE_DIR, _autosave_index], blob)
			_autosave_index = (_autosave_index + 1) % AUTOSAVE_COUNT
			_refresh_list()


func _on_manual_save(slot: int) -> void:
	if not NetSession.is_host:
		_status.text = "Сохранять может только хост"
		return
	var blob := NetSession.save_snapshot()
	if blob.is_empty():
		_status.text = "Нет активной партии"
		return
	_write("%s/manual_%d.json" % [SAVE_DIR, slot], blob)
	_status.text = "Сохранено в слот %d" % (slot + 1)
	_refresh_list()


func _on_load(path: String) -> void:
	if not NetSession.is_host:
		_status.text = "Загружать может только хост"
		return
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		_status.text = "Не удалось открыть сейв"
		return
	var blob := file.get_as_text()
	_status.text = "Загружено" if NetSession.host_load_snapshot(blob) else "Сейв отклонён ядром"


func _write(path: String, blob: String) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file != null:
		file.store_string(blob)


func _refresh_list() -> void:
	for child in _list.get_children():
		child.queue_free()
	var dir := DirAccess.open(SAVE_DIR)
	if dir == null:
		return
	for name in dir.get_files():
		if not name.ends_with(".json"):
			continue
		var row := HBoxContainer.new()
		var label := Label.new()
		label.text = name
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(label)
		var load_btn := Button.new()
		load_btn.text = "Загрузить"
		load_btn.pressed.connect(_on_load.bind("%s/%s" % [SAVE_DIR, name]))
		row.add_child(load_btn)
		_list.add_child(row)

extends PanelContainer
## Лента событий партии (SPEC §7): ходы и катастрофы, видимые всем игрокам.
## Форматирует события из NetSession.party_event в читаемые строки.

const MAX_LINES := 40
## Русские названия ресурсов для сообщений о краже.
const RESOURCE_NAMES := {
	"food": "еды", "wood": "дерева", "stone": "камня", "gold": "золота",
	"hammers": "молотков", "swords": "мечей", "culture": "культуры",
}

var _log: RichTextLabel


func _ready() -> void:
	var box := VBoxContainer.new()
	add_child(box)
	var title := Label.new()
	title.text = "События партии"
	box.add_child(title)
	_log = RichTextLabel.new()
	_log.custom_minimum_size = Vector2(280, 150)
	_log.scroll_following = true
	box.add_child(_log)

	NetSession.party_event.connect(_on_party_event)


func _on_party_event(event: Dictionary) -> void:
	var line := _format(event)
	if line.is_empty():
		return
	_log.add_text(line + "\n")
	# Ограничиваем историю, чтобы лента не росла бесконечно.
	var lines := _log.text.split("\n")
	if lines.size() > MAX_LINES:
		_log.remove_paragraph(0)


func _format(event: Dictionary) -> String:
	var payload: Dictionary = event.get("payload", {})
	match event["type"]:
		"turn_started":
			return "— Ход %s —" % payload.get("turn", "?")
		"disaster":
			return _format_disaster(payload)
		"battle":
			var defender := int(payload.get("defender", -1)) + 1
			return "Бой на острове игрока %d: разрушено зданий %s, защитников осталось %s" % [
				defender, payload.get("destroyed", "0"), payload.get("defenders_left", "?")]
		_:
			return ""


func _format_disaster(payload: Dictionary) -> String:
	var player := int(payload.get("player", -1)) + 1
	match payload.get("disaster", ""):
		"thieves":
			var res: String = RESOURCE_NAMES.get(payload.get("resource", ""), payload.get("resource", "?"))
			return "Воры у игрока %d: украдено %s %s" % [player, payload.get("amount", "?"), res]
		_:
			return "Катастрофа «%s» у игрока %d" % [payload.get("disaster", "?"), player]

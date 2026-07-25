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
		"landscape":
			return "Обрушение края у игрока %d: откололось клеток %s" % [
				int(payload.get("player", -1)) + 1, payload.get("cells", "?")]
		"target_choice":
			return "Игрок %d выбирает цель Тёмной магии" % (int(payload.get("chooser", -1)) + 1)
		_:
			return ""


const DISASTER_NAMES := {
	"thieves": "Воры", "lightning": "Удар молнии", "disease": "Болезнь",
	"sabotage": "Саботаж", "storm": "Шторм", "pirates": "Нашествие пиратов",
	"meteor": "Метеорит", "edge_collapse": "Обрушение края",
}


func _format_disaster(payload: Dictionary) -> String:
	var id: String = payload.get("disaster", "?")
	var name: String = DISASTER_NAMES.get(id, id)
	var victim := int(payload.get("victim", payload.get("player", -1))) + 1
	if id == "thieves":
		var res: String = RESOURCE_NAMES.get(payload.get("resource", ""), payload.get("resource", "?"))
		return "Воры у игрока %d: украдено %s %s" % [victim, payload.get("amount", "?"), res]
	return "«%s» у игрока %d" % [name, victim]

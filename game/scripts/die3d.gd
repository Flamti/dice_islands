extends Control
## 3D-кубик d6 (SPEC §6): куб в собственном SubViewport, 6 граней с текстом
## ресурсов (данные из data/dice.json), поворот-«кувырок» на выпавшую грань, клик
## выделяет кубик для переброса. Чистая визуализация — правила считает ядро.

signal selection_toggled(die: Control)

const VIEW_SIZE := 72
const CUBE_SIZE := 1.4
const CAMERA_SIZE := 3.4 ## орто-высота обзора (с запасом на кувырок)
const LABEL_EPS := 0.02 ## вынос текста за грань, чтобы не тонул в теле
const ROLL_TIME := 0.45
const SELECT_COLOR := Color(1.0, 0.85, 0.2)
const DISABLED_COLOR := Color(0.6, 0.6, 0.6, 0.7)
const BORDER_WIDTH := 3.0

## Цвет тела кубика по типу (визуальная константа, не игровое число).
const DIE_COLORS := {
	"universal": Color(0.85, 0.85, 0.88),
	"food": Color(0.55, 0.78, 0.45),
	"wood": Color(0.55, 0.38, 0.22),
	"stone": Color(0.55, 0.55, 0.6),
	"gold": Color(0.9, 0.76, 0.3),
	"hammers": Color(0.72, 0.5, 0.32),
	"swords": Color(0.8, 0.4, 0.4),
	"culture": Color(0.7, 0.5, 0.85),
}
const DEFAULT_COLOR := Color(0.8, 0.8, 0.8)
## Символы ресурсов на гранях (порядок вывода как в dice_tray).
const GAIN_LABELS := {
	"wood": "Д", "stone": "К", "food": "Е", "gold": "З",
	"hammers": "М", "swords": "Меч", "culture": "Кул",
}
## Позиция и Euler-поворот (град.) текста на каждой из 6 граней:
## индекс 0 (+Z) всегда обращён к камере — на него кладём выпавшую грань.
const FACE_XFORMS := [
	{"pos": Vector3(0, 0, 1), "rot": Vector3(0, 0, 0)},
	{"pos": Vector3(0, 0, -1), "rot": Vector3(0, 180, 0)},
	{"pos": Vector3(1, 0, 0), "rot": Vector3(0, 90, 0)},
	{"pos": Vector3(-1, 0, 0), "rot": Vector3(0, -90, 0)},
	{"pos": Vector3(0, 1, 0), "rot": Vector3(-90, 0, 0)},
	{"pos": Vector3(0, -1, 0), "rot": Vector3(90, 0, 0)},
]

var selected := false
var disabled := false

var _faces: Array = [] ## 6 граней типа (из dice.json)
var _cube: Node3D
var _labels: Array[Label3D] = []
var _body_material: StandardMaterial3D


func _ready() -> void:
	custom_minimum_size = Vector2(VIEW_SIZE, VIEW_SIZE)
	_build_viewport()


func _build_viewport() -> void:
	var container := SubViewportContainer.new()
	container.set_anchors_preset(Control.PRESET_FULL_RECT)
	container.stretch = true
	container.mouse_filter = Control.MOUSE_FILTER_IGNORE # клики ловит сам Die3D
	add_child(container)

	var viewport := SubViewport.new()
	viewport.own_world_3d = true
	viewport.transparent_bg = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.size = Vector2i(VIEW_SIZE, VIEW_SIZE)
	container.add_child(viewport)

	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = CAMERA_SIZE
	# look_at требует нахождения в дереве; задаём трансформ напрямую от позиции.
	camera.look_at_from_position(Vector3(1.5, 1.6, 2.6), Vector3.ZERO, Vector3.UP)
	viewport.add_child(camera)

	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-50, -35, 0)
	viewport.add_child(light)

	_cube = Node3D.new()
	viewport.add_child(_cube)

	var body := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(CUBE_SIZE, CUBE_SIZE, CUBE_SIZE)
	body.mesh = box
	_body_material = StandardMaterial3D.new()
	_body_material.albedo_color = DEFAULT_COLOR
	_body_material.roughness = 0.9
	body.material_override = _body_material
	_cube.add_child(body)

	var half := CUBE_SIZE * 0.5 + LABEL_EPS
	for xform in FACE_XFORMS:
		var label := Label3D.new()
		label.font_size = 48
		label.outline_size = 12
		label.modulate = Color.WHITE
		label.outline_modulate = Color.BLACK
		label.pixel_size = 0.006
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		label.position = (xform["pos"] as Vector3) * half
		label.rotation_degrees = xform["rot"]
		_cube.add_child(label)
		_labels.append(label)


## Задать тип кубика и его 6 граней (массив словарей граней из dice.json).
func setup(die_type: String, faces: Array) -> void:
	_faces = faces
	_body_material.albedo_color = DIE_COLORS.get(die_type, DEFAULT_COLOR)


## Показать выпавшую грань (индекс в _faces) на передней стороне и крутануть куб.
func show_face(rolled_index: int) -> void:
	if _faces.is_empty():
		return
	_labels[0].text = _face_text(_faces[rolled_index])
	var others: Array = []
	for i in _faces.size():
		if i != rolled_index:
			others.append(_faces[i])
	for j in range(1, _labels.size()):
		_labels[j].text = _face_text(others[j - 1]) if j - 1 < others.size() else "—"
	_play_roll()


## Заблокировать выделение (грань с крестом — переброс запрещён, SPEC §6).
func set_disabled(value: bool) -> void:
	disabled = value
	if disabled:
		selected = false
	queue_redraw()


func _play_roll() -> void:
	# Случайный старт-поворот и возврат к тождеству (перед-грань = выпавшая).
	_cube.quaternion = Quaternion.from_euler(
		Vector3(randf() * TAU, randf() * TAU, randf() * TAU))
	var tween := create_tween()
	tween.tween_property(_cube, "quaternion", Quaternion.IDENTITY, ROLL_TIME) \
		.set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)


func _face_text(face: Dictionary) -> String:
	var parts: Array[String] = []
	for key in GAIN_LABELS:
		if int(face.get(key, 0)) > 0:
			parts.append("%d%s" % [int(face[key]), GAIN_LABELS[key]])
	for i in int(face.get("cross", 0)):
		parts.append("✗")
	return "\n".join(parts) if not parts.is_empty() else "—"


func _gui_input(event: InputEvent) -> void:
	if disabled:
		return
	if event is InputEventMouseButton and event.pressed \
			and event.button_index == MOUSE_BUTTON_LEFT:
		selected = not selected
		queue_redraw()
		selection_toggled.emit(self)
		accept_event()


func _draw() -> void:
	var rect := Rect2(Vector2.ZERO, size)
	if disabled:
		draw_rect(rect, DISABLED_COLOR, false, BORDER_WIDTH)
	elif selected:
		draw_rect(rect, SELECT_COLOR, false, BORDER_WIDTH)

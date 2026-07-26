extends Node3D
## Обзорная камера карты (SPEC §14): орбитальный риг над картой, изолированный
## от игровой логики — задел под VR. Риг = пивот (этот Node3D) + Camera3D на
## отдалении; VR-режим позже подменит камеру, сохранив иерархию рига.
## Управление: перетаскивание ПКМ/СКМ — орбита, колесо — зум, WASD — панорама.

const MIN_DISTANCE := 12.0
const MAX_DISTANCE := 220.0
const ZOOM_STEP := 0.9
const ORBIT_SPEED := 0.008
const PAN_SPEED := 0.08
const MIN_PITCH := -1.4 ## почти сверху
const MAX_PITCH := -0.15 ## почти сбоку

var _camera: Camera3D
var _distance := 60.0
var _yaw := 0.0
var _pitch := -0.9
var _dragging := false


func _ready() -> void:
	_camera = Camera3D.new()
	add_child(_camera)
	_apply()


## Навести риг на точку карты (без резкого скачка ориентации).
func focus(target: Vector3) -> void:
	position = target
	_apply()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		match event.button_index:
			MOUSE_BUTTON_WHEEL_UP:
				_distance = clampf(_distance * ZOOM_STEP, MIN_DISTANCE, MAX_DISTANCE)
				_apply()
			MOUSE_BUTTON_WHEEL_DOWN:
				_distance = clampf(_distance / ZOOM_STEP, MIN_DISTANCE, MAX_DISTANCE)
				_apply()
			MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE:
				_dragging = event.pressed
	elif event is InputEventMouseMotion and _dragging:
		_yaw -= event.relative.x * ORBIT_SPEED
		_pitch = clampf(_pitch - event.relative.y * ORBIT_SPEED, MIN_PITCH, MAX_PITCH)
		_apply()


func _process(delta: float) -> void:
	# Панорама пивота по плоскости карты (WASD), относительно направления взгляда.
	var move := Vector3.ZERO
	if Input.is_key_pressed(KEY_W):
		move.z -= 1.0
	if Input.is_key_pressed(KEY_S):
		move.z += 1.0
	if Input.is_key_pressed(KEY_A):
		move.x -= 1.0
	if Input.is_key_pressed(KEY_D):
		move.x += 1.0
	if move != Vector3.ZERO:
		var forward := Vector3(sin(_yaw), 0.0, cos(_yaw))
		var right := Vector3(cos(_yaw), 0.0, -sin(_yaw))
		position += (right * move.x + forward * move.z) * PAN_SPEED * _distance * delta
		_apply()


func _apply() -> void:
	if _camera == null:
		return
	# Камера на сфере вокруг пивота; всегда смотрит в пивот (мировой центр рига).
	var offset := Vector3(
		sin(_yaw) * cos(_pitch),
		-sin(_pitch),
		cos(_yaw) * cos(_pitch)
	) * _distance
	_camera.position = offset
	_camera.look_at(global_position, Vector3.UP)

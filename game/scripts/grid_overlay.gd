extends Node3D
## Оверлей строительной сетки (SPEC §11.3): полупрозрачные квадраты клеток
## поверх острова (зелёный — Buildable, красный — Blocked). Маркеры POI и
## клетки-леса динамичны и рисуются island_view из снапшота хода.
## Только отображение: данные сетки считает ядро.

## Типы клеток — значения enum CellType ядра (core/src/gen/island_gen.hpp).
const CELL_VOID := 0
const CELL_BUILDABLE := 1
const CELL_BLOCKED := 2

const OVERLAY_LIFT := 0.06 ## подъём квадратов над поверхностью против z-fighting
const CELL_INSET := 0.06 ## отступ квадрата от границ клетки
const BUILDABLE_COLOR := Color(0.2, 0.9, 0.3, 0.4)
const BLOCKED_COLOR := Color(0.9, 0.25, 0.2, 0.4)
## Контур клетки — яркие непрозрачные линии, чтобы сетка читалась поверх острова.
const BUILDABLE_LINE := Color(0.6, 1.0, 0.7)
const BLOCKED_LINE := Color(1.0, 0.55, 0.5)
const LINE_LIFT := 0.08 ## контур чуть выше заливки

const GRIDS_GROUP := "island_grids"

var buildable_count := 0
var poi_count := 0


func _ready() -> void:
	add_to_group(GRIDS_GROUP)


## grid — Dictionary из DiceCoreServer.generate_island()["grid"].
func setup(grid: Dictionary) -> void:
	var cells_x: int = grid["cells_x"]
	var cells_z: int = grid["cells_z"]
	var cell: float = grid["cell_size"]
	var origin := Vector3(grid["origin_x"], 0.0, grid["origin_z"])
	var types: PackedInt32Array = grid["types"]
	var heights: PackedFloat32Array = grid["heights"]

	var surface := SurfaceTool.new()
	surface.begin(Mesh.PRIMITIVE_TRIANGLES)
	var lines := SurfaceTool.new()
	lines.begin(Mesh.PRIMITIVE_LINES)
	for cz in cells_z:
		for cx in cells_x:
			var cell_type := types[cz * cells_x + cx]
			if cell_type != CELL_BUILDABLE and cell_type != CELL_BLOCKED:
				continue
			if cell_type == CELL_BUILDABLE:
				buildable_count += 1
			var buildable := cell_type == CELL_BUILDABLE
			var color := BUILDABLE_COLOR if buildable else BLOCKED_COLOR
			var line_color := BUILDABLE_LINE if buildable else BLOCKED_LINE
			var base_y := heights[cz * cells_x + cx]
			var y := base_y + OVERLAY_LIFT
			var x0 := origin.x + cx * cell + CELL_INSET
			var x1 := origin.x + (cx + 1) * cell - CELL_INSET
			var z0 := origin.z + cz * cell + CELL_INSET
			var z1 := origin.z + (cz + 1) * cell - CELL_INSET
			_add_quad(surface, color, Vector3(x0, y, z0), Vector3(x1, y, z0),
					Vector3(x1, y, z1), Vector3(x0, y, z1))
			_add_cell_border(lines, line_color, base_y + LINE_LIFT, x0, x1, z0, z1)

	var mesh_instance := MeshInstance3D.new()
	mesh_instance.mesh = surface.commit()
	mesh_instance.material_override = _overlay_material()
	add_child(mesh_instance)

	var lines_instance := MeshInstance3D.new()
	lines_instance.mesh = lines.commit()
	lines_instance.material_override = _overlay_material()
	add_child(lines_instance)

	poi_count = grid["poi"].size()


func _overlay_material() -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.vertex_color_use_as_albedo = true
	return material


func _add_cell_border(lines: SurfaceTool, color: Color, y: float, x0: float, x1: float, z0: float, z1: float) -> void:
	var corners := [
		Vector3(x0, y, z0), Vector3(x1, y, z0),
		Vector3(x1, y, z1), Vector3(x0, y, z1),
	]
	for i in corners.size():
		lines.set_color(color)
		lines.add_vertex(corners[i])
		lines.set_color(color)
		lines.add_vertex(corners[(i + 1) % corners.size()])


func _add_quad(surface: SurfaceTool, color: Color, a: Vector3, b: Vector3, c: Vector3, d: Vector3) -> void:
	surface.set_color(color)
	surface.add_vertex(a)
	surface.set_color(color)
	surface.add_vertex(c)
	surface.set_color(color)
	surface.add_vertex(b)
	surface.set_color(color)
	surface.add_vertex(a)
	surface.set_color(color)
	surface.add_vertex(d)
	surface.set_color(color)
	surface.add_vertex(c)

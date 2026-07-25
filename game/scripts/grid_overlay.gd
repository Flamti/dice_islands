extends Node3D
## Оверлей строительной сетки (SPEC §11.3): полупрозрачные квадраты клеток
## поверх острова (зелёный — Buildable, красный — Blocked) и маркеры POI.
## Только отображение: данные сетки считает ядро.

## Типы клеток — значения enum CellType ядра (core/src/gen/island_gen.hpp).
const CELL_VOID := 0
const CELL_BUILDABLE := 1
const CELL_BLOCKED := 2

const OVERLAY_LIFT := 0.06 ## подъём квадратов над поверхностью против z-fighting
const CELL_INSET := 0.08 ## отступ квадрата от границ клетки
const BUILDABLE_COLOR := Color(0.2, 0.9, 0.3, 0.28)
const BLOCKED_COLOR := Color(0.9, 0.25, 0.2, 0.28)
const POI_STONE_COLOR := Color(0.55, 0.55, 0.6)
const POI_WOOD_COLOR := Color(0.5, 0.33, 0.15)
const POI_MARKER_SIZE := Vector3(0.9, 0.9, 0.9)

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
	for cz in cells_z:
		for cx in cells_x:
			var cell_type := types[cz * cells_x + cx]
			if cell_type != CELL_BUILDABLE and cell_type != CELL_BLOCKED:
				continue
			if cell_type == CELL_BUILDABLE:
				buildable_count += 1
			var color := BUILDABLE_COLOR if cell_type == CELL_BUILDABLE else BLOCKED_COLOR
			var y := heights[cz * cells_x + cx] + OVERLAY_LIFT
			var x0 := origin.x + cx * cell + CELL_INSET
			var x1 := origin.x + (cx + 1) * cell - CELL_INSET
			var z0 := origin.z + cz * cell + CELL_INSET
			var z1 := origin.z + (cz + 1) * cell - CELL_INSET
			_add_quad(surface, color, Vector3(x0, y, z0), Vector3(x1, y, z0),
					Vector3(x1, y, z1), Vector3(x0, y, z1))

	var mesh_instance := MeshInstance3D.new()
	mesh_instance.mesh = surface.commit()
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.vertex_color_use_as_albedo = true
	mesh_instance.material_override = material
	add_child(mesh_instance)

	for poi in grid["poi"]:
		_add_poi_marker(poi, origin, cell, heights, cells_x)
		poi_count += 1


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


func _add_poi_marker(poi: Dictionary, origin: Vector3, cell: float,
		heights: PackedFloat32Array, cells_x: int) -> void:
	var marker := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = POI_MARKER_SIZE
	marker.mesh = box
	var material := StandardMaterial3D.new()
	material.albedo_color = POI_STONE_COLOR if poi["kind"] == "stone" else POI_WOOD_COLOR
	marker.material_override = material
	var cx: int = poi["cell_x"]
	var cz: int = poi["cell_z"]
	var y := heights[cz * cells_x + cx]
	marker.position = Vector3(
		origin.x + (cx + 0.5) * cell,
		y + POI_MARKER_SIZE.y * 0.5,
		origin.z + (cz + 0.5) * cell
	)
	add_child(marker)

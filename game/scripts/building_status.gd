extends RefCounted
## Отображение статусов зданий (SPEC §5): цвет и подпись заглушки по статусу
## из ядра. Единая точка соответствия BuildingStatus -> визуал, чтобы island_view
## и HUD не расходились. Голод (этап 6) показывается блёклым оранжевым.

## Значения = BuildingStatus в core/src/include/dicecore/core.hpp.
const STATUS_UNDER_CONSTRUCTION := 0
const STATUS_ACTIVE := 1
const STATUS_STARVING := 2
const STATUS_DESTROYED := 3
const STATUS_DISEASED := 4
const STATUS_DISABLED := 5

const COLOR_CONSTRUCTION := Color(0.85, 0.8, 0.3, 0.6)
const COLOR_CASTLE := Color(0.6, 0.42, 0.72)
const COLOR_ACTIVE := Color(0.45, 0.5, 0.72)
const COLOR_STARVING := Color(0.82, 0.5, 0.2)
const COLOR_DESTROYED := Color(0.28, 0.26, 0.25)
const COLOR_OTHER := Color(0.5, 0.32, 0.3)

const LABELS := {
	STATUS_UNDER_CONSTRUCTION: "стройка",
	STATUS_ACTIVE: "",
	STATUS_STARVING: "голод",
	STATUS_DESTROYED: "руины",
	STATUS_DISEASED: "болезнь",
	STATUS_DISABLED: "отключено",
}


## Цвет заглушки по типу и статусу здания. Замок узнаётся отдельно, чтобы
## выделять главное здание, пока оно активно.
static func color_for(building_type: String, status: int) -> Color:
	match status:
		STATUS_UNDER_CONSTRUCTION:
			return COLOR_CONSTRUCTION
		STATUS_STARVING:
			return COLOR_STARVING
		STATUS_DESTROYED:
			return COLOR_DESTROYED
		STATUS_ACTIVE:
			return COLOR_CASTLE if building_type == "castle" else COLOR_ACTIVE
		_:
			return COLOR_OTHER


## Полупрозрачная заглушка (стройка): здание ещё не активно.
static func is_translucent(status: int) -> bool:
	return status == STATUS_UNDER_CONSTRUCTION


## Короткая подпись статуса для HUD; пустая строка для активного.
static func label_for(status: int) -> String:
	return LABELS.get(status, "")

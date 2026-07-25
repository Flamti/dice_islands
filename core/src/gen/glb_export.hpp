#pragma once

#include "gen/island_gen.hpp"

#include <cstdint>
#include <vector>

// Экспорт меша острова в GLB (бинарный glTF 2.0). Без привязки к сцене:
// один меш, атрибуты POSITION/NORMAL/TEXCOORD_0, материал назначает движок.
// Вывод детерминирован: одинаковый меш -> идентичные байты.

namespace dicecore::gen {

std::vector<uint8_t> export_glb(const IslandData &island);

} // namespace dicecore::gen

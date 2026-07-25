#pragma once

#include "math/vec3.hpp"

#include <cstdint>
#include <functional>
#include <vector>

// Marching Cubes (таблицы Пола Бурка, public domain). Полигонизация скалярного
// поля по регулярной сетке с изоуровнем 0: положительная плотность — внутри.
// Вершины индексируются глобальными рёбрами решётки, поэтому меш выходит
// сшитым (общие вершины соседних ячеек) — это нужно тестам замкнутости
// и деструктору ландшафта.

namespace dicecore::gen {

struct Mesh {
    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals; // заполняет вызывающий (градиент поля)
    std::vector<uint32_t> indices; // тройки, CCW при взгляде снаружи
};

// density(ix, iy, iz) — значение поля в узле решётки.
using DensityAt = std::function<float(int, int, int)>;

// Полигонизация поля размером nx*ny*nz узлов; позиции вершин — в координатах
// решётки (узел (i,j,k) → (i,j,k)); масштаб/смещение применяет вызывающий.
Mesh polygonize(int nx, int ny, int nz, const DensityAt &density);

} // namespace dicecore::gen

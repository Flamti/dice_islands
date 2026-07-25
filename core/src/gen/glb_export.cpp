#include "gen/glb_export.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace dicecore::gen {

namespace {

constexpr uint32_t kGlbMagic = 0x46546C67; // "glTF"
constexpr uint32_t kGlbVersion = 2;
constexpr uint32_t kChunkJson = 0x4E4F534A; // "JSON"
constexpr uint32_t kChunkBin = 0x004E4942; // "BIN"

// Компоненты glTF-аксессоров.
constexpr int kComponentFloat = 5126;
constexpr int kComponentUint32 = 5125;
constexpr int kTargetArrayBuffer = 34962;
constexpr int kTargetElementArray = 34963;

void append_u32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void append_bytes(std::vector<uint8_t> &out, const void *data, size_t size) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void pad_to_4(std::vector<uint8_t> &out, uint8_t filler) {
    while (out.size() % 4 != 0) {
        out.push_back(filler);
    }
}

// Детерминированное форматирование float для JSON (без зависимости от локали).
std::string format_float(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    return buffer;
}

} // namespace

std::vector<uint8_t> export_glb(const IslandData &island) {
    const Mesh &mesh = island.mesh;
    const size_t vertex_count = mesh.positions.size();

    // --- Бинарный буфер: POSITION | NORMAL | TEXCOORD_0 | indices ---
    std::vector<uint8_t> bin;
    const size_t pos_offset = bin.size();
    for (const math::Vec3 &p : mesh.positions) {
        append_bytes(bin, &p.x, sizeof(float));
        append_bytes(bin, &p.y, sizeof(float));
        append_bytes(bin, &p.z, sizeof(float));
    }
    const size_t norm_offset = bin.size();
    for (const math::Vec3 &n : mesh.normals) {
        append_bytes(bin, &n.x, sizeof(float));
        append_bytes(bin, &n.y, sizeof(float));
        append_bytes(bin, &n.z, sizeof(float));
    }
    const size_t uv_offset = bin.size();
    append_bytes(bin, island.uvs.data(), island.uvs.size() * sizeof(float));
    const size_t index_offset = bin.size();
    append_bytes(bin, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
    pad_to_4(bin, 0);

    // --- Границы позиций (min/max обязательны для POSITION по спецификации) ---
    math::Vec3 min_p{0.0f, 0.0f, 0.0f};
    math::Vec3 max_p{0.0f, 0.0f, 0.0f};
    if (vertex_count > 0) {
        min_p = max_p = mesh.positions[0];
        for (const math::Vec3 &p : mesh.positions) {
            min_p.x = p.x < min_p.x ? p.x : min_p.x;
            min_p.y = p.y < min_p.y ? p.y : min_p.y;
            min_p.z = p.z < min_p.z ? p.z : min_p.z;
            max_p.x = p.x > max_p.x ? p.x : max_p.x;
            max_p.y = p.y > max_p.y ? p.y : max_p.y;
            max_p.z = p.z > max_p.z ? p.z : max_p.z;
        }
    }

    // --- JSON-чанк ---
    std::string json;
    json += "{\"asset\":{\"version\":\"2.0\",\"generator\":\"dicecore\"},";
    json += "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],";
    json += "\"nodes\":[{\"mesh\":0,\"name\":\"island\"}],";
    json += "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,"
            "\"TEXCOORD_0\":2},\"indices\":3,\"mode\":4}]}],";
    json += "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],";
    json += "\"bufferViews\":[";
    json += "{\"buffer\":0,\"byteOffset\":" + std::to_string(pos_offset) +
            ",\"byteLength\":" + std::to_string(vertex_count * 12) +
            ",\"target\":" + std::to_string(kTargetArrayBuffer) + "},";
    json += "{\"buffer\":0,\"byteOffset\":" + std::to_string(norm_offset) +
            ",\"byteLength\":" + std::to_string(vertex_count * 12) +
            ",\"target\":" + std::to_string(kTargetArrayBuffer) + "},";
    json += "{\"buffer\":0,\"byteOffset\":" + std::to_string(uv_offset) +
            ",\"byteLength\":" + std::to_string(vertex_count * 8) +
            ",\"target\":" + std::to_string(kTargetArrayBuffer) + "},";
    json += "{\"buffer\":0,\"byteOffset\":" + std::to_string(index_offset) +
            ",\"byteLength\":" + std::to_string(mesh.indices.size() * 4) +
            ",\"target\":" + std::to_string(kTargetElementArray) + "}],";
    json += "\"accessors\":[";
    json += "{\"bufferView\":0,\"componentType\":" + std::to_string(kComponentFloat) +
            ",\"count\":" + std::to_string(vertex_count) + ",\"type\":\"VEC3\",";
    json += "\"min\":[" + format_float(min_p.x) + "," + format_float(min_p.y) + "," +
            format_float(min_p.z) + "],";
    json += "\"max\":[" + format_float(max_p.x) + "," + format_float(max_p.y) + "," +
            format_float(max_p.z) + "]},";
    json += "{\"bufferView\":1,\"componentType\":" + std::to_string(kComponentFloat) +
            ",\"count\":" + std::to_string(vertex_count) + ",\"type\":\"VEC3\"},";
    json += "{\"bufferView\":2,\"componentType\":" + std::to_string(kComponentFloat) +
            ",\"count\":" + std::to_string(vertex_count) + ",\"type\":\"VEC2\"},";
    json += "{\"bufferView\":3,\"componentType\":" + std::to_string(kComponentUint32) +
            ",\"count\":" + std::to_string(mesh.indices.size()) + ",\"type\":\"SCALAR\"}]}";

    std::vector<uint8_t> json_chunk(json.begin(), json.end());
    pad_to_4(json_chunk, ' '); // JSON-чанк дополняется пробелами по спецификации

    // --- Сборка GLB ---
    std::vector<uint8_t> glb;
    const uint32_t total_length = static_cast<uint32_t>(12 + 8 + json_chunk.size() + 8 + bin.size());
    append_u32(glb, kGlbMagic);
    append_u32(glb, kGlbVersion);
    append_u32(glb, total_length);
    append_u32(glb, static_cast<uint32_t>(json_chunk.size()));
    append_u32(glb, kChunkJson);
    append_bytes(glb, json_chunk.data(), json_chunk.size());
    append_u32(glb, static_cast<uint32_t>(bin.size()));
    append_u32(glb, kChunkBin);
    append_bytes(glb, bin.data(), bin.size());
    return glb;
}

} // namespace dicecore::gen

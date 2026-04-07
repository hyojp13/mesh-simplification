#include "obj_io.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gfx::obj_io {

namespace {

constexpr std::string_view Trim(std::string_view text, const std::string_view delimiter = " \t\r\n") noexcept {
  text.remove_prefix(std::min(text.find_first_not_of(delimiter), text.size()));
  const auto last = text.find_last_not_of(delimiter);
  text.remove_suffix(last == std::string_view::npos ? text.size() : text.size() - last - 1);
  return text;
}

std::vector<std::string_view> Split(const std::string_view text, const std::string_view delimiter = " \t") {
  std::vector<std::string_view> tokens;
  for (auto index = text.find_first_not_of(delimiter); index < text.size();) {
    const auto next = std::min(text.find_first_of(delimiter, index), text.size());
    tokens.push_back(text.substr(index, next - index));
    index = text.find_first_not_of(delimiter, next);
  }
  return tokens;
}

double ParseDouble(const std::string_view token) {
  const auto text = std::string{token};
  std::size_t parsed_characters = 0;
  try {
    const auto value = std::stod(text, &parsed_characters);
    if (parsed_characters == text.size()) {
      return value;
    }
  } catch (...) {
  }
  throw std::invalid_argument{"Unable to parse floating point value in OBJ"};
}

bool IsDegenerateTriangle(const std::vector<Vec3>& positions, const Index i0, const Index i1, const Index i2) {
  if (i0 == i1 || i1 == i2 || i0 == i2) return true;

  const auto edge01 = positions[i1] - positions[i0];
  const auto edge02 = positions[i2] - positions[i0];
  return Length(Cross(edge01, edge02)) == 0.0;
}

std::array<Index, 3> MakeUndirectedTriangleKey(const Index i0, const Index i1, const Index i2) {
  std::array key{i0, i1, i2};
  std::ranges::sort(key);
  return key;
}

Index ParseFaceIndex(const std::string_view token, const std::size_t vertex_count) {
  const auto separator = token.find('/');
  const auto index_token = token.substr(0, separator);
  if (index_token.empty()) {
    throw std::invalid_argument{"Unsupported OBJ face index"};
  }

  int one_based_index = 0;
  if (const auto [_, error_code] =
          std::from_chars(index_token.data(), index_token.data() + index_token.size(), one_based_index);
      error_code != std::errc{} || one_based_index == 0) {
    throw std::invalid_argument{"Unable to parse face index in OBJ"};
  }

  std::size_t zero_based_index = 0;
  if (one_based_index > 0) {
    zero_based_index = static_cast<std::size_t>(one_based_index - 1);
  } else {
    const auto relative_index = static_cast<std::ptrdiff_t>(vertex_count) + one_based_index;
    if (relative_index < 0) {
      throw std::invalid_argument{"OBJ face index is out of bounds"};
    }
    zero_based_index = static_cast<std::size_t>(relative_index);
  }

  if (zero_based_index >= vertex_count) {
    throw std::invalid_argument{"OBJ face index is out of bounds"};
  }

  return static_cast<Index>(zero_based_index);
}

}  // namespace

Mesh LoadMesh(std::istream& input) {
  std::vector<Vec3> positions;
  std::vector<Index> indices;
  std::set<std::array<Index, 3>> triangles;

  for (std::string line; std::getline(input, line);) {
    const auto trimmed_line = Trim(line);
    if (trimmed_line.empty() || trimmed_line.starts_with('#')) continue;

    const auto tokens = Split(trimmed_line);
    if (tokens.empty()) continue;

    if (tokens[0] == "v") {
      if (tokens.size() != 4) {
        throw std::invalid_argument{"Unsupported vertex format in OBJ"};
      }
      positions.emplace_back(ParseDouble(tokens[1]), ParseDouble(tokens[2]), ParseDouble(tokens[3]));
      continue;
    }

    if (tokens[0] == "f") {
      if (tokens.size() != 4) {
        throw std::invalid_argument{"Only triangular faces are supported"};
      }
      const auto i0 = ParseFaceIndex(tokens[1], positions.size());
      const auto i1 = ParseFaceIndex(tokens[2], positions.size());
      const auto i2 = ParseFaceIndex(tokens[3], positions.size());
      if (!IsDegenerateTriangle(positions, i0, i1, i2) && triangles.emplace(MakeUndirectedTriangleKey(i0, i1, i2)).second) {
        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
      }
    }
  }

  return Mesh{positions, indices};
}

Mesh LoadMesh(const std::filesystem::path& obj_filepath) {
  std::ifstream input{obj_filepath};
  if (!input.good()) {
    throw std::runtime_error{"Failed to open input OBJ file"};
  }
  return LoadMesh(input);
}

void WriteMesh(std::ostream& output, const Mesh& mesh) {
  output << std::setprecision(17);

  for (const auto& position : mesh.positions()) {
    output << "v " << position.x << ' ' << position.y << ' ' << position.z << '\n';
  }

  const auto& indices = mesh.indices();
  for (std::size_t index = 0; index < indices.size(); index += 3) {
    output << "f " << indices[index] + 1 << ' ' << indices[index + 1] + 1 << ' ' << indices[index + 2] + 1 << '\n';
  }
}

void WriteMesh(const std::filesystem::path& obj_filepath, const Mesh& mesh) {
  std::ofstream output{obj_filepath};
  if (!output.good()) {
    throw std::runtime_error{"Failed to open output OBJ file"};
  }
  WriteMesh(output, mesh);
}

}  // namespace gfx::obj_io

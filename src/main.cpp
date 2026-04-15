#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

#include "geometry/mesh_simplifier.h"
#include "obj_io.h"

namespace {

double ParseTargetVertexFraction(std::string_view token) {
  auto uses_percent_units = false;
  if (!token.empty() && token.back() == '%') {
    uses_percent_units = true;
    token.remove_suffix(1);
  }

  const auto text = std::string{token};
  std::size_t parsed_characters = 0;
  double value = 0.0;
  try {
    value = std::stod(text, &parsed_characters);
  } catch (...) {
    throw std::invalid_argument{"Target vertex percentage must be a number such as 50, 50%, or 0.5"};
  }

  if (parsed_characters != text.size()) {
    throw std::invalid_argument{"Target vertex percentage must be a number such as 50, 50%, or 0.5"};
  }

  if (uses_percent_units || value > 1.0) {
    if (value < 0.0 || value > 100.0) {
      throw std::invalid_argument{"Target vertex percentage must be in the range [0, 100]"};
    }
    return value / 100.0;
  }

  if (value < 0.0 || value > 1.0) {
    throw std::invalid_argument{"Target vertex ratio must be in the range [0, 1]"};
  }

  return value;
}

std::size_t ParseThreadCount(const std::string_view token) {
  const auto text = std::string{token};
  std::size_t parsed_characters = 0;
  unsigned long long value = 0;
  try {
    value = std::stoull(text, &parsed_characters);
  } catch (...) {
    throw std::invalid_argument{"Thread count must be a positive integer"};
  }

  if (parsed_characters != text.size() || value == 0) {
    throw std::invalid_argument{"Thread count must be a positive integer"};
  }

  return static_cast<std::size_t>(value);
}

std::filesystem::path ResolvePathForComparison(const std::filesystem::path& path) {
  std::error_code error_code;

  if (const auto canonical_path = std::filesystem::weakly_canonical(path, error_code); !error_code) {
    return canonical_path.lexically_normal();
  }

  error_code.clear();
  if (const auto absolute_path = std::filesystem::absolute(path, error_code); !error_code) {
    return absolute_path.lexically_normal();
  }

  return path.lexically_normal();
}

void ValidateDistinctPaths(const std::filesystem::path& input_path, const std::filesystem::path& output_path) {
  if (ResolvePathForComparison(input_path) == ResolvePathForComparison(output_path)) {
    throw std::invalid_argument{"Input and output paths must be different"};
  }

  std::error_code error_code;
  if (std::filesystem::exists(output_path, error_code) && !error_code) {
    error_code.clear();
    if (std::filesystem::equivalent(input_path, output_path, error_code) && !error_code) {
      throw std::invalid_argument{"Input and output paths resolve to the same file"};
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  // Extract arguments and check for the --simd flag
  bool use_simd = false;
  std::vector<std::string> args;
  
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--simd") {
      use_simd = true;
    } else {
      args.push_back(argv[i]);
    }
  }

  if (args.size() != 3 && args.size() != 4) {
    std::cerr << "Usage: " << argv[0] << " <input.obj> <output.obj> <target_vertex_percent> [threads] [--simd]\n";
    std::cerr << "Example: " << argv[0] << " bunny.obj bunny_simplified.obj 50% 4 --simd\n";
    return EXIT_FAILURE;
  }

  try {
    const std::filesystem::path input_path{args[0]};
    const std::filesystem::path output_path{args[1]};
    ValidateDistinctPaths(input_path, output_path);

    const auto target_vertex_fraction = ParseTargetVertexFraction(args[2]);
    const auto num_threads = args.size() == 4 ? ParseThreadCount(args[3]) : std::size_t{1};
    const auto mesh = gfx::obj_io::LoadMesh(input_path);

    if (mesh.indices().empty()) {
      throw std::invalid_argument{"Input OBJ must contain triangular faces"};
    }

    std::cout << "Starting simplification...\n";
    std::cout << "Threads: " << num_threads << " | SIMD Matrix Math: " << (use_simd ? "ON" : "OFF") << '\n';

    const auto simplification_start = std::chrono::high_resolution_clock::now();
    
    // NOTE: You will need to update your Simplify function signature to accept the `use_simd` boolean!
    const auto simplified_mesh = gfx::mesh::Simplify(mesh, target_vertex_fraction, num_threads, use_simd);
    
    const std::chrono::duration<double> simplification_time =
        std::chrono::high_resolution_clock::now() - simplification_start;

    gfx::obj_io::WriteMesh(output_path, simplified_mesh);

    std::cout << "Simplified mesh written to " << output_path << '\n';
    std::cout << "Input vertices: " << mesh.positions().size() << ", faces: " << mesh.indices().size() / 3 << '\n';
    std::cout << "Output vertices: " << simplified_mesh.positions().size()
              << ", faces: " << simplified_mesh.indices().size() / 3 << '\n';
    std::cout << "Simplification time: " << simplification_time.count() << " s\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
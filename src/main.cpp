#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "geometry/mesh_simplifier.h"
#include "obj_io.h"

namespace {

using gfx::mesh::SimplificationMode;
using gfx::mesh::SimplifyOptions;
using gfx::mesh::SimplifyResult;

struct CommandLineOptions {
  std::filesystem::path input_path;
  std::filesystem::path output_path;
  std::optional<std::filesystem::path> stats_out;
  SimplifyOptions simplify;
};

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

std::size_t ParsePositiveInteger(const std::string_view token, const std::string_view name) {
  const auto text = std::string{token};
  std::size_t parsed_characters = 0;
  unsigned long long value = 0;
  try {
    value = std::stoull(text, &parsed_characters);
  } catch (...) {
    throw std::invalid_argument{std::string{name} + " must be a positive integer"};
  }

  if (parsed_characters != text.size() || value == 0) {
    throw std::invalid_argument{std::string{name} + " must be a positive integer"};
  }

  return static_cast<std::size_t>(value);
}

SimplificationMode ParseMode(std::string_view token) {
  if (token == "sequential") return SimplificationMode::kSequential;
  if (token == "partitioned_batch") return SimplificationMode::kPartitionedBatch;
  throw std::invalid_argument{"Mode must be 'sequential' or 'partitioned_batch'"};
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

bool StartsWithFlag(const std::string_view token) {
  return token.starts_with("--");
}

std::optional<std::string_view> MatchFlag(std::string_view token, std::string_view flag) {
  if (token == flag) return std::string_view{};
  if (token.starts_with(flag) && token.size() > flag.size() && token[flag.size()] == '=') {
    return token.substr(flag.size() + 1);
  }
  return std::nullopt;
}

std::string_view ReadFlagValue(const int argc, char** argv, int& index, const std::string_view flag) {
  const auto token = std::string_view{argv[index]};
  if (const auto inline_value = MatchFlag(token, flag); inline_value.has_value() && !inline_value->empty()) {
    return *inline_value;
  }

  if (token != flag) {
    throw std::invalid_argument{"Unexpected flag syntax: " + std::string{token}};
  }
  if (++index >= argc) {
    throw std::invalid_argument{"Missing value for " + std::string{flag}};
  }
  return argv[index];
}

CommandLineOptions ParseArguments(const int argc, char** argv) {
  if (argc < 4) {
    throw std::invalid_argument{"Usage requested"};
  }

  CommandLineOptions options;
  options.input_path = argv[1];
  options.output_path = argv[2];
  ValidateDistinctPaths(options.input_path, options.output_path);
  options.simplify.target_vertex_fraction = ParseTargetVertexFraction(argv[3]);

  auto index = 4;
  if (index < argc && !StartsWithFlag(argv[index])) {
    options.simplify.num_threads = ParsePositiveInteger(argv[index], "Thread count");
    ++index;
  }

  for (; index < argc; ++index) {
    const auto token = std::string_view{argv[index]};

    if (MatchFlag(token, "--mode").has_value()) {
      options.simplify.mode = ParseMode(ReadFlagValue(argc, argv, index, "--mode"));
      continue;
    }

    if (MatchFlag(token, "--threads").has_value()) {
      options.simplify.num_threads = ParsePositiveInteger(ReadFlagValue(argc, argv, index, "--threads"), "Thread count");
      continue;
    }

    if (MatchFlag(token, "--partitions").has_value()) {
      options.simplify.num_partitions =
          ParsePositiveInteger(ReadFlagValue(argc, argv, index, "--partitions"), "Partition count");
      continue;
    }

    if (MatchFlag(token, "--batch-size").has_value()) {
      options.simplify.batch_size =
          ParsePositiveInteger(ReadFlagValue(argc, argv, index, "--batch-size"), "Batch size");
      continue;
    }

    if (MatchFlag(token, "--repartition-every").has_value()) {
      options.simplify.repartition_every =
          ParsePositiveInteger(ReadFlagValue(argc, argv, index, "--repartition-every"), "Repartition interval");
      continue;
    }

    if (MatchFlag(token, "--stats-out").has_value()) {
      options.stats_out = std::filesystem::path{std::string{ReadFlagValue(argc, argv, index, "--stats-out")}};
      continue;
    }

    throw std::invalid_argument{"Unknown argument: " + std::string{token}};
  }

  return options;
}

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " <input.obj> <output.obj> <target_vertex_percent> [threads]\n";
  std::cerr << "       " << argv0
            << " <input.obj> <output.obj> <target_vertex_percent> [--mode MODE] [--threads N]\n";
  std::cerr << "            [--partitions N] [--batch-size N] [--repartition-every N] [--stats-out PATH]\n";
  std::cerr << "Modes: sequential, partitioned_batch\n";
  std::cerr << "Example: " << argv0
            << " bunny.obj bunny_simplified.obj 25% --mode partitioned_batch --threads 8 --stats-out stats.csv\n";
}

std::string CsvEscape(const std::string_view value) {
  auto escaped = std::string{};
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (const auto character : value) {
    if (character == '"') escaped.push_back('"');
    escaped.push_back(character);
  }
  escaped.push_back('"');
  return escaped;
}

void WriteStatsCsv(const std::filesystem::path& path,
                   const CommandLineOptions& options,
                   const SimplifyResult& result,
                   const double mesh_load_seconds,
                   const double mesh_write_seconds,
                   const double total_seconds) {
  std::error_code error_code;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error_code);
    if (error_code) {
      throw std::runtime_error{"Failed to create stats output directory"};
    }
  }

  std::ofstream output{path};
  if (!output.good()) {
    throw std::runtime_error{"Failed to open stats output file"};
  }

  output << "kind,scope,name,numeric_value,text_value\n";
  output << "metadata,run,mode,," << CsvEscape(std::string{gfx::mesh::ToString(options.simplify.mode)}) << '\n';
  output << "metadata,run,input_path,," << CsvEscape(options.input_path.string()) << '\n';
  output << "metadata,run,output_path,," << CsvEscape(options.output_path.string()) << '\n';
  output << "metadata,run,target_vertex_fraction," << options.simplify.target_vertex_fraction << ",\n";
  output << "metadata,run,threads," << options.simplify.num_threads << ",\n";
  output << "metadata,run,partitions," << options.simplify.num_partitions << ",\n";
  output << "metadata,run,batch_size," << options.simplify.batch_size << ",\n";
  output << "metadata,run,repartition_every," << options.simplify.repartition_every << ",\n";

  output << "phase,total,mesh_load_seconds," << mesh_load_seconds << ",\n";
  for (const auto& phase : result.stats.phase_timings) {
    output << "phase,total," << CsvEscape(phase.name) << ',' << phase.seconds << ",\n";
  }
  output << "phase,total,mesh_write_seconds," << mesh_write_seconds << ",\n";
  output << "phase,total,total_seconds," << total_seconds << ",\n";

  for (const auto& metric : result.stats.summary_metrics) {
    output << "metric,summary," << CsvEscape(metric.name) << ',' << metric.value << ",\n";
  }

  for (const auto& round : result.stats.round_metrics) {
    const auto scope = "round_" + std::to_string(round.round);
    output << "metric," << CsvEscape(scope) << ",interior_edge_fraction," << round.interior_edge_fraction << ",\n";
    output << "metric," << CsvEscape(scope) << ",boundary_edge_fraction," << round.boundary_edge_fraction << ",\n";
    output << "metric," << CsvEscape(scope) << ",partition_load_imbalance," << round.partition_load_imbalance << ",\n";
    output << "metric," << CsvEscape(scope) << ",local_queue_size," << round.local_queue_size << ",\n";
    output << "metric," << CsvEscape(scope) << ",boundary_queue_size," << round.boundary_queue_size << ",\n";
    output << "metric," << CsvEscape(scope) << ",accepted_local_collapses," << round.accepted_local_collapses << ",\n";
    output << "metric," << CsvEscape(scope) << ",accepted_boundary_collapses," << round.accepted_boundary_collapses
           << ",\n";
    output << "metric," << CsvEscape(scope) << ",skipped_boundary_edges," << round.skipped_boundary_edges << ",\n";
    output << "metric," << CsvEscape(scope) << ",rejected_conflicts," << round.rejected_conflicts << ",\n";
    output << "metric," << CsvEscape(scope) << ",repartitioned," << (round.repartitioned ? 1 : 0) << ",\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 1) {
      PrintUsage(argv[0]);
      return EXIT_FAILURE;
    }

    const auto options = ParseArguments(argc, argv);

    const auto total_start = std::chrono::high_resolution_clock::now();
    const auto load_start = total_start;
    const auto mesh = gfx::obj_io::LoadMesh(options.input_path);
    const std::chrono::duration<double> mesh_load_time = std::chrono::high_resolution_clock::now() - load_start;

    if (mesh.indices().empty()) {
      throw std::invalid_argument{"Input OBJ must contain triangular faces"};
    }

    const auto simplification_start = std::chrono::high_resolution_clock::now();
    auto result = gfx::mesh::Simplify(mesh, options.simplify);
    const std::chrono::duration<double> simplification_time =
        std::chrono::high_resolution_clock::now() - simplification_start;

    const auto write_start = std::chrono::high_resolution_clock::now();
    gfx::obj_io::WriteMesh(options.output_path, result.mesh);
    const std::chrono::duration<double> mesh_write_time = std::chrono::high_resolution_clock::now() - write_start;
    const std::chrono::duration<double> total_time = std::chrono::high_resolution_clock::now() - total_start;

    if (options.stats_out.has_value()) {
      WriteStatsCsv(*options.stats_out, options, result, mesh_load_time.count(), mesh_write_time.count(),
                    total_time.count());
    }

    std::cout << "Mode: " << gfx::mesh::ToString(options.simplify.mode) << '\n';
    std::cout << "Simplified mesh written to " << options.output_path << '\n';
    std::cout << "Input vertices: " << mesh.positions().size() << ", faces: " << mesh.indices().size() / 3 << '\n';
    std::cout << "Output vertices: " << result.mesh.positions().size()
              << ", faces: " << result.mesh.indices().size() / 3 << '\n';
    std::cout << "Mesh load time: " << mesh_load_time.count() << " s\n";
    std::cout << "Simplification time: " << simplification_time.count() << " s\n";
    std::cout << "Mesh write time: " << mesh_write_time.count() << " s\n";
    std::cout << "Total time: " << total_time.count() << " s\n";
    if (options.stats_out.has_value()) {
      std::cout << "Stats CSV: " << *options.stats_out << '\n';
    }
    return EXIT_SUCCESS;
  } catch (const std::invalid_argument& error) {
    if (std::string_view{error.what()} == "Usage requested") {
      PrintUsage(argv[0]);
    } else {
      std::cerr << error.what() << '\n';
      PrintUsage(argv[0]);
    }
    return EXIT_FAILURE;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}

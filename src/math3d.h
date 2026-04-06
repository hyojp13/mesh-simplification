#ifndef MESH_SIMPLIFICATION_MATH3D_H_
#define MESH_SIMPLIFICATION_MATH3D_H_

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace gfx {

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;

  constexpr Vec3() = default;
  constexpr explicit Vec3(const double value) : x{value}, y{value}, z{value} {}
  constexpr Vec3(const double x_value, const double y_value, const double z_value)
      : x{x_value}, y{y_value}, z{z_value} {}

  constexpr double& operator[](const std::size_t index) noexcept {
    if (index == 0) return x;
    if (index == 1) return y;
    return z;
  }

  constexpr const double& operator[](const std::size_t index) const noexcept {
    if (index == 0) return x;
    if (index == 1) return y;
    return z;
  }

  constexpr Vec3& operator+=(const Vec3& rhs) noexcept {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
  }

  constexpr Vec3& operator-=(const Vec3& rhs) noexcept {
    x -= rhs.x;
    y -= rhs.y;
    z -= rhs.z;
    return *this;
  }

  constexpr Vec3& operator*=(const double scalar) noexcept {
    x *= scalar;
    y *= scalar;
    z *= scalar;
    return *this;
  }

  constexpr Vec3& operator/=(const double scalar) noexcept {
    x /= scalar;
    y /= scalar;
    z /= scalar;
    return *this;
  }

  friend constexpr bool operator==(const Vec3&, const Vec3&) noexcept = default;
};

inline constexpr Vec3 operator+(Vec3 lhs, const Vec3& rhs) noexcept { return lhs += rhs; }
inline constexpr Vec3 operator-(Vec3 lhs, const Vec3& rhs) noexcept { return lhs -= rhs; }
inline constexpr Vec3 operator-(const Vec3& value) noexcept { return Vec3{-value.x, -value.y, -value.z}; }
inline constexpr Vec3 operator*(Vec3 lhs, const double scalar) noexcept { return lhs *= scalar; }
inline constexpr Vec3 operator*(const double scalar, Vec3 rhs) noexcept { return rhs *= scalar; }
inline constexpr Vec3 operator/(Vec3 lhs, const double scalar) noexcept { return lhs /= scalar; }

struct Vec4 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double w = 0.0;

  constexpr Vec4() = default;
  constexpr explicit Vec4(const double value) : x{value}, y{value}, z{value}, w{value} {}
  constexpr Vec4(const double x_value, const double y_value, const double z_value, const double w_value)
      : x{x_value}, y{y_value}, z{z_value}, w{w_value} {}
  constexpr Vec4(const Vec3& xyz, const double w_value) : x{xyz.x}, y{xyz.y}, z{xyz.z}, w{w_value} {}

  constexpr double& operator[](const std::size_t index) noexcept {
    if (index == 0) return x;
    if (index == 1) return y;
    if (index == 2) return z;
    return w;
  }

  constexpr const double& operator[](const std::size_t index) const noexcept {
    if (index == 0) return x;
    if (index == 1) return y;
    if (index == 2) return z;
    return w;
  }

  [[nodiscard]] constexpr Vec3 xyz() const noexcept { return Vec3{x, y, z}; }
};

struct Mat3 {
  std::array<std::array<double, 3>, 3> values{};

  constexpr double& operator()(const std::size_t row, const std::size_t column) noexcept {
    return values[row][column];
  }

  constexpr const double& operator()(const std::size_t row, const std::size_t column) const noexcept {
    return values[row][column];
  }
};

struct Mat4 {
  std::array<std::array<double, 4>, 4> values{};

  constexpr explicit Mat4(const double diagonal = 0.0) noexcept {
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        values[row][column] = row == column ? diagonal : 0.0;
      }
    }
  }

  constexpr double& operator()(const std::size_t row, const std::size_t column) noexcept {
    return values[row][column];
  }

  constexpr const double& operator()(const std::size_t row, const std::size_t column) const noexcept {
    return values[row][column];
  }

  constexpr Mat4& operator+=(const Mat4& rhs) noexcept {
    for (std::size_t row = 0; row < 4; ++row) {
      for (std::size_t column = 0; column < 4; ++column) {
        values[row][column] += rhs.values[row][column];
      }
    }
    return *this;
  }
};

inline constexpr Mat4 operator+(Mat4 lhs, const Mat4& rhs) noexcept { return lhs += rhs; }

inline constexpr double Dot(const Vec3& lhs, const Vec3& rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline constexpr double Dot(const Vec4& lhs, const Vec4& rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

inline constexpr Vec3 Cross(const Vec3& lhs, const Vec3& rhs) noexcept {
  return Vec3{lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x};
}

inline double Length(const Vec3& value) noexcept { return std::sqrt(Dot(value, value)); }

inline Vec3 Normalize(const Vec3& value) noexcept {
  const auto length = Length(value);
  return length == 0.0 ? Vec3{} : value / length;
}

inline constexpr Mat4 OuterProduct(const Vec4& lhs, const Vec4& rhs) noexcept {
  Mat4 result;
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      result(row, column) = lhs[row] * rhs[column];
    }
  }
  return result;
}

inline constexpr Vec4 operator*(const Mat4& matrix, const Vec4& vector) noexcept {
  return Vec4{
      matrix(0, 0) * vector.x + matrix(0, 1) * vector.y + matrix(0, 2) * vector.z + matrix(0, 3) * vector.w,
      matrix(1, 0) * vector.x + matrix(1, 1) * vector.y + matrix(1, 2) * vector.z + matrix(1, 3) * vector.w,
      matrix(2, 0) * vector.x + matrix(2, 1) * vector.y + matrix(2, 2) * vector.z + matrix(2, 3) * vector.w,
      matrix(3, 0) * vector.x + matrix(3, 1) * vector.y + matrix(3, 2) * vector.z + matrix(3, 3) * vector.w};
}

inline constexpr Mat3 UpperLeft3x3(const Mat4& matrix) noexcept {
  Mat3 result;
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      result(row, column) = matrix(row, column);
    }
  }
  return result;
}

inline constexpr Vec3 RightColumnXYZ(const Mat4& matrix) noexcept {
  return Vec3{matrix(0, 3), matrix(1, 3), matrix(2, 3)};
}

inline constexpr double Determinant(const Mat3& matrix) noexcept {
  return matrix(0, 0) * (matrix(1, 1) * matrix(2, 2) - matrix(1, 2) * matrix(2, 1)) -
         matrix(0, 1) * (matrix(1, 0) * matrix(2, 2) - matrix(1, 2) * matrix(2, 0)) +
         matrix(0, 2) * (matrix(1, 0) * matrix(2, 1) - matrix(1, 1) * matrix(2, 0));
}

inline std::optional<Vec3> SolveLinearSystem(Mat3 matrix, Vec3 rhs, const double epsilon = 1.0e-12) {
  double augmented[3][4] = {
      {matrix(0, 0), matrix(0, 1), matrix(0, 2), rhs.x},
      {matrix(1, 0), matrix(1, 1), matrix(1, 2), rhs.y},
      {matrix(2, 0), matrix(2, 1), matrix(2, 2), rhs.z},
  };

  for (std::size_t pivot = 0; pivot < 3; ++pivot) {
    auto max_row = pivot;
    auto max_value = std::fabs(augmented[pivot][pivot]);
    for (std::size_t row = pivot + 1; row < 3; ++row) {
      const auto value = std::fabs(augmented[row][pivot]);
      if (value > max_value) {
        max_value = value;
        max_row = row;
      }
    }

    if (max_value < epsilon) return std::nullopt;

    if (max_row != pivot) {
      for (std::size_t column = pivot; column < 4; ++column) {
        std::swap(augmented[pivot][column], augmented[max_row][column]);
      }
    }

    for (std::size_t row = pivot + 1; row < 3; ++row) {
      const auto factor = augmented[row][pivot] / augmented[pivot][pivot];
      for (std::size_t column = pivot; column < 4; ++column) {
        augmented[row][column] -= factor * augmented[pivot][column];
      }
    }
  }

  Vec3 solution;
  for (int row = 2; row >= 0; --row) {
    auto value = augmented[row][3];
    for (std::size_t column = static_cast<std::size_t>(row) + 1; column < 3; ++column) {
      value -= augmented[row][column] * solution[column];
    }
    if (std::fabs(augmented[row][row]) < epsilon) return std::nullopt;
    solution[static_cast<std::size_t>(row)] = value / augmented[row][row];
  }

  return solution;
}

inline double QuadricError(const Mat4& quadric, const Vec3& position) noexcept {
  const Vec4 homogeneous_position{position, 1.0};
  return Dot(homogeneous_position, quadric * homogeneous_position);
}

}  // namespace gfx

#endif  // MESH_SIMPLIFICATION_MATH3D_H_

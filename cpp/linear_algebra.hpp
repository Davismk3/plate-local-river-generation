#pragma once

#include <cmath>

template <typename T>
struct Vector3 {
    T x;
    T y;
    T z;

    T vectorMagnitude() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    Vector3<T> vectorNormalize() const {
        T eps = static_cast<T>(1e-12);
        T magnitude = vectorMagnitude();
        if (magnitude < eps) {
            return Vector3<T>{static_cast<T>(0), static_cast<T>(0), static_cast<T>(0)};
        }
        return Vector3<T>{x / magnitude, y / magnitude, z / magnitude};
    }

    Vector3<T> vectorAddVector(Vector3<T> other_vector) const {
        return Vector3<T>{
            x + other_vector.x,
            y + other_vector.y,
            z + other_vector.z
        };
    }

    Vector3<T> vectorSubtractVector(Vector3<T> other_vector) const {
        return Vector3<T>{
            x - other_vector.x,
            y - other_vector.y,
            z - other_vector.z
        };
    }

    Vector3<T> vectorMultiplyVector(Vector3<T> other_vector) const {
        return Vector3<T>{
            x * other_vector.x,
            y * other_vector.y,
            z * other_vector.z
        };
    }

    Vector3<T> vectorDivideVector(Vector3<T> other_vector) const {
        T eps = static_cast<T>(1e-8);
        return Vector3<T>{
            x / (other_vector.x + eps),
            y / (other_vector.y + eps),
            z / (other_vector.z + eps)
        };
    }

    Vector3<T> vectorModulusVector(Vector3<T> other_vector) const {
        T eps = static_cast<T>(1e-8);
        return Vector3<T>{
            x % (other_vector.x + eps),
            y % (other_vector.y + eps),
            z % (other_vector.z + eps)
        };
    }

    Vector3<T> vectorAddScalar(T scalar) const {
        return Vector3<T>{
            x + scalar,
            y + scalar,
            z + scalar
        };
    }

    Vector3<T> vectorSubtractScalar(T scalar) const {
        return Vector3<T>{
            x - scalar,
            y - scalar,
            z - scalar
        };
    }

    Vector3<T> vectorMultiplyScalar(T scalar) const {
        return Vector3<T>{
            x * scalar,
            y * scalar,
            z * scalar
        };
    }

    Vector3<T> vectorDivideScalar(T scalar) const {
        T eps = static_cast<T>(1e-8);
        return Vector3<T>{
            x / (scalar + eps),
            y / (scalar + eps),
            z / (scalar + eps)
        };
    }

    Vector3<T> vectorModulusScalar(T scalar) const {
        T eps = static_cast<T>(1e-8);
        return Vector3<T>{
            x % (scalar + eps),
            y % (scalar + eps),
            z % (scalar + eps)
        };
    }

    // Ensures Lexicographic Ordering For Dictionary Usage
    bool operator<(Vector3<T> other_vector) const {
        if (x != other_vector.x) return x < other_vector.x;
        if (y != other_vector.y) return y < other_vector.y;
        return z < other_vector.z;
    }
};

using Vector3I = Vector3<int>;
using Vector3F = Vector3<float>;
using Vector3D = Vector3<double>;

// Dot Product (My Favorite Math Operation)
template <typename T>
T dotProduct(const Vector3<T>& vector_1, const Vector3<T>& vector_2) {
    return vector_1.x * vector_2.x + vector_1.y * vector_2.y + vector_1.z * vector_2.z;
}

// Cross Product (My 2nd Favorite Math Operation)
template <typename T>
Vector3<T> crossProduct(const Vector3<T>& vector_1, const Vector3<T>& vector_2) {
    return Vector3<T>{
        vector_1.y * vector_2.z - vector_1.z * vector_2.y,
        vector_1.z * vector_2.x - vector_1.x * vector_2.z,
        vector_1.x * vector_2.y - vector_1.y * vector_2.x
    };
}

// Remove n-Facing Components From v | v = v - n(v·n)
template <typename T>
Vector3<T> vectorRemoveComponents(const Vector3<T>& vector_1, const Vector3<T>& vector_2) {
    T dot = dotProduct(vector_1, vector_2);
    return Vector3<T>{
        vector_1.x - vector_2.x * dot,
        vector_1.y - vector_2.y * dot,
        vector_1.z - vector_2.z * dot
    };
}

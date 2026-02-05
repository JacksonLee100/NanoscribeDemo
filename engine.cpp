#include <immintrin.h> // AVX SIMD
#include <mutex>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <thread>
#include <vector>

namespace py = pybind11;

// Global hardware mutexes for stress testing
std::mutex laser_mtx;
std::mutex stage_mtx;

class NanoscribeEngine {
public:
  // --- 1. PERFORMANCE: SIMD Optimized STL Slicing ---
  // Uses Structure of Arrays (SoA) to enable 8-wide float processing
  int process_stl_simd(py::array_t<float> z_min, py::array_t<float> z_max,
                       float layer_z) {
    auto r_min = z_min.unchecked<1>();
    auto r_max = z_max.unchecked<1>();
    size_t size = r_min.shape(0);
    int count = 0;

    // Broadcast layer_z to all slots in the 256-bit register
    __m256 v_layer_z = _mm256_set1_ps(layer_z);

    for (size_t i = 0; i <= size - 8; i += 8) {
      __m256 v_min = _mm256_loadu_ps(&r_min(i));
      __m256 v_max = _mm256_loadu_ps(&r_max(i));

      // SIMD check: (z_min <= layer_z) AND (z_max >= layer_z)
      // This eliminates branching inside the high-frequency loop
      __m256 mask = _mm256_and_ps(_mm256_cmp_ps(v_min, v_layer_z, _CMP_LE_OQ),
                                  _mm256_cmp_ps(v_max, v_layer_z, _CMP_GE_OQ));
      count += __builtin_popcount(_mm256_movemask_ps(mask));
    }
    return count;
  }

  // --- KO EXAMPLE 1: SCALAR (Non-SIMD) ---
  // Standard approach with branching, prone to misprediction and lower
  // throughput
  int process_stl_scalar(py::array_t<float> z_min, py::array_t<float> z_max,
                         float layer_z) {
    auto r_min = z_min.unchecked<1>();
    auto r_max = z_max.unchecked<1>();
    size_t size = r_min.shape(0);
    int count = 0;

    for (size_t i = 0; i < size; ++i) {
      // Branching logic implies 2 potentially mispredicted branches per
      // triangle
      if (r_min(i) <= layer_z && r_max(i) >= layer_z) {
        count++;
      }
    }
    return count;
  }

  // --- KO EXAMPLE 2: DEADLOCK PRONE (Circular Wait) ---
  // Intentionally risky locking order to demonstrate deadlock
  void run_unsafe_stress_test(int iterations, bool reverse_order) {
    py::gil_scoped_release release;
    for (int i = 0; i < iterations; ++i) {
      if (!reverse_order) {
        // Thread A: Lock 1 then Lock 2
        laser_mtx.lock();
        std::this_thread::sleep_for(
            std::chrono::microseconds(1)); // Force context switch
        stage_mtx.lock();
      } else {
        // Thread B: Lock 2 then Lock 1 (Circular Wait!)
        stage_mtx.lock();
        std::this_thread::sleep_for(
            std::chrono::microseconds(1)); // Force context switch
        laser_mtx.lock();
      }

      // Critical section
      std::this_thread::yield();

      // Unlock (Manual implementation is also error-prone regarding exception
      // safety)
      laser_mtx.unlock();
      stage_mtx.unlock();
    }
  }

  // --- 2. RELIABILITY: Deadlock Stress Test ---
  void run_stress_test(int iterations) {
    // Release the Python GIL to allow true hardware-level concurrency
    py::gil_scoped_release release;
    for (int i = 0; i < iterations; ++i) {
      // Breaking the "Circular Wait" Coffman condition using C++17 scoped_lock
      // This atomically acquires both mutexes, preventing deadlocks
      std::scoped_lock lock(laser_mtx, stage_mtx);
      std::this_thread::yield();
    }
  }
};

PYBIND11_MODULE(nano_engine, m) {
  py::class_<NanoscribeEngine>(m, "NanoscribeEngine")
      .def(py::init<>())
      .def("process_stl_simd", &NanoscribeEngine::process_stl_simd)
      .def("process_stl_scalar", &NanoscribeEngine::process_stl_scalar)
      .def("run_stress_test", &NanoscribeEngine::run_stress_test)
      .def("run_unsafe_stress_test", &NanoscribeEngine::run_unsafe_stress_test);
}

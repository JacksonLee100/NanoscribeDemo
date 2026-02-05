# Nanoscribe Demo: Comparison Analysis

This document explains the technical "why" and "how" behind the comparisons implemented in the project. It details exactly how the **OK (Optimized/Safe)** examples achieve their results versus the **KO (Inefficient/Unsafe)** examples.

---

## 1. Performance: SIMD vs. Scalar

### The Goal
Process millions of triangles (checking if they intersect a Z-plane) as fast as possible.

### KO Example: Scalar Implementation (`process_stl_scalar`)
**Mechanism:** "One by One"
This implementation uses a standard C++ `for` loop that processes one triangle at a time.

```cpp
for (size_t i = 0; i < size; ++i) {
    if (r_min(i) <= layer_z && r_max(i) >= layer_z) { // BRANCH
        count++;
    }
}
```

**Why it is slow:**
1.  **Scalar Processing**: It uses Standard registers (xmm or scalar GP registers) dealing with 1 float interaction per instruction.
2.  **Branching (`if`)**: The CPU attempts to predict whether the condition will be true or false to pre-load upcoming instructions.
    - Since our test data is random (`np.random.uniform`), the branch predictor fails ~50% of the time.
    - **Result**: Pipeline flushes. The CPU realizes it guessed wrong, dumps its work, and starts over for that iteration.

### OK Example: SIMD Implementation (`process_stl_simd`)
**Mechanism:** "8 by 8" (AVX2)
This implementation uses **Single Instruction, Multiple Data** (SIMD) instructions to process 8 floats simultaneously.

```cpp
// 1. Load 8 float pairs at once into 256-bit registers
__m256 v_min = _mm256_loadu_ps(&r_min(i));
__m256 v_max = _mm256_loadu_ps(&r_max(i));

// 2. Perform 8 comparisons in parallel
__m256 mask = _mm256_and_ps(
    _mm256_cmp_ps(v_min, v_layer_z, _CMP_LE_OQ), // Compare 8 mins
    _mm256_cmp_ps(v_max, v_layer_z, _CMP_GE_OQ)  // Compare 8 maxs
);

// 3. Count hits without branching
count += __builtin_popcount(_mm256_movemask_ps(mask));
```

**Why it is fast:**
1.  **Parallelism**: One instruction does the work of 8 scalar instructions.
2.  **Branchless Logic**: There are no `if` statements. The result is calculated using bitwise logic (`AND`) and bit counting. The flow of instructions is perfectly predictable, so the CPU pipeline never stalls.

---

## 2. Reliability: Safe vs. Unsafe Locking

### The Goal
Ensure two threads can safely access two shared hardware resources (e.g., `Laser` and `Stage`) without freezing the application (Deadlock).

### KO Example: Unsafe Locking (`run_unsafe_stress_test`)
**Mechanism:** Inconsistent Lock Ordering (Circular Wait)
We forced a specific order of recursive resource acquisition that varies by thread.

- **Thread A**: Locks `Laser` -> *Wait* -> Locks `Stage`
- **Thread B**: Locks `Stage` -> *Wait* -> Locks `Laser`

```cpp
if (!reverse_order) {
    laser_mtx.lock(); // A holds Laser
    // Context switch happens here...
    stage_mtx.lock(); // A waits for Stage (Held by B)
} else {
    stage_mtx.lock(); // B holds Stage
    // Context switch happens here...
    laser_mtx.lock(); // B waits for Laser (Held by A)
}
```

**The Crash (Deadlock):**
1. A grabs Laser.
2. B grabs Stage.
3. A wants Stage, but B has it. A waits.
4. B wants Laser, but A has it. B waits.
5. **Result**: Both threads wait forever. The application hangs.

### OK Example: Safe Locking (`run_stress_test`)
**Mechanism:** `std::scoped_lock` (C++17)
This modern C++ feature uses a deadlock-avoidance algorithm (like `std::lock`'s try-and-back-off or strict address ordering) to ensure multiple mutexes are acquired safely.

```cpp
std::scoped_lock lock(laser_mtx, stage_mtx);
```

**Why it works:**
- It is an **Atomic Transaction** (conceptually).
- Under the hood, it ensures that if it acquires `Laser` but cannot get `Stage`, it will release `Laser` and try again, or it enforces a global sorting order on locks so that both threads effectively try to lock `Laser` then `Stage`, preventing the cycle.
- **RAII**: The lock is automatically released when the variable `lock` goes out of scope (end of loop), ensuring no exceptions leave a mutex locked forever.

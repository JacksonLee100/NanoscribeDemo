import nano_engine
import numpy as np
import threading
import time

engine = nano_engine.NanoscribeEngine()

def run_performance_demo():
    print("--- Phase 1: SIMD Performance ---")
    num_triangles = 1_000_000
    z_mins = np.random.uniform(0, 10, num_triangles).astype(np.float32)
    z_maxs = z_mins + 0.1
    
    start = time.time()
    hits = engine.process_stl_simd(z_mins, z_maxs, 5.0)
    print(f"Processed {num_triangles} triangles in {(time.time()-start)*1000:.2f}ms")
    print(f"Intersections found: {hits}")

def run_stability_demo():
    print("\n--- Phase 2: Deadlock Stress Test ---")
    def hammer():
        engine.run_stress_test(100_000)
    
    threads = [threading.Thread(target=hammer, name=f"H-Thread-{i}") for i in range(4)]
    
    start = time.time()
    for t in threads: t.start()
    for t in threads: t.join()
    print(f"Stability Check Passed: 400k lock cycles in {time.time()-start:.2f}s without Deadlock.")

if __name__ == "__main__":
    print(f"{'='*40}")
    print(f"{'NANOSCRIBE TECH DEMO - BENCHMARK':^40}")
    print(f"{'='*40}\n")

    run_performance_demo()
    
    # Run Comparison
    print("\n--- Phase 1b: KO Comparison (Scalar) ---")
    num_triangles = 1_000_000
    z_mins = np.random.uniform(0, 10, num_triangles).astype(np.float32)
    z_maxs = z_mins + 0.1
    
    start = time.time()
    hits = engine.process_stl_scalar(z_mins, z_maxs, 5.0)
    print(f"Processed {num_triangles} triangles in {(time.time()-start)*1000:.2f}ms (Scalar)")
    print(f"Intersections found: {hits}")

    run_stability_demo()

    # Run Deadlock Demonstration
    print("\n--- Phase 2b: KO Comparison (Deadlock) ---")
    print("Attempting to run unsafe locking with circular wait...")
    
    def unsafe_hammer(reverse):
        try:
            engine.run_unsafe_stress_test(1000, reverse)
        except Exception as e:
            print(f"Error: {e}")

    t1 = threading.Thread(target=unsafe_hammer, args=(False,), name="Unsafe-A")
    t2 = threading.Thread(target=unsafe_hammer, args=(True,), name="Unsafe-B")
    
    start = time.time()
    t1.start()
    t2.start()
    
    t1.join(timeout=2.0)
    t2.join(timeout=2.0)
    
    if t1.is_alive() or t2.is_alive():
        print("!!! DEADLOCK DETECTED !!!")
        print("Threads failed to complete within 2 seconds.")
        print("This demonstrates why std::scoped_lock / lock hierarchies are critical.")
        # We can't easily kill C++ threads from Python, so the process might hang on exit
        # but for the demo output, we've proven the point.
        import os
        os._exit(1) # Force exit to avoid hanging verify script
    else:
        print("Unsafe test ran without deadlock (lucky timing or insufficient load).")

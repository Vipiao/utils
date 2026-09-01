# utils

Shared C++20 library for the Orbital Industries projects: math, geometry and
general-purpose helpers with no rendering or windowing dependencies. Consumers
add it with `add_subdirectory` and link `Utils::utils`; glm is vendored under
`external/` so every consumer compiles against the same version.

## Tests

```sh
./run_tests.sh
```

Configures a Debug build under `build/tests`, builds it, and runs the suite.
Re-running only rebuilds and re-runs. Equivalently, by hand:

```sh
cmake -B build/tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

Tests are registered with CTest, which ships with CMake, so the suite pulls in
no third-party dependency. Each test decides its own verdict and reports it
through the exit code — nothing needs a human to read the output. The printed
statistics are context for a failure, not the verdict.

Tests are built only when utils is the top-level project, so a consumer that
adds this directory gets the library and nothing else. Set `UTILS_BUILD_TESTS`
to override.

### dekker_arithmetic

`DekkerArithmetic<float>` represents a value as two floats — a main part and a
correction — to carry roughly twice a float's mantissa. The test measures it
against double precision over 30736 sample pairs, drawn from a seeded uniform
spread plus the full cross product of the awkward values (zero, denormals,
extremes, infinities, NaN), and asserts:

- **Accuracy.** For add, multiply, divide and square root, the pair is never
  further from the double reference than a plain float is, allowing for the
  finest the pair can resolve. In practice it lands about seven orders of
  magnitude closer.
- **Exactness.** The product of two floats needs 48 mantissa bits and the pair
  holds them, so that product must be exact, not merely close.
- **Round trip.** A double survives the split and rejoin to within 2^-46
  relative.
- **Domain.** NaN propagates rather than resolving to a number, and an operand
  outside float's finite range stays visibly outside instead of collapsing into
  a plausible finite result.

That last one is the boundary of what the type supports: an infinity has no
meaningful correction term, and the compensation reduces to `inf - inf`, so
non-finite operands are excluded from the accuracy comparison and checked
separately for staying loud.

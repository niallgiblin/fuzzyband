# C++ Fundamentals — Learn Through Metal Accompaniment

This is a guided lesson for developers coming from **Python**, **Java**, or **JavaScript**. Every concept below appears in this repo. When you see a keyword or pattern you do not recognize, search for it in `src/` — the examples here are real, not toy snippets.

**Suggested path:** read [CPP_LIBRARIES.md](CPP_LIBRARIES.md) first for *what* libraries exist, then use this doc for *how C++ works* inside them.

---

## Table of contents

1. [How C++ is different at a glance](#1-how-c-is-different-at-a-glance)
2. [The build model: compile and link](#2-the-build-model-compile-and-link)
3. [Headers (`.h`) vs implementation (`.cpp`)](#3-headers-h-vs-implementation-cpp)
4. [Types, variables, and literals](#4-types-variables-and-literals)
5. [Functions and methods](#5-functions-and-methods)
6. [Classes, structs, and objects](#6-classes-structs-and-objects)
7. [Inheritance, interfaces, and polymorphism](#7-inheritance-interfaces-and-polymorphism)
8. [const — immutability that the compiler enforces](#8-const--immutability-that-the-compiler-enforces)
9. [References and pointers](#9-references-and-pointers)
10. [Memory: stack, heap, ownership, RAII](#10-memory-stack-heap-ownership-raii)
11. [Standard containers and arrays](#11-standard-containers-and-arrays)
12. [Templates and generics](#12-templates-and-generics)
13. [Enums and state machines](#13-enums-and-state-machines)
14. [Namespaces and organization](#14-namespaces-and-organization)
15. [Preprocessor and conditional compilation](#15-preprocessor-and-conditional-compilation)
16. [Concurrency: threads, atomics, mutexes](#16-concurrency-threads-atomics-mutexes)
17. [Real-time audio methodology in this project](#17-real-time-audio-methodology-in-this-project)
18. [Testing in C++ (Catch2)](#18-testing-in-c-catch2)
19. [Keyword cheat sheet](#19-keyword-cheat-sheet)
20. [Where to read next in this repo](#20-where-to-read-next-in-this-repo)

---

## 1. How C++ is different at a glance

| Idea | Python | Java | JavaScript | C++ (this project) |
|------|--------|------|------------|-------------------|
| Execution | Interpreted / bytecode VM | JVM bytecode | JIT in browser/Node | **Compiled to native machine code** |
| Memory | Garbage collected | Garbage collected | Garbage collected | **You own lifetimes** — but smart pointers + stack objects handle most of it |
| Typing | Dynamic (optional hints) | Static, reference types | Dynamic | **Static, value-by-default** — types checked at compile time |
| `null` | `None` | `null` | `null` / `undefined` | **`nullptr`** — typed null pointer |
| Classes | Yes | Yes | Prototypes / classes | Yes, plus **structs**, **no `interface` keyword** (use abstract base classes) |
| Threads | GIL limits CPU parallelism | `Thread`, `synchronized` | Single-threaded event loop (+ Workers) | **`std::thread`**, **`std::atomic`**, explicit rules |
| Package/import | `import foo` | `import foo.Bar` | `import` / `require` | **`#include "file.h"`** — textually pasted at compile time |

**Big mental shift:** In Python/JS you often think *“create an object, use it, forget it.”* In C++ you think *“where does this live, who owns it, and when is it destroyed?”* This project pushes that further on the **audio thread**, where allocation and locking are forbidden during playback.

**What feels familiar:** Classes with methods, `if`/`for`/`while`, exceptions (rarely used on audio thread), unit tests with assertions, dependency injection via interfaces (`IInference` ≈ Java interface).

---

## 2. The build model: compile and link

### Python / JS mental model
You run `python script.py` or `node app.js`. Source is read at runtime.

### Java mental model
`.java` → `.class` bytecode → JVM loads classes on demand.

### C++ mental model
Each `.cpp` file is **compiled separately** into an object file (`.o`). The **linker** combines object files + libraries into one executable or plugin (`.vst3`, `.component`).

```
src/AccompanimentProcessor.cpp  ──compile──▶  AccompanimentProcessor.o
src/analysis/OnsetDetector.cpp  ──compile──▶  OnsetDetector.o
         ...                                           │
                                                       ▼
                                              linker + JUCE + moodycamel
                                                       │
                                                       ▼
                                    Metal Accompaniment.vst3
```

**CMake** (`CMakeLists.txt`) is the build recipe: which files to compile, which libraries to link, compiler flags (`-O3`, C++20).

**Similar to:** Java’s `javac` + jar packaging, or webpack bundling — except C++ linking happens before you run anything.

---

## 3. Headers (`.h`) vs implementation (`.cpp`)

C++ splits **declarations** (what exists) from **definitions** (how it works).

| File | Role | Python/JS analogy |
|------|------|-------------------|
| `OnsetDetector.h` | Class shape: public methods, private members | A `.pyi` stub or TypeScript `.d.ts` |
| `OnsetDetector.cpp` | Method bodies | The actual `.py` / `.js` implementation |

### `#pragma once`
At the top of every header — prevents the same file being included twice (like a guard against duplicate imports).

### Include order in this project
1. Standard library (`<vector>`, `<atomic>`)
2. Third-party / JUCE (`<juce_dsp/juce_dsp.h>`)
3. Local project headers (`"analysis/OnsetDetector.h"`)

### Why split at all?
When `AccompanimentProcessor.cpp` includes ten analysis headers, the compiler only needs to know *names and signatures* — not every line of FFT math. Changing one `.cpp` recompiles only that file, not the whole plugin.

**Java comparison:** Like putting interfaces in one file and implementation in another — except in C++ the compiler literally needs the header to know object sizes and method signatures.

---

## 4. Types, variables, and literals

### Fixed-width integers
```cpp
int64_t hostSampleTime = 0;   // signed 64-bit — sample counter
uint8_t note = 36;            // unsigned 8-bit — MIDI note number
uint64_t getOnnxErrorCount(); // error counters
```

| C++ | Approx. Python | Approx. Java |
|-----|----------------|--------------|
| `int` | `int` | `int` |
| `float` | `float` | `float` |
| `double` | `float` (double precision) | `double` |
| `bool` | `bool` | `boolean` |
| `size_t` | `len()` index type | `size_t`-like for lengths |

**No `var` / untyped variables.** Types are declared explicitly (or deduced with `auto` — see below).

### `auto` — local type inference
```cpp
auto onnx = std::make_unique<OnnxInference>();  // compiler deduces unique_ptr<OnnxInference>
```
Like Python’s inferred assignment, but **only for the variable’s type at declaration** — still statically typed forever after.

### Member initialization
```cpp
struct FeatureVector {
    float bpm = 120.0f;                    // default at construction
    StructureState state = StructureState::SILENT;
};
```
Similar to Java field initializers or Python dataclass defaults.

### `float` vs `double`
Audio samples here are mostly **`float`** (`120.0f` — the `f` suffix means float literal). Sample rate and timing often use **`double`** for precision.

### Trailing underscores on members
`PitchEstimator` uses `sampleRate_`, `ringWrite_` — a naming convention meaning “private member” (not enforced by the language; Java might use `this.sampleRate`).

---

## 5. Functions and methods

### Declaration vs definition
Header (`RuleBasedInference.h`):
```cpp
int selectPattern(const FeatureVector& features, int excludeIndex = -1) override;
```

Implementation (`RuleBasedInference.cpp`):
```cpp
int RuleBasedInference::selectPattern(const FeatureVector& f, int excludeIndex)
{
    int result = std::clamp(PatternRules::rulePatternForState(f), 0, 6);
    return PatternRules::applyExclusion(result, excludeIndex);
}
```

**`ClassName::methodName`** — defines a method outside the class body. Like Java’s same syntax.

### Default arguments
`excludeIndex = -1` in the header — callers can omit it. Python does this with `def f(x, exclude=-1)`. Java has no default args (use overloads).

### `(void)parameter` — silence unused warnings
```cpp
(void)sampleRate;  // parameter required by interface but unused in this impl
```
Python would use `_sampleRate` or `# noqa`.

### `noexcept` — “this won’t throw”
```cpp
float getDisplayBpm() const noexcept { return displayBpm.load(...); }
```
Promises no exceptions escape. Critical on real-time paths. Java has no direct equivalent (`throws` is the opposite — declares what *can* throw).

### Free functions vs methods
```cpp
namespace {
float foldTrackerAliasTowardOnset(float trackerBpm, float onsetBpm) noexcept { ... }
}
```
Module-level function in an anonymous namespace — **private to this `.cpp` file**. Like a Python function not exported from a module.

---

## 6. Classes, structs, and objects

### `class` vs `struct`
In C++ they are almost identical. Convention in this repo:

- **`struct`** — plain data bags (`FeatureVector`, `MidiEvent`, `BassStepFrame`)
- **`class`** — behavior + state (`OnsetDetector`, `AccompanimentProcessor`)

Both can have methods, constructors, and access control.

### Access control
```cpp
class StructureTagger {
public:
    void prepare(double sampleRate);
    StructureState update(...);
private:
    StructureState currentState = StructureState::SILENT;
};
```

| C++ | Java |
|-----|------|
| `public:` | `public` |
| `private:` | `private` |
| (no `protected` in most of this repo) | `protected` |

### Constructors and initializer lists
```cpp
AccompanimentProcessor::AccompanimentProcessor()
    : AudioProcessor(BusesProperties()...)
    , apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
    , inference(makeInference())
{
    inferenceThread = std::thread([this] { inferenceLoop(); });
}
```

The **`:` list** runs *before* the constructor body — initializes base classes and members. Order matters for dependencies.

**Java comparison:** Like calling `super(...)` then assigning fields — but C++ *requires* explicit initialization for bases and const/reference members.

### Destructors
```cpp
~AccompanimentProcessor() override {
    inferenceRunning.store(false, ...);
    if (inferenceThread.joinable())
        inferenceThread.join();
}
```

**`~ClassName()`** runs when the object is destroyed. This is where you **join threads**, **release resources**. Python/JS rely on GC; Java has `finalize()` (deprecated) — C++ destructors are **deterministic** and central to RAII.

### `final` — no further subclassing
```cpp
class AccompanimentProcessor final : public juce::AudioProcessor
```
Like Java’s `final class`.

### Copy deletion
```cpp
JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentProcessor)
```
Macro that deletes copy constructor/assignment — this processor must not be duplicated (owns a thread). Java objects are always handle-copied; C++ defaults to **copy-by-value** unless you forbid it.

---

## 7. Inheritance, interfaces, and polymorphism

### Abstract base class = Java interface (with optional default impl)
```cpp
class IInference {
public:
    virtual ~IInference() = default;
    virtual void prepare(double sampleRate) = 0;           // pure virtual — must implement
    virtual int selectPattern(const FeatureVector& features, int excludeIndex = -1) = 0;
    virtual std::string getName() const = 0;
};
```

| C++ | Java |
|-----|------|
| `virtual ... = 0` | `abstract` method |
| `virtual ~IInference() = default` | no direct equivalent — virtual destructor for polymorphic delete |
| `class RuleBasedInference final : public IInference` | `class RuleBasedInference implements IInference` |

**Python:** `IInference` is like an ABC (`abc.ABC` + `@abstractmethod`).

### `override` — explicit virtual override
```cpp
void prepare(double sampleRate) override;
```
Compiler error if the base class didn’t have a matching virtual method. Catches typos Java would catch at compile time too.

### Runtime polymorphism via pointer
```cpp
std::unique_ptr<IInference> inference;
inference = std::make_unique<RuleBasedInference>();
inference->selectPattern(fv);  // dispatches to RuleBasedInference or OnnxInference
```

**`->`** is “dereference pointer, then call method” — like Java’s `.` on a reference, but the object is behind a pointer.

Factory in `AccompanimentProcessor.cpp`:
```cpp
std::unique_ptr<IInference> makeInference() {
#if defined(MA_ENABLE_ONNX)
    auto onnx = std::make_unique<OnnxInference>();
    if (onnx->tryLoadModel())
        return onnx;
#endif
    return std::make_unique<RuleBasedInference>();
}
```

**Strategy pattern** — same as injecting an interface implementation in Java/Spring or passing a callable in Python.

### `dynamic_cast` — safe downcast
```cpp
if (auto* onnx = dynamic_cast<OnnxInference*>(inference.get()))
    total += onnx->getLoadErrorCount();
```
Like Java’s `instanceof` + cast, but explicit. Used sparingly for test/introspection hooks.

---

## 8. `const` — immutability that the compiler enforces

`const` appears everywhere. It does **not** mean “constant at compile time” — it means **“this binding won’t change through this access path.”**

```cpp
const float rms = energyAnalyser.getRmsEnergy();     // won't reassign rms
const FeatureVector& features                       // won't modify the FeatureVector through this ref
const MidiPattern& getPattern(int index) const;     // method promises not to mutate *this
void processBlock(..., juce::MidiBuffer& midi);     // midi is mutable — we write MIDI into it
```

| Pattern | Meaning |
|---------|---------|
| `const float x` | Local variable not reassigned |
| `const FeatureVector& f` | Reference to caller’s struct — no copy, read-only through `f` |
| `float getX() const` | Method does not modify object’s members |
| `const int*` | Pointer to const int (value can’t change via pointer) |
| `int* const` | Const pointer (pointer itself fixed) — rare in this repo |

**Python analogy:** No exact match — closest is type hints with `Final` or passing tuples for read-only. **Java:** `final` local variables and `final` method parameters (references still mutable unless the object is immutable).

**Why it matters here:** Passing `const FeatureVector&` into inference avoids copying ~10 floats every queue drain while documenting “inference must not mutate the feature snapshot.”

---

## 9. References and pointers

### Reference `&` — alias to existing object
```cpp
void setPatternLibrary(const MidiPatternLibrary* lib);  // pointer — nullable
juce::AudioBuffer<float>& buffer                          // reference — must exist
const FeatureVector& features                             // const reference
```

| Feature | Reference `T&` | Pointer `T*` |
|---------|----------------|--------------|
| Can be null | No | Yes (`nullptr`) |
| Reassign to point elsewhere | No (binding fixed) | Yes |
| Syntax for member access | `.` | `->` or `(*p).` |
| This project | Parameters, return types | Optional deps, C callbacks, audio buffers |

### Raw pointers for audio buffers
```cpp
float* in = buffer.getWritePointer(0);
if (in == nullptr)
    return;
onsetDetector.process(in, numSamples);
```

`process(const float* audioData, int numSamples)` — **pointer + length** is the C/C++ idiom for “array view.”

**Python:** like `memoryview` or NumPy array — pointer to first element, count separate.

**Java:** like `float[]` but without bounds checking — **no length stored in the pointer**.

### C callback with `void*`
```cpp
void maBeatFluxSink(void* user, float flux, int64_t totalSamples) noexcept {
    if (user != nullptr)
        static_cast<BeatTracker*>(user)->pushFluxSample(flux, totalSamples);
}
```
Classic C pattern: opaque user pointer cast back to C++ object. Java would use a functional interface; Python a callable.

---

## 10. Memory: stack, heap, ownership, RAII

### Value semantics by default
```cpp
OnsetDetector onsetDetector;      // embedded member — lives with AccompanimentProcessor
StructureTagger structureTagger;  // same — not pointers
```

When `AccompanimentProcessor` is destroyed, **`onsetDetector` is destroyed automatically** — nested ownership, no GC scan.

**Java:** fields are references to heap objects. **Python:** everything is a reference. **C++:** small subsystems are often **embedded values** inside the parent.

### Heap with explicit ownership: `std::unique_ptr`
```cpp
std::unique_ptr<IInference> inference;
std::unique_ptr<juce::dsp::FFT> fft;
```

| Concept | Meaning |
|---------|---------|
| `unique_ptr<T>` | Exactly **one owner**; when owner dies, `T` is deleted |
| `std::make_unique<T>(...)` | Allocate on heap, return unique_ptr |
| `inference.get()` | Raw pointer without transferring ownership |
| Move semantics | `return onnx;` transfers ownership out of function |

**Python:** like sole ownership — no shared ref counting. **Java:** closest is “only this field references the object” but GC still decides when to free.

**Why not raw `new`/`delete`?** This codebase avoids manual `delete`. Smart pointers make leaks and double-frees harder.

### RAII — Resource Acquisition Is Initialization
```cpp
void processBlock(...) {
    juce::ScopedNoDenormals noDenormals;  // constructed at entry
    ...
}  // destroyed here — restores CPU FP mode
```

**Pattern:** Tie resource lifetime to scope. Constructor acquires; destructor releases.

**Examples in this project:**
- Destructor joins inference thread
- `std::lock_guard<std::mutex>` releases lock at scope exit
- Stack `MidiPattern p` in builder functions

**Java try-with-resources** and **Python `with`** are the same idea — C++ uses destructors universally.

### Pimpl idiom (`OnnxInference`)
Header exposes `struct Impl;` — implementation details (ONNX session) hidden in `.cpp`. Reduces compile-time coupling. Like Java interface hiding package-private classes.

---

## 11. Standard containers and arrays

### `std::vector` — dynamic array
```cpp
std::vector<MidiEvent> drumEvents;
std::vector<float> buf(static_cast<size_t>(chunk), 0.0f);
```

Like Python `list` or Java `ArrayList`, but **typed** and ** contiguous memory**. `push_back` may allocate — **avoid on audio thread** after `prepare()`.

### `std::array` — fixed size on stack
```cpp
std::array<float, 16> pitchOffsets{};
```
Size is part of the type. `{}` zero-initializes. Used for 16-step bass grids — **no heap**, predictable size.

### `std::string`
```cpp
std::string activeInferenceName = "None";
```
Mutable UTF-8 string. Like Java `String` but mutable unless `const std::string&`. Not used on hottest audio paths.

### Iterator-style loops
```cpp
for (int i = 0; i < numSamples; ++i) {
    const float s = in[i];
    ...
}
```

**`++i` vs `i++`:** prefix increment preferred for iterators/integers (minor style/perf convention).

Range-for also appears in tests and offline code:
```cpp
for (const auto& ev : list) { ... }
```

---

## 12. Templates and generics

C++ generics are **compile-time**. The compiler generates separate code for each type used.

### Standard library templates
```cpp
std::unique_ptr<juce::dsp::FFT> fft;
moodycamel::ReaderWriterQueue<FeatureVector> featureQueue{ 4096 };
juce::AudioBuffer<float>& buffer
```

**Java:** `ArrayList<FeatureVector>`, `Queue<T>`. **Python:** generics exist only in type hints at runtime.

### JUCE template example
`juce::AudioBuffer<float>` — buffer of `float` samples. Could be `double` with a different template argument.

### `constexpr` — compile-time constants
```cpp
static constexpr float kSilentRms = 0.012f;
static constexpr int kPatternCount = 7;
```

Values known at compile time. **`const`** alone might be runtime; **`constexpr`** forces compile-time where possible.

### `inline` free functions in headers
`pattern_rules.h` uses `inline int rulePatternForState(...)` — header-only shared logic without linker duplicate-symbol errors.

---

## 13. Enums and state machines

### `enum class` — scoped enumeration
```cpp
enum class StructureState {
    SILENT,
    SOFT,
    LOUD
};
```

Usage:
```cpp
if (st == StructureState::SILENT)
    ...
f.state = StructureState::SOFT;
```

| C++ `enum class` | Java | Python |
|------------------|------|--------|
| `StructureState::SILENT` | `StructureState.SILENT` | `StructureState.SILENT` (Enum) |
| Strongly typed — no silent int conversion | enum types | IntEnum |

**Old C `enum`** (not used here) implicitly converts to `int` — avoid in new code.

`StructureTagger` is a **state machine** with hysteresis — same pattern you’d write in any language; C++ makes state a typed enum instead of string/int magic values.

---

## 14. Namespaces and organization

### Named namespace
```cpp
namespace PatternRules {
    inline float adjustedBpm(const FeatureVector& f) noexcept { ... }
}
// call: PatternRules::rulePatternForState(f)
```

Like Java packages or Python packages — groups free functions without a class.

### Anonymous namespace
```cpp
namespace {
    MidiPattern buildVerseSlow() { ... }
}
```

File-local linkage — symbols not visible to other `.cpp` files. Preferred over `static` functions in modern C++.

---

## 15. Preprocessor and conditional compilation

Lines starting with `#` are handled **before** compilation.

```cpp
#if defined(MA_ENABLE_ONNX)
#include "inference/OnnxInference.h"
#endif
```

CMake sets `MA_ENABLE_ONNX=1` when `-DMA_ENABLE_ONNX=ON`. The ONNX code **is not compiled at all** in default builds.

| Directive | Role |
|-----------|------|
| `#include` | Paste another file |
| `#if` / `#ifdef` / `#endif` | Conditional compilation |
| `#define` | Macros (used for feature flags from CMake) |

**Python equivalent:** `if TYPE_CHECKING:` or optional imports — except C++ completely removes dead branches from the binary.

**Java:** no standard equivalent — use separate modules or reflection.

---

## 16. Concurrency: threads, atomics, mutexes

This plugin runs **two worlds**:

| Thread | Work | Constraints |
|--------|------|-------------|
| **Audio** (DAW callback) | Analysis, MIDI output | Hard deadline ~5 ms; **no blocking, no alloc** |
| **Inference** (background) | Pattern/structure/bass selection | Can sleep 20 ms; uses mutex around drain |

### `std::thread`
```cpp
inferenceThread = std::thread([this] { inferenceLoop(); });
// destructor:
inferenceThread.join();
```

**Lambda `[this] { ... }`** captures `this` pointer — like Python `lambda: self.inference_loop()` or Java `() -> inferenceLoop()`.

### `std::atomic` — lock-free shared flags
```cpp
std::atomic<float> displayBpm{ 120.0f };
std::atomic<int> latestPatternIndex{ 0 };

displayBpm.load(std::memory_order_relaxed);
latestPatternIndex.store(idx, std::memory_order_release);
```

**Java:** `AtomicInteger`, `volatile` (weaker guarantees). **Python:** no atomics in CPython for user code — use `threading` locks or `queue`.

**Memory orders** (`relaxed`, `release`, `acquire`) control visibility across cores — stricter than Java’s `volatile`. This project uses a conservative subset documented in `ARCHITECTURE.md`.

### `std::mutex` + `lock_guard`
```cpp
std::lock_guard<std::mutex> lock(inferenceDrainMutex);
drainFeatureQueueAndRunInference();
```

**Java:** `synchronized (inferenceDrainMutex) { ... }`. **Python:** `with lock:`.

Used on the **inference thread**, not in `processBlock` — audio thread must not wait on mutex.

### Lock-free queue
```cpp
(void)featureQueue.try_enqueue(fv);  // audio thread — never blocks
while (featureQueue.try_dequeue(tmp)) { ... }  // inference thread
```

See [CPP_LIBRARIES.md](CPP_LIBRARIES.md) for moodycamel details.

---

## 17. Real-time audio methodology in this project

Concepts you won’t see in typical Python/JS web work:

### 1. Audio thread safety
**Allowed in `processBlock`:**
- Stack variables
- Preallocated buffers (sized in `prepare()`)
- Atomics load/store
- Lock-free queue try operations

**Forbidden or avoided:**
- `new` / `malloc` / `std::vector::push_back` if it might allocate
- `std::mutex::lock`
- File I/O, logging, network
- Throwing exceptions

`PitchEstimator` comment: *“no heap in steady-state process”* — buffers grown only in `prepare()`.

### 2. `prepare()` vs `process()`
| Phase | When | Typical work |
|-------|------|--------------|
| `prepareToPlay(sr, blockSize)` | Track start / sample rate change | Allocate FFT buffers, reset state |
| `processBlock(buffer, midi)` | Every audio callback | Read samples, write MIDI |

Like initializing a NumPy workspace once, then processing chunks in a loop — except the DAW calls your loop.

### 3. Pass-through and denormals
```cpp
juce::ScopedNoDenormals noDenormals;
juce::FloatVectorOperations::clip(in, in, -2.0f, 2.0f, numSamples);
```
CPU micro-optimization for subnormal floating-point values — niche DSP concern.

### 4. Separation of concerns
```
Audio thread:  mic → OnsetDetector → EnergyAnalyser → FeatureVector → queue
                                                      ↓
Background:    queue → IInference → atomic pattern index
                                                      ↓
Audio thread:  PatternPlayer reads index → MidiBuffer → DAW
```

Same **producer/consumer** idea as Python `queue.Queue` between threads — but lock-free and fixed capacity here.

---

## 18. Testing in C++ (Catch2)

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("RuleBasedInference maps states to pattern indices", "[inference]")
{
    RuleBasedInference inf;
    inf.prepare(44100.0);

    FeatureVector f;
    f.state = StructureState::SILENT;
    REQUIRE(inf.selectPattern(f) == 0);
}
```

| Catch2 | pytest / unittest | Jest / JUnit |
|--------|-------------------|--------------|
| `TEST_CASE("name", "[tag]")` | `def test_name():` | `@Test` |
| `REQUIRE(x == y)` | `assert x == y` | `assertEquals` |
| `[inference]` tag | markers `@pytest.mark.inference` | tags |

Tests compile to a **native executable** (`MetalAccompanimentTests`) — not interpreted. CMake registers them with CTest.

---

## 19. Keyword cheat sheet

Quick lookup for symbols you’ll see in `src/`:

| Keyword / symbol | Plain English |
|------------------|---------------|
| `#pragma once` | Include guard |
| `public:` / `private:` | Access control |
| `virtual` | Method can be overridden; dynamic dispatch |
| `= 0` | Pure virtual — abstract |
| `override` | “I’m overriding a base virtual method” |
| `final` | No further overrides/subclasses |
| `const` | Won’t modify (context-dependent) |
| `constexpr` | Compile-time constant |
| `static` | File/class scope, not instance member |
| `noexcept` | Won’t throw exceptions |
| `explicit` | Prevent implicit constructor conversions |
| `nullptr` | Null pointer |
| `&` | Reference (alias) |
| `*` | Pointer |
| `->` | Member access through pointer |
| `::` | Scope — `PatternRules::foo`, `StructureState::SILENT` |
| `std::` | Standard library namespace |
| `auto` | Deduce type at declaration |
| `template<typename T>` | Generic type parameter |
| `using` | Type alias (like Python `TypeAlias`) |
| `typedef` | Older alias syntax — rare in new code |
| `sizeof` | Size of type in bytes |
| `static_cast<T>(x)` | Safe-ish numeric/conversion cast |
| `reinterpret_cast<T>(x)` | Low-level bit reinterpretation — ONNX byte buffer |
| `dynamic_cast<T>(x)` | Polymorphic downcast |
| `std::move` | Transfer ownership |
| `= default` | Compiler-generated special member |
| `= delete` | Forbidden operation |
| `{}` | Uniform initialization / list init |
| `.` | Member access on object/reference |
| `this` | Pointer to current object — like Java `this` |
| `friend` | Grant access to another class — rare |
| `mutable` | Member modifiable in `const` method — rare here |
| `volatile` | Special memory semantics — **not** Java volatile; rare |
| `alignas` / `alignof` | Memory alignment — SIMD |

---

## 20. Where to read next in this repo

| Goal | Document / code |
|------|-----------------|
| What each subsystem does | [docs/CODEBASE_WALKTHROUGH.md](docs/CODEBASE_WALKTHROUGH.md) |
| Thread and data flow | [docs/RUNTIME_ARCHITECTURE.md](docs/RUNTIME_ARCHITECTURE.md) |
| Libraries used | [CPP_LIBRARIES.md](CPP_LIBRARIES.md) |
| RT safety rules | [docs/CPP_JUCE_AUDIO_BEST_PRACTICES_AUDIT.md](docs/CPP_JUCE_AUDIO_BEST_PRACTICES_AUDIT.md) |
| Smallest complete class | `src/inference/RuleBasedInference.{h,cpp}` |
| Richest “kitchen sink” file | `src/AccompanimentProcessor.{h,cpp}` |
| Plain data struct | `src/analysis/FeatureVector.h` |
| Interface pattern | `src/inference/IInference.h` |
| State machine | `src/analysis/StructureTagger.{h,cpp}` |
| Unit test example | `tests/test_rule_based_inference.cpp` |

### Suggested exercises

1. **Trace one audio block:** start at `AccompanimentProcessor::processBlock`, follow calls until MIDI is written.
2. **Compare `FeatureVector`:** list each field and find where it is set (audio thread) vs read (inference thread).
3. **Swap mental model:** for each `std::unique_ptr` member, ask “who creates it, who destroys it, and when?”
4. **Read a test:** run `ctest --test-dir build -R unit` and pick one failure to debug — C++ errors are compile-time heavy; learn to read template error tails.
5. **Change something tiny:** e.g. adjust `kSilentRms` in `StructureTagger.h`, bump patch version in `CMakeLists.txt`, rebuild, confirm UI version string in the DAW.

---

## Cross-language pattern map (summary)

| Pattern in this repo | Python | Java | JavaScript |
|----------------------|--------|------|------------|
| `IInference` + impls | ABC + classes | `interface` + `implements` | class + duck typing |
| `FeatureVector` struct | `@dataclass` | POJO / record | plain object |
| `std::unique_ptr` | single owner (discipline) | strong ref + no GC for native | N/A |
| `std::atomic` | `queue` + locks usually | `java.util.concurrent.atomic` | Atomics via SharedArrayBuffer (niche) |
| `#include "foo.h"` | `import foo` | `import foo.Bar` | `import './foo.js'` |
| CMake build | `pyproject`/setup | Gradle/Maven | webpack/vite |
| Catch2 `TEST_CASE` | pytest | JUnit `@Test` | Jest `test()` |
| `enum class` | `Enum` / `IntEnum` | `enum` | const object / TS enum |
| Header/impl split | single module file | one class per file common | single module |

C++ in this project is **not** “C with classes.” It is a statically typed, compiled language where **ownership, thread boundaries, and real-time constraints** are as important as algorithms. The good news: most of your OOP intuition transfers; the new work is learning **when not to allocate, lock, or copy** on the audio path — and letting the compiler’s type system catch mistakes early.

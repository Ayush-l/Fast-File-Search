# Fast File Searcher — C++17 Windows Edition

A native Windows desktop file-name search application, independently reimplemented in C++17 based on the feature set of [s0n1c07/Fast_File_Searcher](https://github.com/s0n1c07/Fast_File_Searcher).

## Features
- Parallel recursive directory traversal using `std::thread`, `std::filesystem`, mutexes and condition variables
- Uses available logical CPU cores (capped at 32)
- Case-insensitive substring matching of file names
- Responsive Win32 GUI with folder picker, live results, cancellation and elapsed time
- Double-click a result to select the file in Windows Explorer
- Permission errors skipped; directory symlinks/junctions not followed
- Batches search results to the UI every 100 ms

## Build on Windows
Requires Windows 10/11, Visual Studio 2022 with Desktop development with C++ workload, and CMake.

In a **Developer Command Prompt** at the repository root:

```bat
build.bat
```

Or:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Run `build\Release\FastFileSearcher.exe`.

For MinGW use a compatible CMake generator rather than `-A x64`.

## Usage
1. Choose the directory to search (default `C:\`).
2. Enter a filename substring such as `report` or `.pdf`.
3. Click **Search**. Matches appear progressively.
4. Click **Cancel** to stop searching.
5. Double-click a result to reveal it in Explorer.

## Architecture
- `src/main.cpp`: Win32 GUI, event handling, timer-based updates, folder picker and Explorer integration.
- `src/SearchEngine.h`: Search engine API and callback types.
- `src/SearchEngine.cpp`: Parallel work queue, worker threads, traversal, cancellation and statistics.
- `CMakeLists.txt`: C++17 Windows executable build.

## Notes
- Matches filenames, not file contents.
- Results are not sorted and depend on concurrent discovery order.
- Searches do not honor `.gitignore` rules.
- Some inaccessible/system files will be skipped.
- The application targets Windows and has not been compiled on Windows in this environment.
- Cancellation is cooperative; cancellation responsiveness and thread wake-up should be tested and hardened.

## Attribution
Original Rust project: https://github.com/s0n1c07/Fast_File_Searcher
This C++ implementation is independent rather than a direct source translation.

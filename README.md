# WhereIsByLabel (mini WhereIsIt-like)

Windows-only C++17 CLI tool that:
- Finds a disk by **Volume Label** (ime diska)
- Indexes all files (relative paths)
- Lets you search the saved index later

## Build (CMake)
```bat
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

## Usage
Index:
```bat
whereisbylabel index "Backup_2TB" "backup_2tb.wibl"
```

Search:
```bat
whereisbylabel search "backup_2tb.wibl" "movie"
whereisbylabel search "backup_2tb.wibl" ".mkv" --name
```

#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <chrono>

namespace fs = std::filesystem;

static std::string wideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), out.data(), size, nullptr, nullptr);
    return out;
}

static std::wstring toLowerW(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool iequals(const std::wstring& a, const std::wstring& b) {
    return toLowerW(a) == toLowerW(b);
}

static std::vector<std::wstring> listDriveRoots() {
    DWORD mask = GetLogicalDrives();
    std::vector<std::wstring> roots;
    for (int i = 0; i < 26; i++) {
        if (mask & (1u << i)) {
            wchar_t root[] = { wchar_t(L'A' + i), L':', L'\\', 0 };
            roots.push_back(root);
        }
    }
    return roots;
}

struct VolumeInfo {
    std::wstring root;   // drive root, example: E:
    std::wstring label;  // volume label (disk name)
    DWORD serial = 0;    // volume serial number
    std::wstring fsName; // filesystem name
    bool ok = false;
};

static VolumeInfo getVolumeInfo(const std::wstring& root) {
    VolumeInfo v;
    v.root = root;

    wchar_t volumeName[MAX_PATH] = {0};
    wchar_t fsName[MAX_PATH] = {0};
    DWORD serial = 0, maxCompLen = 0, fsFlags = 0;

    if (GetVolumeInformationW(
            root.c_str(),
            volumeName, MAX_PATH,
            &serial,
            &maxCompLen,
            &fsFlags,
            fsName, MAX_PATH)) {
        v.label = volumeName;
        v.serial = serial;
        v.fsName = fsName;
        v.ok = true;
    }
    return v;
}

static bool findDriveByLabel(const std::wstring& wantedLabel, VolumeInfo& out) {
    for (const auto& root : listDriveRoots()) {
        auto v = getVolumeInfo(root);
        if (!v.ok) continue;
        if (iequals(v.label, wantedLabel)) {
            out = v;
            return true;
        }
    }
    return false;
}

static std::string escapeTSV(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

static std::string unescapeTSV(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == '\\') { out += '\\'; i++; continue; }
            if (n == 't')  { out += '\t'; i++; continue; }
            if (n == 'n')  { out += '\n'; i++; continue; }
            if (n == 'r')  { out += '\r'; i++; continue; }
        }
        out += c;
    }
    return out;
}

static long long fileTimeToUnixSeconds(const fs::file_time_type& ft) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}

static void cmdIndex(const std::wstring& label, const fs::path& outFile) {
    VolumeInfo v;
    if (!findDriveByLabel(label, v)) {
        std::wcerr << L"Disk sa labelom '" << label << L"' nije pronadjen.\n";
        exit(2);
    }

    std::ofstream out(outFile, std::ios::binary);
    if (!out) {
        std::wcerr << L"Ne mogu da otvorim izlazni fajl: " << outFile.wstring() << L"\n";
        exit(3);
    }

    out << "#WHEREISBYLABEL\t1\n";
    out << "#DISK_LABEL\t" << escapeTSV(wideToUtf8(v.label)) << "\n";
    out << "#DISK_SERIAL\t" << v.serial << "\n";
    out << "#DISK_FS\t" << escapeTSV(wideToUtf8(v.fsName)) << "\n";
    out << "#DISK_ROOT\t" << escapeTSV(wideToUtf8(v.root)) << "\n";
    out << "#COLUMNS\tREL_PATH\tSIZE\tMTIME_UNIX\n";

    std::wcout << L"Indeksiram disk: " << v.root << L" (" << v.label << L")\n";
    std::wcout << L"Upisujem u: " << outFile.wstring() << L"\n";

    fs::path rootPath = fs::path(v.root);

    size_t count = 0;
    size_t errors = 0;

    fs::directory_options opts = fs::directory_options::skip_permission_denied;
    std::error_code ec;

    for (fs::recursive_directory_iterator it(rootPath, opts, ec), end; it != end; it.increment(ec)) {
        if (ec) { errors++; ec.clear(); continue; }

        const fs::directory_entry& de = *it;
        std::error_code ec2;

        if (!de.is_regular_file(ec2)) continue;

        fs::path p = de.path();
        fs::path rel;
        try {
            rel = fs::relative(p, rootPath);
        } catch (...) {
            std::wstring pw = p.wstring();
            std::wstring rw = rootPath.wstring();
            if (pw.rfind(rw, 0) == 0) rel = fs::path(pw.substr(rw.size()));
            else rel = p.filename();
        }

        uintmax_t sz = 0;
        auto ftime = de.last_write_time(ec2);
        if (!ec2) sz = de.file_size(ec2);

        long long mtime = 0;
        if (!ec2) mtime = fileTimeToUnixSeconds(ftime);

        std::string relUtf8 = wideToUtf8(rel.wstring());
        out << escapeTSV(relUtf8) << "\t" << sz << "\t" << mtime << "\n";

        count++;
        if (count % 5000 == 0) std::wcout << L"  ... " << count << L" fajlova\n";
    }

    std::wcout << L"Gotovo. Fajlova: " << count << L", gresaka/skip: " << errors << L"\n";
}

static bool parseHeader(std::ifstream& in, std::string& label, uint32_t& serial) {
    label.clear();
    serial = 0;

    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("#COLUMNS", 0) == 0) return true;
        if (line.rfind("#DISK_LABEL\t", 0) == 0) {
            label = unescapeTSV(line.substr(std::string("#DISK_LABEL\t").size()));
        } else if (line.rfind("#DISK_SERIAL\t", 0) == 0) {
            serial = (uint32_t)std::stoul(line.substr(std::string("#DISK_SERIAL\t").size()));
        }
        if (!line.empty() && line[0] != '#') break;
    }
    return false;
}

static void cmdSearch(const fs::path& indexFile, const std::string& patternUtf8, bool onlyFilename) {
    std::ifstream in(indexFile, std::ios::binary);
    if (!in) {
        std::wcerr << L"Ne mogu da otvorim indeks: " << indexFile.wstring() << L"\n";
        exit(4);
    }

    std::string diskLabel;
    uint32_t diskSerial = 0;
    if (!parseHeader(in, diskLabel, diskSerial)) {
        std::wcerr << L"Indeks izgleda neispravno.\n";
        exit(5);
    }

    std::string needle = toLower(patternUtf8);
    std::string line;
    size_t hits = 0;

    std::cout << "Index: " << indexFile.u8string() << "\n";
    std::cout << "Disk label: " << diskLabel << " | Serial: " << diskSerial << "\n";
    std::cout << "Query: " << patternUtf8 << "\n\n";

    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;

        std::string relEsc = line.substr(0, t1);
        std::string rel = unescapeTSV(relEsc);

        std::string hay = rel;
        if (onlyFilename) {
            // safe: both slash types
            size_t p1 = hay.find_last_of(std::string("\\/"));
            if (p1 != std::string::npos) hay = hay.substr(p1 + 1);
        }

        if (toLower(hay).find(needle) != std::string::npos) {
            std::cout << rel << "\n";
            hits++;
        }
    }

    std::cout << "\nHits: " << hits << "\n";
}

static void printUsage() {
    std::cout <<
R"(WhereIsByLabel (mini WhereIsIt-like indexer) - Windows-only

USAGE:
  whereisbylabel index  "<DISK_LABEL>"  "<output_index_file>"
  whereisbylabel search "<index_file>"  "<pattern>" [--name]

EXAMPLES:
  whereisbylabel index  "Backup_2TB" "backup_2tb.wibl"
  whereisbylabel search "backup_2tb.wibl" "movie"
  whereisbylabel search "backup_2tb.wibl" ".mkv" --name
)";
}

int wmain(int argc, wchar_t** wargv) {
    std::vector<std::wstring> wargs(wargv, wargv + argc);

    if (argc < 2) { printUsage(); return 1; }
    std::wstring cmd = wargs[1];

    if (iequals(cmd, L"index")) {
        if (argc < 4) { printUsage(); return 1; }
        std::wstring label = wargs[2];
        fs::path outFile = fs::path(wargs[3]);
        cmdIndex(label, outFile);
        return 0;
    }

    if (iequals(cmd, L"search")) {
        if (argc < 4) { printUsage(); return 1; }
        fs::path idx = fs::path(wargs[2]);
        std::string pattern = wideToUtf8(wargs[3]);
        bool onlyName = (argc >= 5 && iequals(wargs[4], L"--name"));
        cmdSearch(idx, pattern, onlyName);
        return 0;
    }

    printUsage();
    return 1;
}

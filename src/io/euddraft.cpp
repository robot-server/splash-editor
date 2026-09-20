#include "io/euddraft.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace splash::io::euddraft {

namespace {

namespace fs = std::filesystem;

/// 셸에 넘길 수 있게 감싼다. 맵 이름에 공백·따옴표가 흔하다.
std::string quote(const std::string & text)
{
#ifdef _WIN32
    // cmd 는 작은따옴표를 모른다. 큰따옴표로 감싸고 안의 큰따옴표만 뺀다.
    std::string out = "\"";
    for (const char c : text)
    {
        if (c != '"')
            out.push_back(c);
    }
    out.push_back('"');
    return out;
#else
    std::string out = "'";
    for (const char c : text)
    {
        if (c == '\'')
            out += "'\\''";
        else
            out.push_back(c);
    }
    out.push_back('\'');
    return out;
#endif
}

bool isExecutableFile(const fs::path & path)
{
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        return false;
#ifdef _WIN32
    return true;
#else
    return ::access(path.c_str(), X_OK) == 0;
#endif
}

/// PATH 를 훑는다.
std::string searchPath(const std::string & name)
{
    const char * raw = std::getenv("PATH");
    if (raw == nullptr)
        return {};

    std::stringstream stream(raw);
    std::string entry;
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    while (std::getline(stream, entry, separator))
    {
        if (entry.empty())
            continue;
        const fs::path candidate = fs::path(entry) / name;
        if (isExecutableFile(candidate))
            return candidate.string();
    }
    return {};
}

/// 그 폴더 바로 아래에서 `euddraft` 로 시작하는 폴더를 뒤진다.
/// 배포본이 `euddraft0.11.0.1` 처럼 판 번호를 달고 풀리기 때문이다.
std::string searchUnpacked(const fs::path & root, const std::string & name)
{
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return {};

    if (const fs::path direct = root / name; isExecutableFile(direct))
        return direct.string();

    for (const auto & entry : fs::directory_iterator(root, ec))
    {
        if (ec)
            break;
        if (!entry.is_directory())
            continue;

        const std::string folder = entry.path().filename().string();
        if (folder.rfind("euddraft", 0) != 0)
            continue;

        if (const fs::path candidate = entry.path() / name; isExecutableFile(candidate))
            return candidate.string();
    }
    return {};
}

/// `[section]` 머리말로 쓸 수 있게 다듬는다. 대괄호는 못 들어간다.
std::string sanitizeSection(const std::string & name)
{
    std::string out;
    for (const char c : name)
    {
        if (c != '[' && c != ']' && c != '\n' && c != '\r')
            out.push_back(c);
    }
    return out;
}

/// 앞뒤 공백을 턴다.
std::string trimmed(const std::string & text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

Plugin parsePlugin(const std::string & line)
{
    Plugin plugin;

    const std::size_t colon = line.find(':');
    if (colon == std::string::npos)
    {
        plugin.name = trimmed(line);
        return plugin;
    }

    plugin.name = trimmed(line.substr(0, colon));

    std::stringstream rest(line.substr(colon + 1));
    std::string entry;
    while (std::getline(rest, entry, ','))
    {
        const std::string pair = trimmed(entry);
        if (pair.empty())
            continue;

        const std::size_t equals = pair.find('=');
        if (equals == std::string::npos)
        {
            // 값 없는 키. euddraft 의 [freeze] 처럼 "있기만 하면 되는" 것들.
            plugin.settings.emplace_back(pair, std::string {});
            continue;
        }
        plugin.settings.emplace_back(trimmed(pair.substr(0, equals)),
                                     trimmed(pair.substr(equals + 1)));
    }
    return plugin;
}

std::string findExecutable()
{
#ifdef _WIN32
    const std::string name = "euddraft.exe";
#else
    const std::string name = "euddraft";
#endif

    if (const char * fromEnv = std::getenv("SPLASH_EUDDRAFT");
        fromEnv != nullptr && *fromEnv != '\0')
    {
        const fs::path path(fromEnv);
        std::error_code ec;
        if (fs::is_directory(path, ec))
        {
            if (const fs::path candidate = path / name; isExecutableFile(candidate))
                return candidate.string();
        }
        else if (isExecutableFile(path))
        {
            return path.string();
        }
    }

    if (std::string found = searchPath(name); !found.empty())
        return found;

    // 흔히 풀어 두는 자리들.
    std::vector<fs::path> roots;
    if (const char * home = std::getenv("HOME"); home != nullptr)
    {
        roots.emplace_back(fs::path(home) / "euddraft");
        roots.emplace_back(fs::path(home) / "Downloads");
        roots.emplace_back(fs::path(home) / "Applications");
        roots.push_back(fs::path(home));
    }
    roots.emplace_back("/opt/euddraft");
    roots.emplace_back("/usr/local/share/euddraft");
    roots.emplace_back("/Applications");

    for (const fs::path & root : roots)
    {
        if (std::string found = searchUnpacked(root, name); !found.empty())
            return found;
    }
    return {};
}

std::string settingsText(const BuildRequest & request)
{
    std::ostringstream out;

    out << ":: splash-editor 가 만든 euddraft 설정입니다. 고쳐도 다음 빌드에 덮입니다.\n";
    out << "[main]\n";
    out << "input: " << request.inputMap << "\n";
    out << "output: " << request.outputMap << "\n";
    for (const auto & [key, value] : request.mainOptions)
        out << key << ": " << value << "\n";

    for (const std::string & script : request.scripts)
    {
        if (script.empty())
            continue;
        out << "\n[" << sanitizeSection(script) << "]\n";
    }

    for (const Plugin & plugin : request.plugins)
    {
        if (plugin.name.empty())
            continue;
        out << "\n[" << sanitizeSection(plugin.name) << "]\n";
        for (const auto & [key, value] : plugin.settings)
        {
            // euddraft 의 readconfig 는 키만 있는 줄도 받는다. 값이 비었으면
            // 콜론을 붙이지 않는다 — 빈 값을 넘기면 플러그인이 그것을 값으로 읽는다.
            if (value.empty())
                out << key << "\n";
            else
                out << key << ": " << value << "\n";
        }
    }

    // euddraft 는 freeze(맵 보호)를 기본으로 켠다. 끄려면 [freeze] 안에
    // freeze 키가 있어야 한다 — 값은 보지 않는다.
    if (!request.freeze)
        out << "\n[freeze]\nfreeze: off\n";

    return out.str();
}

std::vector<std::string> importantLines(const std::string & log)
{
    static const std::array<const char *, 8> kMarkers {
        "Error", "error", "ERROR", "Traceback", "Warning", "warning", "Exception", "오류"
    };

    std::vector<std::string> out;
    std::istringstream stream(log);
    std::string line;
    while (std::getline(stream, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.empty())
            continue;

        for (const char * marker : kMarkers)
        {
            if (line.find(marker) != std::string::npos)
            {
                out.push_back(line);
                break;
            }
        }
    }
    return out;
}

std::string normalize(BuildRequest & request)
{
    if (request.executable.empty())
        request.executable = findExecutable();
    if (request.executable.empty())
    {
        return "euddraft 를 찾지 못했습니다. 환경 변수 SPLASH_EUDDRAFT 에 "
               "실행 파일 경로를 적거나 PATH 에 넣어 주세요.";
    }

    std::error_code ec;
    if (request.inputMap.empty() || !fs::exists(request.inputMap, ec))
        return "원본 맵이 없습니다: " + request.inputMap;
    if (request.outputMap.empty())
        return "만들 맵의 경로가 비어 있습니다.";

    const fs::path inputPath = fs::absolute(request.inputMap, ec);
    const fs::path outputPath = fs::absolute(request.outputMap, ec);

    // euddraft 는 입력과 출력이 같으면 거부한다. 우리가 먼저 걸러 준다.
    if (inputPath == outputPath)
        return "원본과 만들 맵의 경로가 같습니다. 다른 이름을 주세요.";

    for (const std::string & script : request.scripts)
    {
        if (!fs::exists(script, ec))
            return "스크립트가 없습니다: " + script;
    }

    request.inputMap = inputPath.string();
    request.outputMap = outputPath.string();
    for (std::string & script : request.scripts)
        script = fs::absolute(script, ec).string();
    for (Plugin & plugin : request.plugins)
    {
        const bool isFile =
            (plugin.name.size() > 4 && plugin.name.substr(plugin.name.size() - 4) == ".eps") ||
            (plugin.name.size() > 3 && plugin.name.substr(plugin.name.size() - 3) == ".py");
        if (isFile)
            plugin.name = fs::absolute(plugin.name, ec).string();
    }
    return {};
}

BuildResult build(const BuildRequest & request)
{
    BuildResult result;

    BuildRequest normalized = request;
    if (std::string why = normalize(normalized); !why.empty())
    {
        result.ok = false;
        result.message = std::move(why);
        return result;
    }

    std::error_code ec;
    const fs::path outputPath(normalized.outputMap);

    fs::path workDir = normalized.workingDirectory.empty()
                           ? outputPath.parent_path()
                           : fs::path(normalized.workingDirectory);
    if (workDir.empty())
        workDir = fs::current_path(ec);
    fs::create_directories(workDir, ec);

    const fs::path settingsPath = workDir / (outputPath.stem().string() + ".splash.eds");
    result.settingsPath = settingsPath.string();

    {
        std::ofstream settings(settingsPath, std::ios::binary | std::ios::trunc);
        if (!settings)
        {
            result.ok = false;
            result.message = "설정 파일을 쓰지 못했습니다: " + settingsPath.string();
            return result;
        }
        settings << settingsText(normalized);
    }

    // 출력 맵이 이미 있으면 치운다. euddraft 가 죽어도 옛 파일이 남아
    // "됐다" 로 잘못 읽히지 않도록.
    const bool hadOutput = fs::exists(outputPath, ec);
    std::uintmax_t previousSize = hadOutput ? fs::file_size(outputPath, ec) : 0;
    fs::remove(outputPath, ec);

    const std::string command =
        quote(normalized.executable) + " " + quote(settingsPath.string()) + " 2>&1";
    result.command = command;

#ifdef _WIN32
    std::FILE * pipe = _popen(command.c_str(), "r");
#else
    std::FILE * pipe = ::popen(command.c_str(), "r");
#endif
    if (pipe == nullptr)
    {
        result.ok = false;
        result.message = "euddraft 를 실행하지 못했습니다.";
        return result;
    }

    std::string log;
    std::array<char, 4096> buffer {};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        log += buffer.data();
    result.log = log;

#ifdef _WIN32
    result.exitCode = _pclose(pipe);
#else
    const int status = ::pclose(pipe);
    if (status == -1)
    {
        result.exitCode = -1;
    }
    else if (WIFSIGNALED(status))
    {
        result.signalNumber = WTERMSIG(status);
        result.exitCode = 128 + result.signalNumber;
    }
    else
    {
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : status;
        // 셸이 신호로 죽은 자식을 128+n 으로 옮겨 주기도 한다.
        if (result.exitCode > 128 && result.exitCode < 160)
            result.signalNumber = result.exitCode - 128;
    }
#endif

    result.outputWritten = fs::exists(outputPath, ec) && fs::file_size(outputPath, ec) > 0;

    if (result.outputWritten)
    {
        result.ok = true;
        const std::uintmax_t size = fs::file_size(outputPath, ec);
        std::ostringstream message;
        message << "맵을 만들었습니다: " << outputPath.string() << " (" << size << " 바이트)";
        if (result.exitCode != 0)
        {
            // 0.11.0.1 macOS 배포본은 맵을 다 쓴 뒤 freezeMpq 안에서 SIGBUS
            // 로 죽는다(업스트림 PR #178 이 릴리스 뒤에 고쳤다). 산출물이
            // 멀쩡하면 알리기만 하고 실패로 치지 않는다.
            message << "\n주의: euddraft 가 " << result.exitCode << " 로 끝났습니다";
            if (result.signalNumber != 0)
                message << " (신호 " << result.signalNumber << ")";
            message << ". 산출물은 생겼으니 게임에서 확인해 보세요.";
        }
        result.message = message.str();
    }
    else
    {
        result.ok = false;
        std::ostringstream message;
        message << "euddraft 가 맵을 만들지 못했습니다 (끝 코드 " << result.exitCode << ").";
        const auto lines = importantLines(log);
        if (!lines.empty())
            message << "\n" << lines.back();
        result.message = message.str();

        if (hadOutput && previousSize > 0)
            result.message += "\n(먼저 있던 출력 맵은 지워졌습니다.)";
    }

    return result;
}

} // namespace splash::io::euddraft

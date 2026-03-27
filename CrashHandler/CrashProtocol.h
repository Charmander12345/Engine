#pragma once
// ── CrashHandler IPC Protocol ───────────────────────────────────────────
// Shared header between the engine (Logger) and the out-of-process CrashHandler.
// Communication is unidirectional (engine → handler) over a named pipe.
// Each message is a length-prefixed UTF-8 string:
//    [4 bytes little-endian uint32_t length][payload bytes]
// Payload format:  "TAG|data..."
//
// The CrashHandler accumulates all received data.  When the pipe breaks
// (engine crashed / exited) it checks whether a CRASH tag was received.
// If yes (or the disconnect was unexpected), it shows a crash report dialog.

#include <cstdint>
#include <string>

namespace CrashProtocol
{
    // Pipe name (Windows named pipe / Linux domain socket path)
    // PID-parameterized so multiple engine instances can each have their own CrashHandler.
    inline std::string pipeName(uint32_t pid)
    {
#if defined(_WIN32)
        return "\\\\.\\pipe\\HorizonEngineCrashPipe_" + std::to_string(pid);
#else
        return "/tmp/HorizonEngineCrashPipe_" + std::to_string(pid);
#endif
    }

    // Message tags (prefixed before '|' in each payload)
    namespace Tag
    {
        inline const char* Heartbeat   = "HB";        // "HB|<timestamp_ms>"
        inline const char* LogEntry    = "LOG";        // "LOG|level|category|message"
        inline const char* EngineState = "STATE";      // "STATE|key=value\nkey=value\n..."
        inline const char* Hardware    = "HW";         // "HW|cpu=...\ngpu=...\nvram=...\nram=..."
        inline const char* Project     = "PROJ";       // "PROJ|name|version|path|level"
        inline const char* FrameInfo   = "FRAME";      // "FRAME|fps|cpuMs|gpuMs|entities|visible"
        inline const char* Crash       = "CRASH";      // "CRASH|description|stacktrace"
        inline const char* Shutdown    = "QUIT";       // "QUIT|" – graceful engine shutdown
        inline const char* Modules     = "MODS";       // "MODS|dll1\ndll2\n..."  loaded modules
        inline const char* Uptime      = "UP";         // "UP|seconds"
        inline const char* Commandline = "CMD";        // "CMD|full commandline string"
        inline const char* WorkingDir  = "CWD";        // "CWD|path"
        inline const char* EngineVer   = "VER";        // "VER|version string"
    }

    // Helper: build a length-prefixed message from tag + data.
    inline std::string buildMessage(const char* tag, const std::string& data)
    {
        std::string payload = std::string(tag) + "|" + data;
        uint32_t len = static_cast<uint32_t>(payload.size());
        std::string msg;
        msg.resize(4 + len);
        msg[0] = static_cast<char>((len      ) & 0xFF);
        msg[1] = static_cast<char>((len >>  8) & 0xFF);
        msg[2] = static_cast<char>((len >> 16) & 0xFF);
        msg[3] = static_cast<char>((len >> 24) & 0xFF);
        std::memcpy(msg.data() + 4, payload.data(), len);
        return msg;
    }

    // Helper: extract tag and data from a payload string (without length prefix).
    inline bool parsePayload(const std::string& payload, std::string& outTag, std::string& outData)
    {
        auto sep = payload.find('|');
        if (sep == std::string::npos) return false;
        outTag  = payload.substr(0, sep);
        outData = payload.substr(sep + 1);
        return true;
    }
}

#pragma once
#include "platform.hpp"

namespace lux {
inline constexpr const wchar_t *appVersion = L"1.1.1";
// A separate process keeps networking, ZIP preparation and replacement off the input/UI threads.
class Updates {
  HANDLE process = nullptr;
  std::filesystem::path session;

public:
  bool ready = false, automatic = true;
  std::wstring message = L"Updates: automatic checks enabled";
  ~Updates() {
    if (process)
      CloseHandle(process);
  }
  void load() {
    automatic =
        GetPrivateProfileIntW(L"Updates", L"Automatic", 1, (dataFolder() / L"updates.ini").c_str()) != 0;
  }
  void setAutomatic(bool value) {
    automatic = value;
    WritePrivateProfileStringW(L"Updates", L"Automatic", value ? L"1" : L"0",
                               (dataFolder() / L"updates.ini").c_str());
  }
  bool busy() const {
    return process != nullptr;
  }
  static std::wstring quote(const std::wstring &s) {
    // Paths are local Windows paths: double quotes are forbidden in filenames.
    if (s.find(L'"') != std::wstring::npos)
      throw std::runtime_error("Invalid update path");
    return L"\"" + s + L"\"";
  }
  bool start(bool install = false) {
    if (busy() || (install && !ready))
      return false;
    try {
      if (!install) {
        session = dataFolder() / L"Updates" /
                  (std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(session);
        std::filesystem::copy_file(appFolder() / L"Updates/Update.ps1", session / L"Update.ps1");
        ready = false;
      }
      wchar_t system[32768]{};
      if (!GetSystemDirectoryW(system, 32768))
        throw std::runtime_error("Cannot locate Windows");
      auto powershell = std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe";
      auto command =
          quote(powershell.wstring()) + L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " +
          quote((session / L"Update.ps1").wstring()) + L" -Mode " + (install ? L"Install" : L"Check") +
          L" -InstallDirectory " + quote(appFolder().wstring()) + L" -StateDirectory " +
          quote(session.wstring()) + L" -CurrentVersion " + appVersion + L" -ParentProcessId " +
          std::to_wstring(GetCurrentProcessId());
      STARTUPINFOW si{};
      si.cb = sizeof(si);
      PROCESS_INFORMATION pi{};
      if (!CreateProcessW(powershell.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, session.c_str(), &si, &pi))
        throw std::runtime_error("Could not start Windows PowerShell updater");
      CloseHandle(pi.hThread);
      process = pi.hProcess;
      message = install ? L"Closing Luxware to install update..." : L"Checking for updates...";
      return true;
    } catch (const std::exception &e) {
      message = wide(e.what());
      return false;
    }
  }
  void poll() {
    if (!process)
      return;
    wchar_t text[1024]{}, phase[64]{};
    auto file = session / L"status.ini";
    GetPrivateProfileStringW(L"Update", L"Message", L"Checking for updates...", text, 1024, file.c_str());
    GetPrivateProfileStringW(L"Update", L"State", L"Checking", phase, 64, file.c_str());
    message = text;
    if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
      DWORD code = 1;
      GetExitCodeProcess(process, &code);
      ready = code == 0 && std::wstring(phase) == L"Ready";
      if (code != 0 && std::wstring(phase) != L"Error")
        message = L"Update check failed. Try again from Settings.";
      CloseHandle(process);
      process = nullptr;
    }
  }
};
} // namespace lux

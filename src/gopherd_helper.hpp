#pragma once
#include <string>
#include <iostream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  #include <sys/types.h>
  #include <signal.h>
  #include <spawn.h>
  extern char **environ;
#endif


constexpr uint16_t DAEMON_PORT = 43823;

inline bool is_daemon_running(){
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return false;

  sockaddr_in addr{};
  addr.sin_family =  AF_INET;
  addr.sin_port   =  htons(DAEMON_PORT);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  int result = connect(sock, (sockaddr*)&addr, sizeof(addr));
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
  return result == 0;
}


inline bool launch_daemon(const std::string& daemon_path = "gopherd") {
#ifdef _WIN32
  STARTUPINFOA si = { sizeof(si) };
  PROCESS_INFORMATION pi;

  DWORD parent_pid = GetCurrentProcessId();
  std::stringstream cmd;
  cmd << daemon_path << " " << parent_pid;

  std::string cmd = daemon_path + "";
  BOOL success = CreateProcessA(
    NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, FALSE,
    DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  if (success) {
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
  } else {
    std::cerr << "CreateProcess failed: " << GetLastError() << std::endl;
    return false;
  }
#else
  pid_t pid;
  std::string parent_pid = std::to_string(getpid());
  char* argv[] = { const_cast<char*>(daemon_path.c_str()), nullptr };
  int status = posix_spawnp(&pid, daemon_path.c_str(), nullptr, nullptr, argv, environ);
  return status == 0;
#endif
}


inline void ensure_daemon_running(const std::string& daemon_path = "gopherd") {
  if (is_daemon_running()) {
    std::cout << "[gopher] Daemon already running.\n";
  } else {
    std::cout << "[gopher] Launching daemon...\n";
    if (!launch_daemon(daemon_path)) {
      std::cerr << "[gopher] Failed to launch discovery daemon." << std::endl;
    }
  }
}
/**
 * @file src/apollide/notify_relay.cpp
 * @brief Apollide: forwards the H1 user's Windows notifications (see notify_relay.h).
 *
 * The helper is launched and authenticated the same way as the upstream end-stream hotkey
 * helper (src/platform/windows/end_stream_hotkey.cpp): a pipe only SYSTEM and the console user
 * may open, the exact child process we started, in the active console session.
 */
#include "notify_relay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <stop_token>
#include <thread>
#include <unordered_map>

#include "src/logging.h"
#include "src/platform/common.h"
#include "src/utility.h"

#ifdef _WIN32
  #include "src/platform/windows/misc.h"

  #include <objbase.h>
  #include <sddl.h>
  #include <windows.h>
  #include <wtsapi32.h>
#endif

using namespace std::literals;

namespace apollide::notify {
  namespace {
    namespace fs = std::filesystem;
    using clock = std::chrono::steady_clock;

    constexpr std::size_t kMaxStored = 50;
    constexpr auto kStreamAlive = 3s;  ///< a session the control loop polled this recently is streaming

    struct session_t {
      bool subscribed = false;
      std::deque<std::uint32_t> pending;  ///< seqs to send
      int sent_forwarding = -1;
      int sent_helper = -1;
      clock::time_point polled;
    };

    struct last_t {
      std::string app;
      std::string title;
      std::int64_t at = 0;  ///< Unix seconds
      bool sent = false;
      std::string why;  ///< when not sent
    };

    std::mutex g_lock;
    std::uint32_t g_boot = 0;
    std::uint32_t g_seq = 0;
    std::vector<stored_t> g_items;
    std::unordered_map<const void *, session_t> g_sessions;
    bool g_enabled = true;
    bool g_loaded = false;
    helper_state_e g_helper = helper_state_e::none;
    std::uint32_t g_idle_ms = 0xFFFFFFFF;
    clock::time_point g_idle_at;
    std::optional<clock::time_point> g_stream_end;
    last_t g_last;
    std::jthread g_worker;

    fs::path store_path() {
      return platf::appdata() / "apollide_notify.json";
    }

    std::int64_t now_ms() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
    }

    std::int64_t unix_seconds() {
      return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void load_locked() {
      if (g_loaded) {
        return;
      }
      g_loaded = true;
      std::ifstream file(store_path());
      const auto j = nlohmann::json::parse(file, nullptr, false);
      if (j.is_object()) {
        g_enabled = j.value("enabled", true);
      }
    }

    void save_locked() {
      const auto path = store_path();
      const auto tmp = path.string() + ".tmp";
      {
        std::ofstream file(tmp, std::ios::trunc);
        file << nlohmann::json {{"enabled", g_enabled}}.dump(2);
      }
      std::error_code ec;
      fs::rename(tmp, path, ec);
    }

    bool streaming_locked() {
      const auto now = clock::now();
      for (const auto &[_, s] : g_sessions) {
        if (now - s.polled < kStreamAlive) {
          return true;
        }
      }
      return false;
    }

    std::int64_t idle_now_locked() {
      if (g_idle_ms == 0xFFFFFFFF) {
        return std::numeric_limits<std::int64_t>::max();
      }
      return g_idle_ms + std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - g_idle_at).count();
    }

    bool local_use_locked() {
      const std::int64_t since_end = g_stream_end ? std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - *g_stream_end).count() : -1;
      return local_use(streaming_locked(), idle_now_locked(), since_end);
    }

    void prune_locked() {
      const auto now = now_ms();
      std::erase_if(g_items, [&](const stored_t &s) {
        return now - s.arrived_ms > kKeepMs;
      });
      if (g_items.size() > kMaxStored) {
        g_items.erase(g_items.begin(), g_items.end() - static_cast<std::ptrdiff_t>(kMaxStored));
      }
    }

    void on_notification(const helper_message_t &m) {
      std::lock_guard lg {g_lock};
      load_locked();
      g_idle_ms = m.idle_ms;
      g_idle_at = clock::now();
      g_last = {m.n.app, m.n.title, unix_seconds(), false, {}};
      if (!g_enabled) {
        g_last.why = "off";
        return;
      }
      if (local_use_locked()) {
        g_last.why = "local";
        BOOST_LOG(debug) << "Apollide notify: someone uses H1 directly, keeping "sv << m.n.app << " here"sv;
        return;
      }
      g_last.sent = true;
      const auto seq = ++g_seq;
      g_items.push_back({m.n, seq, now_ms()});
      prune_locked();
      for (auto &[_, s] : g_sessions) {
        if (s.subscribed) {
          s.pending.push_back(seq);
        }
      }
      BOOST_LOG(info) << "Apollide notify: #"sv << seq << " from "sv << m.n.app;
    }

    void on_helper_state(helper_state_e state) {
      std::lock_guard lg {g_lock};
      g_helper = state;
    }

#ifdef _WIN32
    bool console_current(std::stop_token stop, DWORD session) {
      return !stop.stop_requested() && WTSGetActiveConsoleSessionId() == session;
    }

    int g_last_fail = 0;
    int g_helper_exit = -1;  ///< set when the launched helper is retired, after run_helper has returned

    /// Says once why the helper could not run (the supervisor retries every few seconds)
    int fail_at(int line) {
      const DWORD error = GetLastError();
      if (line != g_last_fail) {
        g_last_fail = line;
        BOOST_LOG(warning) << "Apollide notify: helper not running (step "sv << line << ", winerr "sv << error << ")"sv;
      }
      return -1;
    }

    /// One helper run; its exit code lands in g_helper_exit
    int run_helper(std::stop_token stop, DWORD session) {
      HANDLE token = nullptr;
      const bool system = platf::is_running_as_system();
      if (system) {
        token = platf::retrieve_users_token(false);
      } else if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return fail_at(__LINE__);
      }
      if (!token) {
        return fail_at(__LINE__);
      }
      auto close_token = util::fail_guard([&] {
        CloseHandle(token);
      });
      DWORD token_session = 0, needed = 0;
      if (!GetTokenInformation(token, TokenSessionId, &token_session, sizeof(token_session), &needed) || token_session != session) {
        return fail_at(__LINE__);
      }
      GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
      std::vector<std::uint8_t> token_user(needed);
      if (!GetTokenInformation(token, TokenUser, token_user.data(), needed, &needed)) {
        return fail_at(__LINE__);
      }
      LPWSTR sid_text = nullptr;
      if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(token_user.data())->User.Sid, &sid_text)) {
        return fail_at(__LINE__);
      }
      auto free_sid = util::fail_guard([&] {
        LocalFree(sid_text);
      });
      // Write plus FILE_READ_ATTRIBUTES: the helper's GetNamedPipeServerProcessId check is denied with
      // write alone (seen on H1 2026-10-25: the helper quit with code 4 every time)
      std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;0x40000080;;;" + std::wstring(sid_text) + L")S:(ML;;NW;;;ME)";
      PSECURITY_DESCRIPTOR security = nullptr;
      if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &security, nullptr)) {
        return fail_at(__LINE__);
      }
      auto free_security = util::fail_guard([&] {
        LocalFree(security);
      });
      SECURITY_ATTRIBUTES attributes {sizeof(attributes), security, FALSE};
      GUID guid {};
      wchar_t guid_text[40] {};
      if (FAILED(CoCreateGuid(&guid)) || StringFromGUID2(guid, guid_text, 40) == 0) {
        return fail_at(__LINE__);
      }
      const std::wstring pipe_name = L"\\\\.\\pipe\\apollide_notify_" + std::wstring(guid_text);
      HANDLE pipe = CreateNamedPipeW(pipe_name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, 65536, 0, &attributes);
      if (pipe == INVALID_HANDLE_VALUE) {
        return fail_at(__LINE__);
      }
      auto close_pipe = util::fail_guard([&] {
        CancelIoEx(pipe, nullptr);
        CloseHandle(pipe);
      });
      HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (!event) {
        return fail_at(__LINE__);
      }
      auto close_event = util::fail_guard([&] {
        CloseHandle(event);
      });
      OVERLAPPED connection {};
      connection.hEvent = event;
      const bool immediate = ConnectNamedPipe(pipe, &connection) != FALSE;
      const DWORD connection_error = immediate ? ERROR_SUCCESS : GetLastError();
      if (!immediate && connection_error != ERROR_IO_PENDING && connection_error != ERROR_PIPE_CONNECTED) {
        return fail_at(__LINE__);
      }
      auto cancel_connection = util::fail_guard([&] {
        if (connection_error == ERROR_IO_PENDING) {
          CancelIoEx(pipe, &connection);
          DWORD ignored = 0;
          GetOverlappedResult(pipe, &connection, &ignored, TRUE);
        }
      });
      std::array<wchar_t, 32768> executable {};
      const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
      if (length == 0 || length >= executable.size()) {
        return fail_at(__LINE__);
      }
      const auto directory = fs::path(executable.data()).parent_path();
      const auto helper = directory / L"tools" / L"apollide_notify.exe";
      if (!fs::exists(helper)) {
        BOOST_LOG(warning) << "Apollide notify: helper missing at "sv << helper.string();
        return fail_at(__LINE__);
      }
      const std::wstring command = L"\"" + helper.wstring() + L"\" \"" + pipe_name + L"\" " + std::to_wstring(GetCurrentProcessId());
      std::error_code error;
      auto startup = platf::create_startup_info(nullptr, nullptr, error);
      if (error) {
        return fail_at(__LINE__);
      }
      auto free_startup = util::fail_guard([&] {
        platf::free_proc_thread_attr_list(startup.lpAttributeList);
      });
      wchar_t desktop[] = L"winsta0\\default";
      startup.StartupInfo.lpDesktop = desktop;
      PROCESS_INFORMATION child {};
      constexpr DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;
      const bool launched = system ?
                              platf::launch_process_with_impersonation(false, platf::to_utf8(command), directory.wstring(), flags, startup, child, error) :
                              platf::launch_process_without_impersonation(platf::to_utf8(command), directory.wstring(), flags, startup, child, error);
      if (!launched) {
        BOOST_LOG(warning) << "Apollide notify: helper could not be launched: "sv << error.message();
        return fail_at(__LINE__);
      }
      CloseHandle(child.hThread);
      BOOST_LOG(debug) << "Apollide notify: helper started (pid "sv << child.dwProcessId << ")"sv;
      int exit_code = -1;
      auto retire_child = util::fail_guard([&] {
        DisconnectNamedPipe(pipe);
        if (WaitForSingleObject(child.hProcess, 1500) == WAIT_TIMEOUT) {
          TerminateProcess(child.hProcess, 0);
          WaitForSingleObject(child.hProcess, 1500);
        }
        DWORD code = 0;
        if (GetExitCodeProcess(child.hProcess, &code)) {
          exit_code = static_cast<int>(code);
          g_helper_exit = exit_code;
        }
        CloseHandle(child.hProcess);
      });
      DWORD child_session = 0;
      if (!ProcessIdToSessionId(child.dwProcessId, &child_session) || child_session != session) {
        return fail_at(__LINE__);
      }
      const auto deadline = clock::now() + 10s;
      if (connection_error == ERROR_IO_PENDING) {
        while (WaitForSingleObject(event, 100) == WAIT_TIMEOUT) {
          if (!console_current(stop, session) || clock::now() >= deadline || WaitForSingleObject(child.hProcess, 0) != WAIT_TIMEOUT) {
            return fail_at(__LINE__);
          }
        }
        DWORD ignored = 0;
        if (!GetOverlappedResult(pipe, &connection, &ignored, FALSE)) {
          return fail_at(__LINE__);
        }
      }
      cancel_connection.disable();
      BOOST_LOG(debug) << "Apollide notify: helper connected"sv;
      ULONG client_pid = 0, client_session = 0;
      if (!GetNamedPipeClientProcessId(pipe, &client_pid) || !GetNamedPipeClientSessionId(pipe, &client_session) || client_pid != child.dwProcessId || client_session != session) {
        BOOST_LOG(warning) << "Apollide notify: unexpected pipe client"sv;
        return fail_at(__LINE__);
      }
      std::vector<char> buffer(65536);
      while (console_current(stop, session) && WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT) {
        ResetEvent(event);
        OVERLAPPED read {};
        read.hEvent = event;
        DWORD bytes = 0;
        const bool read_now = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, &read) != FALSE;
        if (!read_now) {
          if (GetLastError() != ERROR_IO_PENDING) {
            break;
          }
          auto cancel_read = util::fail_guard([&] {
            CancelIoEx(pipe, &read);
            GetOverlappedResult(pipe, &read, &bytes, TRUE);
          });
          // The helper beats every 2 s; silence for 10 s means it is stuck
          const auto read_deadline = clock::now() + 10s;
          DWORD wait = WAIT_TIMEOUT;
          while ((wait = WaitForSingleObject(event, 100)) == WAIT_TIMEOUT) {
            if (!console_current(stop, session) || clock::now() >= read_deadline) {
              break;
            }
          }
          if (wait != WAIT_OBJECT_0 || !GetOverlappedResult(pipe, &read, &bytes, FALSE)) {
            break;
          }
          cancel_read.disable();
        }
        const auto message = parse_helper(std::string_view(buffer.data(), bytes));
        if (!message) {
          BOOST_LOG(warning) << "Apollide notify: malformed helper message ("sv << bytes << " bytes)"sv;
          break;
        }
        switch (message->type) {
          case helper_type_e::ready:
            on_helper_state(message->access);
            if (message->access == helper_state_e::ok) {
              BOOST_LOG(info) << "Apollide notify: reading notifications in console session "sv << session;
            } else {
              BOOST_LOG(warning) << "Apollide notify: Windows settings block notification access"sv;
            }
            break;
          case helper_type_e::heartbeat:
            {
              std::lock_guard lg {g_lock};
              g_idle_ms = message->idle_ms;
              g_idle_at = clock::now();
            }
            break;
          case helper_type_e::notification:
            on_notification(*message);
            break;
        }
      }
      return exit_code;
    }

    void supervise(std::stop_token stop) {
      BOOST_LOG(info) << "Apollide notify: watching for the console user's notifications"sv;
      int last_code = -1;
      while (!stop.stop_requested()) {
        const DWORD session = WTSGetActiveConsoleSessionId();
        if (session == 0 || session == 0xFFFFFFFF) {
          std::this_thread::sleep_for(2s);
          continue;
        }
        int code = -1;
        try {
          g_helper_exit = -1;
          run_helper(stop, session);
          code = g_helper_exit;
        } catch (const std::exception &e) {
          BOOST_LOG(error) << "Apollide notify: "sv << e.what();
        } catch (...) {
          BOOST_LOG(error) << "Apollide notify: helper run failed"sv;
        }
        if (code > 0 && code != 5 && code != last_code) {
          BOOST_LOG(warning) << "Apollide notify: helper stopped with code "sv << code;
        }
        last_code = code;
        // 5 = access denied in Windows settings: look again in a minute, quietly
        if (code != 5) {
          on_helper_state(helper_state_e::none);
        }
        const auto pause = code == 5 ? 60s : 5s;
        for (auto waited = 0ms; waited < pause && !stop.stop_requested(); waited += 250ms) {
          std::this_thread::sleep_for(250ms);
        }
      }
    }
#endif
  }  // namespace

  void start() {
    {
      std::lock_guard lg {g_lock};
      std::random_device rd;
      do {
        g_boot = rd();
      } while (g_boot == 0);
      load_locked();
    }
#ifdef _WIN32
    g_worker = std::jthread(supervise);
#endif
  }

  void on_subscribe(const void *session, std::string_view payload) {
    const auto sub = parse_subscribe(payload);
    if (!sub) {
      BOOST_LOG(warning) << "Apollide notify: ignoring malformed subscribe ("sv << payload.size() << " bytes)"sv;
      return;
    }
    std::lock_guard lg {g_lock};
    load_locked();
    auto &s = g_sessions[session];
    s.polled = clock::now();
    s.subscribed = sub->enabled;
    s.pending.clear();
    s.sent_forwarding = -1;
    s.sent_helper = -1;
    if (s.subscribed && g_enabled) {
      prune_locked();
      for (const auto i : select_since(g_items, g_boot, sub->boot, sub->last_seq, now_ms())) {
        s.pending.push_back(g_items[i].seq);
      }
    }
    BOOST_LOG(info) << "Apollide notify: client "sv << (sub->enabled ? "subscribed"sv : "unsubscribed"sv) << ", catching up "sv << s.pending.size();
  }

  std::vector<std::vector<std::uint8_t>> take_messages(const void *session) {
    std::vector<std::vector<std::uint8_t>> out;
    std::lock_guard lg {g_lock};
    auto &s = g_sessions[session];  // every streaming session counts for 'someone uses H1 remotely'
    s.polled = clock::now();
    if (!s.subscribed) {
      return out;
    }
    const int forwarding = g_enabled ? 1 : 0;
    const int helper = static_cast<int>(g_helper);
    if (forwarding != s.sent_forwarding || helper != s.sent_helper) {
      out.push_back(notify_state_packet(g_enabled, g_helper, g_boot));
      s.sent_forwarding = forwarding;
      s.sent_helper = helper;
    }
    const auto now = now_ms();
    // A few per pass so a catch-up does not crowd the control stream
    for (int sent = 0; sent < 4 && !s.pending.empty(); ++sent) {
      const auto seq = s.pending.front();
      s.pending.pop_front();
      for (const auto &item : g_items) {
        if (item.seq == seq) {
          out.push_back(notification_packet(g_boot, seq, static_cast<std::uint32_t>(std::max<std::int64_t>(0, now - item.arrived_ms)), item.n));
          break;
        }
      }
    }
    return out;
  }

  void forget(const void *session) {
    std::lock_guard lg {g_lock};
    if (g_sessions.erase(session) > 0 && !streaming_locked()) {
      g_stream_end = clock::now();
    }
  }

  query_reply_t query(bool has_cursor, std::uint32_t client_boot, std::uint32_t after) {
    std::lock_guard lg {g_lock};
    load_locked();
    prune_locked();
    query_reply_t reply {g_boot, g_seq, g_enabled, g_helper, {}};
    if (has_cursor) {
      for (const auto i : select_since(g_items, g_boot, client_boot, after, now_ms())) {
        reply.items.push_back(g_items[i]);
      }
    }
    return reply;
  }

  nlohmann::json settings() {
    std::lock_guard lg {g_lock};
    load_locked();
    nlohmann::json j {
      {"enabled", g_enabled},
      {"helper", static_cast<int>(g_helper)},
      {"localUse", local_use_locked()},
      {"streaming", streaming_locked()}
    };
    if (g_last.at != 0) {
      j["last"] = {{"app", g_last.app}, {"title", g_last.title}, {"at", g_last.at}, {"sent", g_last.sent}, {"why", g_last.why}};
    }
    return j;
  }

  void set_enabled(bool enabled) {
    std::lock_guard lg {g_lock};
    load_locked();
    if (g_enabled == enabled) {
      return;
    }
    g_enabled = enabled;
    if (!enabled) {
      // Off means nothing leaves H1, including what was kept for a client to pick up later
      g_items.clear();
      for (auto &[_, s] : g_sessions) {
        s.pending.clear();
      }
    }
    save_locked();
    BOOST_LOG(info) << "Apollide notify: forwarding "sv << (enabled ? "on"sv : "off"sv);
  }
}  // namespace apollide::notify

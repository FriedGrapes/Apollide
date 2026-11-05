/**
 * @file src/apollide/update_link.cpp
 * @brief Apollide: updates between the host and its Artemide clients (see update_link.h).
 */
#include "update_link.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "auto_update.h"
#include "src/logging.h"
#include "src/platform/common.h"

using namespace std::literals;

namespace apollide::link {
  namespace {
    namespace fs = std::filesystem;

    struct client_t {
      std::string name;
      std::string current;
      std::string latest;
      int state = 0;  ///< client_state_e
      bool requested = false;  ///< The host asked this client to update
      std::string requested_from;  ///< Client version when it was asked
      std::int64_t seen = 0;  ///< Unix seconds
    };

    struct session_t {
      std::string uuid;
      int sent_state = -1;
      std::string sent_latest;
      bool sent_request = false;
    };

    std::mutex g_lock;
    std::map<std::string, client_t> g_clients;  // by client uuid; guarded by g_lock
    std::unordered_map<const void *, session_t> g_sessions;
    bool g_loaded = false;

    fs::path store_path() {
      return platf::appdata() / "apollide_clients.json";
    }

    std::int64_t now_seconds() {
      return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    void load_locked() {
      if (g_loaded) {
        return;
      }
      g_loaded = true;
      std::ifstream file(store_path());
      const auto j = nlohmann::json::parse(file, nullptr, false);
      if (!j.is_object()) {
        return;
      }
      for (const auto &[uuid, c] : j.items()) {
        client_t client;
        client.name = c.value("name", "");
        client.current = c.value("current", "");
        client.latest = c.value("latest", "");
        client.requested = c.value("requested", false);
        client.requested_from = c.value("requestedFrom", "");
        client.seen = c.value("seen", 0LL);
        g_clients[uuid] = client;
      }
    }

    void save_locked() {
      nlohmann::json j = nlohmann::json::object();
      for (const auto &[uuid, c] : g_clients) {
        j[uuid] = {{"name", c.name}, {"current", c.current}, {"latest", c.latest}, {"requested", c.requested}, {"requestedFrom", c.requested_from}, {"seen", c.seen}};
      }
      const auto path = store_path();
      const auto tmp = path.string() + ".tmp";
      {
        std::ofstream file(tmp, std::ios::trunc);
        file << j.dump(2);
      }
      std::error_code ec;
      fs::rename(tmp, path, ec);
    }

    /// The client now runs a different version than when it was asked: the request is done
    void report_locked(client_t &client, const std::string &name, std::string_view current, std::string_view latest, int state) {
      if (!name.empty()) {
        client.name = name;
      }
      client.current = std::string(current);
      client.latest = std::string(latest);
      client.state = state;
      client.seen = now_seconds();
      if (client.requested && !client.current.empty() && client.current != client.requested_from) {
        client.requested = false;
        BOOST_LOG(info) << "Apollide link: "sv << client.name << " updated to "sv << client.current;
      }
    }
  }  // namespace

  void start() {
    auto_update::set_before_install([] {
      // The control loop sends the installing state to every session within a few hundred ms
      std::this_thread::sleep_for(1500ms);
    });
  }

  void on_client_hello(const void *session, const std::string &uuid, const std::string &device_name, std::string_view payload) {
    const auto hello = parse_client_hello(payload);
    if (!hello) {
      BOOST_LOG(warning) << "Apollide link: ignoring malformed client hello ("sv << payload.size() << " bytes)"sv;
      return;
    }
    std::lock_guard lg {g_lock};
    load_locked();
    auto &s = g_sessions[session];
    s.uuid = uuid;
    auto &client = g_clients[uuid];
    report_locked(client, hello->name.empty() ? device_name : hello->name, hello->current, hello->latest, static_cast<int>(hello->state));
    save_locked();
  }

  void on_update_host_now(const std::string &device_name) {
    BOOST_LOG(info) << "Apollide link: "sv << device_name << " asked the host to update now"sv;
    auto_update::install_now();
  }

  std::vector<std::vector<std::uint8_t>> take_messages(const void *session, const std::string &uuid) {
    std::vector<std::vector<std::uint8_t>> out;
    const auto status = auto_update::status();

    std::lock_guard lg {g_lock};
    load_locked();
    // Only clients that said hello (Artemide) understand these messages
    auto found = g_sessions.find(session);
    if (found == g_sessions.end()) {
      return out;
    }
    auto &s = found->second;
    s.uuid = uuid;
    const int state = static_cast<int>(status.state);
    if (state != s.sent_state || status.latest != s.sent_latest) {
      out.push_back(host_update(static_cast<std::uint8_t>(state), status.current, status.latest));
      s.sent_state = state;
      s.sent_latest = status.latest;
    }
    auto it = g_clients.find(uuid);
    if (it != g_clients.end() && it->second.requested && !s.sent_request) {
      out.push_back(update_client_now());
      s.sent_request = true;
    }
    return out;
  }

  void forget(const void *session) {
    std::lock_guard lg {g_lock};
    g_sessions.erase(session);
  }

  status_reply_t on_status_query(const std::string &uuid, const std::string &device_name, std::string_view client_version,
                                 std::string_view client_latest, int client_state) {
    const auto status = auto_update::status();
    std::lock_guard lg {g_lock};
    load_locked();
    bool requested = false;
    if (!uuid.empty()) {
      auto &client = g_clients[uuid];
      if (!client_version.empty()) {
        report_locked(client, device_name, client_version, client_latest, client_state);
        save_locked();
      }
      requested = client.requested;
    }
    return {status.current, status.latest, static_cast<int>(status.state), requested};
  }

  bool on_update_query(const std::string &device_name) {
    BOOST_LOG(info) << "Apollide link: "sv << device_name << " asked the host to update now"sv;
    return auto_update::install_now();
  }

  nlohmann::json snapshot() {
    const auto status = auto_update::status();
    std::lock_guard lg {g_lock};
    load_locked();
    nlohmann::json clients = nlohmann::json::array();
    for (const auto &[uuid, c] : g_clients) {
      bool connected = false;
      for (const auto &[_, s] : g_sessions) {
        connected = connected || s.uuid == uuid;
      }
      clients.push_back({{"uuid", uuid}, {"name", c.name}, {"current", c.current}, {"latest", c.latest}, {"state", c.state}, {"requested", c.requested}, {"connected", connected}, {"seen", c.seen}});
    }
    return {
      {"host", {{"current", status.current}, {"latest", status.latest}, {"state", static_cast<int>(status.state)}}},
      {"clients", clients}
    };
  }

  bool request_client_update(const std::string &uuid) {
    std::lock_guard lg {g_lock};
    load_locked();
    auto it = g_clients.find(uuid);
    if (it == g_clients.end()) {
      return false;
    }
    it->second.requested = true;
    it->second.requested_from = it->second.current;
    for (auto &[_, s] : g_sessions) {
      if (s.uuid == uuid) {
        s.sent_request = false;  // a connected client gets it on the next control loop pass
      }
    }
    save_locked();
    BOOST_LOG(info) << "Apollide link: asked "sv << it->second.name << " to update"sv;
    return true;
  }
}  // namespace apollide::link

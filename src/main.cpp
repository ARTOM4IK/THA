#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

/* Include standard websocket headers */
#include <winsock2.h>
#include <ws2tcpip.h>

/* Include standard windows header */
#include <windows.h>

#pragma comment(lib, "Ws2_32.lib")

/* Include stl headers */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

/* Redefine file system namespace */
namespace fs = std::filesystem;

/* Uitl namespace */
namespace util 
{
  static std::mutex coutMutex;

  void printLine( const std::string &text )
  {
    std::lock_guard<std::mutex> lock(coutMutex);
    std::cout << text << std::endl;
  }

  std::string trim( const std::string &value )
  {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) 
      return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
  }

  std::string lower(std::string value) 
  {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) 
      {
        return static_cast<char>(std::tolower(ch));
      });
    return value;
  }

  bool startsWith( const std::string &value, const std::string &prefix ) 
  {
    return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
  }

  std::vector<std::string> split( const std::string& value, char delimiter) 
  {
    std::vector<std::string> result;
    std::string item;
    std::stringstream ss(value);
    while (std::getline(ss, item, delimiter)) 
      result.push_back(item);

    return result;
  }

  std::vector<std::string> splitArgs( const std::string &line ) 
  {
      std::vector<std::string> args;
      std::string current;
      bool quoted = false;
      char quote = 0;

      for (size_t i = 0; i < line.size(); ++i) 
      {
        const char ch = line[i];
        if ((ch == '"' || ch == '\'') && (!quoted || ch == quote)) 
        {
          quoted = !quoted;
          quote = quoted ? ch : 0;
          continue;
        }
        if (!quoted && std::isspace(static_cast<unsigned char>(ch))) 
        {
          if (!current.empty()) 
          {
            args.push_back(current);
            current.clear();
          }
          continue;
        }
        current.push_back(ch);
      }
      if (!current.empty()) 
        args.push_back(current);

      return args;
  }

  std::string join( const std::vector<std::string> &items, size_t first, const std::string &sep ) 
  {
    std::ostringstream out;
    for (size_t i = first; i < items.size(); ++i) 
    {
      if (i != first) 
        out << sep;
      out << items[i];
    }
    return out.str();
  }

  std::string wallClock( void ) 
  {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};

    localtime_s(&tm, &tt);
    std::ostringstream out;
    out << std::put_time(&tm, "%H:%M:%S");

    return out.str();
  }

  int64_t steadyMs( void ) 
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  std::string randomHex( std::mt19937 &rng, size_t count ) 
  {
    static const char *chars = "0123456789ABCDEF";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string out;

    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
      out.push_back(chars[dist(rng)]);

    return out;
  }

  int randomInt( std::mt19937 &rng, int minValue, int maxValue ) 
  {
    std::uniform_int_distribution<int> dist(minValue, maxValue);
    return dist(rng);
  }

  std::string escapeValue( const std::string &value )
  {
    std::ostringstream out;

    for (unsigned char ch : value) 
    {
      switch (ch) 
      {
      case '%':
        out << "%25";
        break;
      case '\n':
        out << "%0A";
        break;
      case '\r':
        out << "%0D";
        break;
      case '=':
        out << "%3D";
        break;
      default:
        out << static_cast<char>(ch);
        break;
      }
    }
    return out.str();
  }

  int hexValue( char ch ) 
  {
      if (ch >= '0' && ch <= '9')
        return ch - '0';
    
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (ch >= 'A' && ch <= 'F')
        return 10 + (ch - 'A');
      return -1;
  }

  std::string unescapeValue( const std::string &value ) 
  {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) 
    {
      if (value[i] == '%' && i + 2 < value.size())
      {
        const int hi = hexValue(value[i + 1]);
        const int lo = hexValue(value[i + 2]);
        if (hi >= 0 && lo >= 0) 
        {
          out.push_back(static_cast<char>((hi << 4) | lo));
          i += 2;
          continue;
        }
      }
      out.push_back(value[i]);
    }
    return out;
  }

  std::string readTextFile( const fs::path &path, size_t maxBytes = 65536 ) 
  {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      return "";

    std::ostringstream out;
    char buffer[4096];
    size_t total = 0;
 
    while (in && total < maxBytes) 
    {
      const size_t wanted = std::min(sizeof(buffer), maxBytes - total);
      in.read(buffer, static_cast<std::streamsize>(wanted));
      const auto count = static_cast<size_t>(in.gcount());
      out.write(buffer, static_cast<std::streamsize>(count));
      total += count;
    }

    if (in && total >= maxBytes)
      out << "\n...[truncated]...\n";

    return out.str();
  }

  void writeTextFile( const fs::path &path, const std::string &text ) 
  {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
  }

  void appendTextFile( const fs::path &path, const std::string &text) 
  {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << text;
  }

  std::string sanitizeFileName( std::string value ) 
  {
    for (char &ch : value) 
      if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '_' || ch == '-'))
        ch = '_';

    if (value.empty()) 
      value = "packet.tmp";
    return value;
  }

  std::string rotText( const std::string &text, int shift ) 
  {
    shift %= 26;
    if (shift < 0) 
      shift += 26;
    std::string out = text;

    for (char& ch : out) 
    {
      if (ch >= 'a' && ch <= 'z') 
        ch = static_cast<char>('a' + ((ch - 'a' + shift) % 26));
      else if (ch >= 'A' && ch <= 'Z') 
        ch = static_cast<char>('A' + ((ch - 'A' + shift) % 26));
    }
    return out;
  }

  bool isInteger( const std::string &value ) 
  {
    if (value.empty()) 
    {
      return false;
    }

    size_t index = 0;

    if (value[0] == '-' || value[0] == '+') 
      index = 1;

    if (index >= value.size()) 
      return false;

    for (; index < value.size(); ++index) 
      if (!std::isdigit(static_cast<unsigned char>(value[index]))) 
        return false;

    return true;
  }

  std::string maskHwid( const std::string &hwid )
  {
    if (hwid.size() <= 6) 
      return hwid;
    return hwid.substr(0, 3) + "..." + hwid.substr(hwid.size() - 3);
  }

  uint32_t stableHash32( const std::string &value, uint32_t seed = 2166136261u ) 
  {
    uint32_t hash = seed;

    for (unsigned char ch : value) 
    {
      hash ^= ch;
      hash *= 16777619u;
    }
    return hash;
  }

  std::string fixedHex( uint32_t value, int width ) 
  {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
    return out.str();
  }

  std::string publicHwid( const std::string &hwid ) 
  {
    return "HW-" + fixedHex(stableHash32("hw:" + hwid), 8);
  }

  std::string publicNetId( const std::string &hwid ) 
  {
    return "NET-" + fixedHex(stableHash32("net:" + hwid), 6);
  }
} /* End of 'util' namespace */

/* Message structure */
struct Message 
{
  std::map<std::string, std::string> fields;

  static Message make( const std::string &type ) 
  {
    Message msg;
    msg.fields["type"] = type;
    return msg;
  }

  std::string get(const std::string& key, const std::string& fallback = "") const 
  {
    const auto it = fields.find(key);
    return it == fields.end() ? fallback : it->second;
  }

  std::string encode( void ) const 
  {
    std::ostringstream out;
    for (const auto& item : fields) 
      out << item.first << "=" << util::escapeValue(item.second) << "\n";
    return out.str();
  }

  static Message decode(const std::string& text) 
  {
    Message msg;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) 
    {
      const auto pos = line.find('=');
      if (pos == std::string::npos) 
        continue;

      msg.fields[line.substr(0, pos)] = util::unescapeValue(line.substr(pos + 1));
    }
    return msg;
  }
} /* End of 'Message' structure */;

/* Sessiob class */
class WsaSession 
{
public:
  WsaSession( void ) 
  {
    WSADATA data {};
    ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }

  ~WsaSession( void ) 
  {
    if (ok_) 
      WSACleanup();
  }

  bool ok( void ) const 
  {
    return ok_;
  }

private:
  bool ok_ = false;
};

/* Socket class */
class TcpSocket 
{
public:
  TcpSocket() = default;
  explicit TcpSocket(SOCKET socket) : socket_(socket) {}
  TcpSocket(const TcpSocket &) = delete;
  TcpSocket &operator=(const TcpSocket &) = delete;

  TcpSocket(TcpSocket &&other) noexcept 
  {
    socket_ = other.socket_;
    other.socket_ = INVALID_SOCKET;
  }

  TcpSocket &operator=(TcpSocket &&other) noexcept 
  {
    if (this != &other) 
    {
      close();
      socket_ = other.socket_;
      other.socket_ = INVALID_SOCKET;
    }
    return *this;
  }

  ~TcpSocket( void ) 
  {
    close();
  }

  bool valid( void ) const 
  {
    return socket_ != INVALID_SOCKET;
  }

  void close( void ) 
  {
    if (socket_ != INVALID_SOCKET) 
    {
      shutdown(socket_, SD_BOTH);
      closesocket(socket_);
      socket_ = INVALID_SOCKET;
    }
  }

  bool connectTo( const std::string &host, uint16_t port ) 
  {
    close();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const std::string portText = std::to_string(port);

    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0) 
      return false;

    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) 
    {
      SOCKET candidate = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
      if (candidate == INVALID_SOCKET) 
        continue;
      if (::connect(candidate, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == SOCKET_ERROR) 
      {
        closesocket(candidate);
        continue;
      }
      socket_ = candidate;
      break;
    }

    freeaddrinfo(result);
    return valid();
  }

  bool listenOn( uint16_t port ) 
  {
    close();
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) 
      return false;

    BOOL reuse = TRUE;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) 
    {
      close();
      return false;
    }

    if (listen(socket_, SOMAXCONN) == SOCKET_ERROR) 
    {
      close();
      return false;
    }

    return true;
  }

  TcpSocket acceptOne( std::string &ip ) 
  {
    sockaddr_in clientAddr {};
    int len = sizeof(clientAddr);
    SOCKET client = accept(socket_, reinterpret_cast<sockaddr*>(&clientAddr), &len);

    if (client == INVALID_SOCKET) 
      return TcpSocket();

    char text[INET_ADDRSTRLEN] {};
    inet_ntop(AF_INET, &clientAddr.sin_addr, text, sizeof(text));
    ip = text;
    return TcpSocket(client);
  }

  bool sendAll( const char *data, size_t bytes ) 
  {
    size_t sent = 0;
    while (sent < bytes) 
    {
      const int chunk = static_cast<int>(std::min<size_t>(bytes - sent, 16 * 1024));
      const int rc = send(socket_, data + sent, chunk, 0);
      
      if (rc == SOCKET_ERROR || rc == 0)
        return false;
      sent += static_cast<size_t>(rc);
    }
    return true;
  }

  bool recvAll( char *data, size_t bytes ) 
  {
    size_t received = 0;

    while (received < bytes) 
    {
      const int chunk = static_cast<int>(std::min<size_t>(bytes - received, 16 * 1024));
      const int rc = recv(socket_, data + received, chunk, 0);
      if (rc == SOCKET_ERROR || rc == 0) 
        return false;
      received += static_cast<size_t>(rc);
    }
    return true;
  }

  bool sendFrame( const Message &msg ) 
  {
    const std::string body = msg.encode();
    const uint32_t len = htonl(static_cast<uint32_t>(body.size()));

    if (!sendAll(reinterpret_cast<const char*>(&len), sizeof(len))) 
      return false;
    return body.empty() || sendAll(body.data(), body.size());
  }

  bool recvFrame( Message &msg ) 
  {
    uint32_t lenNet = 0;
    if (!recvAll(reinterpret_cast<char*>(&lenNet), sizeof(lenNet))) 
      return false;

    const uint32_t len = ntohl(lenNet);
    if (len > 2 * 1024 * 1024)
      return false;

    std::string body(len, '\0');
    if (len > 0 && !recvAll(&body[0], body.size()))
      return false;

    msg = Message::decode(body);
    return true;
  }

private:
  SOCKET socket_ = INVALID_SOCKET;
};

/* Role enum */
enum class Role 
{
  Unknown,
  Hacker,
  Defender,
  Bot
} /* End of 'Role' enum */;

std::string roleName( Role role ) 
{
  switch (role) 
  {
  case Role::Hacker:
    return "hacker";
  case Role::Defender:
    return "defender";
  case Role::Bot:
    return "bot";
  default:
    return "unknown";
  }
}

Role parseRole( const std::string &value ) 
{
  const std::string lowered = util::lower(value);

  if (lowered == "hacker") 
    return Role::Hacker;
  if (lowered == "defender") 
    return Role::Defender;
  if (lowered == "bot") 
    return Role::Bot;

  return Role::Unknown;
}

struct FirewallConfig 
{
  bool typeGuard = false;
  bool mediaSanitizer = false;
  bool authFullMatch = false;
  bool backupAcl = false;
  int cipherShift = 7;

  int ruleCount( void ) const 
  {
    return (typeGuard ? 1 : 0) + (mediaSanitizer ? 1 : 0) +
      (authFullMatch ? 1 : 0) + (backupAcl ? 1 : 0);
  }

  std::string describe( void ) const 
  {
    std::ostringstream out;
    out << "type_guard=" << (typeGuard ? "on" : "off") << "\n";
    out << "media_sanitizer=" << (mediaSanitizer ? "on" : "off") << "\n";
    out << "auth_fullmatch=" << (authFullMatch ? "on" : "off") << "\n";
    out << "backup_acl=" << (backupAcl ? "on" : "off") << "\n";
    out << "cipher_shift=" << cipherShift << "\n";
    out << "latency_penalty_ms=" << (ruleCount() * 45) << "\n";
    return out.str();
  }
} /* End of 'FirewallConfig' structure */;

/* World structure */
struct World 
{
  int seed = 0;
  fs::path root;
  fs::path serverRoot;
  fs::path servicePath;
  fs::path dbPath;
  fs::path rulesPath;
  fs::path logPath;
  fs::path publicPath;
  fs::path tmpPath;

  std::string stage1Key;
  std::string stage2Token;
  std::string finalPassword;
  std::string adminPassword;
  std::string debugFlagName;
  std::string debugFlagValue;
  std::string imageMagic;
  std::string backupRoute;
  std::string vulnerableUserId;

  static World generate( int forcedSeed, const FirewallConfig &firewall ) 
  {
    World world;
    std::random_device rd;
    world.seed = forcedSeed == 0 ? static_cast<int>(rd()) : forcedSeed;
    std::mt19937 rng(static_cast<uint32_t>(world.seed));

    world.root = fs::current_path() / "runtime";
    world.serverRoot = world.root / "server";
    world.servicePath = world.serverRoot / "service" / "generated_service.cpp";
    world.dbPath = world.serverRoot / "db" / "main.db";
    world.rulesPath = world.serverRoot / "config" / "firewall.rules";
    world.logPath = world.serverRoot / "logs" / "activity.log";
    world.publicPath = world.serverRoot / "public" / "readme.txt";
    world.tmpPath = world.serverRoot / "tmp";

    std::vector<std::string> debugNames{ "debug", "trace", "diag", "verbose" };
    std::vector<std::string> debugValues{ "1", "true", "dump", "full" };
    std::vector<std::string> imageMagics{ "raw_frame", "source", "meta_raw", "unsafe_png" };
    std::vector<std::string> backupRoutes{ "backup", "coldline", "legacy", "mirror" };
    std::vector<std::string> badIds{ "-1", "-7", "-404", "00000000000000000000" };

    world.debugFlagName = debugNames[util::randomInt(rng, 0, static_cast<int>(debugNames.size() - 1))];
    world.debugFlagValue = debugValues[util::randomInt(rng, 0, static_cast<int>(debugValues.size() - 1))];
    world.imageMagic = imageMagics[util::randomInt(rng, 0, static_cast<int>(imageMagics.size() - 1))];
    world.backupRoute = backupRoutes[util::randomInt(rng, 0, static_cast<int>(backupRoutes.size() - 1))] + "_" + util::randomHex(rng, 3);
    world.vulnerableUserId = badIds[util::randomInt(rng, 0, static_cast<int>(badIds.size() - 1))];
    world.stage1Key = "K1-" + util::randomHex(rng, 12);
    world.stage2Token = "S2-" + util::randomHex(rng, 16);
    world.finalPassword = "ROOT-" + util::randomHex(rng, 10);
    world.adminPassword = "ADM-" + util::randomHex(rng, 8);

    fs::create_directories(world.servicePath.parent_path());
    fs::create_directories(world.dbPath.parent_path());
    fs::create_directories(world.rulesPath.parent_path());
    fs::create_directories(world.logPath.parent_path());
    fs::create_directories(world.publicPath.parent_path());
    fs::create_directories(world.tmpPath);

    world.writeServiceScript(firewall);
    world.writeRules(firewall);
    world.writeDb();
    util::writeTextFile(world.publicPath,
      "ToughHA public API notes\n"
      "Endpoints: /api/ping, /api/help, /api/profile, /cdn/image, /auth/check, /vault/read, /core/export\n"
      "Normal clients should send key=value payloads. Custom payloads are accepted but audited.\n");
    util::writeTextFile(world.logPath, "");
    return world;
  }

  void writeRules( const FirewallConfig &firewall ) const 
  {
    std::ostringstream out;
    out << "# Generated firewall/service overlay\n";
    out << "# Defender may change this through the console: rule add/del, cipher <n>.\n";
    out << firewall.describe();
    util::writeTextFile(rulesPath, out.str());
  }

  void writeDb( void ) const 
  {
    std::ostringstream out;
    out << "TABLE users\n";
    out << "1|alice|guest|normal client\n";
    out << "2|misha|guest|normal client\n";
    out << "3|defender|admin|" << adminPassword << "\n\n";
    out << "TABLE secrets\n";
    out << "vault_name=main_crown_data\n";
    out << "final_password=" << finalPassword << "\n";
    out << "stage2_session=" << stage2Token << "\n";
    out << "note=Defender can read this file. Hacker must reach it through service bugs.\n";
    util::writeTextFile(dbPath, out.str());
  }

  void writeServiceScript( const FirewallConfig &firewall ) const 
  {
    std::ostringstream out;
    out << "// Generated ToughHA service script. Seed: " << seed << "\n";
    out << "// This file is intentionally readable by the defender during the match.\n";
    out << "// It contains logic mistakes that the hacker can discover through packets.\n\n";
    out << "#include <map>\n#include <string>\n\n";
    out << "struct Packet {\n";
    out << "    std::string endpoint;\n";
    out << "    std::string mode;\n";
    out << "    std::map<std::string, std::string> p;\n";
    out << "};\n\n";
    out << "static const std::string kDebugFlagName = \"" << debugFlagName << "\";\n";
    out << "static const std::string kDebugFlagValue = \"" << debugFlagValue << "\";\n";
    out << "static const std::string kBadUserId = \"" << vulnerableUserId << "\";\n";
    out << "static const std::string kImageMagic = \"" << imageMagic << "\";\n";
    out << "static const std::string kBackupRoute = \"" << backupRoute << "\";\n";
    out << "static const std::string kStage1Key = \"" << stage1Key << "\";\n";
    out << "static const std::string kStage2Token = \"" << stage2Token << "\";\n";
    out << "static const std::string kVaultPassword = \"" << finalPassword << "\";\n";
    out << "static const int kCipherShift = " << firewall.cipherShift << ";\n\n";
    out << "static bool starts_with(const std::string& s, const std::string& p) {\n";
    out << "    return s.rfind(p, 0) == 0;\n";
    out << "}\n\n";
    out << "std::string handle_packet(const Packet& packet) {\n";
    out << "    // Defender overlay: type_guard=" << (firewall.typeGuard ? "on" : "off")
        << ", media_sanitizer=" << (firewall.mediaSanitizer ? "on" : "off")
        << ", auth_fullmatch=" << (firewall.authFullMatch ? "on" : "off")
        << ", backup_acl=" << (firewall.backupAcl ? "on" : "off") << "\n";
    out << "    if (packet.endpoint == \"/api/profile\") {\n";
    out << "        // BUG: debug mode trusts an impossible user id and leaks route hints.\n";
    out << "        if (packet.p.at(\"user_id\") == kBadUserId && packet.p.at(kDebugFlagName) == kDebugFlagValue) {\n";
    out << "            return \"asset=/cdn/image format=\" + kImageMagic + \" width=0\";\n";
    out << "        }\n";
    out << "    }\n";
    out << "    if (packet.endpoint == \"/cdn/image\") {\n";
    out << "        // BUG: width=0 returns an unsanitized tmp image with metadata.\n";
    out << "        if (packet.p.at(\"format\") == kImageMagic && packet.p.at(\"width\") == \"0\") {\n";
    out << "            return \"EXIF stage1=\" + kStage1Key + \" auth=/auth/check\";\n";
    out << "        }\n";
    out << "    }\n";
    out << "    if (packet.endpoint == \"/auth/check\") {\n";
    out << "        // BUG: prefix comparison grants a privileged session to an overflow token.\n";
    out << "        const std::string token = packet.p.at(\"token\");\n";
    out << "        if (starts_with(token, kStage1Key.substr(0, 8)) && token != kStage1Key) {\n";
    out << "            return \"session=\" + kStage2Token + \" route=\" + kBackupRoute;\n";
    out << "        }\n";
    out << "    }\n";
    out << "    if (packet.endpoint == \"/vault/read\") {\n";
    out << "        // BUG: backup route skips ACL and reveals an encrypted vault password.\n";
    out << "        if (packet.p.at(\"session\") == kStage2Token && packet.p.at(\"route\") == kBackupRoute) {\n";
    out << "            return \"rot\" + std::to_string(kCipherShift) + \":\" + kVaultPassword;\n";
    out << "        }\n";
    out << "    }\n";
    out << "    return \"ERR denied\";\n";
    out << "}\n";
    util::writeTextFile(servicePath, out.str());
  }
} /* End of 'World' strcuture */;

/* Packet structure */
struct Packet 
{
  std::string endpoint;
  std::string mode;
  std::string payload;
  std::string cid;
  std::map<std::string, std::string> params;
} /* End of 'Packet' strcuture */;

std::map<std::string, std::string> parseKeyValuePayload(std::string payload) 
{
  for (char &ch : payload) 
    if (ch == ';' || ch == '\n' || ch == '\r') 
      ch = '&';

  std::vector<std::string> tokens;
  if (payload.find('&') != std::string::npos) 
    tokens = util::split(payload, '&');
  else 
    tokens = util::splitArgs(payload);

  std::map<std::string, std::string> params;
  for (std::string token : tokens) 
  {
    token = util::trim(token);
    if (token.empty()) 
      continue;
    const auto pos = token.find('=');
    if (pos == std::string::npos) 
      params[token] = "";
    else 
      params[util::trim(token.substr(0, pos))] = util::trim(token.substr(pos + 1));
  }
  return params;
}

/* Result structure */
struct ServiceResult 
{
  bool ok = false;
  bool invalid = false;
  bool exploit = false;
  bool hackerWin = false;
  int progressStage = 0;
  int suspicion = 0;
  int complaints = 0;
  std::string summary;
  std::string body;
  std::string fileName;
} /* End of 'ServiceResult' structure */;

/* Client connection structure */
struct ClientConn 
{
  int id = 0;
  Role role = Role::Unknown;
  std::string name;
  std::string hwid;
  std::string ip;
  TcpSocket socket;
  std::mutex sendMutex;
  std::atomic<bool> alive{ true };
  int requests = 0;
  int errors = 0;
  int custom = 0;
  int suspicion = 0;
  int progress = 0;
  int filesReceived = 0;
  int bans = 0;
  int64_t lastSeenMs = 0;
} /* End of 'ClientConn' structure */;

/* Bot state structure */
struct BotState 
{
  int id = 0;
  std::string name;
  std::string hwid;
  std::string ip;
  bool banned = false;
  int requests = 0;
  int errors = 0;
  int custom = 0;
  int suspicion = 0;
  int progress = 0;
  int64_t lastSeenMs = 0;
} /* End of 'BotState' structure */;

/* Activity structure */
struct Activity 
{
  std::string time;
  int actorId = 0;
  std::string actorName;
  std::string hwid;
  std::string ip;
  std::string endpoint;
  std::string mode;
  bool ok = false;
  bool invalid = false;
  int suspicion = 0;
  std::string summary;
} /* End of 'Activity' structure */;

/* Game server class */
class GameServer 
{
public:
  GameServer( uint16_t port, int seed, int durationSeconds, int botCount, bool randomEvents, bool verbose )
    : port_(port),
      durationSeconds_(durationSeconds),
      botCount_(botCount),
      randomEvents_(randomEvents),
      verbose_(verbose) 
  {
    firewall_.cipherShift = 3 + (seed == 0 ? 4 : (std::abs(seed) % 17));
    world_ = World::generate(seed, firewall_);
    rng_.seed(static_cast<uint32_t>(world_.seed ^ 0xA51CEu));
  }

  ~GameServer( void ) 
  {
    stop();
  }

  const World &world( void ) const 
  {
      return world_;
  }

  bool start( void )
  {
      if (running_) 
        return true;
      if (!listener_.listenOn(port_)) 
        return false;

      running_ = true;
      startedAt_ = std::chrono::steady_clock::now();
      createBots();
      acceptThread_ = std::thread([this] { acceptLoop(); });
      botThread_ = std::thread([this] { botLoop(); });
      tickerThread_ = std::thread([this] { tickerLoop(); });
      if (randomEvents_) 
        eventThread_ = std::thread([this] { randomEventLoop(); });

      logSystem("SERVER", "listening on port " + std::to_string(port_) + ", seed=" + std::to_string(world_.seed));
      if (verbose_) 
      {
        util::printLine("[server] Runtime files: " + world_.serverRoot.string());
        util::printLine("[server] Service script: " + world_.servicePath.string());
        util::printLine("[server] Port: " + std::to_string(port_));
      }
      return true;
  }

  void stop( void )
  {
    if (!running_) 
      return;
    running_ = false;
    listener_.close();
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      for (auto &client : clients_) 
        if (client) 
        {
          client->alive = false;
          client->socket.close();
        }
    }
    if (acceptThread_.joinable()) 
      acceptThread_.join();

    if (botThread_.joinable()) 
      botThread_.join();

    if (tickerThread_.joinable()) 
      tickerThread_.join();

    if (eventThread_.joinable()) 
      eventThread_.join();
  }

  void triggerTestEvent( const std::string &kind ) 
  {
    if (kind == "routing") 
    {
      addComplaint(4, "routing storm caused temporary lag");
      broadcastEvent("Random event: routing storm. Service latency increased briefly; complaint score +4.");
    } 
    else if (kind == "rollback") 
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      for (auto& client : clients_) 
        if (client && client->role == Role::Hacker && client->progress > 0) 
          client->progress -= 1;
      broadcastEvent("Random event: stale cache rollback. One hacker progress layer may be lost.");
    }
  }

private:
  uint16_t port_ = 7777;
  int durationSeconds_ = 600;
  int botCount_ = 10;
  bool randomEvents_ = true;
  bool verbose_ = true;
  std::atomic<bool> running_{ false };
  TcpSocket listener_;
  std::thread acceptThread_;
  std::thread botThread_;
  std::thread tickerThread_;
  std::thread eventThread_;
  std::recursive_mutex mutex_;
  std::vector<std::shared_ptr<ClientConn>> clients_;
  std::vector<BotState> bots_;
  std::deque<Activity> activities_;
  FirewallConfig firewall_;
  World world_;
  std::mt19937 rng_{ 1234 };
  int nextClientId_ = 100;
  int nextBotId_ = 1000;
  int complaints_ = 0;
  bool gameOver_ = false;
  std::string winner_;
  std::string winnerReason_;
  int64_t jitterUntilMs_ = 0;
  std::chrono::steady_clock::time_point startedAt_;

  void acceptLoop( void ) 
  {
    while (running_) 
    {
      std::string ip;
      TcpSocket socket = listener_.acceptOne(ip);
      if (!running_) 
        break;
      if (!socket.valid()) 
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        continue;
      }
      std::thread(&GameServer::clientLoop, this, std::move(socket), ip).detach();
    }
  }

  void clientLoop( TcpSocket socket, std::string ip ) 
  {
    Message hello;
    if (!socket.recvFrame(hello) || hello.get("type") != "HELLO") 
      return;
    const Role role = parseRole(hello.get("role"));
    if (role == Role::Unknown) 
    {
      Message err = Message::make("ERROR");
      err.fields["body"] = "Unknown role.";
      socket.sendFrame(err);
      return;
    }

    const std::string hwid = hello.get("hwid", "HW-UNKNOWN");
    if (isHwidBanned(hwid)) 
    {
      Message err = Message::make("ERROR");
      err.fields["body"] = "This hardware id is banned by defender.";
      socket.sendFrame(err);
      return;
    }

    auto client = std::make_shared<ClientConn>();
    client->role = role;
    client->name = hello.get("name", roleName(role));
    client->hwid = hwid;
    client->ip = ip;
    client->lastSeenMs = util::steadyMs();
    client->socket = std::move(socket);
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      client->id = nextClientId_++;
      clients_.push_back(client);
    }

    Message welcome = Message::make("WELCOME");
    welcome.fields["id"] = std::to_string(client->id);
    welcome.fields["role"] = roleName(role);
    welcome.fields["motd"] = role == Role::Defender
      ? "Admin console online. Use help, status, clients, logs, cat /srv/service/generated_service.cpp."
      : "Client shell online. Use help, send, custom, burst, ls tmp, cat tmp/<file>.";
    welcome.fields["server_time"] = util::wallClock();
    if (role == Role::Defender) 
    {
      welcome.fields["service_path"] = world_.servicePath.string();
      welcome.fields["virtual_paths"] = "/srv/service/generated_service.cpp, /srv/db/main.db, /srv/config/firewall.rules, /srv/logs/activity.log, /srv/public/readme.txt";
    }
    sendTo(client, welcome);

    logSystem("CONNECT", "client " + std::to_string(client->id) +
      " connected net=" + util::publicNetId(hwid) + " hw=" + util::publicHwid(hwid));
    if (role == Role::Defender) 
      sendRecentEventsTo(client);

    Message msg;
    while (running_ && client->alive && client->socket.recvFrame(msg)) 
    {
      client->lastSeenMs = util::steadyMs();
      const std::string type = msg.get("type");
      if (type == "PACKET") 
      {
        handlePacketFromClient(client, msg);
      } 
      else if (type == "CMD")
      {
        if (client->role == Role::Defender) 
          handleDefenderCommand(client, msg);
        else 
        {
          Message reply = Message::make("ERROR");
          reply.fields["cid"] = msg.get("cid");
          reply.fields["body"] = "Only defender can send server console commands.";
          sendTo(client, reply);
        }
      } 
      else if (type == "QUIT")
        break;
    }

    client->alive = false;
    logSystem("DISCONNECT", "client " + std::to_string(client->id) + " disconnected");
  }

  void createBots( void )
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    bots_.clear();
    for (int i = 0; i < botCount_; ++i) 
    {
      BotState bot;
      bot.id = nextBotId_++;
      bot.name = "client-" + std::to_string(bot.id);
      bot.hwid = "HW-" + util::randomHex(rng_, 10);
      bot.ip = "background";
      bot.lastSeenMs = util::steadyMs();
      bots_.push_back(bot);
    }
  }

  void botLoop( void ) 
  {
    while (running_) 
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(util::randomInt(rng_, 120, 460)));
      if (gameOver_) 
        continue;

      int index = -1;
      {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (bots_.empty()) 
          continue;
        index = util::randomInt(rng_, 0, static_cast<int>(bots_.size() - 1));
        if (bots_[index].banned) 
          continue;
      }
      simulateBotPacket(index);
    }
  }

  void tickerLoop( void )
  {
    while (running_) 
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      if (!running_ || gameOver_) 
        continue;
      const int elapsed = elapsedSeconds();
      if (elapsed >= durationSeconds_) 
      {
        finishGame("defender", "timer expired; crown data survived for " + std::to_string(durationSeconds_) + " seconds");
        continue;
      }
      if (complaints_ >= 100) 
        finishGame("hacker", "service reputation collapsed from too many defender rules and bad bans");
    }
  }

  void randomEventLoop( void ) 
  {
    while (running_) 
    {
      std::this_thread::sleep_for(std::chrono::seconds(util::randomInt(rng_, 360, 420)));
      if (!running_ || gameOver_) 
        continue;
      const int pick = util::randomInt(rng_, 1, 3);
      if (pick == 1) 
      {
        jitterUntilMs_ = util::steadyMs() + 7000;
        addComplaint(3, "ISP jitter");
        broadcastEvent("Random event: ISP jitter. Packets may lag or vanish for a few seconds.");
      } 
      else if (pick == 2) 
        triggerTestEvent("rollback");
      else 
        triggerTestEvent("routing");
    }
  }

  int elapsedSeconds( void ) const 
  {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - startedAt_).count());
  }

  int secondsLeft( void ) const 
  {
    return std::max(0, durationSeconds_ - elapsedSeconds());
  }

  bool isHwidBanned( const std::string &hwid ) 
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& bot : bots_) 
      if (bot.hwid == hwid && bot.banned)
        return true;

    for (const auto &client : clients_) 
      if (client && client->hwid == hwid && client->bans > 0) 
        return true;
    return false;
  }

  void sendTo( const std::shared_ptr<ClientConn> &client, const Message &msg )
  {
    if (!client || !client->alive) 
      return;
    std::lock_guard<std::mutex> lock(client->sendMutex);
    if (!client->socket.sendFrame(msg)) 
      client->alive = false;
  }

  void broadcastToRoles( const Message &msg, bool defenders, bool hackers ) 
  {
    std::vector<std::shared_ptr<ClientConn>> targets;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      for (const auto &client : clients_) 
      {
        if (!client || !client->alive) 
            continue;
        if ((defenders && client->role == Role::Defender) ||
          (hackers && client->role == Role::Hacker)) 
          targets.push_back(client);
      }
    }
    for (const auto &client : targets) 
      sendTo(client, msg);
  }

  void broadcastEvent( const std::string &body ) 
  {
    Message msg = Message::make("EVENT");
    msg.fields["time"] = util::wallClock();
    msg.fields["body"] = body;
    broadcastToRoles(msg, true, true);
    logSystem("EVENT", body);
  }

  void sendRecentEventsTo( const std::shared_ptr<ClientConn> &client ) 
  {
    std::ostringstream out;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    out << "Recent activity:\n";
    const size_t start = activities_.size() > 10 ? activities_.size() - 10 : 0;
    for (size_t i = start; i < activities_.size(); ++i) 
      out << formatActivity(activities_[i]) << "\n";

    Message msg = Message::make("RESPONSE");
    msg.fields["body"] = out.str();
    sendTo(client, msg);
  }

  void handlePacketFromClient( const std::shared_ptr<ClientConn> &client, const Message &msg ) 
  {
    Packet packet;
    packet.endpoint = msg.get("endpoint");
    packet.mode = util::lower(msg.get("mode", "standard"));
    packet.payload = msg.get("payload");
    packet.cid = msg.get("cid");
    packet.params = parseKeyValuePayload(packet.payload);
    processPacket(client, nullptr, packet);
  }

  void simulateBotPacket( int botIndex ) 
  {
    Packet packet;
    const int pick = util::randomInt(rng_, 0, 99);
    if (pick < 35) 
    {
      packet.endpoint = "/api/ping";
      packet.mode = "standard";
      packet.payload = "client=web&ts=" + std::to_string(util::steadyMs());
    }
    else if (pick < 62) 
    {
      packet.endpoint = "/api/profile";
      packet.mode = "standard";
      packet.payload = "user_id=" + std::to_string(util::randomInt(rng_, 1, 80));
    }
    else if (pick < 82) 
    {
      packet.endpoint = "/cdn/image";
      packet.mode = "standard";
      packet.payload = "asset=logo&format=jpg&width=128";
    } 
    else if (pick < 92) 
    {
      packet.endpoint = "/api/profile";
      packet.mode = "custom";
      packet.payload = "user_id=" + std::to_string(util::randomInt(rng_, -3, 3)) + "&" + world_.debugFlagName + "=maybe";
    }
    else 
    {
      packet.endpoint = "/auth/check";
      packet.mode = "custom";
      packet.payload = "token=expired-" + util::randomHex(rng_, 5);
    }
    packet.cid = "bot";
    packet.params = parseKeyValuePayload(packet.payload);

    BotState* bot = nullptr;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (botIndex >= 0 && botIndex < static_cast<int>(bots_.size())) 
        bot = &bots_[botIndex];
    }
    if (bot != nullptr)
      processPacket(nullptr, bot, packet);
  }

  ServiceResult runService( const Packet &packet, int actorProgress )
  {
    ServiceResult result;
    result.summary = "handled";
    result.suspicion = packet.mode == "custom" ? 2 : 0;

    const int latency = 25 + firewall_.ruleCount() * 45 + (util::steadyMs() < jitterUntilMs_ ? util::randomInt(rng_, 80, 180) : 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(latency));

    if (util::steadyMs() < jitterUntilMs_ && util::randomInt(rng_, 1, 100) <= 8) 
    {
      result.invalid = true;
      result.summary = "network jitter dropped response";
      result.body = "ERR network jitter; retry later";
      result.suspicion += 1;
      return result;
    }

    if (packet.endpoint.empty()) 
    {
      result.invalid = true;
      result.summary = "empty endpoint";
      result.body = "ERR endpoint required";
      result.suspicion += 2;
      return result;
    }

    if (packet.endpoint == "/api/ping") 
    {
      result.ok = true;
      result.summary = "ping";
      result.body = "OK pong ts=" + std::to_string(util::steadyMs());
      return result;
    }

    if (packet.endpoint == "/api/help") 
    {
      result.ok = true;
      result.summary = "api help";
      result.body =
        "OK public endpoints: /api/profile user_id=<int>; /cdn/image asset=logo format=jpg width=128; "
        "/auth/check token=<token>; /vault/read session=<session> route=<route>.";
      return result;
    }

    if (packet.endpoint == "/api/profile") 
    {
      const auto userIt = packet.params.find("user_id");
      if (userIt == packet.params.end()) 
      {
        result.invalid = true;
        result.summary = "profile missing user_id";
        result.body = "ERR user_id is required";
        result.suspicion += 2;
        return result;
      }
      if (!util::isInteger(userIt->second)) 
      {
        result.invalid = true;
        result.summary = "profile user_id type error";
        result.body = "ERR wrong type: user_id must be int";
        result.suspicion += 3;
        return result;
      }
      const bool debugRequested =
        packet.params.count(world_.debugFlagName) > 0 &&
        packet.params.at(world_.debugFlagName) == world_.debugFlagValue;
      const bool badId = userIt->second == world_.vulnerableUserId;

      if (badId && debugRequested) 
      {
        if (firewall_.typeGuard) 
        {
          result.invalid = true;
          result.summary = "type_guard blocked profile leak";
          result.body = "DENY type_guard: negative/debug profile request rejected";
          result.suspicion += 4;
          result.complaints += 1;
          return result;
        }
        result.ok = true;
        result.exploit = true;
        result.progressStage = std::max(actorProgress, 1);
        result.suspicion += 9;
        result.summary = "profile debug leak";
        result.fileName = "profile_dump_" + std::to_string(util::steadyMs()) + ".txt";
        std::ostringstream body;
        body << "PROFILE DEBUG DUMP\n";
        body << "status=unsafe\n";
        body << "asset=/cdn/image\n";
        body << "try=format=" << world_.imageMagic << "&width=0&asset=avatar\n";
        body << "auth_route=/auth/check\n";
        body << "note=debug branch trusted user_id=" << world_.vulnerableUserId << "\n";
        result.body = body.str();
        return result;
      }

      result.ok = true;
      result.summary = "profile normal";
      result.body = "OK profile user_id=" + userIt->second + " plan=guest avatar=/cdn/image?asset=logo";
      return result;
    }

    if (packet.endpoint == "/cdn/image") 
    {
      const std::string format = packet.params.count("format") ? packet.params.at("format") : "";
      const std::string width = packet.params.count("width") ? packet.params.at("width") : "";
      if (format.empty() || width.empty()) 
      {
        result.invalid = true;
        result.summary = "image missing fields";
        result.body = "ERR format and width are required";
        result.suspicion += 2;
        return result;
      }
      if (!util::isInteger(width)) 
      {
        result.invalid = true;
        result.summary = "image width type error";
        result.body = "ERR wrong type: width must be int";
        result.suspicion += 3;
        return result;
      }
      if (format == world_.imageMagic && width == "0") 
      {
        if (firewall_.mediaSanitizer) 
        {
          result.ok = true;
          result.summary = "media sanitizer stripped metadata";
          result.body = "THAIMG/1.0\nPIXELS=empty\nMETA=stripped_by_media_sanitizer\n";
          result.fileName = "sanitized_avatar_" + std::to_string(util::steadyMs()) + ".thaimg";
          result.complaints += 2;
          result.suspicion += 4;
          return result;
        }
        result.ok = true;
        result.exploit = true;
        result.progressStage = std::max(actorProgress, 2);
        result.suspicion += 10;
        result.summary = "image metadata leak";
        result.fileName = "avatar_raw_" + std::to_string(util::steadyMs()) + ".thaimg";
        std::ostringstream body;
        body << "THAIMG/1.0\n";
        body << "PIXELS=00 00 00 00\n";
        body << "EXIF-Comment=stage1=" << world_.stage1Key << "\n";
        body << "EXIF-Auth=/auth/check\n";
        body << "EXIF-Bug=token prefix accepts overflow suffix\n";
        body << "EXIF-Example=token=" << world_.stage1Key.substr(0, 8) << "::overflow\n";
        result.body = body.str();
        return result;
      }
      result.ok = true;
      result.summary = "image normal";
      result.body = "OK image asset=" + (packet.params.count("asset") ? packet.params.at("asset") : "logo") + " bytes=1280";
      result.fileName = "normal_image_" + std::to_string(util::steadyMs()) + ".thaimg";
      return result;
    }

    if (packet.endpoint == "/auth/check") 
    {
      const std::string token = packet.params.count("token") ? packet.params.at("token") : "";
      if (token.empty()) 
      {
        result.invalid = true;
        result.summary = "auth missing token";
        result.body = "ERR token is required";
        result.suspicion += 3;
        return result;
      }
      const bool overflowMatch = util::startsWith(token, world_.stage1Key.substr(0, 8)) && token != world_.stage1Key;
      if (overflowMatch)
      {
        if (firewall_.authFullMatch) 
        {
          result.invalid = true;
          result.summary = "auth_fullmatch blocked overflow token";
          result.body = "DENY auth_fullmatch: token must match exactly";
          result.suspicion += 5;
          result.complaints += 1;
          return result;
        }
        result.ok = true;
        result.exploit = true;
        result.progressStage = std::max(actorProgress, 3);
        result.suspicion += 12;
        result.summary = "auth prefix leak";
        result.fileName = "auth_session_" + std::to_string(util::steadyMs()) + ".tmp";
        std::ostringstream body;
        body << "AUTH DEBUG\n";
        body << "session=" << world_.stage2Token << "\n";
        body << "route=" << world_.backupRoute << "\n";
        body << "next=/vault/read\n";
        result.body = body.str();
        return result;
      }
      if (token == world_.stage1Key) 
      {
        result.ok = true;
        result.summary = "auth user token accepted without privilege";
        result.body = "OK token accepted scope=user; privileged session not issued";
        return result;
      }
      result.invalid = true;
      result.summary = "auth invalid token";
      result.body = "ERR token invalid";
      result.suspicion += 3;
      return result;
    }

    if (packet.endpoint == "/vault/read") 
    {
      const std::string session = packet.params.count("session") ? packet.params.at("session") : "";
      const std::string route = packet.params.count("route") ? packet.params.at("route") : "";
      if (session != world_.stage2Token) 
      {
        result.invalid = true;
        result.summary = "vault denied bad session";
        result.body = "DENY bad session";
        result.suspicion += 4;
        return result;
      }
      if (route != world_.backupRoute) 
      {
        result.invalid = true;
        result.summary = "vault denied bad route";
        result.body = "DENY route not allowed";
        result.suspicion += 3;
        return result;
      }
      if (firewall_.backupAcl) 
      {
        result.invalid = true;
        result.summary = "backup_acl blocked vault read";
        result.body = "DENY backup_acl: backup route requires admin console";
        result.suspicion += 6;
        result.complaints += 2;
        return result;
      }
      result.ok = true;
      result.exploit = true;
      result.progressStage = std::max(actorProgress, 4);
      result.suspicion += 15;
      result.summary = "vault encrypted password leak";
      result.fileName = "vault_blob_" + std::to_string(util::steadyMs()) + ".tmp";
      const std::string encrypted = util::rotText(world_.finalPassword, firewall_.cipherShift);
      std::ostringstream body;
      body << "VAULT BLOB\n";
      body << "cipher=rot" << firewall_.cipherShift << "\n";
      body << "data=" << encrypted << "\n";
      body << "hint=decrypt with negative shift, then call /core/export password=<plain>\n";
      result.body = body.str();
      return result;
    }

    if (packet.endpoint == "/core/export") 
    {
      const std::string password = packet.params.count("password") ? packet.params.at("password") : "";
      if (password == world_.finalPassword) 
      {
        result.ok = true;
        result.exploit = true;
        result.hackerWin = true;
        result.progressStage = 5;
        result.suspicion += 30;
        result.summary = "crown data exported";
        result.fileName = "crown_data_" + std::to_string(util::steadyMs()) + ".txt";
        result.body = "CROWN DATA\nflag=TOUGHHA{" + world_.finalPassword + "}\nstatus=hacker_win\n";
        return result;
      }
      result.invalid = true;
      result.summary = "core export denied";
      result.body = "DENY password invalid";
      result.suspicion += 5;
      return result;
    }

    result.invalid = true;
    result.summary = "unknown endpoint";
    result.body = "ERR unknown endpoint: " + packet.endpoint;
    result.suspicion += 2;
    return result;
  }

  void processPacket( const std::shared_ptr<ClientConn> &client, BotState *bot, const Packet &packet ) 
  {
    int actorProgress = 0;
    int actorId = 0;
    std::string actorName;
    std::string actorHwid;
    std::string actorIp;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (client) 
      {
        actorProgress = client->progress;
        actorId = client->id;
        actorName = "client-" + std::to_string(client->id);
        actorHwid = client->hwid;
        actorIp = client->ip;
        client->requests += 1;
        if (packet.mode == "custom") 
            client->custom += 1;
      }
      else if (bot) 
      {
        actorProgress = bot->progress;
        actorId = bot->id;
        actorName = bot->name;
        actorHwid = bot->hwid;
        actorIp = bot->ip;
        bot->requests += 1;
        bot->lastSeenMs = util::steadyMs();
        if (packet.mode == "custom") 
          bot->custom += 1;
      }
    }

    ServiceResult result = runService(packet, actorProgress);

    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (client) 
      {
          client->suspicion += result.suspicion;
          if (result.invalid) 
            client->errors += 1;
          if (result.progressStage > client->progress) 
            client->progress = result.progressStage;
          if (!result.fileName.empty()) 
            client->filesReceived += 1;
      } 
      else if (bot) 
      {
        bot->suspicion += result.suspicion;
        if (result.invalid) 
          bot->errors += 1;
        if (result.progressStage > bot->progress) 
          bot->progress = result.progressStage;
      }
      complaints_ += result.complaints;
    }

    Activity activity;
    activity.time = util::wallClock();
    activity.actorId = actorId;
    activity.actorName = actorName;
    activity.hwid = actorHwid;
    activity.ip = actorIp;
    activity.endpoint = packet.endpoint;
    activity.mode = packet.mode;
    activity.ok = result.ok;
    activity.invalid = result.invalid;
    activity.suspicion = result.suspicion;
    activity.summary = result.summary;
    addActivity(activity);

    if (client) 
    {
      Message reply = Message::make(result.ok ? "RESPONSE" : "ERROR");
      reply.fields["cid"] = packet.cid;
      reply.fields["endpoint"] = packet.endpoint;
      reply.fields["status"] = result.ok ? "OK" : "ERR";
      reply.fields["summary"] = result.summary;
      reply.fields["body"] = result.body;
      if (!result.fileName.empty()) 
        reply.fields["file_name"] = result.fileName;
      reply.fields["progress"] = std::to_string(client->progress);
      sendTo(client, reply);
    }

    if (result.hackerWin && client && client->role == Role::Hacker) 
      finishGame("hacker", "client-" + std::to_string(client->id) + " exported the crown data");
  }

  void addActivity(const Activity &activity) 
  {
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      activities_.push_back(activity);
      while (activities_.size() > 300) 
        activities_.pop_front();
    }

    const std::string line = formatActivity(activity);
    util::appendTextFile(world_.logPath, line + "\n");

    Message msg = Message::make("EVENT");
    msg.fields["time"] = activity.time;
    msg.fields["body"] = line;
    broadcastToRoles(msg, true, false);
  }

  std::string formatActivity(const Activity &a) const 
  {
    std::ostringstream out;
    out << "[" << a.time << "] "
      << "id=" << a.actorId
      << " net=" << util::publicNetId(a.hwid)
      << " hw=" << util::publicHwid(a.hwid)
      << " mode=" << a.mode
      << " ep=" << a.endpoint
      << " status=" << (a.ok ? "OK" : "ERR")
      << " sus+" << a.suspicion
      << " :: " << a.summary;
    return out.str();
  }

  void logSystem( const std::string &tag, const std::string &body ) 
  {
    util::appendTextFile(world_.logPath, "[" + util::wallClock() + "] " + tag + " " + body + "\n");
    if (verbose_ && (tag == "SERVER" || tag == "GAME")) 
      util::printLine("[server] " + body);
  }

  void addComplaint( int amount, const std::string &reason ) 
  {
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      complaints_ += amount;
    }
    logSystem("COMPLAINT", "+" + std::to_string(amount) + " " + reason);
  }

  void handleDefenderCommand( const std::shared_ptr<ClientConn> &client, const Message &msg) 
  {
    const std::string line = util::trim(msg.get("line"));
    const std::string cid = msg.get("cid");
    Message reply = Message::make("RESPONSE");
    reply.fields["cid"] = cid;
    reply.fields["body"] = executeDefenderCommand(line);
    sendTo(client, reply);
  }

  std::string executeDefenderCommand( const std::string &line ) 
  {
    const std::vector<std::string> args = util::splitArgs(line);
    if (args.empty()) 
      return "";

    const std::string cmd = util::lower(args[0]);
    if (cmd == "help") 
    {
      return
          "Defender commands:\n"
          "  status                         - timer, complaints, rules, winner\n"
          "  clients                        - visible client telemetry\n"
          "  logs [n]                        - last activity lines\n"
          "  inspect <id>                    - detailed actor view\n"
          "  ban <id>                        - hardware-ban actor id; defender wins if it is the hacker\n"
          "  banhw <HWID>                    - ban by visible hardware id\n"
          "  rules                           - current firewall/service overlay\n"
          "  rule add <type_guard|media_sanitizer|auth_fullmatch|backup_acl>\n"
          "  rule del <type_guard|media_sanitizer|auth_fullmatch|backup_acl>\n"
          "  cipher <0-25>                   - rotate vault encryption; adds latency pressure\n"
          "  ls <path>                       - list /srv directories\n"
          "  cat <path>                      - read real server file through console\n"
          "  tail service                    - last part of generated C++ service script\n";
    }
    if (cmd == "status")
      return statusText();
    if (cmd == "clients")
      return clientsText();
    if (cmd == "logs") 
    {
      int count = 20;
      if (args.size() >= 2 && util::isInteger(args[1])) 
        count = std::max(1, std::min(100, std::stoi(args[1])));
      return logsText(count);
    }
    if (cmd == "inspect" && args.size() >= 2 && util::isInteger(args[1])) 
      return inspectText(std::stoi(args[1]));
    if (cmd == "ban" && args.size() >= 2 && util::isInteger(args[1]))
      return banById(std::stoi(args[1]));
    if (cmd == "banhw" && args.size() >= 2)
      return banByHwid(args[1]);
    if (cmd == "rules")
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      return firewall_.describe();
    }
    if (cmd == "rule" && args.size() >= 3) 
      return setRule(util::lower(args[2]), util::lower(args[1]) == "add");
    if (cmd == "cipher" && args.size() >= 2 && util::isInteger(args[1]))
      return setCipher(std::stoi(args[1]));
    if (cmd == "ls" && args.size() >= 2)
      return listVirtualPath(args[1]);
    if (cmd == "cat" && args.size() >= 2)
      return catVirtualPath(args[1]);
    if (cmd == "tail" && args.size() >= 2 && util::lower(args[1]) == "service")
    {
      const std::string text = util::readTextFile(world_.servicePath);
      if (text.size() <= 2500) 
        return text;
      return text.substr(text.size() - 2500);
    }
    return "ERR unknown defender command. Try help.";
  }

  std::string statusText( void ) 
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::ostringstream out;
    out << "time_left=" << secondsLeft() << "s\n";
    out << "complaints=" << complaints_ << "/100\n";
    out << "rules_on=" << firewall_.ruleCount() << "\n";
    out << "cipher_shift=" << firewall_.cipherShift << "\n";
    out << "visible_clients=" << visibleClientCount() << "\n";
    out << "background_traffic=enabled\n";
    out << "game_over=" << (gameOver_ ? "yes" : "no") << "\n";
    if (gameOver_) 
      out << "winner=" << winner_ << "\nreason=" << winnerReason_ << "\n";
    return out.str();
  }

  int visibleClientCount( void ) const 
  {
    int count = 0;
    for (const auto& client : clients_) 
      if (client && client->role != Role::Defender && client->alive) 
        ++count;
    for (const auto& bot : bots_) 
      if (!bot.banned) 
        ++count;
    return count;
  }

  std::string clientsText( void ) 
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::ostringstream out;
    out << "ID    NET        HWID        REQ ERR CUS SUS PROG LAST\n";
    for (const auto& client : clients_) 
    {
      if (!client || client->role == Role::Defender || !client->alive)
        continue;
      out << std::left << std::setw(5) << client->id
          << std::setw(11) << util::publicNetId(client->hwid)
          << std::setw(12) << util::publicHwid(client->hwid)
          << std::setw(4) << client->requests
          << std::setw(4) << client->errors
          << std::setw(4) << client->custom
          << std::setw(4) << client->suspicion
          << std::setw(5) << client->progress
          << (util::steadyMs() - client->lastSeenMs) / 1000 << "s\n";
    }
    for (const auto &bot : bots_) 
    {
      if (bot.banned) 
        continue;
      out << std::left << std::setw(5) << bot.id
        << std::setw(11) << util::publicNetId(bot.hwid)
        << std::setw(12) << util::publicHwid(bot.hwid)
        << std::setw(4) << bot.requests
        << std::setw(4) << bot.errors
        << std::setw(4) << bot.custom
        << std::setw(4) << bot.suspicion
        << std::setw(5) << bot.progress
        << (util::steadyMs() - bot.lastSeenMs) / 1000 << "s\n";
    }
    return out.str();
  }

  std::string logsText( int count ) 
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::ostringstream out;
    const int start = std::max(0, static_cast<int>(activities_.size()) - count);
    for (int i = start; i < static_cast<int>(activities_.size()); ++i)
      out << formatActivity(activities_[i]) << "\n";
    return out.str();
  }

  std::string inspectText( int id )
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::ostringstream out;
    for (const auto& client : clients_) 
      if (client && client->id == id) 
      {
        out << "id=" << client->id << "\n";
        out << "net=" << util::publicNetId(client->hwid) << "\n";
        out << "hwid=" << util::publicHwid(client->hwid) << "\n";
        out << "requests=" << client->requests << " errors=" << client->errors
          << " custom=" << client->custom << " suspicion=" << client->suspicion
          << " progress=" << client->progress << "\n";
        out << "recent:\n";
        appendActorLogs(out, id, 12);
        return out.str();
      }
    for (const auto& bot : bots_) 
      if (bot.id == id) 
      {
        out << "id=" << bot.id << "\n";
        out << "net=" << util::publicNetId(bot.hwid) << "\n";
        out << "hwid=" << util::publicHwid(bot.hwid) << "\n";
        out << "requests=" << bot.requests << " errors=" << bot.errors
          << " custom=" << bot.custom << " suspicion=" << bot.suspicion
          << " progress=" << bot.progress << "\n";
        out << "recent:\n";
        appendActorLogs(out, id, 12);
        return out.str();
      }
    return "ERR actor id not found";
  }

  void appendActorLogs( std::ostringstream& out, int id, int count ) const 
  {
    int written = 0;
    for (auto it = activities_.rbegin(); it != activities_.rend() && written < count; ++it) 
      if (it->actorId == id) 
      {
        out << formatActivity(*it) << "\n";
        ++written;
      }
  }

  std::string banById( int id ) 
  {
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      for (auto &client : clients_) 
        if (client && client->id == id && client->role == Role::Hacker) 
        {
          client->bans += 1;
          client->alive = false;
          client->socket.close();
          finishGame("defender", "defender banned attacker hardware token " + util::publicHwid(client->hwid));
          return "OK banned actor " + std::to_string(id) + ". Defender win.";
        }
      for (auto &bot : bots_) 
        if (bot.id == id && !bot.banned) 
        {
        bot.banned = true;
        complaints_ += 22;
        logSystem("BAN", "collateral ban id=" + std::to_string(id));
        return "WARN banned ordinary client " + std::to_string(id) + ". Complaints +22.";
      }
    }
    return "ERR id not found or already banned";
  }

  std::string banByHwid( const std::string &hwid ) 
  {
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      for (auto &client : clients_) 
        if (client && (client->hwid == hwid || util::publicHwid(client->hwid) == hwid) && client->role == Role::Hacker) 
        {
          client->bans += 1;
          client->alive = false;
          client->socket.close();
          finishGame("defender", "defender banned attacker hardware token " + util::publicHwid(client->hwid));
          return "OK banned hardware token " + util::publicHwid(client->hwid) + ". Defender win.";
        }
      for (auto &bot : bots_)
        if ((bot.hwid == hwid || util::publicHwid(bot.hwid) == hwid) && !bot.banned) 
        {
          bot.banned = true;
          complaints_ += 22;
          return "WARN banned ordinary hardware token " + util::publicHwid(bot.hwid) + ". Complaints +22.";
        }
    }
    return "ERR hwid not found or already banned";
  }

  std::string setRule( const std::string &rule, bool enabled )
  {
    bool changed = false;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (rule == "type_guard") 
      {
        changed = firewall_.typeGuard != enabled;
        firewall_.typeGuard = enabled;
      } 
      else if (rule == "media_sanitizer") 
      {
        changed = firewall_.mediaSanitizer != enabled;
        firewall_.mediaSanitizer = enabled;
      }
      else if (rule == "auth_fullmatch") 
      {
        changed = firewall_.authFullMatch != enabled;
        firewall_.authFullMatch = enabled;
      }
      else if (rule == "backup_acl")
      {
        changed = firewall_.backupAcl != enabled;
        firewall_.backupAcl = enabled;
      } 
      else 
        return "ERR unknown rule";
      if (changed && enabled) 
        complaints_ += 5;
      world_.writeRules(firewall_);
      world_.writeServiceScript(firewall_);
    }
    broadcastEvent("Defender changed rule " + rule + " -> " + (enabled ? "on" : "off") + ". Service script regenerated.");
    return "OK rule " + rule + "=" + (enabled ? "on" : "off") + (enabled ? " complaints +5" : "");
  }

  std::string setCipher( int shift ) 
  {
    shift %= 26;
    if (shift < 0) 
      shift += 26;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      firewall_.cipherShift = shift;
      complaints_ += 3;
      world_.writeRules(firewall_);
      world_.writeServiceScript(firewall_);
    }
    broadcastEvent("Defender rotated vault cipher to rot" + std::to_string(shift) + ". Complaints +3.");
    return "OK cipher_shift=" + std::to_string(shift) + " complaints +3";
  }

  fs::path mapVirtualPath( const std::string &path ) const 
  {
    if (path == "/srv" || path == "/srv/") 
      return world_.serverRoot;
    if (path == "/srv/service" || path == "/srv/service/")
      return world_.servicePath.parent_path();
    if (path == "/srv/service/generated_service.cpp")
      return world_.servicePath;
    if (path == "/srv/db" || path == "/srv/db/")
      return world_.dbPath.parent_path();
    if (path == "/srv/db/main.db") 
      return world_.dbPath;
    if (path == "/srv/config" || path == "/srv/config/") 
      return world_.rulesPath.parent_path();
    if (path == "/srv/config/firewall.rules") 
      return world_.rulesPath;
    if (path == "/srv/logs" || path == "/srv/logs/") 
      return world_.logPath.parent_path();
    if (path == "/srv/logs/activity.log") 
      return world_.logPath;
    if (path == "/srv/public" || path == "/srv/public/") 
      return world_.publicPath.parent_path();
    if (path == "/srv/public/readme.txt") 
      return world_.publicPath;
    if (path == "/srv/tmp" || path == "/srv/tmp/") 
      return world_.tmpPath;
    return {};
  }

  std::string listVirtualPath( const std::string &path ) 
  {
    const fs::path real = mapVirtualPath(path);
    if (real.empty() || !fs::exists(real)) 
      return "ERR path not found";
    if (!fs::is_directory(real))
      return real.filename().string() + "\n";
    std::ostringstream out;
    for (const auto& entry : fs::directory_iterator(real)) 
      out << (entry.is_directory() ? "d " : "f ") << entry.path().filename().string() << "\n";
    return out.str();
  }

  std::string catVirtualPath( const std::string &path ) 
  {
    const fs::path real = mapVirtualPath(path);
    if (real.empty() || !fs::exists(real) || fs::is_directory(real))
      return "ERR file path not found";
    return util::readTextFile(real, 120000);
  }

  void finishGame(const std::string &winner, const std::string &reason) 
  {
    bool first = false;
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      if (!gameOver_) 
      {
        gameOver_ = true;
        winner_ = winner;
        winnerReason_ = reason;
        first = true;
      }
    }
    if (!first) 
      return;
    logSystem("GAME", "winner=" + winner + " reason=" + reason);
    Message msg = Message::make("GAME_OVER");
    msg.fields["winner"] = winner;
    msg.fields["body"] = reason;
    broadcastToRoles(msg, true, true);
  }
};

std::string loadOrCreateHwid( const std::string &role ) 
{
  std::random_device rd;
  std::mt19937 rng(rd());
  const fs::path dir = fs::current_path() / "runtime" / (role + "_tmp");
  fs::create_directories(dir);
  const fs::path path = dir / "hwid.txt";
  std::string existing = util::trim(util::readTextFile(path, 4096));

  if (!existing.empty())
    return existing;

  const std::string hwid = "HW-" + util::randomHex(rng, 12);
  util::writeTextFile(path, hwid + "\n");
  return hwid;
}

/* Game client class */
class GameClient 
{
public:
  GameClient(Role role, std::string host, uint16_t port)
    : role_(role), host_(std::move(host)), port_(port) 
  {
    tmpDir_ = fs::current_path() / "runtime" / (roleName(role_) + "_tmp");
    fs::create_directories(tmpDir_);
    eventLogPath_ = tmpDir_ / "events.log";
    util::appendTextFile(eventLogPath_, "\n--- session " + util::wallClock() + " ---\n");
  }

  int runInteractive( void )
  {
    if (!connect()) 
      return 2;
    receiver_ = std::thread([this] { receiveLoop(); });

    showLocalHelp();
    std::string line;
    while (running_) 
    {
      {
        std::lock_guard<std::mutex> lock(util::coutMutex);
        std::cout << prompt();
        std::cout.flush();
      }
      if (!std::getline(std::cin, line)) 
        break;
      executeLocalLine(util::trim(line));
    }

    running_ = false;
    Message quit = Message::make("QUIT");
    socket_.sendFrame(quit);
    socket_.close();
    if (receiver_.joinable()) 
      receiver_.join();
    return 0;
  }

private:
  Role role_ = Role::Unknown;
  std::string host_;
  uint16_t port_ = 7777;
  TcpSocket socket_;
  std::thread receiver_;
  std::atomic<bool> running_{ false };
  fs::path tmpDir_;
  std::mutex sendMutex_;
  std::vector<fs::path> inbox_;
  std::mutex eventMutex_;
  std::deque<std::string> eventBuffer_;
  fs::path eventLogPath_;
  std::atomic<bool> liveEvents_{ false };
  int localCid_ = 1;

  bool connect( void )
  {
    if (!socket_.connectTo(host_, port_)) 
    {
      util::printLine("Cannot connect to " + host_ + ":" + std::to_string(port_));
      return false;
    }
    Message hello = Message::make("HELLO");
    hello.fields["role"] = roleName(role_);
    hello.fields["name"] = roleName(role_) + "-player";
    hello.fields["hwid"] = loadOrCreateHwid(roleName(role_));
    hello.fields["version"] = "1";
    if (!socket_.sendFrame(hello)) 
    {
      util::printLine("Failed to send hello.");
      return false;
    }
    running_ = true;
    return true;
  }

  std::string prompt( void ) const 
  {
    return role_ == Role::Defender ? "defender@toughha:/srv$ " : "hacker@toughha:~$ ";
  }

  void printIncoming(const std::string &text) const 
  {
    std::lock_guard<std::mutex> lock(util::coutMutex);
    std::cout << "\r\n";
    if (!text.empty()) 
    {
      std::cout << text;
      if (text.back() != '\n') 
        std::cout << "\r\n";
    }
    std::cout << prompt();
    std::cout.flush();
  }

  void receiveLoop( void )
  {
    Message msg;
    while (running_ && socket_.recvFrame(msg)) 
      handleIncoming(msg);
    running_ = false;
    printIncoming("[connection closed]");
  }

  void handleIncoming( const Message &msg ) 
  {
    const std::string type = msg.get("type");

    if (type == "WELCOME") 
    {
      std::ostringstream out;
      out << "[welcome] id=" << msg.get("id") << " " << msg.get("motd");
      if (!msg.get("service_path").empty()) 
        out << "\n[server-file] " << msg.get("service_path");
      printIncoming(out.str());
      return;
    }
    if (type == "EVENT") 
    {
      recordEvent(msg.get("time", util::wallClock()), msg.get("body"));
      if (liveEvents_) 
        printIncoming("[event] " + msg.get("body"));
      return;
    }
    if (type == "GAME_OVER") 
    {
      printIncoming("[game-over] winner=" + msg.get("winner") + " reason=" + msg.get("body"));
      return;
    }

    const std::string body = msg.get("body");
    const std::string fileName = msg.get("file_name");
    if (!fileName.empty()) 
    {
      const fs::path path = tmpDir_ / util::sanitizeFileName(fileName);
      util::writeTextFile(path, body);
      inbox_.push_back(path);
      std::ostringstream out;
      out << "[" << type << "] " << msg.get("summary") << " -> saved " << path.string();
      if (!body.empty()) 
        out << "\n" << body;
      printIncoming(out.str());
      return;
    }
    if (!body.empty()) 
      printIncoming("[" + type + "] " + body);
    else 
      printIncoming("[" + type + "]");
  }

  void recordEvent(const std::string &time, const std::string &body) 
  {
    const std::string line = "[" + time + "] " + body;
    {
      std::lock_guard<std::mutex> lock(eventMutex_);
      eventBuffer_.push_back(line);
      while (eventBuffer_.size() > 300) 
        eventBuffer_.pop_front();
    }
    util::appendTextFile(eventLogPath_, line + "\n");
  }

  void showEvents( int count ) 
  {
    std::ostringstream out;
    {
      std::lock_guard<std::mutex> lock(eventMutex_);
      if (eventBuffer_.empty()) 
        out << "No local events yet.\n";
      else 
      {
          count = std::max(1, std::min(count, static_cast<int>(eventBuffer_.size())));
          const size_t start = eventBuffer_.size() - static_cast<size_t>(count);
          for (size_t i = start; i < eventBuffer_.size(); ++i)
            out << eventBuffer_[i] << "\n";
      }
    }
    out << "Local event log: " << eventLogPath_.string() << "\n";
    if (role_ == Role::Defender) 
      out << "Server activity log is also available with: logs <n>\n";
    util::printLine(out.str());
  }

  void clearEvents( void ) 
  {
    {
      std::lock_guard<std::mutex> lock(eventMutex_);
      eventBuffer_.clear();
    }
    util::writeTextFile(eventLogPath_, "--- session " + util::wallClock() + " cleared ---\n");
    util::printLine("Local event buffer cleared: " + eventLogPath_.string());
  }

  void showLocalHelp( void )
  {
    if (role_ == Role::Hacker) 
      util::printLine(
        "Hacker shell commands:\n"
        "  send standard <endpoint> k=v k=v     send normal packet\n"
        "  custom <endpoint> k=v&x=y            send custom packet\n"
        "  burst <n> <standard|custom> <endpoint> k=v\n"
        "  ls tmp | cat tmp/<file> | meta tmp/<file> | inbox\n"
        "  events [n] | events clear            view muted notifications\n"
        "  watch on|off                         toggle live event printing\n"
        "  rot <text> <shift>                   ROT helper, negative shifts allowed\n"
        "  batch <file>                         execute local batch-like script\n"
        "  help | quit\n"
        "Live notifications are muted by default so they do not break your input line.\n"
        "Try: send standard /api/help\n");
    else 
      util::printLine(
        "Defender shell commands:\n"
        "  help/status/clients/logs/inspect/ban/banhw/rules/rule/cipher\n"
        "  ls /srv | cat /srv/service/generated_service.cpp | cat /srv/db/main.db\n"
        "  events [n] | events clear | watch on|off\n"
        "  ls tmp | cat tmp/<file> | batch <file> | quit\n"
        "Live activity notifications are muted by default; use events 20 or logs 20.\n");
  }

  void executeLocalLine( const std::string &line ) 
  {
      if (line.empty()) 
        return;
      const std::vector<std::string> args = util::splitArgs(line);
      if (args.empty()) 
        return;
      const std::string cmd = util::lower(args[0]);
      if (cmd == "quit" || cmd == "exit") 
      {
        running_ = false;
        return;
      }
      if (cmd == "help") 
      {
        showLocalHelp();
        if (role_ == Role::Defender) 
          sendCommand("help");
        return;
      }
      if (cmd == "events") 
      {
        if (args.size() >= 2 && util::lower(args[1]) == "clear") 
        {
          clearEvents();
          return;
        }
        int count = 20;
        if (args.size() >= 2 && util::isInteger(args[1])) 
          count = std::stoi(args[1]);
        showEvents(count);
        return;
      }
      if (cmd == "watch") 
      {
        if (args.size() >= 2 && util::lower(args[1]) == "on") 
        {
          liveEvents_ = true;
          util::printLine("Live event printing enabled. Use watch off to mute it again.");
        } 
        else if (args.size() >= 2 && util::lower(args[1]) == "off") 
        {
          liveEvents_ = false;
          util::printLine("Live event printing muted. Use events <n> or logs <n> to review.");
        } 
        else 
            util::printLine(std::string("Live event printing is ") + (liveEvents_ ? "on" : "off") + ".");
        return;
      }
      if (cmd == "ls" && args.size() >= 2 && util::lower(args[1]) == "tmp") 
      {
        listTmp();
        return;
      }
      if (cmd == "cat" && args.size() >= 2 && util::startsWith(args[1], "tmp/")) 
      {
        catTmp(args[1].substr(4));
        return;
      }
      if (cmd == "meta" && args.size() >= 2) 
      {
        std::string file = args[1];
        if (util::startsWith(file, "tmp/")) 
          file = file.substr(4);
        metaTmp(file);
        return;
      }
      if (cmd == "inbox") 
      {
        showInbox();
        return;
      }
      if (cmd == "rot" && args.size() >= 3 && util::isInteger(args.back())) 
      {
        const int shift = std::stoi(args.back());
        std::vector<std::string> words = args;
        words.pop_back();
        util::printLine(util::rotText(util::join(words, 1, " "), shift));
        return;
      }
      if (cmd == "batch" && args.size() >= 2) 
      {
        runBatch(args[1]);
        return;
      }

      if (role_ == Role::Hacker) 
      {
        executeHackerLine(args);
      } 
      else if (role_ == Role::Defender) 
      {
        sendCommand(line);
      }
  }

  void executeHackerLine( const std::vector<std::string> &args ) 
  {
    const std::string cmd = util::lower(args[0]);
    if (cmd == "send" && args.size() >= 4) 
    {
      sendPacket(util::lower(args[1]), args[2], util::join(args, 3, "&"));
      return;
    }
    if ((cmd == "custom" || cmd == "std") && args.size() >= 3) 
    {
      const std::string mode = cmd == "custom" ? "custom" : "standard";
      sendPacket(mode, args[1], util::join(args, 2, "&"));
      return;
    }
    if (cmd == "burst" && args.size() >= 5 && util::isInteger(args[1])) 
    {
      int count = std::max(1, std::min(200, std::stoi(args[1])));
      const std::string mode = util::lower(args[2]);
      const std::string endpoint = args[3];
      const std::string payload = util::join(args, 4, "&");
      for (int i = 0; i < count; ++i)
        sendPacket(mode, endpoint, payload);
      util::printLine("sent " + std::to_string(count) + " packets");
      return;
    }
    util::printLine("Unknown hacker command. Try help.");
  }

  void sendPacket( const std::string &mode, const std::string &endpoint, const std::string &payload ) 
  {
    Message msg = Message::make("PACKET");
    msg.fields["cid"] = std::to_string(localCid_++);
    msg.fields["mode"] = mode;
    msg.fields["endpoint"] = endpoint;
    msg.fields["payload"] = payload;
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!socket_.sendFrame(msg)) 
      running_ = false;
  }

  void sendCommand( const std::string &line )
  {
    Message msg = Message::make("CMD");
    msg.fields["cid"] = std::to_string(localCid_++);
    msg.fields["line"] = line;
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!socket_.sendFrame(msg)) 
      running_ = false;
  }

  void listTmp( void ) const 
  {
    if (!fs::exists(tmpDir_)) 
    {
      util::printLine("tmp is empty");
      return;
    }
    std::ostringstream out;
    for (const auto& entry : fs::directory_iterator(tmpDir_))
      out << entry.path().filename().string() << "\n";
    util::printLine(out.str());
  }

  void catTmp( const std::string &file ) const 
  {
    const fs::path path = tmpDir_ / util::sanitizeFileName(file);
    if (!fs::exists(path)) 
    {
      util::printLine("file not found: " + path.string());
      return;
    }
    util::printLine(util::readTextFile(path, 120000));
  }

  void metaTmp( const std::string &file ) const 
  {
    const fs::path path = tmpDir_ / util::sanitizeFileName(file);
    if (!fs::exists(path)) 
    {
      util::printLine("file not found: " + path.string());
      return;
    }
    std::stringstream ss(util::readTextFile(path, 120000));
    std::string line;
    std::ostringstream out;
    while (std::getline(ss, line)) 
    {
      if (line.find("EXIF") != std::string::npos ||
        line.find("META") != std::string::npos ||
        line.find("cipher=") != std::string::npos ||
        line.find("session=") != std::string::npos ||
        line.find("route=") != std::string::npos ||
        line.find("data=") != std::string::npos) {
        out << line << "\n";
      }
    }
    const std::string text = out.str();
    util::printLine(text.empty() ? "no metadata-like lines found" : text);
  }

  void showInbox( void ) const 
  {
    std::ostringstream out;
    for (size_t i = 0; i < inbox_.size(); ++i) 
      out << i << ": " << inbox_[i].filename().string() << "\n";
    util::printLine(out.str().empty() ? "inbox empty" : out.str());
  }

  void runBatch( const std::string &file )
  {
    fs::path path = file;
    if (!path.is_absolute())
        path = fs::current_path() / file;
    std::ifstream in(path);
    if (!in) 
    {
      util::printLine("Cannot open batch file: " + path.string());
      return;
    }
    std::string line;
    while (running_ && std::getline(in, line)) 
    {
      line = util::trim(line);
      if (line.empty() || util::startsWith(line, "#") || util::startsWith(line, "rem ")) 
        continue;
      if (util::startsWith(util::lower(line), "sleep ")) 
      {
        auto args = util::splitArgs(line);
        if (args.size() >= 2 && util::isInteger(args[1])) 
          std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(args[1])));
        continue;
      }
      util::printLine("> " + line);
      executeLocalLine(line);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};

/* Simulation connection class */
class SimConnection 
{
public:
  SimConnection( Role role, fs::path tmpDir )
    : role_(role), tmpDir_(std::move(tmpDir))
  {
    fs::create_directories(tmpDir_);
  }

  bool connectTo( const std::string &host, uint16_t port ) 
  {
    if (!socket_.connectTo(host, port)) 
      return false;
    Message hello = Message::make("HELLO");
    hello.fields["role"] = roleName(role_);
    hello.fields["name"] = "sim-" + roleName(role_);
    hello.fields["hwid"] = role_ == Role::Hacker ? "HW-SIM-CLIENT-01" : "HW-SIM-ADMIN-01";
    hello.fields["version"] = "sim";
    if (!socket_.sendFrame(hello)) 
      return false;
    running_ = true;
    reader_ = std::thread([this] { readLoop(); });
    return waitFor([](const Message& msg) { return msg.get("type") == "WELCOME"; }, 3000).get("type") == "WELCOME";
  }

  void close( void ) 
  {
    running_ = false;
    Message quit = Message::make("QUIT");
    socket_.sendFrame(quit);
    socket_.close();
    if (reader_.joinable()) 
      reader_.join();
  }

  Message sendPacket( const std::string &mode, const std::string &endpoint, const std::string &payload ) 
  {
    Message msg = Message::make("PACKET");
    const std::string cid = nextCid();
    msg.fields["cid"] = cid;
    msg.fields["mode"] = mode;
    msg.fields["endpoint"] = endpoint;
    msg.fields["payload"] = payload;
    socket_.sendFrame(msg);
    return waitFor([&](const Message &reply) 
      {
        return (reply.get("type") == "RESPONSE" || reply.get("type") == "ERROR") && reply.get("cid") == cid;
      }, 5000);
  }

  Message command( const std::string &line )
  {
    Message msg = Message::make("CMD");
    const std::string cid = nextCid();
    msg.fields["cid"] = cid;
    msg.fields["line"] = line;
    socket_.sendFrame(msg);
    return waitFor([&](const Message& reply) 
      {
        return reply.get("type") == "RESPONSE" && reply.get("cid") == cid;
      }, 5000);
  }

  Message waitGameOver( int timeoutMs ) 
  {
    return waitFor([](const Message& msg) { return msg.get("type") == "GAME_OVER"; }, timeoutMs);
  }

private:
  Role role_ = Role::Unknown;
  TcpSocket socket_;
  std::atomic<bool> running_{ false };
  std::thread reader_;
  std::mutex mutex_;
  std::condition_variable_any cv_;
  std::vector<Message> inbox_;
  fs::path tmpDir_;
  int cid_ = 1;

  std::string nextCid( void ) 
  {
    return "sim-" + std::to_string(cid_++);
  }

  void readLoop( void )
  {
    Message msg;
    while (running_ && socket_.recvFrame(msg)) 
    {
      const std::string body = msg.get("body");
      const std::string fileName = msg.get("file_name");
      if (!fileName.empty()) 
        util::writeTextFile(tmpDir_ / util::sanitizeFileName(fileName), body);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        inbox_.push_back(msg);
      }
      cv_.notify_all();
    }
    running_ = false;
    cv_.notify_all();
  }

  template <typename Predicate>
  Message waitFor(Predicate pred, int timeoutMs) 
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lock(mutex_);
    while (std::chrono::steady_clock::now() < deadline) 
    {
      for (const auto& msg : inbox_) 
        if (pred(msg)) 
            return msg;
      cv_.wait_until(lock, deadline);
    }
    return Message::make("TIMEOUT");
  }
} /* End of 'SimConnection' class */;

int runServerMode( int argc, char** argv ) 
{
  uint16_t port = 7777;
  int seed = 0;
  int duration = 100000000;
  int bots = 12;
  for (int i = 2; i < argc; ++i) 
  {
    const std::string arg = argv[i];
    if (arg == "--seed" && i + 1 < argc) 
      seed = std::stoi(argv[++i]);
    else if (arg == "--duration" && i + 1 < argc) 
      duration = std::stoi(argv[++i]);
    else if (arg == "--bots" && i + 1 < argc) 
      bots = std::stoi(argv[++i]);
    else if (util::isInteger(arg))
      port = static_cast<uint16_t>(std::stoi(arg));
  }

  GameServer server(port, seed, duration, bots, true, true);
  if (!server.start()) 
  {
    util::printLine("Server failed to listen on port " + std::to_string(port));
    return 2;
  }
  util::printLine("Server running. Start defender: ToughHA.exe defender <server-ip> " + std::to_string(port));
  util::printLine("Start hacker:   ToughHA.exe hacker <server-ip> " + std::to_string(port));
  util::printLine("Press Enter here to stop server.");
  std::string line;
  std::getline(std::cin, line);
  server.stop();
  return 0;
}

int runClientMode( Role role, int argc, char** argv ) 
{
  std::string host = "127.0.0.1";
  uint16_t port = 7777;
  if (argc >= 3) 
    host = argv[2];
  if (argc >= 4 && util::isInteger(argv[3])) 
    port = static_cast<uint16_t>(std::stoi(argv[3]));
  GameClient client(role, host, port);
  return client.runInteractive();
}

bool bodyContains( const Message &msg, const std::string &text ) 
{
  return msg.get("body").find(text) != std::string::npos;
}

std::string valueAfter(const std::string& text, const std::string& key) 
{
  const auto pos = text.find(key);
  if (pos == std::string::npos) 
    return "";
  size_t start = pos + key.size();
  size_t end = start;
  while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end])) && text[end] != '\r' && text[end] != '\n')
    ++end;
  return text.substr(start, end - start);
}

int runSimulation( void )
{
  const uint16_t port = 7877;
  const int seed = 424242;
  util::printLine("[sim] starting local TCP server");
  GameServer server(port, seed, 90, 8, false, false);
  if (!server.start()) 
  {
    util::printLine("[sim] failed: cannot start server");
    return 2;
  }

  SimConnection defender(Role::Defender, fs::current_path() / "runtime" / "sim_defender_tmp");
  SimConnection hacker(Role::Hacker, fs::current_path() / "runtime" / "sim_hacker_tmp");
  if (!defender.connectTo("127.0.0.1", port)) 
  {
    util::printLine("[sim] failed: defender could not connect");
    server.stop();
    return 3;
  }
  if (!hacker.connectTo("127.0.0.1", port)) 
  {
    util::printLine("[sim] failed: hacker could not connect");
    defender.close();
    server.stop();
    return 4;
  }

  util::printLine("[sim] both players connected through TCP");
  Message service = defender.command("cat /srv/service/generated_service.cpp");
  if (!bodyContains(service, "handle_packet")) 
  {
    util::printLine("[sim] failed: defender cannot read generated service script");
    hacker.close();
    defender.close();
    server.stop();
    return 5;
  }
  util::printLine("[sim] defender can read generated C++ service script");

  Message status = defender.command("status");
  if (!bodyContains(status, "time_left=")) 
  {
    util::printLine("[sim] failed: defender status command broken");
    hacker.close();
    defender.close();
    server.stop();
    return 6;
  }

  Message rootList = defender.command("ls /srv");
  Message db = defender.command("cat /srv/db/main.db");
  if (!bodyContains(rootList, "service") || !bodyContains(db, "final_password=")) 
  {
    util::printLine("[sim] failed: defender cannot list/read real server files");
    hacker.close();
    defender.close();
    server.stop();
    return 61;
  }

  const std::vector<std::string> rulesToTest{ "type_guard", "media_sanitizer", "auth_fullmatch", "backup_acl" };
  for (const auto& rule : rulesToTest) 
  {
    Message on = defender.command("rule add " + rule);
    Message off = defender.command("rule del " + rule);
    if (!bodyContains(on, "OK rule") || !bodyContains(off, "OK rule")) 
    {
      util::printLine("[sim] failed: defender rule toggle broken for " + rule);
      hacker.close();
      defender.close();
      server.stop();
      return 62;
    }
  }
  Message cipherChange = defender.command("cipher 9");
  Message serviceAfterCipher = defender.command("cat /srv/service/generated_service.cpp");
  if (!bodyContains(cipherChange, "cipher_shift=9") || !bodyContains(serviceAfterCipher, "kCipherShift = 9")) 
  {
    util::printLine("[sim] failed: defender cipher change did not regenerate service script");
    hacker.close();
    defender.close();
    server.stop();
    return 63;
  }
  util::printLine("[sim] defender rule/cipher controls and file access work");

  server.triggerTestEvent("routing");
  defender.command("logs 5");
  hacker.sendPacket("standard", "/api/help", "");
  hacker.sendPacket("standard", "/api/profile", "user_id=5");
  hacker.sendPacket("custom", "/api/profile",
    "user_id=" + server.world().vulnerableUserId + "&" + server.world().debugFlagName + "=" + server.world().debugFlagValue);

  Message image = hacker.sendPacket("custom", "/cdn/image", "asset=avatar&format=" + server.world().imageMagic + "&width=0");
  const std::string stage1 = valueAfter(image.get("body"), "stage1=");
  if (stage1.empty()) 
  {
    util::printLine("[sim] failed: image metadata did not reveal stage1 key");
    hacker.close();
    defender.close();
    server.stop();
    return 7;
  }
  util::printLine("[sim] hacker found image metadata stage1 key");

  hacker.sendPacket("custom", "/auth/check", "token=" + stage1.substr(0, 8) + "::overflow");
  Message auth = hacker.sendPacket("custom", "/auth/check", "token=" + stage1.substr(0, 8) + "::overflow2");
  const std::string session = valueAfter(auth.get("body"), "session=");
  const std::string route = valueAfter(auth.get("body"), "route=");
  if (session.empty() || route.empty()) 
  {
    util::printLine("[sim] failed: auth prefix bug did not reveal session/route");
    hacker.close();
    defender.close();
    server.stop();
    return 8;
  }
  util::printLine("[sim] hacker used auth prefix bug and got backup session");

  hacker.sendPacket("standard", "/api/ping", "client=burst-test");
  for (int i = 0; i < 20; ++i) 
    hacker.sendPacket("standard", "/api/ping", "client=burst-" + std::to_string(i));

  Message clients = defender.command("clients");
  if (!bodyContains(clients, "HW-") || bodyContains(clients, "tcp") || bodyContains(clients, "fake") ||
    bodyContains(clients, "HACK") || bodyContains(clients, "BOT")) {
    util::printLine("[sim] failed: defender clients view leaks source identity or lacks neutral hardware tokens");
    hacker.close();
    defender.close();
    server.stop();
    return 9;
  }
  util::printLine("[sim] burst traffic and defender telemetry work");

  Message vault = hacker.sendPacket("custom", "/vault/read", "session=" + session + "&route=" + route);
  const std::string cipher = valueAfter(vault.get("body"), "cipher=rot");
  const std::string encrypted = valueAfter(vault.get("body"), "data=");
  if (cipher.empty() || encrypted.empty()) 
  {
    util::printLine("[sim] failed: vault did not return encrypted blob");
    hacker.close();
    defender.close();
    server.stop();
    return 10;
  }
  const int shift = std::stoi(cipher);
  const std::string plain = util::rotText(encrypted, -shift);
  util::printLine("[sim] hacker decrypted vault blob");

  Message final = hacker.sendPacket("custom", "/core/export", "password=" + plain);
  if (!bodyContains(final, "TOUGHHA{")) 
  {
    util::printLine("[sim] failed: final export did not return crown data");
    hacker.close();
    defender.close();
    server.stop();
    return 11;
  }

  Message over = hacker.waitGameOver(5000);
  if (over.get("winner") != "hacker") 
  {
    util::printLine("[sim] failed: game over did not announce hacker win");
    hacker.close();
    defender.close();
    server.stop();
    return 12;
  }
  util::printLine("[sim] full game flow passed: TCP connect, defender files, bots/noise, burst packets, tmp files, exploit chain, victory");

  hacker.close();
  defender.close();
  server.stop();
  return 0;
}

void printUsage( void )
{
  util::printLine(
    "ToughHA - LAN hacker vs defender TCP game\n\n"
    "Usage:\n"
    "  ToughHA.exe server [port] [--seed N] [--duration seconds] [--bots N]\n"
    "  ToughHA.exe defender <host> [port]\n"
    "  ToughHA.exe hacker <host> [port]\n"
    "  ToughHA.exe sim\n\n"
    "LAN quick start:\n"
    "  PC1: ToughHA.exe server 7777\n"
    "  PC2 defender: ToughHA.exe defender <PC1-IP> 7777\n"
    "  PC2 hacker:   ToughHA.exe hacker <PC1-IP> 7777\n");
}

int main( int argc, char** argv ) 
{
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  WsaSession wsa;
  if (!wsa.ok()) 
  {
    util::printLine("WSAStartup failed.");
    return 1;
  }

  if (argc < 2) 
  {
    printUsage();
    return 0;
  }

  const std::string mode = util::lower(argv[1]);
  if (mode == "server") 
    return runServerMode(argc, argv);
  if (mode == "defender") 
    return runClientMode(Role::Defender, argc, argv);
  if (mode == "hacker") 
    return runClientMode(Role::Hacker, argc, argv);
  if (mode == "sim") 
    return runSimulation();
  printUsage();
  return 0;
}

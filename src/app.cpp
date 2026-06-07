#include "app.h"

#include "game_client.h"
#include "game_server.h"
#include "sim_connection.h"
#include "util.h"

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

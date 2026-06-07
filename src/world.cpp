#include "world.h"

  World World::generate( int forcedSeed, const FirewallConfig &firewall ) 
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

  void World::writeRules( const FirewallConfig &firewall ) const 
  {
    std::ostringstream out;
    out << "# Generated firewall/service overlay\n";
    out << "# Defender may change this through the console: rule add/del, cipher <n>.\n";
    out << firewall.describe();
    util::writeTextFile(rulesPath, out.str());
  }

  void World::writeDb() const 
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

  void World::writeServiceScript( const FirewallConfig &firewall ) const 
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

#include "game_server.h"

GameServer::GameServer( uint16_t port, int seed, int durationSeconds, int botCount, bool randomEvents, bool verbose ) : port_(port), durationSeconds_(durationSeconds), botCount_(botCount), randomEvents_(randomEvents), verbose_(verbose)
{

    firewall_.cipherShift = 3 + (seed == 0 ? 4 : (std::abs(seed) % 17));
    world_ = World::generate(seed, firewall_);
    rng_.seed(static_cast<uint32_t>(world_.seed ^ 0xA51CEu));
  
}

GameServer::~GameServer( void )
{

    stop();
  
}

const World & GameServer::world( void ) const
{

      return world_;
  
}

bool GameServer::start( void )
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

void GameServer::stop( void )
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

void GameServer::triggerTestEvent( const std::string &kind )
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

void GameServer::acceptLoop( void )
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

void GameServer::clientLoop( TcpSocket socket, std::string ip )
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

void GameServer::createBots( void )
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

void GameServer::botLoop( void )
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

void GameServer::tickerLoop( void )
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

void GameServer::randomEventLoop( void )
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

int GameServer::elapsedSeconds( void ) const
{

    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now() - startedAt_).count());
  
}

int GameServer::secondsLeft( void ) const
{

    return std::max(0, durationSeconds_ - elapsedSeconds());
  
}

bool GameServer::isHwidBanned( const std::string &hwid )
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

void GameServer::sendTo( const std::shared_ptr<ClientConn> &client, const Message &msg )
{

    if (!client || !client->alive) 
      return;
    std::lock_guard<std::mutex> lock(client->sendMutex);
    if (!client->socket.sendFrame(msg)) 
      client->alive = false;
  
}

void GameServer::broadcastToRoles( const Message &msg, bool defenders, bool hackers )
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

void GameServer::broadcastEvent( const std::string &body )
{

    Message msg = Message::make("EVENT");
    msg.fields["time"] = util::wallClock();
    msg.fields["body"] = body;
    broadcastToRoles(msg, true, true);
    logSystem("EVENT", body);
  
}

void GameServer::sendRecentEventsTo( const std::shared_ptr<ClientConn> &client )
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

void GameServer::handlePacketFromClient( const std::shared_ptr<ClientConn> &client, const Message &msg )
{

    Packet packet;
    packet.endpoint = msg.get("endpoint");
    packet.mode = util::lower(msg.get("mode", "standard"));
    packet.payload = msg.get("payload");
    packet.cid = msg.get("cid");
    packet.params = parseKeyValuePayload(packet.payload);
    processPacket(client, nullptr, packet);
  
}

void GameServer::simulateBotPacket( int botIndex )
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

ServiceResult GameServer::runService( const Packet &packet, int actorProgress )
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

void GameServer::processPacket( const std::shared_ptr<ClientConn> &client, BotState *bot, const Packet &packet )
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

void GameServer::addActivity(const Activity &activity)
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

std::string GameServer::formatActivity(const Activity &a) const
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

void GameServer::logSystem( const std::string &tag, const std::string &body )
{

    util::appendTextFile(world_.logPath, "[" + util::wallClock() + "] " + tag + " " + body + "\n");
    if (verbose_ && (tag == "SERVER" || tag == "GAME")) 
      util::printLine("[server] " + body);
  
}

void GameServer::addComplaint( int amount, const std::string &reason )
{

    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      complaints_ += amount;
    }
    logSystem("COMPLAINT", "+" + std::to_string(amount) + " " + reason);
  
}

void GameServer::handleDefenderCommand( const std::shared_ptr<ClientConn> &client, const Message &msg)
{

    const std::string line = util::trim(msg.get("line"));
    const std::string cid = msg.get("cid");
    Message reply = Message::make("RESPONSE");
    reply.fields["cid"] = cid;
    reply.fields["body"] = executeDefenderCommand(line);
    sendTo(client, reply);
  
}

std::string GameServer::executeDefenderCommand( const std::string &line )
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

std::string GameServer::statusText( void )
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

int GameServer::visibleClientCount( void ) const
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

std::string GameServer::clientsText( void )
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

std::string GameServer::logsText( int count )
{

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::ostringstream out;
    const int start = std::max(0, static_cast<int>(activities_.size()) - count);
    for (int i = start; i < static_cast<int>(activities_.size()); ++i)
      out << formatActivity(activities_[i]) << "\n";
    return out.str();
  
}

std::string GameServer::inspectText( int id )
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

void GameServer::appendActorLogs( std::ostringstream& out, int id, int count ) const
{

    int written = 0;
    for (auto it = activities_.rbegin(); it != activities_.rend() && written < count; ++it) 
      if (it->actorId == id) 
      {
        out << formatActivity(*it) << "\n";
        ++written;
      }
  
}

std::string GameServer::banById( int id )
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

std::string GameServer::banByHwid( const std::string &hwid )
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

std::string GameServer::setRule( const std::string &rule, bool enabled )
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

std::string GameServer::setCipher( int shift )
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

fs::path GameServer::mapVirtualPath( const std::string &path ) const
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

std::string GameServer::listVirtualPath( const std::string &path )
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

std::string GameServer::catVirtualPath( const std::string &path )
{

    const fs::path real = mapVirtualPath(path);
    if (real.empty() || !fs::exists(real) || fs::is_directory(real))
      return "ERR file path not found";
    return util::readTextFile(real, 120000);
  
}

void GameServer::finishGame(const std::string &winner, const std::string &reason)
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


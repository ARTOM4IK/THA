#pragma once

#include "hwid.h"
#include "types.h"

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

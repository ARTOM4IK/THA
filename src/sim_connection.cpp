#include "sim_connection.h"

SimConnection::SimConnection( Role role, fs::path tmpDir ) : role_(role), tmpDir_(std::move(tmpDir))
{

    fs::create_directories(tmpDir_);
  
}

bool SimConnection::connectTo( const std::string &host, uint16_t port )
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

void SimConnection::close( void )
{

    running_ = false;
    Message quit = Message::make("QUIT");
    socket_.sendFrame(quit);
    socket_.close();
    if (reader_.joinable()) 
      reader_.join();
  
}

Message SimConnection::sendPacket( const std::string &mode, const std::string &endpoint, const std::string &payload )
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

Message SimConnection::command( const std::string &line )
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

Message SimConnection::waitGameOver( int timeoutMs )
{

    return waitFor([](const Message& msg) { return msg.get("type") == "GAME_OVER"; }, timeoutMs);
  
}

std::string SimConnection::nextCid( void )
{

    return "sim-" + std::to_string(cid_++);
  
}

void SimConnection::readLoop( void )
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


#pragma once

#include "types.h"

class SimConnection
{
public:
  SimConnection( Role role, fs::path tmpDir );
  bool connectTo( const std::string &host, uint16_t port );
  void close( void );
  Message sendPacket( const std::string &mode, const std::string &endpoint, const std::string &payload );
  Message command( const std::string &line );
  Message waitGameOver( int timeoutMs );
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
  std::string nextCid( void );
  void readLoop( void );
  template <typename Predicate>
  Message waitFor( Predicate pred, int timeoutMs )
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
    std::unique_lock<std::mutex> lock( mutex_ );
    while (std::chrono::steady_clock::now() < deadline)
    {
      for ( const auto &msg : inbox_ )
        if ( pred( msg ) )
          return msg;
      cv_.wait_until( lock, deadline );
    }
    return Message::make( "TIMEOUT" );
  }
};

#pragma once

#include "hwid.h"
#include "types.h"

class GameClient
{
public:
  GameClient( Role role, std::string host, uint16_t port );
  int runInteractive( void );
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
  bool connect( void );
  std::string prompt( void ) const;
  void printIncoming( const std::string &text ) const;
  void receiveLoop( void );
  void handleIncoming( const Message &msg );
  void recordEvent( const std::string &time, const std::string &body );
  void showEvents( int count );
  void clearEvents( void );
  void showLocalHelp( void );
  void executeLocalLine( const std::string &line );
  void executeHackerLine( const std::vector<std::string> &args );
  void sendPacket( const std::string &mode, const std::string &endpoint, const std::string &payload );
  void sendCommand( const std::string &line );
  void listTmp( void ) const;
  void catTmp( const std::string &file ) const;
  void metaTmp( const std::string &file ) const;
  void showInbox( void ) const;
  void runBatch( const std::string &file );
};

#pragma once

#include "world.h"

class GameServer
{
public:
  GameServer( uint16_t port, int seed, int durationSeconds, int botCount, bool randomEvents, bool verbose );
  ~GameServer( void );
  const World &world( void ) const;
  bool start( void );
  void stop( void );
  void triggerTestEvent( const std::string &kind );
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
  void acceptLoop( void );
  void clientLoop( TcpSocket socket, std::string ip );
  void createBots( void );
  void botLoop( void );
  void tickerLoop( void );
  void randomEventLoop( void );
  int elapsedSeconds( void ) const;
  int secondsLeft( void ) const;
  bool isHwidBanned( const std::string &hwid );
  void sendTo( const std::shared_ptr<ClientConn> &client, const Message &msg );
  void broadcastToRoles( const Message &msg, bool defenders, bool hackers );
  void broadcastEvent( const std::string &body );
  void sendRecentEventsTo( const std::shared_ptr<ClientConn> &client );
  void handlePacketFromClient( const std::shared_ptr<ClientConn> &client, const Message &msg );
  void simulateBotPacket( int botIndex );
  ServiceResult runService( const Packet &packet, int actorProgress );
  void processPacket( const std::shared_ptr<ClientConn> &client, BotState *bot, const Packet &packet );
  void addActivity( const Activity &activity);
  std::string formatActivity( const Activity &a) const;
  void logSystem( const std::string &tag, const std::string &body );
  void addComplaint( int amount, const std::string &reason );
  void handleDefenderCommand( const std::shared_ptr<ClientConn> &client, const Message &msg);
  std::string executeDefenderCommand( const std::string &line );
  std::string statusText( void );
  int visibleClientCount( void ) const;
  std::string clientsText( void );
  std::string logsText( int count );
  std::string inspectText( int id );
  void appendActorLogs( std::ostringstream & out, int id, int count ) const;
  std::string banById( int id );
  std::string banByHwid( const std::string &hwid );
  std::string setRule( const std::string &rule, bool enabled );
  std::string setCipher( int shift );
  fs::path mapVirtualPath( const std::string &path ) const;
  std::string listVirtualPath( const std::string &path );
  std::string catVirtualPath( const std::string &path );
  void finishGame( const std::string &winner, const std::string &reason);
};

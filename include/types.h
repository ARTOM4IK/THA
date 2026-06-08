#pragma once

#include "network.h"

enum class Role
{
  Unknown,
  Hacker,
  Defender,
  Bot
};

std::string roleName( Role role );
Role parseRole( const std::string &value );

struct FirewallConfig
{
  bool typeGuard = false;
  bool mediaSanitizer = false;
  bool authFullMatch = false;
  bool backupAcl = false;
  int cipherShift = 7;

  int ruleCount( void ) const;
  std::string describe( void ) const;
};

struct Packet
{
  std::string endpoint;
  std::string mode;
  std::string payload;
  std::string cid;
  std::map<std::string, std::string> params;
};

std::map<std::string, std::string> parseKeyValuePayload( std::string payload );

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
};

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
};

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
};

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
};

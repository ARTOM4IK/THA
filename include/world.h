#pragma once

#include "types.h"

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

  static World generate( int forcedSeed, const FirewallConfig &firewall );
  void writeRules( const FirewallConfig &firewall ) const;
  void writeDb( void ) const;
  void writeServiceScript( const FirewallConfig &firewall ) const;
};

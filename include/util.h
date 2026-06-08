#pragma once

#include "common.h"

namespace util
{
  extern std::mutex coutMutex;

  void printLine( const std::string &text );
  std::string trim( const std::string &value );
  std::string lower( std::string value );
  bool startsWith( const std::string &value, const std::string &prefix );
  std::vector<std::string> split( const std::string &value, char delimiter );
  std::vector<std::string> splitArgs( const std::string &line );
  std::string join( const std::vector<std::string> &items, size_t first, const std::string &sep );
  std::string wallClock( void );
  int64_t steadyMs( void );
  std::string randomHex( std::mt19937 &rng, size_t count );
  int randomInt( std::mt19937 &rng, int minValue, int maxValue );
  std::string escapeValue( const std::string &value );
  int hexValue( char ch );
  std::string unescapeValue( const std::string &value );
  std::string readTextFile( const fs::path &path, size_t maxBytes = 65536 );
  void writeTextFile( const fs::path &path, const std::string &text );
  void appendTextFile( const fs::path &path, const std::string &text );
  std::string sanitizeFileName( std::string value );
  std::string rotText( const std::string &text, int shift );
  bool isInteger( const std::string &value );
  std::string maskHwid( const std::string &hwid );
  uint32_t stableHash32( const std::string &value, uint32_t seed = 2166136261u );
  std::string fixedHex( uint32_t value, int width );
  std::string publicHwid( const std::string &hwid );
  std::string publicNetId( const std::string &hwid );
}

#include "util.h"

namespace util
{
  std::mutex coutMutex;

  void printLine( const std::string &text )
  {
    std::lock_guard<std::mutex> lock(coutMutex);
    std::cout << text << std::endl;
  }

  std::string trim( const std::string &value )
  {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) 
      return "";
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
  }

  std::string lower(std::string value) 
  {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) 
      {
        return static_cast<char>(std::tolower(ch));
      });
    return value;
  }

  bool startsWith( const std::string &value, const std::string &prefix ) 
  {
    return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
  }

  std::vector<std::string> split( const std::string& value, char delimiter) 
  {
    std::vector<std::string> result;
    std::string item;
    std::stringstream ss(value);
    while (std::getline(ss, item, delimiter)) 
      result.push_back(item);

    return result;
  }

  std::vector<std::string> splitArgs( const std::string &line ) 
  {
      std::vector<std::string> args;
      std::string current;
      bool quoted = false;
      char quote = 0;

      for (size_t i = 0; i < line.size(); ++i) 
      {
        const char ch = line[i];
        if ((ch == '"' || ch == '\'') && (!quoted || ch == quote)) 
        {
          quoted = !quoted;
          quote = quoted ? ch : 0;
          continue;
        }
        if (!quoted && std::isspace(static_cast<unsigned char>(ch))) 
        {
          if (!current.empty()) 
          {
            args.push_back(current);
            current.clear();
          }
          continue;
        }
        current.push_back(ch);
      }
      if (!current.empty()) 
        args.push_back(current);

      return args;
  }

  std::string join( const std::vector<std::string> &items, size_t first, const std::string &sep ) 
  {
    std::ostringstream out;
    for (size_t i = first; i < items.size(); ++i) 
    {
      if (i != first) 
        out << sep;
      out << items[i];
    }
    return out.str();
  }

  std::string wallClock( void ) 
  {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm {};

    localtime_s(&tm, &tt);
    std::ostringstream out;
    out << std::put_time(&tm, "%H:%M:%S");

    return out.str();
  }

  int64_t steadyMs( void ) 
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  std::string randomHex( std::mt19937 &rng, size_t count ) 
  {
    static const char *chars = "0123456789ABCDEF";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string out;

    out.reserve(count);
    for (size_t i = 0; i < count; ++i)
      out.push_back(chars[dist(rng)]);

    return out;
  }

  int randomInt( std::mt19937 &rng, int minValue, int maxValue ) 
  {
    std::uniform_int_distribution<int> dist(minValue, maxValue);
    return dist(rng);
  }

  std::string escapeValue( const std::string &value )
  {
    std::ostringstream out;

    for (unsigned char ch : value) 
    {
      switch (ch) 
      {
      case '%':
        out << "%25";
        break;
      case '\n':
        out << "%0A";
        break;
      case '\r':
        out << "%0D";
        break;
      case '=':
        out << "%3D";
        break;
      default:
        out << static_cast<char>(ch);
        break;
      }
    }
    return out.str();
  }

  int hexValue( char ch ) 
  {
      if (ch >= '0' && ch <= '9')
        return ch - '0';
    
      ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (ch >= 'A' && ch <= 'F')
        return 10 + (ch - 'A');
      return -1;
  }

  std::string unescapeValue( const std::string &value ) 
  {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) 
    {
      if (value[i] == '%' && i + 2 < value.size())
      {
        const int hi = hexValue(value[i + 1]);
        const int lo = hexValue(value[i + 2]);
        if (hi >= 0 && lo >= 0) 
        {
          out.push_back(static_cast<char>((hi << 4) | lo));
          i += 2;
          continue;
        }
      }
      out.push_back(value[i]);
    }
    return out;
  }

  std::string readTextFile( const fs::path &path, size_t maxBytes ) 
  {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      return "";

    std::ostringstream out;
    char buffer[4096];
    size_t total = 0;
 
    while (in && total < maxBytes) 
    {
      const size_t wanted = std::min(sizeof(buffer), maxBytes - total);
      in.read(buffer, static_cast<std::streamsize>(wanted));
      const auto count = static_cast<size_t>(in.gcount());
      out.write(buffer, static_cast<std::streamsize>(count));
      total += count;
    }

    if (in && total >= maxBytes)
      out << "\n...[truncated]...\n";

    return out.str();
  }

  void writeTextFile( const fs::path &path, const std::string &text ) 
  {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
  }

  void appendTextFile( const fs::path &path, const std::string &text) 
  {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << text;
  }

  std::string sanitizeFileName( std::string value ) 
  {
    for (char &ch : value) 
      if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '_' || ch == '-'))
        ch = '_';

    if (value.empty()) 
      value = "packet.tmp";
    return value;
  }

  std::string rotText( const std::string &text, int shift ) 
  {
    shift %= 26;
    if (shift < 0) 
      shift += 26;
    std::string out = text;

    for (char& ch : out) 
    {
      if (ch >= 'a' && ch <= 'z') 
        ch = static_cast<char>('a' + ((ch - 'a' + shift) % 26));
      else if (ch >= 'A' && ch <= 'Z') 
        ch = static_cast<char>('A' + ((ch - 'A' + shift) % 26));
    }
    return out;
  }

  bool isInteger( const std::string &value ) 
  {
    if (value.empty()) 
    {
      return false;
    }

    size_t index = 0;

    if (value[0] == '-' || value[0] == '+') 
      index = 1;

    if (index >= value.size()) 
      return false;

    for (; index < value.size(); ++index) 
      if (!std::isdigit(static_cast<unsigned char>(value[index]))) 
        return false;

    return true;
  }

  std::string maskHwid( const std::string &hwid )
  {
    if (hwid.size() <= 6) 
      return hwid;
    return hwid.substr(0, 3) + "..." + hwid.substr(hwid.size() - 3);
  }

  uint32_t stableHash32( const std::string &value, uint32_t seed ) 
  {
    uint32_t hash = seed;

    for (unsigned char ch : value) 
    {
      hash ^= ch;
      hash *= 16777619u;
    }
    return hash;
  }

  std::string fixedHex( uint32_t value, int width ) 
  {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
    return out.str();
  }

  std::string publicHwid( const std::string &hwid ) 
  {
    return "HW-" + fixedHex(stableHash32("hw:" + hwid), 8);
  }

  std::string publicNetId( const std::string &hwid ) 
  {
    return "NET-" + fixedHex(stableHash32("net:" + hwid), 6);
  }
}

#include "message.h"

  Message Message::make( const std::string &type ) 
  {
    Message msg;
    msg.fields["type"] = type;
    return msg;
  }

  std::string Message::get(const std::string& key, const std::string& fallback) const 
  {
    const auto it = fields.find(key);
    return it == fields.end() ? fallback : it->second;
  }

  std::string Message::encode( void ) const 
  {
    std::ostringstream out;
    for (const auto& item : fields) 
      out << item.first << "=" << util::escapeValue(item.second) << "\n";
    return out.str();
  }

  Message Message::decode(const std::string& text) 
  {
    Message msg;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) 
    {
      const auto pos = line.find('=');
      if (pos == std::string::npos) 
        continue;

      msg.fields[line.substr(0, pos)] = util::unescapeValue(line.substr(pos + 1));
    }
    return msg;
  }

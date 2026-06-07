#include "types.h"

std::string roleName( Role role ) 
{
  switch (role) 
  {
  case Role::Hacker:
    return "hacker";
  case Role::Defender:
    return "defender";
  case Role::Bot:
    return "bot";
  default:
    return "unknown";
  }
}

Role parseRole( const std::string &value ) 
{
  const std::string lowered = util::lower(value);

  if (lowered == "hacker") 
    return Role::Hacker;
  if (lowered == "defender") 
    return Role::Defender;
  if (lowered == "bot") 
    return Role::Bot;

  return Role::Unknown;
}

  int FirewallConfig::ruleCount() const 
  {
    return (typeGuard ? 1 : 0) + (mediaSanitizer ? 1 : 0) +
      (authFullMatch ? 1 : 0) + (backupAcl ? 1 : 0);
  }

  std::string FirewallConfig::describe() const 
  {
    std::ostringstream out;
    out << "type_guard=" << (typeGuard ? "on" : "off") << "\n";
    out << "media_sanitizer=" << (mediaSanitizer ? "on" : "off") << "\n";
    out << "auth_fullmatch=" << (authFullMatch ? "on" : "off") << "\n";
    out << "backup_acl=" << (backupAcl ? "on" : "off") << "\n";
    out << "cipher_shift=" << cipherShift << "\n";
    out << "latency_penalty_ms=" << (ruleCount() * 45) << "\n";
    return out.str();
  }

std::map<std::string, std::string> parseKeyValuePayload(std::string payload) 
{
  for (char &ch : payload) 
    if (ch == ';' || ch == '\n' || ch == '\r') 
      ch = '&';

  std::vector<std::string> tokens;
  if (payload.find('&') != std::string::npos) 
    tokens = util::split(payload, '&');
  else 
    tokens = util::splitArgs(payload);

  std::map<std::string, std::string> params;
  for (std::string token : tokens) 
  {
    token = util::trim(token);
    if (token.empty()) 
      continue;
    const auto pos = token.find('=');
    if (pos == std::string::npos) 
      params[token] = "";
    else 
      params[util::trim(token.substr(0, pos))] = util::trim(token.substr(pos + 1));
  }
  return params;
}

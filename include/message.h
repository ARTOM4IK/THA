#pragma once

#include "util.h"

struct Message
{
  std::map<std::string, std::string> fields;
  static Message make(const std::string &type);
  std::string get(const std::string &key, const std::string &fallback = "") const;
  std::string encode() const;
  static Message decode(const std::string &text);
};

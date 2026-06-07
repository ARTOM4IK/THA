#include "hwid.h"

std::string loadOrCreateHwid( const std::string &role ) 
{
  std::random_device rd;
  std::mt19937 rng(rd());
  const fs::path dir = fs::current_path() / "runtime" / (role + "_tmp");
  fs::create_directories(dir);
  const fs::path path = dir / "hwid.txt";
  std::string existing = util::trim(util::readTextFile(path, 4096));

  if (!existing.empty())
    return existing;

  const std::string hwid = "HW-" + util::randomHex(rng, 12);
  util::writeTextFile(path, hwid + "\n");
  return hwid;
}

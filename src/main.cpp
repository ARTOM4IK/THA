#include "app.h"
#include "network.h"
#include "util.h"

int main( int argc, char** argv ) 
{
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  WsaSession wsa;
  if (!wsa.ok()) 
  {
    util::printLine("WSAStartup failed.");
    return 1;
  }

  if (argc < 2) 
  {
    printUsage();
    return 0;
  }

  const std::string mode = util::lower(argv[1]);
  if (mode == "server") 
    return runServerMode(argc, argv);
  if (mode == "defender") 
    return runClientMode(Role::Defender, argc, argv);
  if (mode == "hacker") 
    return runClientMode(Role::Hacker, argc, argv);
  if (mode == "sim") 
    return runSimulation();
  printUsage();
  return 0;
}

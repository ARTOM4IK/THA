#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

/* Include standard websocket headers */
#include <winsock2.h>
#include <ws2tcpip.h>

/* Include standard windows header */
#include <windows.h>

#pragma comment(lib, "Ws2_32.lib")

/* Include stl headers */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

/* Redefine file system namespace */
namespace fs = std::filesystem;

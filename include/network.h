#pragma once

#include "message.h"

#pragma comment(lib, "Ws2_32.lib")

class WsaSession
{
public:
  WsaSession();
  ~WsaSession();
  bool ok() const;
private:
  bool ok_ = false;
};

class TcpSocket
{
public:
  TcpSocket() = default;
  explicit TcpSocket(SOCKET socket);
  TcpSocket(const TcpSocket &) = delete;
  TcpSocket &operator=(const TcpSocket &) = delete;
  TcpSocket(TcpSocket &&other) noexcept;
  TcpSocket &operator=(TcpSocket &&other) noexcept;
  ~TcpSocket();
  bool valid() const;
  void close();
  bool connectTo(const std::string &host, uint16_t port);
  bool listenOn(uint16_t port);
  TcpSocket acceptOne(std::string &ip);
  bool sendAll(const char *data, size_t bytes);
  bool recvAll(char *data, size_t bytes);
  bool sendFrame(const Message &msg);
  bool recvFrame(Message &msg);
private:
  SOCKET socket_ = INVALID_SOCKET;
};

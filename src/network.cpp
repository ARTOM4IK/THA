#include "network.h"

WsaSession::WsaSession()
{
  WSADATA data {};
  ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

WsaSession::~WsaSession()
{
  if (ok_)
    WSACleanup();
}

bool WsaSession::ok() const
{
  return ok_;
}

TcpSocket::TcpSocket(SOCKET socket) : socket_(socket) {}

TcpSocket::TcpSocket(TcpSocket &&other) noexcept 
  {
    socket_ = other.socket_;
    other.socket_ = INVALID_SOCKET;
  }

  TcpSocket &TcpSocket::operator=(TcpSocket &&other) noexcept 
  {
    if (this != &other) 
    {
      close();
      socket_ = other.socket_;
      other.socket_ = INVALID_SOCKET;
    }
    return *this;
  }

  TcpSocket::~TcpSocket() 
  {
    close();
  }

  bool TcpSocket::valid() const 
  {
    return socket_ != INVALID_SOCKET;
  }

  void TcpSocket::close() 
  {
    if (socket_ != INVALID_SOCKET) 
    {
      shutdown(socket_, SD_BOTH);
      closesocket(socket_);
      socket_ = INVALID_SOCKET;
    }
  }

  bool TcpSocket::connectTo( const std::string &host, uint16_t port ) 
  {
    close();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const std::string portText = std::to_string(port);

    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0) 
      return false;

    for (addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) 
    {
      SOCKET candidate = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
      if (candidate == INVALID_SOCKET) 
        continue;
      if (::connect(candidate, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen)) == SOCKET_ERROR) 
      {
        closesocket(candidate);
        continue;
      }
      socket_ = candidate;
      break;
    }

    freeaddrinfo(result);
    return valid();
  }

  bool TcpSocket::listenOn( uint16_t port ) 
  {
    close();
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) 
      return false;

    BOOL reuse = TRUE;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) 
    {
      close();
      return false;
    }

    if (listen(socket_, SOMAXCONN) == SOCKET_ERROR) 
    {
      close();
      return false;
    }

    return true;
  }

  TcpSocket TcpSocket::acceptOne( std::string &ip ) 
  {
    sockaddr_in clientAddr {};
    int len = sizeof(clientAddr);
    SOCKET client = accept(socket_, reinterpret_cast<sockaddr*>(&clientAddr), &len);

    if (client == INVALID_SOCKET) 
      return TcpSocket();

    char text[INET_ADDRSTRLEN] {};
    inet_ntop(AF_INET, &clientAddr.sin_addr, text, sizeof(text));
    ip = text;
    return TcpSocket(client);
  }

  bool TcpSocket::sendAll( const char *data, size_t bytes ) 
  {
    size_t sent = 0;
    while (sent < bytes) 
    {
      const int chunk = static_cast<int>(std::min<size_t>(bytes - sent, 16 * 1024));
      const int rc = send(socket_, data + sent, chunk, 0);
      
      if (rc == SOCKET_ERROR || rc == 0)
        return false;
      sent += static_cast<size_t>(rc);
    }
    return true;
  }

  bool TcpSocket::recvAll( char *data, size_t bytes ) 
  {
    size_t received = 0;

    while (received < bytes) 
    {
      const int chunk = static_cast<int>(std::min<size_t>(bytes - received, 16 * 1024));
      const int rc = recv(socket_, data + received, chunk, 0);
      if (rc == SOCKET_ERROR || rc == 0) 
        return false;
      received += static_cast<size_t>(rc);
    }
    return true;
  }

  bool TcpSocket::sendFrame( const Message &msg ) 
  {
    const std::string body = msg.encode();
    const uint32_t len = htonl(static_cast<uint32_t>(body.size()));

    if (!sendAll(reinterpret_cast<const char*>(&len), sizeof(len))) 
      return false;
    return body.empty() || sendAll(body.data(), body.size());
  }

  bool TcpSocket::recvFrame( Message &msg ) 
  {
    uint32_t lenNet = 0;
    if (!recvAll(reinterpret_cast<char*>(&lenNet), sizeof(lenNet))) 
      return false;

    const uint32_t len = ntohl(lenNet);
    if (len > 2 * 1024 * 1024)
      return false;

    std::string body(len, '\0');
    if (len > 0 && !recvAll(&body[0], body.size()))
      return false;

    msg = Message::decode(body);
    return true;
  }


#include "MulticastReceiver.hpp"
#include "SystemEventQueue.hpp"
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// Windows specific initialization and cleanup
void initialize_sockets()
{
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
  {
    SystemEventQueue::push("mcast", "WSAStartup failed.");
  }
}

void cleanup_sockets() { WSACleanup(); }

#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// No initialization or cleanup required for sockets on Unix-based systems
void initialize_sockets() {}
void cleanup_sockets() {}

#define closesocket close // Define closesocket as close on Unix-based systems

#endif

MulticastReceiver::MulticastReceiver(const std::string &multicastIP,
                                     unsigned short port)
    : multicastIP(multicastIP), port(port), sockfd(-1), running(false)
{
  initialize_sockets();
}

MulticastReceiver::~MulticastReceiver()
{
  stop();
  cleanup_sockets();
}

void MulticastReceiver::setMessageCallback(MessageCallback callback)
{
  this->onMessageReceived = callback;
}

std::string MulticastReceiver::start()
{
  if (running)
    return "";
  running = true;
  listenerThread = std::thread(&MulticastReceiver::listen, this);
  return "";
}

void MulticastReceiver::stop()
{
  if (!running)
    return;
  running = false;

  if (sockfd != -1)
  {
#if defined(_WIN32) || defined(_WIN64)
    shutdown(sockfd, SD_BOTH);
#else
    shutdown(sockfd, SHUT_RDWR);
#endif
    closesocket(sockfd);
    sockfd = -1;
  }
  if (listenerThread.joinable())
  {
    listenerThread.join();
  }
}

void MulticastReceiver::listen()
{
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
  {
    SystemEventQueue::push("mcast", std::string("Error: cannot open multicast socket: ") +
                                        strerror(errno));
    return;
  }

  int reuse = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse,
                 sizeof(reuse)) < 0)
  {
    SystemEventQueue::push(
        "mcast", std::string("Error: Setting SO_REUSEADDR error: ") + strerror(errno));
    closesocket(sockfd);
    return;
  }

  struct sockaddr_in localSock;
  memset(&localSock, 0, sizeof(localSock));
  localSock.sin_family = AF_INET;
  localSock.sin_port = htons(port);
  localSock.sin_addr.s_addr = INADDR_ANY;

  if (bind(sockfd, (struct sockaddr *)&localSock, sizeof(localSock)) < 0)
  {
    SystemEventQueue::push("mcast", std::string("Error: binding socket: ") +
                                        strerror(errno));
    closesocket(sockfd);
    return;
  }

  struct ip_mreq group;
  group.imr_multiaddr.s_addr = inet_addr(multicastIP.c_str());
  group.imr_interface.s_addr = INADDR_ANY;
  if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&group,
                 sizeof(group)) < 0)
  {
    SystemEventQueue::push("mcast",
                           std::string("Error: Adding multicast group error: ") +
                               strerror(errno));
    closesocket(sockfd);
    return;
  }

  std::stringstream ss;
  ss << "Multicast listening on " << multicastIP << ":" << port;
  SystemEventQueue::push("mcast", ss.str());
  while (running)
  {
    char buffer[4096];
    int nbytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (nbytes <= 0)
    {
      if (!running)
      {
        SystemEventQueue::push("mcast", "Listener stopping.");
        break;
      }
      continue;
    }

    buffer[nbytes] = '\0';

    if (onMessageReceived)
    {
      try
      {
        json j = json::parse(buffer);
        onMessageReceived(j);
      }
      catch (json::parse_error &e)
      {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
      }
    }
  }
}

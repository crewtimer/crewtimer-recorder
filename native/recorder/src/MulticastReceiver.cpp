#include <arpa/inet.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "MulticastReceiver.hpp"

/**
 * Constructor implementation for MulticastReceiver.
 * Initializes the multicast IP and port and sets the socket to an invalid
 * value.
 */
MulticastReceiver::MulticastReceiver(const std::string &multicastIP,
                                     unsigned short port)
    : multicastIP(multicastIP), port(port), sockfd(-1), running(false) {}

/**
 * Destructor implementation for MulticastReceiver.
 * Ensures the receiver stops listening and resources are cleaned up.
 */
MulticastReceiver::~MulticastReceiver() { stop(); }

/**
 * Sets the user-defined message callback function.
 */
void MulticastReceiver::setMessageCallback(MessageCallback callback) {
  this->onMessageReceived = callback;
}

/**
 * Starts the listening thread for receiving multicast messages.
 * If already running, does nothing.
 */
std::string MulticastReceiver::start() {
  if (running)
    return "";
  running = true;
  listenerThread = std::thread(&MulticastReceiver::listen, this);
  return "";
}

/**
 * Stops the listening thread and cleans up resources.
 * Closes the socket and joins the listener thread.
 */
void MulticastReceiver::stop() {
  if (!running)
    return;
  running = false;

  if (sockfd != -1) {
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
    sockfd = -1;
  }
  if (listenerThread.joinable()) {
    listenerThread.join();
  }
}

/**
 * The main listening loop that sets up the socket, joins the multicast group,
 * and listens for incoming messages. Upon receiving a message, it parses it as
 * JSON and triggers the user-defined callback.
 */
void MulticastReceiver::listen() {
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    perror("Error opening socket");
    return;
  }

  int reuse = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse,
                 sizeof(reuse)) < 0) {
    perror("Setting SO_REUSEADDR error");
    close(sockfd);
    return;
  }

  struct sockaddr_in localSock;
  memset(&localSock, 0, sizeof(localSock));
  localSock.sin_family = AF_INET;
  localSock.sin_port = htons(port);
  localSock.sin_addr.s_addr = INADDR_ANY;

  if (bind(sockfd, (struct sockaddr *)&localSock, sizeof(localSock)) < 0) {
    perror("Binding datagram socket error");
    close(sockfd);
    return;
  }

  struct ip_mreq group;
  group.imr_multiaddr.s_addr = inet_addr(multicastIP.c_str());
  group.imr_interface.s_addr = INADDR_ANY;
  if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&group,
                 sizeof(group)) < 0) {
    perror("Adding multicast group error");
    close(sockfd);
    return;
  }

  std::cout << " Multicast listening on" << multicastIP << ":" << port
            << std::endl;
  while (running) {
    char buffer[4096];
    int nbytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (nbytes <= 0) {
      if (!running) {
        std::cout << "Listener stopping." << std::endl;
        break;
      }
      continue;
    }

    buffer[nbytes] = '\0';

    if (onMessageReceived) {
      try {
        json j = json::parse(buffer);
        onMessageReceived(j);
      } catch (json::parse_error &e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
      }
    }
  }
}

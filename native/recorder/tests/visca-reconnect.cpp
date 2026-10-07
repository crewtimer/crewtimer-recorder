#include "visca/IViscaTcpClient.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#ifndef _WIN32
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

int main()
{
  // Reserve a local port without listening so connections are reliably refused.
  const int socketFd = socket(AF_INET, SOCK_STREAM, 0);
  assert(socketFd >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(socketFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
  socklen_t size = sizeof(address);
  assert(getsockname(socketFd, reinterpret_cast<sockaddr *>(&address), &size) == 0);
  const auto port = ntohs(address.sin_port);
  std::atomic<int> failures{0};
  auto client = createViscaTcpClient(nullptr, [&](const std::string &state) {
    if (state == "Disconnected") ++failures;
  }, 1, 1);
  client->start("127.0.0.1", port);
  client->sendCommand({0x81, 0x09, 0x04, 0x38, 0xff}, [](const ViscaResult &result) {
    assert(result.status == ViscaResult::Status::NotConnected);
  });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (failures == 0 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  assert(failures > 0);
  const auto start = std::chrono::steady_clock::now();
  // Changing endpoints during the retry wait previously joined under queueMutex_.
  client->start("127.0.0.2", port);
  client->stop();
  assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
  // Restart after stop must reset the cancellation flag.
  client->start("127.0.0.1", port);
  client->stop();

  // Connected and idle, then the port changes: the worker is parked in the
  // command wait and must reacquire queueMutex_ to exit. This deadlocked when
  // start() held the mutex across join().
  assert(listen(socketFd, 4) == 0);
  std::atomic<bool> connected{false};
  auto connectedClient = createViscaTcpClient(nullptr, [&](const std::string &state) {
    if (state == "Connected") connected = true;
  }, 1, 1);
  connectedClient->start("127.0.0.1", port);
  const auto connectDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!connected && std::chrono::steady_clock::now() < connectDeadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  assert(connected);
  const auto changeStart = std::chrono::steady_clock::now();
  connectedClient->start("127.0.0.1", static_cast<uint16_t>(port + 1));
  connectedClient->stop();
  assert(std::chrono::steady_clock::now() - changeStart < std::chrono::seconds(1));
  close(socketFd);
}
#else
int main() { return 0; }
#endif

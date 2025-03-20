#include "IViscaTcpClient.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#endif

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <algorithm>
#include <sstream>
#include <cstring>  // for memset, memcpy
#include <iostream> // optional

/**
 * @class ViscaTcpClientImpl
 * @brief A concrete implementation of @ref IViscaTcpClient for VISCA-over-TCP.
 *
 * This class uses a background thread to manage a TCP connection to a camera.
 * It automatically attempts to reconnect if disconnected, and processes queued
 * commands in FIFO order. For each command, it sends the data, then reads until
 * a "final" VISCA message is detected (second byte in [0x50..0x7F]) or a timeout.
 *
 * All declarations and definitions are merged inline, still deriving from
 * IViscaTcpClient.
 */
class ViscaTcpClientImpl : public IViscaTcpClient
{
public:
  ViscaTcpClientImpl(StatusCallback statusCb,
                     StatusCallback stateCb,
                     int connectTimeoutSec,
                     int sendTimeoutSec)
      : statusCallback_(std::move(statusCb)), stateCallback_(std::move(stateCb)), connectTimeoutSec_(connectTimeoutSec), sendTimeoutSec_(sendTimeoutSec), exitFlag_(false), connected_(false)
  {
    logState("Idle");
#ifdef _WIN32
    // Optionally only once per process in practice
    static bool winsockInitialized = false;
    if (!winsockInitialized)
    {
      WSADATA wsaData;
      int wsaInit = WSAStartup(MAKEWORD(2, 2), &wsaData);
      if (wsaInit != 0)
      {
        logStatus("Winsock init failed: " + std::to_string(wsaInit));
      }
      winsockInitialized = true;
    }
#endif
  }

  ~ViscaTcpClientImpl() override
  {
    stop();
#ifdef _WIN32
    // WSACleanup() might be done at application shutdown if needed
#endif
  }

  // ------------------------------------------
  // IViscaTcpClient interface
  // ------------------------------------------

  void start(const std::string &ip, uint16_t port) override
  {
    std::lock_guard<std::recursive_mutex> lock(queueMutex_);
    if (workerThread_.joinable())
    {
      // If already running with the same config, do nothing
      if (ip == ipAddress_ && port == port_)
      {
        return;
      }
      // Otherwise stop old thread before starting new
      stop();
      exitFlag_ = false;
    }
    ipAddress_ = ip;
    port_ = port;
    workerThread_ = std::thread(&ViscaTcpClientImpl::runThread, this);
  }

  void stop() override
  {
    logState("Stopping");
    {
      std::lock_guard<std::recursive_mutex> lock(queueMutex_);
      if (!workerThread_.joinable())
      {
        return; // Already stopped
      }

      exitFlag_ = true;
      flushQueue();
      queueCondVar_.notify_all();
    }
    if (workerThread_.joinable())
    {
      workerThread_.join();
    }
    closeSocket();
    logState("Stopped");
  }

  void sendCommand(const std::vector<uint8_t> &commandBytes,
                   Callback callback) override
  {
    CommandRequest req;
    req.command = commandBytes;
    req.callback = std::move(callback);

    {
      std::lock_guard<std::recursive_mutex> lock(queueMutex_);
      commandQueue_.push(req);
    }
    queueCondVar_.notify_one();
  }

private:
  struct CommandRequest
  {
    std::vector<uint8_t> command;
    Callback callback;
  };

  void runThread()
  {
    logStatus("[ViscaTcpClient] Thread started.");
    logState("Starting");

    while (!exitFlag_)
    {
      // Attempt connect if not connected
      if (!connected_)
      {
        if (!attemptConnection())
        {
          // Flush the queue with NotConnected status
          flushQueue();

          // Wait a bit before retry
          waitForRetry(2);
          continue;
        }
      }

      // Wait for command or up to 10s
      CommandRequest req;
      {
        std::unique_lock<std::recursive_mutex> lock(queueMutex_);
        queueCondVar_.wait_for(lock, std::chrono::seconds(10),
                               [this]
                               { return !commandQueue_.empty() || exitFlag_; });
        if (exitFlag_)
        {
          break;
        }

        if (commandQueue_.empty())
        {
          // no new command
          continue;
        }

        req = commandQueue_.front();
        commandQueue_.pop();
      }

      // Send + receive
      ViscaResult result = sendAndReceive(req.command);
      if (result.status == ViscaResult::Status::SocketError ||
          result.status == ViscaResult::Status::NotConnected)
      {
        logStatus("[ViscaTcpClient] Communication error; marking disconnected.");
        connected_ = false;
        closeSocket();
        logState("Disconnected");
      }

      // Callback
      req.callback(result);
    }

    closeSocket();
    logStatus("[ViscaTcpClient] Thread stopped.");
  }

  /**
   * @brief Flushes all pending commands in the queue with a `NotConnected` status.
   */
  void flushQueue()
  {
    std::lock_guard<std::recursive_mutex> lock(queueMutex_);
    while (!commandQueue_.empty())
    {
      CommandRequest req = commandQueue_.front();
      commandQueue_.pop();

      if (!exitFlag_)
      {
        ViscaResult result;
        result.status = ViscaResult::Status::NotConnected;
        result.message = "Flushed command queue due to failed connection.";
        req.callback(result);
      }
    }
  }

  bool attemptConnection()
  {
    {
      std::ostringstream oss;
      oss << "[ViscaTcpClient] Attempting to connect to " << ipAddress_ << ":" << port_;
      logStatus(oss.str());
    }

#ifdef _WIN32
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == INVALID_SOCKET)
    {
      logStatus("[ViscaTcpClient] socket() failed: " + std::to_string(WSAGetLastError()));
      return false;
    }
#else
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ < 0)
    {
      logStatus("[ViscaTcpClient] socket() failed. errno=" + std::to_string(errno));
      return false;
    }
#endif

    // Non-blocking for connect
    if (!setBlockingMode(sock_, false))
    {
      closeSocket();
      return false;
    }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    if (!resolveIp(ipAddress_, &addr.sin_addr))
    {
      logStatus("[ViscaTcpClient] Invalid IP address: " + ipAddress_);
      closeSocket();
      return false;
    }

    int ret;
#ifdef _WIN32
    ret = ::connect(sock_, reinterpret_cast<SOCKADDR *>(&addr), sizeof(addr));
    if (ret < 0)
    {
      int err = WSAGetLastError();
      if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS)
      {
        logStatus("[ViscaTcpClient] connect() failed: " + std::to_string(err));
        closeSocket();
        return false;
      }
    }
#else
    ret = ::connect(sock_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    if (ret < 0)
    {
      if (errno != EINPROGRESS)
      {
        logStatus("[ViscaTcpClient] connect() failed immediately. errno=" + std::to_string(errno));
        closeSocket();
        return false;
      }
    }
#endif

    // If connect didn't succeed immediately, wait up to connectTimeoutSec_
    if (ret < 0)
    {
      if (!waitForConnect(sock_, connectTimeoutSec_))
      {
        logStatus("[ViscaTcpClient] connect() timed out or failed.");
        closeSocket();
        return false;
      }
    }

    // Revert to blocking
    if (!setBlockingMode(sock_, true))
    {
      closeSocket();
      return false;
    }

    connected_ = true;
    logStatus("[ViscaTcpClient] Connected successfully.");

    // Set timeouts
    setSendTimeout(sock_, sendTimeoutSec_);
#ifdef _WIN32
    DWORD rcvTimeoutMs = 2000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
               (const char *)&rcvTimeoutMs, sizeof(rcvTimeoutMs));
#else
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    logState("Connected");
    return true;
  }

  bool waitForConnect(int sockFd, int seconds)
  {
#ifdef _WIN32
    fd_set writeSet, errorSet;
    FD_ZERO(&writeSet);
    FD_ZERO(&errorSet);
    FD_SET(sockFd, &writeSet);
    FD_SET(sockFd, &errorSet);

    TIMEVAL timeout;
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    int selRet = select(0, nullptr, &writeSet, &errorSet, &timeout);
    if (selRet < 0)
    {
      int err = WSAGetLastError();
      logStatus("[ViscaTcpClient] select() error: " + std::to_string(err));
      return false;
    }
    if (selRet == 0)
    {
      // timed out
      return false;
    }
    if (FD_ISSET(sockFd, &errorSet))
    {
      int soErr = 0;
      int len = sizeof(soErr);
      getsockopt(sockFd, SOL_SOCKET, SO_ERROR, (char *)&soErr, &len);
      logStatus("[ViscaTcpClient] connect() error: " + std::to_string(soErr));
      return false;
    }
    return true;
#else
    fd_set writeSet, errorSet;
    FD_ZERO(&writeSet);
    FD_ZERO(&errorSet);
    FD_SET(sockFd, &writeSet);
    FD_SET(sockFd, &errorSet);

    struct timeval timeout;
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    int selRet = select(sockFd + 1, nullptr, &writeSet, &errorSet, &timeout);
    if (selRet < 0)
    {
      logStatus(std::string("[ViscaTcpClient] select() error: ") + std::strerror(errno));
      return false;
    }
    if (selRet == 0)
    {
      // timed out
      return false;
    }
    if (FD_ISSET(sockFd, &errorSet))
    {
      int soErr = 0;
      socklen_t soLen = sizeof(soErr);
      getsockopt(sockFd, SOL_SOCKET, SO_ERROR, &soErr, &soLen);
      logStatus("[ViscaTcpClient] connect() error: " + std::to_string(soErr));
      return false;
    }
    return true;
#endif
  }

  bool setBlockingMode(int sockFd, bool blocking)
  {
#ifdef _WIN32
    u_long mode = (blocking ? 0 : 1);
    int ret = ioctlsocket(sockFd, FIONBIO, &mode);
    if (ret != 0)
    {
      logStatus("[ViscaTcpClient] ioctlsocket() failed: " + std::to_string(WSAGetLastError()));
      return false;
    }
#else
    int flags = fcntl(sockFd, F_GETFL, 0);
    if (flags < 0)
    {
      logStatus(std::string("[ViscaTcpClient] fcntl(F_GETFL) failed: ") + std::strerror(errno));
      return false;
    }
    if (blocking)
    {
      flags &= ~O_NONBLOCK;
    }
    else
    {
      flags |= O_NONBLOCK;
    }
    if (fcntl(sockFd, F_SETFL, flags) < 0)
    {
      logStatus(std::string("[ViscaTcpClient] fcntl(F_SETFL) failed: ") + std::strerror(errno));
      return false;
    }
#endif
    return true;
  }

  void setSendTimeout(int sockFd, int seconds)
  {
#ifdef _WIN32
    DWORD ms = seconds * 1000;
    setsockopt(sockFd, SOL_SOCKET, SO_SNDTIMEO,
               (const char *)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(sockFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
  }

  void waitForRetry(int seconds)
  {
    for (int i = 0; i < seconds; i++)
    {
      if (exitFlag_)
      {
        break;
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  void closeSocket()
  {
    if (connected_)
    {
#ifdef _WIN32
      closesocket(sock_);
#else
      close(sock_);
#endif
    }
    connected_ = false;
  }

  ViscaResult sendAndReceive(const std::vector<uint8_t> &cmd)
  {
    ViscaResult result;

    // Send command
#ifdef _WIN32
    int sent = ::send(sock_, reinterpret_cast<const char *>(cmd.data()),
                      (int)cmd.size(), 0);
    if (sent == SOCKET_ERROR)
    {
      int err = WSAGetLastError();
      result.status = ViscaResult::Status::SocketError;
      result.message = "send() failed. Error: " + std::to_string(err);
      return result;
    }
#else
    int sent = ::send(sock_, cmd.data(), cmd.size(), 0);
    if (sent < 0)
    {
      result.status = ViscaResult::Status::SocketError;
      result.message = std::string("send() failed. errno=") + std::to_string(errno);
      return result;
    }
#endif

    // Read until final message or 5-second timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    std::vector<uint8_t> readBuf;
    readBuf.reserve(512);

    while (true)
    {
      if (std::chrono::steady_clock::now() > deadline)
      {
        result.status = ViscaResult::Status::Timeout;
        result.message = "Timed out waiting for VISCA response.";
        return result;
      }

      uint8_t temp[256];
#ifdef _WIN32
      int recvCount = ::recv(sock_, reinterpret_cast<char *>(temp),
                             sizeof(temp), 0);
      if (recvCount == SOCKET_ERROR)
      {
        int err = WSAGetLastError();
        if (err == WSAETIMEDOUT)
        {
          // no data yet
          continue;
        }
        result.status = ViscaResult::Status::SocketError;
        result.message = "recv() failed. Error: " + std::to_string(err);
        return result;
      }
#else
      int recvCount = ::recv(sock_, temp, sizeof(temp), 0);
      if (recvCount < 0)
      {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
          continue;
        }
        result.status = ViscaResult::Status::SocketError;
        result.message = std::string("recv() failed. errno=") + std::to_string(errno);
        return result;
      }
#endif
      if (recvCount == 0)
      {
        // Connection closed
        result.status = ViscaResult::Status::SocketError;
        result.message = "Connection closed by camera.";
        return result;
      }

      // Accumulate
      readBuf.insert(readBuf.end(), temp, temp + recvCount);

      // Parse messages terminated by 0xFF
      while (true)
      {
        auto it = std::find(readBuf.begin(), readBuf.end(), 0xFF);
        if (it == readBuf.end())
        {
          break;
        }
        std::vector<uint8_t> msg(readBuf.begin(), it + 1);
        readBuf.erase(readBuf.begin(), it + 1);

        // If second byte in [0x50..0x7F], treat as final completion/error
        if (msg.size() >= 2)
        {
          uint8_t statusByte = msg[1];
          if (statusByte >= 0x50 && statusByte <= 0x7F)
          {
            result.response = msg;
            result.status = ViscaResult::Status::OK;
            return result;
          }
        }
        // else ignore (likely ACK)
      }
    }

    // Not reached
    return result;
  }

  bool resolveIp(const std::string &ip, struct in_addr *outAddr)
  {
#ifdef _WIN32
    int ret = InetPtonA(AF_INET, ip.c_str(), outAddr);
    return (ret == 1);
#else
    int ret = inet_pton(AF_INET, ip.c_str(), outAddr);
    return (ret == 1);
#endif
  }

  void logStatus(const std::string &msg)
  {
    if (statusCallback_)
    {
      statusCallback_(msg);
    }
  }

  void logState(const std::string &state)
  {
    if (stateCallback_)
    {
      stateCallback_(state);
    }
  }

private:
  std::string ipAddress_;
  uint16_t port_{0};

  StatusCallback statusCallback_;
  StatusCallback stateCallback_;

  int connectTimeoutSec_{10};
  int sendTimeoutSec_{5};

#ifdef _WIN32
  SOCKET sock_{INVALID_SOCKET};
#else
  int sock_{-1};
#endif

  std::thread workerThread_;
  std::recursive_mutex queueMutex_;
  std::condition_variable_any queueCondVar_;
  std::queue<CommandRequest> commandQueue_;

  std::atomic<bool> exitFlag_;
  bool connected_;
};

// -----------------------------------------------------------------------------
std::unique_ptr<IViscaTcpClient> createViscaTcpClient(
    IViscaTcpClient::StatusCallback statusCb,
    IViscaTcpClient::StatusCallback stateCb,
    int connectTimeoutSec,
    int sendTimeoutSec)
{
  return std::unique_ptr<IViscaTcpClient>(
      new ViscaTcpClientImpl(std::move(statusCb), std::move(stateCb),
                             connectTimeoutSec,
                             sendTimeoutSec));
}

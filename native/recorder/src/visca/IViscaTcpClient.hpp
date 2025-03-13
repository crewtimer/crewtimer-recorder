#ifndef IVISCA_TCP_CLIENT_H
#define IVISCA_TCP_CLIENT_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief The result of a single VISCA command transaction.
 *
 * This struct provides status information and holds the final VISCA response
 * message (if any) that indicates either completion or error.
 */
struct ViscaResult
{
  /**
   * @brief Enumeration of possible outcomes from sending a VISCA command.
   */
  enum class Status
  {
    /// The command succeeded, and a final response was received.
    OK,
    /// The client is not currently connected to the camera.
    NotConnected,
    /// A socket error occurred (send or receive).
    SocketError,
    /// The command timed out waiting for a response.
    Timeout,
    /// A general or unknown error occurred.
    UnknownError
  };

  /// The status of the VISCA command transaction.
  Status status;

  /// A human-readable error or diagnostic message.
  std::string message;

  /**
   * @brief The single final VISCA message that indicates completion or error.
   *
   * Often, a VISCA message is terminated by 0xFF. If the second byte is in
   * the range [0x50..0x7F], it signifies completion or an error.
   * This vector contains the raw bytes of that message.
   */
  std::vector<uint8_t> response;
};

/**
 * @brief An interface for controlling a VISCA camera over TCP in a background thread.
 *
 * The interface is platform-agnostic; implementations may use
 * Windows or POSIX APIs internally. A user obtains an instance by
 * calling the @ref createViscaTcpClient() factory function.
 */
class IViscaTcpClient
{
public:
  /**
   * @brief Defines a callback type used to deliver the result of a VISCA command.
   *
   * The callback is typically invoked once a command either completes with a final
   * response or fails (e.g. due to a socket error or timeout).
   */
  using Callback = std::function<void(const ViscaResult &)>;

  /**
   * @brief Defines a callback type used to deliver status/log messages.
   *
   * This callback is optional; if set, the implementation may use it to provide
   * diagnostic or informational messages (e.g. connection attempts, errors, etc.).
   */
  using StatusCallback = std::function<void(const std::string &)>;

  /**
   * @brief Virtual destructor ensures derived class clean-up.
   */
  virtual ~IViscaTcpClient() = default;

  /**
   * @brief Starts the background thread that manages the camera connection and commands.
   *
   * If the implementation is already running, this may be a no-op.
   *
   * @param ip The IP address of the camera (e.g. "192.168.0.100").
   * @param port The TCP port the camera uses for VISCA (often 52381 on Sony devices).
   */
  virtual void start(
      const std::string &ip,
      uint16_t port) = 0;

  /**
   * @brief Stops the background thread, blocking until fully stopped.
   *
   * This closes any open socket connection and prevents further command processing.
   */
  virtual void stop() = 0;

  /**
   * @brief Enqueues a VISCA command to be sent asynchronously.
   *
   * The command is added to an internal queue. Once processed, the specified callback
   * will be invoked with a @ref ViscaResult indicating success or failure.
   *
   * @param commandBytes The raw VISCA command bytes (e.g. `{0x81, 0x01, 0x04, ... 0xFF}`).
   * @param callback A callback function to receive the final @ref ViscaResult.
   */
  virtual void sendCommand(const std::vector<uint8_t> &commandBytes,
                           Callback callback) = 0;
};

/**
 * @brief Factory function that creates a concrete instance of @ref IViscaTcpClient.
 *
 * This function hides the internal implementation (which may be platform-specific)
 * behind a minimal interface. The returned object manages a TCP connection to a
 * VISCA-capable camera in a background thread.
 *
 * @param statusCb An optional callback for status/log messages. May be `nullptr`.
 * @param connectTimeoutSec How many seconds to wait for a TCP connection attempt.
 * @param sendTimeoutSec How many seconds to allow for sending data before timing out.
 * @param autoStart If true, the background thread starts immediately in the constructor.
 * @return A unique_ptr to an @ref IViscaTcpClient instance.
 */
std::unique_ptr<IViscaTcpClient> createViscaTcpClient(
    IViscaTcpClient::StatusCallback statusCb,
    IViscaTcpClient::StatusCallback stateCb,
    int connectTimeoutSec = 5,
    int sendTimeoutSec = 2);

#endif // IVISCA_TCP_CLIENT_H

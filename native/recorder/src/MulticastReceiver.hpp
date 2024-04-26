/**
 * @file
 * @brief Multicast Receiver class for receiving UDP multicast messages and
 * parsing them as JSON.
 */
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

using json = nlohmann::json;

/**
 * @class MulticastReceiver
 * @brief Listens for UDP multicast messages, parses them as JSON, and triggers
 * a callback.
 *
 * This class encapsulates the functionality required to listen for UDP
 * multicast messages on a specified IP and port. It parses the messages as JSON
 * and triggers a user-defined callback function to handle the message.
 */
class MulticastReceiver {
public:
  /**
   * Type definition for the message callback function.
   * @param j JSON object parsed from the received message.
   */
  using MessageCallback = std::function<void(const json &)>;

  /**
   * Constructor for the MulticastReceiver class.
   * @param multicastIP IP address of the multicast group.
   * @param port Port number to listen on for multicast messages.
   */
  MulticastReceiver(const std::string &multicastIP, unsigned short port);

  /**
   * Destructor for the MulticastReceiver class.
   * Stops the listening thread if it is running and cleans up resources.
   */
  ~MulticastReceiver();

  /**
   * Sets the callback function to be triggered when a message is received and
   * parsed.
   * @param callback The function to call with the parsed JSON message.
   */
  void setMessageCallback(MessageCallback callback);

  /**
   * Starts the listening thread to receive multicast messages.
   * If the listener is already running, this method has no effect.
   */
  void start();

  /**
   * Stops the listening thread and cleans up resources.
   * If the listener is not running, this method has no effect.
   */
  void stop();

private:
  std::string multicastIP;    ///< IP address of the multicast group.
  unsigned short port;        ///< Port number to listen on.
  int sockfd;                 ///< Socket file descriptor.
  std::thread listenerThread; ///< Thread for listening to multicast messages.
  bool running;               ///< Flag to control the listener thread.
  MessageCallback onMessageReceived; ///< User-defined callback function.

  /**
   * The main listening loop for the multicast receiver.
   * Initializes the socket, joins the multicast group, and listens for
   * messages. Parses received messages as JSON and triggers the callback
   * function.
   */
  void listen();
};

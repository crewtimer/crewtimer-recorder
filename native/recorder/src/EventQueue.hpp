#include <functional>
#include <mutex>
#include <queue>
#include <unordered_map>

template <typename T> class EventQueue {
public:
  using SubscriberCallback = std::function<void(const T &)>;
  EventQueue(size_t maxQueueSize) : maxQueueSize_(maxQueueSize) {}

  void addEvent(const T &event) {
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      eventQueue.push_back(event);

      // Remove front elements if the queue exceeds the maximum size
      while (eventQueue.size() > maxQueueSize_) {
        eventQueue.pop_front();
      }
    }

    if (subscriber) {
      subscriber(event);
    }
  }

  void setSubscriber(SubscriberCallback callback) { subscriber = callback; }

protected:
  SubscriberCallback subscriber;
  std::deque<T> eventQueue;
  std::recursive_mutex mutex_;
  size_t maxQueueSize_;
};

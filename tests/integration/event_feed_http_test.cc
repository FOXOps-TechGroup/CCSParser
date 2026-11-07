// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void CloseSocket(Socket socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void CloseSocket(Socket socket) { close(socket); }
#endif

#include "ccsparser/event_feed_stream.h"

namespace ccsparser {
namespace {

using namespace std::chrono_literals;

bool SendAll(Socket socket, const std::string &value) {
  size_t sent = 0;
  while (sent < value.size()) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    int n = send(socket, value.data() + sent,
                 static_cast<int>(value.size() - sent), flags);
    if (n <= 0)
      return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

class LocalServer {
  public:
  using Handler = std::function<void(Socket, const std::string &)>;

  explicit LocalServer(Handler handler) {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
      return;
    winsock_started_ = true;
#endif
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ == kInvalidSocket)
      return;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener_, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) != 0 ||
        listen(listener_, 1) != 0)
      return;
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    if (getsockname(listener_, reinterpret_cast<sockaddr *>(&address),
                    &length) != 0)
      return;
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this, handler = std::move(handler)] {
      Socket client = accept(listener_, nullptr, nullptr);
      if (client == kInvalidSocket)
        return;
#if defined(SO_NOSIGPIPE)
      int enabled = 1;
      setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
      std::string request;
      char buffer[1024];
      while (request.find("\r\n\r\n") == std::string::npos &&
             request.size() < 8192) {
        int count = recv(client, buffer, sizeof(buffer), 0);
        if (count <= 0)
          break;
        request.append(buffer, static_cast<size_t>(count));
      }
      handler(client, request);
      CloseSocket(client);
    });
  }

  ~LocalServer() {
    if (listener_ != kInvalidSocket) {
#ifdef _WIN32
      shutdown(listener_, SD_BOTH);
#else
      shutdown(listener_, SHUT_RDWR);
#endif
      CloseSocket(listener_);
    }
    if (thread_.joinable())
      thread_.join();
#ifdef _WIN32
    if (winsock_started_)
      WSACleanup();
#endif
  }

  bool valid() const { return port_ != 0; }
  std::string url() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/event-feed";
  }

  private:
  Socket listener_ = kInvalidSocket;
  unsigned short port_ = 0;
  std::thread thread_;
#ifdef _WIN32
  bool winsock_started_ = false;
#endif
};

std::unique_ptr<StreamingParseSession> Session(ApiVersion version) {
  ParseOptions options;
  options.version = version;
  options.error_policy = ErrorPolicy::kFailFast;
  options.strict_version = version != ApiVersion::kAuto;
  auto result = EventFeedParser::CreateStreamingSession(options);
  EXPECT_TRUE(result.ok());
  return std::move(result.value());
}

TEST(EventFeedHttpTest, AppliesEventsBeforeStreamClosesAndSendsBearer) {
  std::mutex mutex;
  std::condition_variable cv;
  bool first_applied = false;
  std::string request_seen;
  const std::string first =
      R"({"type":"teams","id":"a","data":{"id":"a"},"token":"1"})"
      "\n";
  const std::string second =
      R"({"type":"teams","id":"b","data":{"id":"b"},"token":"2"})"
      "\n";
  LocalServer server([&](Socket socket, const std::string &request) {
    {
      std::lock_guard lock(mutex);
      request_seen = request;
    }
    const std::string headers = "HTTP/1.1 200 OK\r\nContent-Type: "
                                "application/x-ndjson\r\nContent-Length: " +
                                std::to_string(first.size() + second.size()) +
                                "\r\nConnection: close\r\n\r\n";
    if (!SendAll(socket, headers + first))
      return;
    std::unique_lock lock(mutex);
    if (!cv.wait_for(lock, 5s, [&] { return first_applied; }))
      return;
    lock.unlock();
    SendAll(socket, second);
  });
  ASSERT_TRUE(server.valid());
  auto session = Session(ApiVersion::k2026_01);
  int count = 0;
  EventFeedStreamOptions options;
  options.http.bearer_token = "test-token";
  options.on_event = [&](const auto &) {
    ++count;
    if (count == 1) {
      std::lock_guard lock(mutex);
      first_applied = true;
      cv.notify_one();
    }
    return true;
  };
  auto result = StreamEventFeed(server.url(), *session, options);
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  EXPECT_EQ(result.value().http_status, 200);
  EXPECT_EQ(count, 2);
  EXPECT_EQ(*session->cursor().last_token, "2");
  {
    std::lock_guard lock(mutex);
    EXPECT_NE(request_seen.find("Authorization: Bearer test-token"),
              std::string::npos);
  }
}

TEST(EventFeedHttpTest, DoesNotParseHttpErrorBody) {
  const std::string body =
      R"({"type":"teams","id":"wrong","data":{"id":"wrong"},"token":"bad"})"
      "\n";
  LocalServer server([&](Socket socket, const std::string &) {
    SendAll(socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: " +
                        std::to_string(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body);
  });
  ASSERT_TRUE(server.valid());
  auto session = Session(ApiVersion::k2023_06);
  auto result = StreamEventFeed(server.url(), *session);
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  EXPECT_EQ(result.value().http_status, 400);
  EXPECT_EQ(session->cursor().event_count, 0u);
  EXPECT_FALSE(session->cursor().last_token.has_value());
}

TEST(EventFeedHttpTest,
     ReturnsAfterEndOfUpdatesEvenWhenPeerKeepsConnectionOpen) {
  std::mutex mutex;
  std::condition_variable changed;
  bool release = false;
  LocalServer server([&](Socket socket, const std::string &) {
    SendAll(
        socket,
        "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n\r\n"
        R"({"type":"state","data":{"end_of_updates":"2026-01-01T00:00:00Z"},"token":"last"})"
        "\n");
    std::unique_lock lock(mutex);
    changed.wait_for(lock, 4s, [&] { return release; });
  });
  ASSERT_TRUE(server.valid());
  auto session = Session(ApiVersion::k2026_01);
  auto future = std::async(std::launch::async, [&] {
    return StreamEventFeed(server.url(), *session);
  });
  const auto readiness = future.wait_for(2s);
  {
    std::lock_guard lock(mutex);
    release = true;
  }
  changed.notify_one();
  auto result = future.get();
  EXPECT_EQ(readiness, std::future_status::ready);
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result->end_of_updates);
  EXPECT_EQ(result->http_status, 200);
  EXPECT_EQ(*session->cursor().last_token, "last");
}

TEST(EventFeedHttpTest, RequiresExplicitVersion) {
  auto session = Session(ApiVersion::kAuto);
  auto result = StreamEventFeed("http://127.0.0.1:1/event-feed", *session);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kFailedPrecondition);
}

TEST(EventFeedHttpTest, EncodesResumeTokenAndPreservesOtherQuery) {
  std::string request_seen;
  std::mutex mutex;
  LocalServer server([&](Socket socket, const std::string &request) {
    {
      std::lock_guard lock(mutex);
      request_seen = request;
    }
    SendAll(
        socket,
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
  });
  ASSERT_TRUE(server.valid());
  auto session = Session(ApiVersion::k2023_06);
  EventFeedStreamOptions options;
  options.since_token = "a+b /&";
  auto result =
      StreamEventFeed(server.url() + "?strict=true", *session, options);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().http_status, 200);
  std::lock_guard lock(mutex);
  EXPECT_NE(request_seen.find("?strict=true&since_token=a%2Bb%20%2F%26"),
            std::string::npos);
}

TEST(EventFeedHttpTest, CancelsWhileFeedIsIdle) {
  std::atomic<bool> cancel = false;
  std::mutex mutex;
  std::condition_variable cv;
  bool connected = false;
  bool released = false;
  LocalServer server([&](Socket socket, const std::string &) {
    if (!SendAll(socket,
                 "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n"
                 "Connection: close\r\n\r\n"))
      return;
    std::unique_lock lock(mutex);
    connected = true;
    cv.notify_one();
    cv.wait_for(lock, 5s, [&] { return released; });
  });
  ASSERT_TRUE(server.valid());
  auto session = Session(ApiVersion::k2026_01);
  EventFeedStreamOptions options;
  options.http.should_cancel = [&] { return cancel.load(); };
  std::thread cancellation([&] {
    std::unique_lock lock(mutex);
    cv.wait_for(lock, 5s, [&] { return connected; });
    cancel.store(true);
  });
  auto result = StreamEventFeed(server.url(), *session, options);
  cancellation.join();
  {
    std::lock_guard lock(mutex);
    released = true;
  }
  cv.notify_one();
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kCancelled);
}

TEST(EventFeedHttpTest, ResumesAfterTruncatedRecordWithoutSkippingIt) {
  const std::string complete =
      R"({"type":"teams","id":"a","data":{"id":"a"},"token":"1"})"
      "\n";
  const std::string partial = R"({"type":"teams","id":"b")";
  LocalServer first([&](Socket socket, const std::string &) {
    const auto body = complete + partial;
    SendAll(socket, "HTTP/1.1 200 OK\r\nContent-Length: " +
                        std::to_string(body.size()) +
                        "\r\nConnection: close\r\n\r\n" + body);
  });
  ASSERT_TRUE(first.valid());
  auto session = Session(ApiVersion::k2023_06);
  auto interrupted = StreamEventFeed(first.url(), *session);
  ASSERT_FALSE(interrupted.ok());
  EXPECT_EQ(interrupted.status().code(), StatusCode::kCorruptedInput);
  ASSERT_TRUE(session->cursor().last_token.has_value());
  EXPECT_EQ(*session->cursor().last_token, "1");
  EXPECT_TRUE(session->cursor().contiguous);

  std::string request_seen;
  std::mutex mutex;
  const std::string next =
      R"({"type":"teams","id":"b","data":{"id":"b"},"token":"2"})"
      "\n";
  LocalServer second([&](Socket socket, const std::string &request) {
    {
      std::lock_guard lock(mutex);
      request_seen = request;
    }
    SendAll(socket, "HTTP/1.1 200 OK\r\nContent-Length: " +
                        std::to_string(next.size()) +
                        "\r\nConnection: close\r\n\r\n" + next);
  });
  ASSERT_TRUE(second.valid());
  EventFeedStreamOptions options;
  options.since_token = *session->cursor().last_token;
  auto resumed = StreamEventFeed(second.url(), *session, options);
  ASSERT_TRUE(resumed.ok());
  EXPECT_EQ(resumed.value().http_status, 200);
  EXPECT_EQ(*session->cursor().last_token, "2");
  EXPECT_TRUE(session->store().GetObject(ObjectType::kTeams, "a").ok());
  EXPECT_TRUE(session->store().GetObject(ObjectType::kTeams, "b").ok());
  std::lock_guard lock(mutex);
  EXPECT_NE(request_seen.find("?since_token=1"), std::string::npos);
}

} // namespace
} // namespace ccsparser

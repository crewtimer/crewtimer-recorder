/**
 * @file      ndi_mdns.hpp
 * @brief     Header-only C++17 mDNS discovery helper focused on NDI sources.
 *
 * @version   v1.4 (2025-08-22)
 *
 * @details   Discovers NDI-like services over mDNS, joins PTR→SRV→A/AAAA across
 *            packets, and returns resolved sources (instance, host, port, IPs, TXT).
 *            Includes robust name canonicalization, periodic re-queries, optional
 *            QU (unicast-response) fallback when UDP/5353 is busy, multi-NIC
 *            interface selection, and very verbose debug logging. Also provides
 *            collision-aware URL generation helpers for device web UIs.
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif
#include <cerrno>

namespace ndi_mdns
{

  static constexpr uint16_t DNS_PORT = 5353;
  static constexpr const char *MDNS_ADDR4 = "224.0.0.251";

  // Join mDNS multicast (224.0.0.251) on one or more interfaces.
  // If iface_opt is set, join only that interface. Otherwise, enumerate all IPv4
  // UP, non-loopback, multicast-capable interfaces and join per-interface.
  // Returns number of successful joins.
  inline int join_mdns_memberships_ipv4(int s, const std::optional<std::string> &iface_opt, bool debug)
  {
    int joined = 0;
    in_addr maddr{};
    maddr.s_addr = inet_addr(MDNS_ADDR4);

    auto join_one = [&](in_addr iface) -> bool
    {
      ip_mreq m{};
      m.imr_multiaddr = maddr;
      m.imr_interface = iface;
      int res = setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&m, sizeof(m));
      if (res == 0)
      {
        joined++;
        return true;
      }
      return false;
    };

    if (iface_opt && !iface_opt->empty())
    {
      in_addr ifa{};
#ifdef _WIN32
      InetPtonA(AF_INET, iface_opt->c_str(), &ifa);
#else
      inet_pton(AF_INET, iface_opt->c_str(), &ifa);
#endif
      if (debug)
      {
        char buf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &ifa, buf, sizeof(buf));
        std::cerr << "[ndi-mdns] join request for specific iface " << buf << std::endl;
      }
      bool ok = join_one(ifa);
      if (debug)
        std::cerr << "[ndi-mdns] join " << (ok ? "succeeded" : "failed") << std::endl;
      return joined;
    }

#ifdef _WIN32
    // Enumerate IPv4 unicast addresses for all UP adapters
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    IP_ADAPTER_ADDRESSES *addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size);
    if (ret == ERROR_BUFFER_OVERFLOW)
    {
      buf.resize(size);
      addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
      ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size);
    }
    if (ret == NO_ERROR)
    {
      for (auto *a = addrs; a; a = a->Next)
      {
        if (a->OperStatus != IfOperStatusUp)
          continue;
        for (auto *ua = a->FirstUnicastAddress; ua; ua = ua->Next)
        {
          if (!ua->Address.lpSockaddr)
            continue;
          if (ua->Address.lpSockaddr->sa_family != AF_INET)
            continue;
          auto *sin = reinterpret_cast<sockaddr_in *>(ua->Address.lpSockaddr);
          in_addr ifa = sin->sin_addr;
          // skip 127.0.0.0/8
          if ((ntohl(ifa.s_addr) & 0xFF000000u) == 0x7F000000u)
            continue;
          if (debug)
          {
            char buf2[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &ifa, buf2, sizeof(buf2));
            std::cerr << "[ndi-mdns] candidate iface (win) addr=" << buf2 << std::endl;
          }
          bool ok = join_one(ifa);
          if (debug)
          {
            char buf2[INET_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &ifa, buf2, sizeof(buf2));
            std::cerr << "[ndi-mdns] IP_ADD_MEMBERSHIP " << (ok ? "OK" : "ERR") << " for " << buf2 << std::endl;
          }
        }
      }
    }
#else
    // POSIX enumeration via getifaddrs
    struct ifaddrs *ifas = nullptr;
    if (getifaddrs(&ifas) == 0)
    {
      for (auto *it = ifas; it; it = it->ifa_next)
      {
        if (!it->ifa_addr)
          continue;
        if (it->ifa_addr->sa_family != AF_INET)
          continue;
        unsigned int flags = it->ifa_flags;
        if (!(flags & IFF_UP))
          continue;
        if (flags & IFF_LOOPBACK)
          continue;
#ifdef IFF_MULTICAST
        if (!(flags & IFF_MULTICAST))
          continue;
#endif
        in_addr ifa = reinterpret_cast<sockaddr_in *>(it->ifa_addr)->sin_addr;
        if (debug)
        {
          char buf2[INET_ADDRSTRLEN] = {0};
          inet_ntop(AF_INET, &ifa, buf2, sizeof(buf2));
          std::cerr << "[ndi-mdns] candidate iface " << (it->ifa_name ? it->ifa_name : "?") << " addr=" << buf2 << std::endl;
        }
        bool ok = join_one(ifa);
        if (debug)
        {
          char buf2[INET_ADDRSTRLEN] = {0};
          inet_ntop(AF_INET, &ifa, buf2, sizeof(buf2));
          std::cerr << "[ndi-mdns] IP_ADD_MEMBERSHIP " << (ok ? "OK" : "ERR") << " for " << buf2 << std::endl;
        }
      }
      freeifaddrs(ifas);
    }
#endif
    return joined;
  }

  static inline const std::vector<std::string> &DEFAULT_SERVICES()
  {
    static const std::vector<std::string> svcs{"_ndi._tcp.local." /*, "_ndi._udp.local." */};
    return svcs;
  }

  // DNS types
  enum : uint16_t
  {
    T_A = 1,
    T_PTR = 12,
    T_TXT = 16,
    T_AAAA = 28,
    T_SRV = 33
  };

#pragma pack(push, 1)
  struct DnsHeader
  {
    uint16_t id, flags, qdcount, ancount, nscount, arcount;
  };
#pragma pack(pop)

  inline uint16_t rd16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }
  inline uint32_t rd32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

  // Canonicalize DNS names: lowercase and strip trailing dot(s)
  inline std::string canon(const std::string &s)
  {
    size_t n = s.size();
    while (n > 0 && s[n - 1] == '.')
      --n; // strip trailing dots
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
      out.push_back((char)std::tolower((unsigned char)s[i]));
    }
    return out;
  }

  struct DiscoverOptions
  {
    std::chrono::milliseconds timeout{3000};
    std::vector<std::string> services = DEFAULT_SERVICES();
    int query_bursts = 2;                      // initial burst count
    int requery_interval_ms = 400;             // resend PTR every ~this many ms
    bool active_addr_queries = true;           // send A/AAAA for SRV targets
    bool debug = false;                        // print debug to std::cerr
    int debug_level = 1;                       // 1: packets & cache, 2: per-RR
    std::optional<std::string> interface_ipv4; // pick NIC for multicast
    int reenumerate_interval_ms = 0;           // 0 = disabled; >0 to periodically re-enumerate interfaces (ms)
  };

  inline std::vector<uint8_t> encode_qname(const std::string &fqdn)
  {
    std::vector<uint8_t> q;
    size_t i = 0, start = 0;
    auto push = [&](const std::string &s)
    { if(s.size()>63) return false; q.push_back(uint8_t(s.size())); q.insert(q.end(), s.begin(), s.end()); return true; };
    while (i <= fqdn.size())
    {
      if (i == fqdn.size() || fqdn[i] == '.')
      {
        std::string lab = fqdn.substr(start, i - start);
        if (!lab.empty() && !push(lab))
          return {};
        start = i + 1;
      }
      ++i;
    }
    q.push_back(0);
    return q;
  };

  struct SRV
  {
    uint16_t priority{}, weight{}, port{};
    std::string target;
    std::string target_key;
  };
  struct HostAddrs
  {
    std::vector<std::string> v4, v6;
  };

  struct Source
  {
    std::string instance;       // display instance FQDN
    std::string instance_label; // parsed label before service
    std::string service;        // parsed service type (e.g. _ndi._tcp)
    std::string domain;         // parsed domain (e.g. local)
    std::string host;           // display host
    uint16_t port{};            // SRV port
    std::vector<std::string> ipv4;
    std::vector<std::string> ipv6;
    std::vector<std::string> txt; // raw key=value strings
  };

  struct Cache
  {
    std::unordered_map<std::string, std::vector<std::string>> ptr_map;
    std::unordered_map<std::string, SRV> srv_map;
    std::unordered_map<std::string, std::vector<std::string>> txt_map;
    std::unordered_map<std::string, HostAddrs> host_addrs;
    std::unordered_map<std::string, std::string> inst_display;
    std::unordered_map<std::string, std::string> host_display;
  };

  inline bool set_nonblocking(int s)
  {
#ifdef _WIN32
    u_long m = 1;
    return ioctlsocket(s, FIONBIO, &m) == 0;
#else
    int fl = fcntl(s, F_GETFL, 0);
    return (fl != -1) && (fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0);
#endif
  }

  inline void closesock(int s)
  {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
  }

  inline int mdns_socket_ipv4(bool &using_unicast_fallback, const DiscoverOptions &opt)
  {
#ifdef _WIN32
    static bool inited = false;
    if (!inited)
    {
      WSADATA w;
      if (WSAStartup(MAKEWORD(2, 2), &w) != 0)
        return -1;
      inited = true;
    }
#endif
    using_unicast_fallback = false;
    int s = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
      return -1;
    int yes = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    BOOL no = FALSE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&no, sizeof(no));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    if (opt.interface_ipv4)
    {
      in_addr ifa{};
      inet_pton(AF_INET, opt.interface_ipv4->c_str(), &ifa);
      setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (char *)&ifa, sizeof(ifa));
    }
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(DNS_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (sockaddr *)&a, sizeof(a)) < 0)
    {
      closesock(s);
      s = (int)socket(AF_INET, SOCK_DGRAM, 0);
      if (s < 0)
        return -1;
#ifdef _WIN32
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
      BOOL no2 = FALSE;
      setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&no2, sizeof(no2));
#else
      setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
      setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
      if (opt.interface_ipv4)
      {
        in_addr ifa{};
        inet_pton(AF_INET, opt.interface_ipv4->c_str(), &ifa);
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (char *)&ifa, sizeof(ifa));
      }
      sockaddr_in any{};
      any.sin_family = AF_INET;
      any.sin_port = htons(0);
      any.sin_addr.s_addr = htonl(INADDR_ANY);
      if (bind(s, (sockaddr *)&any, sizeof(any)) < 0)
      {
        closesock(s);
        return -1;
      }
      using_unicast_fallback = true;
    }
    else
    {

      // Join per-interface memberships to ensure delivery on all NICs (or a specific NIC if set).
      (void)join_mdns_memberships_ipv4(s, opt.interface_ipv4, opt.debug);
      uint8_t ttl = 255;
      setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&ttl, sizeof(ttl));
    }
    set_nonblocking(s);
    return s;
  }

  // Build the list of IPv4 addresses to use for transmitting multicast.
  // If interface_ipv4 is set, returns just that NIC. Otherwise enumerates all
  // UP, non-loopback, multicast-capable IPv4 interfaces.
  inline std::vector<in_addr>
  get_send_ifaces_ipv4(const std::optional<std::string> &interface_ipv4)
  {
    std::vector<in_addr> out;

    if (interface_ipv4 && !interface_ipv4->empty())
    {
      in_addr ifa{};
#ifdef _WIN32
      InetPtonA(AF_INET, interface_ipv4->c_str(), &ifa);
#else
      inet_pton(AF_INET, interface_ipv4->c_str(), &ifa);
#endif
      out.push_back(ifa);
      return out;
    }

#ifdef _WIN32
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    IP_ADAPTER_ADDRESSES *addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size);
    if (ret == ERROR_BUFFER_OVERFLOW)
    {
      buf.resize(size);
      addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
      ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size);
    }
    if (ret == NO_ERROR)
    {
      for (auto *a = addrs; a; a = a->Next)
      {
        if (a->OperStatus != IfOperStatusUp)
          continue;
        for (auto *ua = a->FirstUnicastAddress; ua; ua = ua->Next)
        {
          if (!ua->Address.lpSockaddr)
            continue;
          if (ua->Address.lpSockaddr->sa_family != AF_INET)
            continue;
          auto *sin = reinterpret_cast<sockaddr_in *>(ua->Address.lpSockaddr);
          in_addr ifa = sin->sin_addr;
          uint32_t v = ntohl(ifa.s_addr);
          if ((v & 0xFF000000u) == 0x7F000000u)
            continue; // skip 127/8
          out.push_back(ifa);
        }
      }
    }
#else
    struct ifaddrs *ifas = nullptr;
    if (getifaddrs(&ifas) == 0)
    {
      for (auto *it = ifas; it; it = it->ifa_next)
      {
        if (!it->ifa_addr)
          continue;
        if (it->ifa_addr->sa_family != AF_INET)
          continue;
        unsigned int flags = it->ifa_flags;
        if (!(flags & IFF_UP))
          continue;
        if (flags & IFF_LOOPBACK)
          continue;
#ifdef IFF_MULTICAST
        if (!(flags & IFF_MULTICAST))
          continue;
#endif
        in_addr ifa = reinterpret_cast<sockaddr_in *>(it->ifa_addr)->sin_addr;
        out.push_back(ifa);
      }
      freeifaddrs(ifas);
    }
#endif
    return out;
  }

  // Send the DNS query buffer once per interface, steering transmit with IP_MULTICAST_IF.
  inline void mdns_send_all_ifaces_ipv4(int s, const void *buf, size_t len, const DiscoverOptions &opt)
  {
    sockaddr_in m{};
    m.sin_family = AF_INET;
    m.sin_port = htons(DNS_PORT);
    m.sin_addr.s_addr = inet_addr(MDNS_ADDR4);

    auto ifaces = get_send_ifaces_ipv4(opt.interface_ipv4);
    if (ifaces.empty())
    {
      // fallback: let OS choose
      sendto(s, (const char *)buf, (int)len, 0, (sockaddr *)&m, sizeof(m));
      return;
    }
    for (const auto &ifa : ifaces)
    {
      setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&ifa, sizeof(ifa));
      sendto(s, (const char *)buf, (int)len, 0, (sockaddr *)&m, sizeof(m));
    }
  }

  inline void send_ptr_query_ipv4(int s, const std::string &fqdn, bool qu, const DiscoverOptions &opt)
  {
    uint8_t buf[512]{};
    auto *h = (DnsHeader *)buf;
    h->id = 0;
    h->flags = htons(0x0000);
    h->qdcount = htons(1);
    size_t off = sizeof(DnsHeader);
    auto qn = encode_qname(fqdn);
    if (qn.empty() || off + qn.size() + 4 > sizeof(buf))
      return;
    memcpy(buf + off, qn.data(), qn.size());
    off += qn.size();
    uint16_t qtype = htons(T_PTR), qclass = htons(0x0001 | (qu ? 0x8000 : 0));
    memcpy(buf + off, &qtype, 2);
    off += 2;
    memcpy(buf + off, &qclass, 2);
    off += 2;
    sockaddr_in m{};
    m.sin_family = AF_INET;
    m.sin_port = htons(DNS_PORT);
    m.sin_addr.s_addr = inet_addr(MDNS_ADDR4);
    if (opt.debug)
      std::cerr << "[ndi-mdns] Q PTR " << fqdn << (qu ? " (QU)" : "") << std::endl;
    mdns_send_all_ifaces_ipv4(s, buf, off, opt);
  }

  inline void send_host_query_ipv4(int s, const std::string &host_fqdn, uint16_t qtype, bool qu, const DiscoverOptions &opt)
  {
    uint8_t buf[512]{};
    auto *h = (DnsHeader *)buf;
    h->id = 0;
    h->flags = htons(0x0000);
    h->qdcount = htons(1);
    size_t off = sizeof(DnsHeader);
    auto qn = encode_qname(host_fqdn);
    if (qn.empty() || off + qn.size() + 4 > sizeof(buf))
      return;
    memcpy(buf + off, qn.data(), qn.size());
    off += qn.size();
    uint16_t qt = htons(qtype), qc = htons(0x0001 | (qu ? 0x8000 : 0));
    memcpy(buf + off, &qt, 2);
    off += 2;
    memcpy(buf + off, &qc, 2);
    off += 2;
    sockaddr_in m{};
    m.sin_family = AF_INET;
    m.sin_port = htons(DNS_PORT);
    m.sin_addr.s_addr = inet_addr(MDNS_ADDR4);
    if (opt.debug)
      std::cerr << "[ndi-mdns] Q " << (qtype == T_A ? "A" : "AAAA") << " " << host_fqdn << (qu ? " (QU)" : "") << std::endl;
    mdns_send_all_ifaces_ipv4(s, buf, off, opt);
  }

  inline void send_host_addr_queries(int s, const std::string &host_fqdn, bool qu, const DiscoverOptions &opt)
  {
    send_host_query_ipv4(s, host_fqdn, T_A, qu, opt);
    send_host_query_ipv4(s, host_fqdn, T_AAAA, qu, opt);
  }

  inline std::string read_name(const uint8_t *buf, size_t len, size_t &off, int depth = 0)
  {
    if (depth > 20)
      return {};
    size_t pos = off;
    std::string out;
    bool jumped = false;
    while (pos < len)
    {
      uint8_t lab = buf[pos++];
      if (lab == 0)
      {
        if (!jumped)
          off = pos;
        return out.empty() ? "." : out;
      }
      if ((lab & 0xC0) == 0xC0)
      {
        if (pos >= len)
          return {};
        uint8_t b2 = buf[pos++];
        uint16_t ptr = ((lab & 0x3F) << 8) | b2;
        if (ptr >= len)
          return {};
        size_t p2 = ptr;
        auto rest = read_name(buf, len, p2, depth + 1);
        if (rest.empty())
          return {};
        if (!jumped)
          off = pos;
        if (!out.empty() && out.back() != '.')
          out.push_back('.');
        out += rest;
        return out;
      }
      if (pos + lab > len)
        return {};
      if (!out.empty() && out.back() != '.')
        out.push_back('.');
      out.append(reinterpret_cast<const char *>(buf + pos), lab);
      pos += lab;
    }
    return {};
  }

  inline size_t parse_rr(const uint8_t *buf, size_t len, size_t off, Cache &c, const DiscoverOptions &opt)
  {
    size_t n_off = off;
    auto name_raw = read_name(buf, len, n_off);
    if (name_raw.empty())
      return 0;
    auto name_key = canon(name_raw);
    if (n_off + 10 > len)
      return 0;
    uint16_t type = rd16(buf + n_off);
    uint16_t klass = rd16(buf + n_off + 2);
    uint32_t ttl = rd32(buf + n_off + 4);
    (void)ttl;
    uint16_t rdlen = rd16(buf + n_off + 8);
    size_t rdoff = n_off + 10;
    size_t next = rdoff + rdlen;
    if (next > len)
      return 0;
    if ((klass & 0x7FFF) != 1)
      return next; // IN only

    if (type == T_PTR)
    {
      size_t t = rdoff;
      auto inst_raw = read_name(buf, len, t);
      if (!inst_raw.empty())
      {
        auto inst_key = canon(inst_raw);
        c.ptr_map[name_key].push_back(inst_key);
        c.inst_display.try_emplace(inst_key, inst_raw);
        if (opt.debug && opt.debug_level >= 2)
          std::cerr << "[RR] PTR " << name_raw << " -> " << inst_raw << std::endl;
      }
    }
    else if (type == T_SRV)
    {
      if (rdlen < 6)
        return next;
      uint16_t pr = rd16(buf + rdoff), we = rd16(buf + rdoff + 2), po = rd16(buf + rdoff + 4);
      size_t t = rdoff + 6;
      auto host_raw = read_name(buf, len, t);
      if (!host_raw.empty())
      {
        auto host_key = canon(host_raw);
        c.host_display.try_emplace(host_key, host_raw);
        c.srv_map[name_key] = SRV{pr, we, po, host_raw, host_key};
        if (opt.debug && opt.debug_level >= 2)
          std::cerr << "[RR] SRV " << name_raw << " pr=" << pr << " we=" << we << " port=" << po << " target=" << host_raw << std::endl;
      }
    }
    else if (type == T_TXT)
    {
      std::vector<std::string> vs;
      size_t p = rdoff;
      while (p < rdoff + rdlen)
      {
        uint8_t l = buf[p++];
        if (p + l > rdoff + rdlen)
          break;
        vs.emplace_back(reinterpret_cast<const char *>(buf + p), l);
        p += l;
      }
      c.txt_map[name_key] = std::move(vs);
      if (opt.debug && opt.debug_level >= 2)
        for (auto &s : c.txt_map[name_key])
          std::cerr << "[RR] TXT " << name_raw << " :: " << s << std::endl;
    }
    else if (type == T_A && rdlen == 4)
    {
      char ip[INET_ADDRSTRLEN]{};
      inet_ntop(AF_INET, buf + rdoff, ip, sizeof(ip));
      c.host_addrs[name_key].v4.emplace_back(ip);
      if (opt.debug && opt.debug_level >= 2)
        std::cerr << "[RR] A   " << name_raw << " -> " << ip << std::endl;
    }
    else if (type == T_AAAA && rdlen == 16)
    {
      char ip6[INET6_ADDRSTRLEN]{};
      inet_ntop(AF_INET6, buf + rdoff, ip6, sizeof(ip6));
      c.host_addrs[name_key].v6.emplace_back(ip6);
      if (opt.debug && opt.debug_level >= 2)
        std::cerr << "[RR] AAAA " << name_raw << " -> " << ip6 << std::endl;
    }
    return next;
  }

  inline void recv_once_and_merge(int s, Cache &cache, const DiscoverOptions &opt)
  {
    uint8_t buf[1500];
    sockaddr_in src{};
#ifdef _WIN32
    int slen = sizeof(src);
#else
    socklen_t slen = sizeof(src);
#endif
    int n = recvfrom(s, (char *)buf, sizeof(buf), 0, (sockaddr *)&src, &slen);
    if (n <= 0)
      return;
    if ((size_t)n < sizeof(DnsHeader))
      return;
    DnsHeader h{};
    memcpy(&h, buf, sizeof(h));
    size_t off = sizeof(DnsHeader);
    uint16_t qd = ntohs(h.qdcount), an = ntohs(h.ancount), ns = ntohs(h.nscount), ar = ntohs(h.arcount);
    if (opt.debug)
    {
      char srcip[INET_ADDRSTRLEN]{};
      inet_ntop(AF_INET, &src.sin_addr, srcip, sizeof(srcip));
      std::cerr << "[ndi-mdns] PKT from " << srcip << ":" << ntohs(src.sin_port)
                << " len=" << n << " qd=" << qd << " an=" << an
                << " ns=" << ns << " ar=" << ar << std::endl;
    }
    for (uint16_t i = 0; i < qd; i++)
    {
      auto qn = read_name(buf, n, off);
      if (qn.empty() || off + 4 > (size_t)n)
      {
        off = n;
        break;
      }
      off += 4;
    }
    auto parse_section = [&](uint16_t cnt)
    { for(uint16_t i=0;i<cnt;i++){ size_t nx = parse_rr(buf,n,off,cache,opt); if(!nx){ off=n; return; } off=nx; } };
    parse_section(an);
    parse_section(ns);
    parse_section(ar);
  }

  struct InstanceParts
  {
    std::string label, service, domain;
  };

  inline InstanceParts split_instance_fqdn(const std::string &instance_fqdn)
  {
    InstanceParts out;
    if (instance_fqdn.empty())
      return out;
    std::string s = instance_fqdn;
    while (!s.empty() && s.back() == '.')
      s.pop_back();
    size_t pos = s.find("._");
    if (pos == std::string::npos)
    {
      out.label = s;
      return out;
    }
    out.label = s.substr(0, pos);
    std::string rest = s.substr(pos + 1);
    size_t i = 0;
    while (i < rest.size())
    {
      size_t dot = rest.find('.', i);
      std::string lab = (dot == std::string::npos) ? rest.substr(i) : rest.substr(i, dot - i);
      if (!lab.empty() && lab[0] == '_')
      {
        if (!out.service.empty())
          out.service.push_back('.');
        out.service += lab;
        if (dot == std::string::npos)
        {
          i = rest.size();
          break;
        }
        i = dot + 1;
      }
      else
      {
        out.domain = (dot == std::string::npos) ? lab : rest.substr(i);
        break;
      }
    }
    return out;
  }

  inline std::vector<Source> consolidate(Cache &c, const std::vector<std::string> &services)
  {
    std::vector<Source> out;
    std::vector<std::string> svc_keys;
    svc_keys.reserve(services.size());
    for (auto &svc : services)
      svc_keys.push_back(canon(svc));
    for (auto &svc_key : svc_keys)
    {
      auto it = c.ptr_map.find(svc_key);
      std::vector<std::string> insts;
      if (it != c.ptr_map.end())
        insts = it->second;
      else
      {
        // No PTR entries for this service; conservatively include SRV-only instances
        // whose parsed service matches this service key. This handles devices that
        // advertise SRV without PTR (UHD-NDI3-X30 observed behavior).
        for (const auto &kv : c.srv_map)
        {
          auto inst_name = kv.first; // instance key (canonicalized instance FQDN)
          auto parts = split_instance_fqdn(inst_name);
          if (!parts.service.empty() && canon(parts.service + ".") == svc_key)
            insts.push_back(inst_name);
        }
        if (insts.empty())
          continue;
      }
      std::sort(insts.begin(), insts.end());
      insts.erase(std::unique(insts.begin(), insts.end()), insts.end());
      for (auto &inst_key : insts)
      {
        Source s;
        if (auto di = c.inst_display.find(inst_key); di != c.inst_display.end())
          s.instance = di->second;
        else
          s.instance = inst_key;
        {
          auto parts = split_instance_fqdn(s.instance);
          s.instance_label = parts.label;
          s.service = parts.service;
          s.domain = parts.domain;
        }
        if (auto srv_it = c.srv_map.find(inst_key); srv_it != c.srv_map.end())
        {
          const auto &srv = srv_it->second;
          s.port = srv.port;
          s.host = c.host_display.count(srv.target_key) ? c.host_display[srv.target_key] : srv.target;
          if (auto ha = c.host_addrs.find(srv.target_key); ha != c.host_addrs.end())
          {
            s.ipv4 = ha->second.v4;
            s.ipv6 = ha->second.v6;
          }
        }
        if (auto tx = c.txt_map.find(inst_key); tx != c.txt_map.end())
          s.txt = tx->second;
        out.push_back(std::move(s));
      }
    }
    auto key = [](const Source &s)
    { return canon(s.instance) + "|" + canon(s.host) + "|" + std::to_string(s.port); };
    std::unordered_map<std::string, size_t> seen;
    std::vector<Source> dedup;
    for (auto &s : out)
    {
      auto k = key(s);
      if (seen.find(k) == seen.end())
      {
        seen[k] = dedup.size();
        dedup.push_back(s);
      }
      else
      {
        auto &d = dedup[seen[k]];
        d.ipv4.insert(d.ipv4.end(), s.ipv4.begin(), s.ipv4.end());
        d.ipv6.insert(d.ipv6.end(), s.ipv6.begin(), s.ipv6.end());
        d.txt.insert(d.txt.end(), s.txt.begin(), s.txt.end());
      }
    }
    auto uniq = [](std::vector<std::string> &v)
    { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
    for (auto &d : dedup)
    {
      uniq(d.ipv4);
      uniq(d.ipv6);
      uniq(d.txt);
    }
    return dedup;
  }

  inline std::optional<std::string> extract_ipv4(const std::string &s)
  {
    int dots = 0, run = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < s.size(); ++i)
    {
      char c = s[i];
      if ((c >= '0' && c <= '9') || c == '.')
      {
        if (run == 0)
        {
          start = i;
          dots = 0;
        }
        run++;
        if (c == '.')
          dots++;
      }
      else
      {
        if (run >= 7 && dots == 3)
          break;
        run = 0;
        start = std::string::npos;
      }
    }
    if (run >= 7 && dots == 3 && start != std::string::npos)
      return s.substr(start, run);
    return std::nullopt;
  }

  // Enumerate candidate IPv4 addresses for interfaces (string form). Reuses
  // platform-specific logic similar to join_mdns_memberships_ipv4 but returns
  // textual addresses for comparison/tracking.
  inline std::vector<std::string> enumerate_ipv4_ifaces()
  {
    std::vector<std::string> out;
#ifdef _WIN32
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf(size);
    IP_ADAPTER_ADDRESSES *addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
    ULONG ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size);
    if (ret == ERROR_BUFFER_OVERFLOW)
    {
      buf.resize(size);
      addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data());
      ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size);
    }
    if (ret == NO_ERROR)
    {
      for (auto *a = addrs; a; a = a->Next)
      {
        if (a->OperStatus != IfOperStatusUp)
          continue;
        for (auto *ua = a->FirstUnicastAddress; ua; ua = ua->Next)
        {
          if (!ua->Address.lpSockaddr)
            continue;
          if (ua->Address.lpSockaddr->sa_family != AF_INET)
            continue;
          auto *sin = reinterpret_cast<sockaddr_in *>(ua->Address.lpSockaddr);
          char buf2[INET_ADDRSTRLEN] = {0};
          inet_ntop(AF_INET, &sin->sin_addr, buf2, sizeof(buf2));
          std::string s(buf2);
          // skip loopback
          if (s.rfind("127.", 0) == 0)
            continue;
          out.push_back(s);
        }
      }
    }
#else
    struct ifaddrs *ifas = nullptr;
    if (getifaddrs(&ifas) == 0)
    {
      for (auto *it = ifas; it; it = it->ifa_next)
      {
        if (!it->ifa_addr)
          continue;
        if (it->ifa_addr->sa_family != AF_INET)
          continue;
        unsigned int flags = it->ifa_flags;
        if (!(flags & IFF_UP))
          continue;
        if (flags & IFF_LOOPBACK)
          continue;
#ifdef IFF_MULTICAST
        if (!(flags & IFF_MULTICAST))
          continue;
#endif
        char buf2[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(it->ifa_addr)->sin_addr, buf2, sizeof(buf2));
        out.emplace_back(buf2);
      }
      freeifaddrs(ifas);
    }
#endif
    // unique
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
  }

  inline void debug_dump_summary(const Cache &C)
  {
    size_t inst_total = 0;
    for (const auto &kv : C.ptr_map)
      inst_total += kv.second.size();
    std::cerr << "[ndi-mdns] cache: services=" << C.ptr_map.size()
              << " instances=" << inst_total
              << " srv=" << C.srv_map.size()
              << " hosts=" << C.host_addrs.size() << std::endl;
    for (const auto &kv : C.ptr_map)
    {
      std::cerr << "  svc " << kv.first << " insts=" << kv.second.size() << std::endl;
    }
  }

  inline void debug_report_instances(const Cache &C, const std::vector<std::string> &services)
  {
    for (const auto &svc : services)
    {
      auto svc_key = canon(svc);
      if (auto it = C.ptr_map.find(svc_key); it != C.ptr_map.end())
      {
        auto insts = it->second;
        std::sort(insts.begin(), insts.end());
        insts.erase(std::unique(insts.begin(), insts.end()), insts.end());
        for (const auto &inst_key : insts)
        {
          const auto &inst_disp = (C.inst_display.count(inst_key) ? C.inst_display.at(inst_key) : inst_key);
          std::cerr << "[inst] " << inst_disp;
          if (auto s = C.srv_map.find(inst_key); s != C.srv_map.end())
          {
            const auto &srv = s->second;
            const auto &host_disp = (C.host_display.count(srv.target_key) ? C.host_display.at(srv.target_key) : srv.target);
            std::cerr << " | SRV " << host_disp << ":" << srv.port;
            if (auto ha = C.host_addrs.find(srv.target_key); ha != C.host_addrs.end())
              std::cerr << " | A=" << ha->second.v4.size() << " AAAA=" << ha->second.v6.size();
            else
              std::cerr << " | (no A/AAAA yet)";
          }
          else
          {
            std::cerr << " | (no SRV yet)";
          }
          std::cerr << std::endl;
        }
      }
    }
  }

  // Persistent-instance class version
  class NdiMdns
  {
  public:
    explicit NdiMdns(const DiscoverOptions &options)
        : opt(options)
    {
      init_socket();
    }
    ~NdiMdns()
    {
      cleanup_socket();
    }
    NdiMdns(const NdiMdns &) = delete;
    NdiMdns &operator=(const NdiMdns &) = delete;
    std::vector<Source> discover()
    {
      bool qu = using_unicast_fallback;
      Cache C;
      std::unordered_set<std::string> asked_hosts;
      if (sock < 0)
        return {};
      if (opt.debug)
      {
        std::cerr << "[ndi-mdns] (instance) using: "
                  << (qu ? "ephemeral (QU fallback)" : "5353 multicast") << std::endl;
        if (opt.interface_ipv4)
          std::cerr << "[ndi-mdns] iface: " << *opt.interface_ipv4 << std::endl;
      }

      flush_sock_recv_buffer(sock);
      for (int i = 0; i < opt.query_bursts; i++)
        for (const auto &svc : opt.services)
        {
          send_ptr_query_ipv4(sock, svc, qu, opt);
        }

      auto end = std::chrono::steady_clock::now() + opt.timeout;
      auto next_requery = std::chrono::steady_clock::now();
      auto next_status = std::chrono::steady_clock::now();
      auto next_reenum = std::chrono::steady_clock::now() + std::chrono::milliseconds(opt.reenumerate_interval_ms > 0 ? opt.reenumerate_interval_ms : INT_MAX);
      // Initialize joined_ifaces by attempting to join candidate interfaces and
      // only record those that actually succeeded.
      if (opt.reenumerate_interval_ms > 0)
      {
        auto cand = enumerate_ipv4_ifaces();
        for (auto &ip : cand)
        {
          in_addr ifa{};
          inet_pton(AF_INET, ip.c_str(), &ifa);
          ip_mreq m{};
          m.imr_multiaddr.s_addr = inet_addr(MDNS_ADDR4);
          m.imr_interface = ifa;
          int res = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&m, sizeof(m));
          if (res == 0)
          {
            joined_ifaces.insert(ip);
            if (opt.debug)
              std::cerr << "[ndi-mdns] initial reenum: joined " << ip << std::endl;
          }
          else if (opt.debug)
          {
            std::cerr << "[ndi-mdns] initial reenum: failed to join " << ip << " err=" << errno << std::endl;
          }
        }
      }
      while (std::chrono::steady_clock::now() < end)
      {
        if (std::chrono::steady_clock::now() >= next_requery)
        {
          for (const auto &svc : opt.services)
          {
            send_ptr_query_ipv4(sock, svc, qu, opt);
          }
          next_requery = std::chrono::steady_clock::now() + std::chrono::milliseconds(opt.requery_interval_ms);
        }
        recv_once_and_merge(sock, C, opt);
        if (opt.active_addr_queries)
        {
          for (const auto &kv : C.srv_map)
          {
            const auto &host_key = kv.second.target_key;
            if (host_key.empty())
              continue;
            if (C.host_addrs.find(host_key) == C.host_addrs.end())
              if (asked_hosts.insert(host_key).second)
                send_host_addr_queries(sock, (C.host_display.count(host_key) ? C.host_display[host_key] : kv.second.target), qu, opt);
          }
        }
#ifdef _WIN32
        Sleep(20);
#else
        usleep(20 * 1000);
#endif
        // Periodic re-enumeration: add/drop multicast memberships for interfaces that changed
        if (opt.reenumerate_interval_ms > 0 && std::chrono::steady_clock::now() >= next_reenum)
        {
          auto cand = enumerate_ipv4_ifaces();
          std::unordered_set<std::string> nowset(cand.begin(), cand.end());
          // Add new
          for (auto &ip : cand)
          {
            if (joined_ifaces.find(ip) == joined_ifaces.end())
            {
              in_addr ifa{};
              inet_pton(AF_INET, ip.c_str(), &ifa);
              ip_mreq m{};
              m.imr_multiaddr.s_addr = inet_addr(MDNS_ADDR4);
              m.imr_interface = ifa;
              int res = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&m, sizeof(m));
              if (res == 0)
              {
                joined_ifaces.insert(ip);
                if (opt.debug)
                  std::cerr << "[ndi-mdns] reenum: joined " << ip << std::endl;
              }
              else if (opt.debug)
                std::cerr << "[ndi-mdns] reenum: failed to join " << ip << " err=" << errno << std::endl;
            }
          }
          // Remove gone
          std::vector<std::string> to_remove;
          for (const auto &j : joined_ifaces)
          {
            if (nowset.find(j) == nowset.end())
              to_remove.push_back(j);
          }
          for (auto &ip : to_remove)
          {
            in_addr ifa{};
            inet_pton(AF_INET, ip.c_str(), &ifa);
            ip_mreq m{};
            m.imr_multiaddr.s_addr = inet_addr(MDNS_ADDR4);
            m.imr_interface = ifa;
            int res = setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *)&m, sizeof(m));
            if (opt.debug)
              std::cerr << "[ndi-mdns] reenum: drop " << ip << " -> " << (res == 0 ? "OK" : "ERR") << std::endl;
            joined_ifaces.erase(ip);
          }
          next_reenum = std::chrono::steady_clock::now() + std::chrono::milliseconds(opt.reenumerate_interval_ms);
        }
        if (opt.debug && opt.debug_level >= 1)
        {
          if (std::chrono::steady_clock::now() >= next_status)
          {
            debug_dump_summary(C);
            if (opt.debug_level >= 2)
              debug_report_instances(C, opt.services);
            next_status = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
          }
        }
      }
      auto out = consolidate(C, opt.services);
      if (opt.debug)
        std::cerr << "[ndi-mdns] consolidated sources=" << out.size() << std::endl;
      for (auto &sref : out)
      {
        if (sref.ipv4.empty())
        {
          if (auto ip = extract_ipv4(sref.instance))
            sref.ipv4.push_back(*ip);
        }
      }
      return out;
    }

  private:
    DiscoverOptions opt;
    int sock = -1;
    bool using_unicast_fallback = false;
    std::unordered_set<std::string> joined_ifaces;

    static void flush_sock_recv_buffer(int s)
    {
      uint8_t buf[2048];
      sockaddr_in src;
#ifdef _WIN32
      int slen = sizeof(src);
      int n;
      // Drain all available packets in non-blocking mode
      do
      {
        n = recvfrom(s, (char *)buf, sizeof(buf), 0, (sockaddr *)&src, &slen);
      } while (n > 0);
#else
      socklen_t slen = sizeof(src);
      int n;
      do
      {
        n = recvfrom(s, (char *)buf, sizeof(buf), MSG_DONTWAIT, (sockaddr *)&src, &slen);
      } while (n > 0);
#endif
    }

    void init_socket()
    {
      sock = mdns_socket_ipv4(using_unicast_fallback, opt);
    }
    void cleanup_socket()
    {
      if (sock >= 0)
      {
        closesock(sock);
        sock = -1;
      }
#ifdef _WIN32
      WSACleanup();
#endif
    }
  };
  // Maintain statics for backwards compatibility
  inline std::vector<Source> discover(const DiscoverOptions &opt)
  {
    NdiMdns inst(opt);
    return inst.discover();
  }

  inline std::vector<Source> discover(std::chrono::milliseconds timeout,
                                      const std::string &service = "_ndi._tcp.local.",
                                      int query_bursts = 2)
  {
    DiscoverOptions opt;
    opt.timeout = timeout;
    opt.services = {service};
    opt.query_bursts = query_bursts;
    return discover(opt);
  }

  inline std::vector<Source> sniff(std::chrono::milliseconds timeout,
                                   const std::string &service = "_ndi._tcp.local.")
  {
    DiscoverOptions opt;
    opt.timeout = timeout;
    opt.services = {service};
    opt.query_bursts = 0;
    opt.active_addr_queries = false;
    return discover(opt);
  }

  struct UrlOptions
  {
    bool https_first = false;
    int http_port = 80;
    int https_port = 443;
    bool include_ipv6 = true;
    bool include_link_local_ipv6 = false;
  };

  inline bool is_link_local_v6_prefix(const std::string &ip6)
  {
    if (ip6.size() < 4)
      return false;
    char a = std::tolower((unsigned char)ip6[0]);
    char b = std::tolower((unsigned char)ip6[1]);
    char c = std::tolower((unsigned char)ip6[2]);
    char d = std::tolower((unsigned char)ip6[3]);
    return a == 'f' && b == 'e' && c == '8' && d == '0';
  }

  inline std::vector<std::string> make_candidate_urls(const Source &s,
                                                      const UrlOptions &opt,
                                                      bool hostname_conflict)
  {
    std::vector<std::string> urls;
    urls.reserve(8);
    auto add = [&](const std::string &scheme, const std::string &host, int port, bool ipv6)
    {
      std::string u = scheme + "://";
      if (ipv6)
        u += "[" + host + "]";
      else
        u += host;
      if ((scheme == "http" && port != 80) ||
          (scheme == "https" && port != 443))
        u += ":" + std::to_string(port);
      u += "/";
      urls.push_back(std::move(u));
    };
    auto push_host = [&]()
    {
      if (s.host.empty())
        return;
      if (opt.https_first)
      {
        add("https", s.host, opt.https_port, false);
        add("http", s.host, opt.http_port, false);
      }
      else
      {
        add("http", s.host, opt.http_port, false);
        add("https", s.host, opt.https_port, false);
      }
    };
    auto push_ipv4s = [&]()
    {
      for (const auto &ip : s.ipv4)
      {
        if (opt.https_first)
        {
          add("https", ip, opt.https_port, false);
          add("http", ip, opt.http_port, false);
        }
        else
        {
          add("http", ip, opt.http_port, false);
          add("https", ip, opt.https_port, false);
        }
      }
    };
    auto push_ipv6s = [&]()
    {
      if (!opt.include_ipv6)
        return;
      for (const auto &ip6 : s.ipv6)
      {
        if (is_link_local_v6_prefix(ip6) && !opt.include_link_local_ipv6)
          continue;
        if (opt.https_first)
        {
          add("https", ip6, opt.https_port, true);
          add("http", ip6, opt.http_port, true);
        }
        else
        {
          add("http", ip6, opt.http_port, true);
          add("https", ip6, opt.https_port, true);
        }
      }
    };
    if (!hostname_conflict)
    {
      push_host();
      push_ipv4s();
      push_ipv6s();
    }
    else
    {
      push_ipv4s();
      push_ipv6s();
      push_host();
    }
    std::vector<std::string> dedup;
    dedup.reserve(urls.size());
    std::unordered_set<std::string> seen;
    for (auto &u : urls)
      if (seen.insert(u).second)
        dedup.push_back(u);
    return dedup;
  }

  inline std::vector<std::string> make_candidate_urls_safe(const Source &s,
                                                           const std::vector<Source> &all,
                                                           const UrlOptions &opt = {})
  {
    bool conflict = false;
    if (!s.host.empty())
    {
      std::unordered_map<std::string, int> counts;
      for (const auto &x : all)
      {
        if (!x.host.empty())
          counts[canon(x.host)]++;
      }
      conflict = counts[canon(s.host)] > 1;
    }
    return make_candidate_urls(s, opt, conflict);
  }

  inline std::unordered_set<std::string> duplicated_host_keys(const std::vector<Source> &list)
  {
    std::unordered_map<std::string, int> counts;
    for (const auto &s : list)
    {
      if (!s.host.empty())
        counts[canon(s.host)]++;
    }
    std::unordered_set<std::string> dup;
    for (const auto &kv : counts)
      if (kv.second > 1)
        dup.insert(kv.first);
    return dup;
  }
} // namespace ndi_mdns

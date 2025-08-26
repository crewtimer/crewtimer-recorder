#include "ndi_mdns.hpp"

int mdns_main_test()
{
  ndi_mdns::DiscoverOptions opt;
  opt.timeout = std::chrono::seconds(5);
  opt.debug = false; // set true for [DBG] lines
  opt.debug_level = 2;
  // opt.interface_ipv4 = "10.0.1.5";   // pick your NIC if needed

  auto list = ndi_mdns::discover(opt);

  for (auto &s : list)
  {
    // std::cout << s.instance << " -> " << s.host << ":" << s.port << "\n";
    std::cout << s.instance_label << " [" << s.service << "." << s.domain << "] -> "
              << s.host << ":" << s.port << std::endl;
    for (auto &ip : s.ipv4)
      std::cout << "  A    " << ip << "\n";
    for (auto &ip : s.ipv6)
      std::cout << "  AAAA " << ip << "\n";
    for (auto &kv : s.txt)
      std::cout << "  TXT  " << kv << "\n";
  }

  ndi_mdns::UrlOptions uopt;
  uopt.https_first = false; // try http:// first
  uopt.include_link_local_ipv6 = false;

  for (auto &s : list)
  {
    auto urls = ndi_mdns::make_candidate_urls_safe(s, list, uopt);
    for (auto &u : urls)
    {
      std::cout << "try " << u << std::endl;
      // attempt fetch, break on success...
    }
  }
  return 0;
}

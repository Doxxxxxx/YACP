#ifndef SIMULATOR
#include "TcpDontFragment.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_netif_net_stack.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip.h>
#include <lwip/netif.h>
#include <lwip/prot/ip4.h>

namespace {
netif_output_fn s_originalOutput = nullptr;

// Runs in the lwIP tcpip task with p->payload pointing at the finished IPv4 header.
err_t outputWithDontFragment(struct netif* netif, struct pbuf* p, const ip4_addr_t* ipaddr) {
  if (p->len >= IP_HLEN) {
    auto* iphdr = static_cast<struct ip_hdr*>(p->payload);
    const u16_t offset = lwip_ntohs(IPH_OFFSET(iphdr));
    // Whole TCP packets only: lwIP has already split anything larger than the MTU into fragments.
    if (IPH_V(iphdr) == 4 && IPH_PROTO(iphdr) == IP_PROTO_TCP && (offset & (IP_MF | IP_OFFMASK)) == 0 &&
        (offset & IP_DF) == 0) {
      IPH_OFFSET_SET(iphdr, lwip_htons(offset | IP_DF));
      IPH_CHKSUM_SET(iphdr, 0);
      IPH_CHKSUM_SET(iphdr, inet_chksum(iphdr, IPH_HL_BYTES(iphdr)));
    }
  }
  return s_originalOutput(netif, p, ipaddr);
}
}  // namespace

void applyTcpDontFragment() {
  auto* netif = static_cast<struct netif*>(esp_netif_get_netif_impl(WiFi.STA.netif()));
  if (!netif || !netif->output) {
    LOG_ERR("WIFI", "STA netif unavailable, TCP Don't-Fragment not applied");
    return;
  }
  if (netif->output == outputWithDontFragment) return;
  s_originalOutput = netif->output;
  netif->output = outputWithDontFragment;
  LOG_INF("WIFI", "TCP Don't-Fragment enabled on STA netif");
}
#endif

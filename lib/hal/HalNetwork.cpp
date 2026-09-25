#include "HalNetwork.h"

#include <Logging.h>

#ifndef SIMULATOR
#include <WiFi.h>
#include <esp_netif_net_stack.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip.h>
#include <lwip/netif.h>
#include <lwip/prot/ip4.h>
#include <lwip/tcpip.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#endif

HalNetwork halNetwork;

#ifndef SIMULATOR
namespace {
netif_output_fn originalStationOutput = nullptr;

err_t outputWithTcpDontFragment(struct netif* netif, struct pbuf* packet, const ip4_addr_t* address) {
  if (packet != nullptr && packet->len >= IP_HLEN) {
    auto* header = static_cast<uint8_t*>(packet->payload);
    const uint8_t versionAndLength = header[offsetof(struct ip_hdr, _v_hl)];
    const uint8_t protocol = header[offsetof(struct ip_hdr, _proto)];
    const uint16_t headerLength = static_cast<uint16_t>(versionAndLength & 0x0fU) * 4U;
    uint16_t offset = 0;
    memcpy(&offset, header + offsetof(struct ip_hdr, _offset), sizeof(offset));
    offset = lwip_ntohs(offset);

    if ((versionAndLength >> 4U) == 4U && protocol == IP_PROTO_TCP && headerLength >= IP_HLEN &&
        packet->len >= headerLength && (offset & (IP_MF | IP_OFFMASK)) == 0U && (offset & IP_DF) == 0U) {
      offset |= IP_DF;
      const uint16_t networkOffset = lwip_htons(offset);
      memcpy(header + offsetof(struct ip_hdr, _offset), &networkOffset, sizeof(networkOffset));

      uint16_t zero = 0;
      memcpy(header + offsetof(struct ip_hdr, _chksum), &zero, sizeof(zero));
      const uint16_t checksum = inet_chksum(header, headerLength);
      memcpy(header + offsetof(struct ip_hdr, _chksum), &checksum, sizeof(checksum));
    }
  }

  if (originalStationOutput == nullptr) {
    return ERR_IF;
  }
  return originalStationOutput(netif, packet, address);
}

struct HookInstallResult {
  bool installed = false;
};

void installStationOutputHook(void* context) {
  auto* result = static_cast<HookInstallResult*>(context);
  auto* stationNetif = static_cast<struct netif*>(esp_netif_get_netif_impl(WiFi.STA.netif()));
  if (stationNetif == nullptr || stationNetif->output == nullptr) {
    return;
  }

  if (stationNetif->output == outputWithTcpDontFragment) {
    result->installed = true;
    return;
  }

  originalStationOutput = stationNetif->output;
  stationNetif->output = outputWithTcpDontFragment;
  result->installed = true;
}
}  // namespace
#endif

bool HalNetwork::enableTcpDontFragmentOnStation() {
#ifdef SIMULATOR
  return true;
#else
  HookInstallResult result;
  const err_t callbackResult = tcpip_callback_wait(installStationOutputHook, &result);
  if (callbackResult != ERR_OK || !result.installed) {
    LOG_ERR("NET", "Failed to enable TCP IPv4 Don't Fragment (callback=%d installed=%d)", callbackResult,
            result.installed);
    return false;
  }

  LOG_INF("NET", "Enabled TCP IPv4 Don't Fragment on WiFi station output");
  return true;
#endif
}

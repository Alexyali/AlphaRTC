/*
 *  Copyright 2009 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_FAKE_NETWORK_H_
#define RTC_BASE_FAKE_NETWORK_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "rtc_base/checks.h"
#include "rtc_base/fake_mdns_responder.h"
#include "rtc_base/message_handler.h"
#include "rtc_base/network.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/thread.h"

namespace rtc {

const int kFakeIPv4NetworkPrefixLength = 24;
const int kFakeIPv6NetworkPrefixLength = 64;

// Fake network manager that allows us to manually specify the IPs to use.
class FakeNetworkManager : public NetworkManagerBase, public MessageHandler {
 public:
  FakeNetworkManager() {}

  typedef std::vector<std::pair<SocketAddress, AdapterType>> IfaceList;

  void AddInterface(const SocketAddress& iface) {
    // Ensure a unique name for the interface if its name is not given.
    AddInterface(iface, "test" + rtc::ToString(next_index_++));
  }

  void AddInterface(const SocketAddress& iface, const std::string& if_name) {
    AddInterface(iface, if_name, ADAPTER_TYPE_UNKNOWN);
  }

  void AddInterface(const SocketAddress& iface,
                    const std::string& if_name,
                    AdapterType type) {
    SocketAddress address(if_name, 0);
    address.SetResolvedIP(iface.ipaddr());
    ifaces_.push_back(std::make_pair(address, type));
    DoUpdateNetworks();
  }

  void RemoveInterface(const SocketAddress& iface) {
    for (IfaceList::iterator it = ifaces_.begin(); it != ifaces_.end(); ++it) {
      if (it->first.EqualIPs(iface)) {
        ifaces_.erase(it);
        break;
      }
    }
    DoUpdateNetworks();
  }

  virtual void StartUpdating() {
    ++start_count_;
    if (start_count_ == 1) {
      sent_first_update_ = false;
      rtc::Thread::Current()->Post(RTC_FROM_HERE, this);
    } else {
      if (sent_first_update_) {
        SignalNetworksChanged();
      }
    }
  }

  virtual void StopUpdating() { --start_count_; }

  // MessageHandler interface.
  virtual void OnMessage(Message* msg) { DoUpdateNetworks(); }

  void CreateMdnsResponder(rtc::Thread* network_thread) {
    if (mdns_responder_ == nullptr) {
      mdns_responder_ =
          absl::make_unique<webrtc::FakeMdnsResponder>(network_thread);
    }
  }

  using NetworkManagerBase::set_enumeration_permission;
  using NetworkManagerBase::set_default_local_addresses;

  // rtc::NetworkManager override.
  webrtc::MdnsResponderInterface* GetMdnsResponder() const override {
    return mdns_responder_.get();
  }

  webrtc::FakeMdnsResponder* GetMdnsResponderForTesting() const {
    return mdns_responder_.get();
  }

 private:
  void DoUpdateNetworks() {
    if (start_count_ == 0)
      return;
    std::vector<Network*> networks;
    for (IfaceList::iterator it = ifaces_.begin(); it != ifaces_.end(); ++it) {
      int prefix_length = 0;
      if (it->first.ipaddr().family() == AF_INET) {
        prefix_length = kFakeIPv4NetworkPrefixLength;
      } else if (it->first.ipaddr().family() == AF_INET6) {
        prefix_length = kFakeIPv6NetworkPrefixLength;
      }
      IPAddress prefix = TruncateIP(it->first.ipaddr(), prefix_length);
      std::unique_ptr<Network> net(new Network(it->first.hostname(),
                                               it->first.hostname(), prefix,
                                               prefix_length, it->second));
      net->set_default_local_address_provider(this);
      net->AddIP(it->first.ipaddr());
      networks.push_back(net.release());
    }
    bool changed;
    MergeNetworkList(networks, &changed);
    if (changed || !sent_first_update_) {
      SignalNetworksChanged();
      sent_first_update_ = true;
    }
  }

  IfaceList ifaces_;
  int next_index_ = 0;
  int start_count_ = 0;
  bool sent_first_update_ = false;

  std::unique_ptr<webrtc::FakeMdnsResponder> mdns_responder_;
};

}  // namespace rtc

#endif  // RTC_BASE_FAKE_NETWORK_H_
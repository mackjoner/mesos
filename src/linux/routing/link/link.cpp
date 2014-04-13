/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/if.h> // Must be included after sys/socket.h.

#include <netlink/errno.h>
#include <netlink/socket.h>

#include <netlink/route/link.h>

#include <netlink/route/link/veth.h>

#include <stout/error.hpp>
#include <stout/none.hpp>
#include <stout/os.hpp>

#include "linux/routing/internal.hpp"

#include "linux/routing/link/internal.hpp"
#include "linux/routing/link/link.hpp"

using std::string;

namespace routing {
namespace link {

Try<bool> exists(const string& _link)
{
  Result<Netlink<struct rtnl_link> > link = internal::get(_link);
  if (link.isError()) {
    return Error(link.error());
  }
  return link.isSome();
}


Try<bool> create(
    const string& veth,
    const string& peer,
    const Option<pid_t>& pid)
{
  Try<Netlink<struct nl_sock> > sock = routing::socket();
  if (sock.isError()) {
    return Error(sock.error());
  }

  int err = rtnl_link_veth_add(
      sock.get().get(),
      veth.c_str(),
      peer.c_str(),
      (pid.isNone() ? getpid() : pid.get()));

  if (err != 0) {
    if (err == -NLE_EXIST) {
      return false;
    }
    return Error(nl_geterror(err));
  }

  return true;
}


Try<bool> remove(const string& _link)
{
  Result<Netlink<struct rtnl_link> > link = internal::get(_link);
  if (link.isError()) {
    return Error(link.error());
  } else if (link.isNone()) {
    return false;
  }

  Try<Netlink<struct nl_sock> > sock = routing::socket();
  if (sock.isError()) {
    return Error(sock.error());
  }

  int err = rtnl_link_delete(sock.get().get(), link.get().get());
  if (err != 0) {
    if (err == -NLE_OBJ_NOTFOUND) {
      return false;
    }
    return Error(nl_geterror(err));
  }

  return true;
}


Result<int> index(const string& _link)
{
  Result<Netlink<struct rtnl_link> > link = internal::get(_link);
  if (link.isError()) {
    return Error(link.error());
  } else if (link.isNone()) {
    return None();
  }

  return rtnl_link_get_ifindex(link.get().get());
}


Result<string> name(int index)
{
  Result<Netlink<struct rtnl_link> > link = internal::get(index);
  if (link.isError()) {
    return Error(link.error());
  } else if (link.isNone()) {
    return None();
  }

  return rtnl_link_get_name(link.get().get());
}


Result<bool> isUp(const string& link)
{
  return internal::test(link, IFF_UP);
}


Try<bool> setUp(const string& link)
{
  return internal::set(link, IFF_UP);
}


Try<bool> setMAC(const string& link, const net::MAC& mac)
{
  // TODO(jieyu): We use ioctl to set the MAC address because the
  // interfaces in libnl have some issues with virtual devices.
  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));

  strncpy(ifr.ifr_name, link.c_str(), IFNAMSIZ);

  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    return ErrnoError();
  }

  // Since loopback interface has sa_family ARPHRD_LOOPBACK, we need
  // to get the MAC address of the link first to decide what value the
  // sa_family should be.
  if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1) {
    if (errno == ENODEV) {
      os::close(fd);
      return false;
    } else {
      // Save the error string as os::close may overwrite errno.
      const string message = strerror(errno);
      os::close(fd);
      return Error(message);
    }
  }

  ifr.ifr_hwaddr.sa_data[0] = mac[0];
  ifr.ifr_hwaddr.sa_data[1] = mac[1];
  ifr.ifr_hwaddr.sa_data[2] = mac[2];
  ifr.ifr_hwaddr.sa_data[3] = mac[3];
  ifr.ifr_hwaddr.sa_data[4] = mac[4];
  ifr.ifr_hwaddr.sa_data[5] = mac[5];

  if (ioctl(fd, SIOCSIFHWADDR, &ifr) == -1) {
    if (errno == ENODEV) {
      os::close(fd);
      return false;
    } else {
      // Save the error string as os::close may overwrite errno.
      const string message = strerror(errno);
      os::close(fd);
      return Error(message);
    }
  }

  os::close(fd);
  return true;
}


Result<hashmap<string, uint64_t> > statistics(const string& _link)
{
  Result<Netlink<struct rtnl_link> > link = internal::get(_link);
  if (link.isError()) {
    return Error(link.error());
  } else if (link.isNone()) {
    return None();
  }

  rtnl_link_stat_id_t stats[] = {
    // Statistics related to receiving.
    RTNL_LINK_RX_PACKETS,
    RTNL_LINK_RX_BYTES,
    RTNL_LINK_RX_ERRORS,
    RTNL_LINK_RX_DROPPED,
    RTNL_LINK_RX_COMPRESSED,
    RTNL_LINK_RX_FIFO_ERR,
    RTNL_LINK_RX_LEN_ERR,
    RTNL_LINK_RX_OVER_ERR,
    RTNL_LINK_RX_CRC_ERR,
    RTNL_LINK_RX_FRAME_ERR,
    RTNL_LINK_RX_MISSED_ERR,
    RTNL_LINK_MULTICAST,

    // Statistics related to sending.
    RTNL_LINK_TX_PACKETS,
    RTNL_LINK_TX_BYTES,
    RTNL_LINK_TX_ERRORS,
    RTNL_LINK_TX_DROPPED,
    RTNL_LINK_TX_COMPRESSED,
    RTNL_LINK_TX_FIFO_ERR,
    RTNL_LINK_TX_ABORT_ERR,
    RTNL_LINK_TX_CARRIER_ERR,
    RTNL_LINK_TX_HBEAT_ERR,
    RTNL_LINK_TX_WIN_ERR,
    RTNL_LINK_COLLISIONS,
  };

  hashmap<string, uint64_t> results;

  char buf[32];
  size_t size = sizeof(stats) / sizeof(stats[0]);

  for (size_t i = 0; i < size; i++) {
    rtnl_link_stat2str(stats[i], buf, 32);
    results[buf] = rtnl_link_get_stat(link.get().get(), stats[i]);
  }

  return results;
}

} // namespace link {
} // namespace routing {
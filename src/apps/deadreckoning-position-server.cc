#include "ns3/log.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/address-utils.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "deadreckoning-position-server.h"

#include <sstream>
#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("DeadreckoningPositionServerApplication");

NS_OBJECT_ENSURE_REGISTERED(DeadreckoningPositionServer);

TypeId DeadreckoningPositionServer::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::DeadreckoningPositionServer")
    .SetParent<Application>()
    .SetGroupName("Applications")
    .AddConstructor<DeadreckoningPositionServer>()
    .AddAttribute("Port", "Port on which we listen for incoming packets.",
                   UintegerValue(9),
                   MakeUintegerAccessor(&DeadreckoningPositionServer::m_port),
                   MakeUintegerChecker<uint16_t>())
    .AddTraceSource("Rx", "A packet has been received",
                     MakeTraceSourceAccessor(&DeadreckoningPositionServer::m_rxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource("RxWithAddresses", "A packet has been received",
                     MakeTraceSourceAccessor(&DeadreckoningPositionServer::m_rxTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
  ;
  return tid;
}

DeadreckoningPositionServer::DeadreckoningPositionServer() {
  NS_LOG_FUNCTION(this);
}

DeadreckoningPositionServer::~DeadreckoningPositionServer() {
  NS_LOG_FUNCTION(this);
  m_socket = 0;
  m_socket6 = 0;
}

void DeadreckoningPositionServer::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();
}

void  DeadreckoningPositionServer::StartApplication(void) {
  NS_LOG_FUNCTION(this);

  if (m_socket == 0) {
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    m_socket = Socket::CreateSocket(GetNode(), tid);
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);

    if (m_socket->Bind (local) == -1) {
      NS_FATAL_ERROR("Failed to bind socket");
    }

    if (addressUtils::IsMulticast(m_local)) {
      Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(m_socket);

      if (udpSocket) {
        // equivalent to setsockopt (MCAST_JOIN_GROUP)
        udpSocket->MulticastJoinGroup(0, m_local);
      } else {
        NS_FATAL_ERROR("Error: Failed to join multicast group");
      }
    }
  }

  if (m_socket6 == 0) {
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    m_socket6 = Socket::CreateSocket(GetNode(), tid);
    Inet6SocketAddress local6 = Inet6SocketAddress(Ipv6Address::GetAny(), m_port);

    if (m_socket6->Bind(local6) == -1) {
      NS_FATAL_ERROR("Failed to bind socket");
    }

    if (addressUtils::IsMulticast (local6)) {
      Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(m_socket6);

      if (udpSocket) {
        // equivalent to setsockopt (MCAST_JOIN_GROUP)
        udpSocket->MulticastJoinGroup(0, local6);
      } else {
        NS_FATAL_ERROR("Error: Failed to join multicast group");
      }
    }
  }

  m_socket->SetRecvCallback(MakeCallback(&DeadreckoningPositionServer::HandleRead, this));
  m_socket6->SetRecvCallback(MakeCallback(&DeadreckoningPositionServer::HandleRead, this));
}

void  DeadreckoningPositionServer::StopApplication() {
  NS_LOG_FUNCTION(this);

  if (m_socket != 0)  {
    m_socket->Close();
    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  }

  if (m_socket6 != 0)  {
    m_socket6->Close();
    m_socket6->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  }
}

void  DeadreckoningPositionServer::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;
  Address from;
  Address localAddress;
  while ((packet = socket->RecvFrom(from))) {
    socket->GetSockName(localAddress);

    m_rxTrace(packet);
    m_rxTraceWithAddresses(packet, from, localAddress);

    packet->RemoveAllPacketTags();
    packet->RemoveAllByteTags();

    auto size = packet->GetSize();
    uint8_t *msgRaw = new uint8_t[size + 1];
    packet->CopyData(msgRaw, size);
    msgRaw[size] = '\0';
    std::string msg = reinterpret_cast<char*>(msgRaw);

    if (InetSocketAddress::IsMatchingType(from)) {
      NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " server received '" << msg << "' from " <<
                   InetSocketAddress::ConvertFrom(from).GetIpv4() << " port " <<
                   InetSocketAddress::ConvertFrom(from).GetPort());
    } else if (Inet6SocketAddress::IsMatchingType(from)) {
      NS_LOG_INFO("At time " << Simulator::Now().As(Time::S) << " server received '" << msg << "' from " <<
                   Inet6SocketAddress::ConvertFrom(from).GetIpv6() << " port " <<
                   Inet6SocketAddress::ConvertFrom(from).GetPort());
    }

    size_t spacePos = msg.find(' ');
    if (spacePos == std::string::npos) {
      NS_LOG_WARN("Invalid message format: no space found");
      delete[] msgRaw;
      continue;
    }

    std::string idStr = msg.substr(0, spacePos);
    std::string posStr = msg.substr(spacePos + 1);

    size_t comma1 = posStr.find(',');
    size_t comma2 = posStr.rfind(',');
    if (comma1 == std::string::npos || comma2 == std::string::npos || comma1 == comma2) {
      NS_LOG_WARN("Invalid position format: not enough commas");
      delete[] msgRaw;
      continue;
    }

    std::string clientIP;
    if (InetSocketAddress::IsMatchingType(from)) {
      clientIP = InetSocketAddress::ConvertFrom(from).GetIpv4().ToString();
    } else if (Inet6SocketAddress::IsMatchingType(from)) {
      clientIP = Inet6SocketAddress::ConvertFrom(from).GetIpv6().ToString();
    } else {
      clientIP = "Unknown";
    }

    auto& positionMap = m_positionMap[clientIP];

    try {
      uint32_t id = std::stoul(idStr);
      double x = std::stod(posStr.substr(0, comma1));
      double y = std::stod(posStr.substr(comma1 + 1, comma2 - comma1 - 1));
      double z = std::stod(posStr.substr(comma2 + 1));

      auto it = m_positionMap.find(id);
      if (it != m_positionMap.end()) {
        // Update existing ID with new position
        m_positionMap[id] = posStr;
      } else {
        // New ID - interpolate position if possible
        if (m_positionMap.empty()) {
          // No existing IDs, use received position
          m_positionMap[id] = posStr;
        } else {
          // Find immediate smaller and larger IDs
          uint32_t smallerId = 0;
          uint32_t largerId = UINT32_MAX;
          bool foundSmaller = false;
          bool foundLarger = false;

          for (const auto& entry : positionMap) {
            uint32_t existingId = entry.first;
            if (existingId < id && (!foundSmaller || existingId > smallerId)) {
              smallerId = existingId;
              foundSmaller = true;
            } else if (existingId > id && (!foundLarger || existingId < largerId)) {
              largerId = existingId;
              foundLarger = true;
            }
          }


          double newX, newY, newZ;
          if (foundSmaller && foundLarger) {
            // Average positions of immediate neighbors
            std::string smallerPosStr = m_positionMap[smallerId];
            std::string largerPosStr = m_positionMap[largerId];

            size_t c1 = smallerPosStr.find(',');
            size_t c2 = smallerPosStr.rfind(',');
            if (c1 == std::string::npos || c2 == std::string::npos || c1 == c2) {
              newX = x; newY = y; newZ = z; // Fallback to received
            } else {
              double x1 = std::stod(smallerPosStr.substr(0, c1));
              double y1 = std::stod(smallerPosStr.substr(c1 + 1, c2 - c1 - 1));
              double z1 = std::stod(smallerPosStr.substr(c2 + 1));

              c1 = largerPosStr.find(',');
              c2 = largerPosStr.rfind(',');
              if (c1 == std::string::npos || c2 == std::string::npos || c1 == c2) {
                newX = x; newY = y; newZ = z; // Fallback to received
              } else {
                double x2 = std::stod(largerPosStr.substr(0, c1));
                double y2 = std::stod(largerPosStr.substr(c1 + 1, c2 - c1 - 1));
                double z2 = std::stod(largerPosStr.substr(c2 + 1));

                newX = (x1 + x2) / 2.0;
                newY = (y1 + y2) / 2.0;
                newZ = (z1 + z2) / 2.0;
              }
            }
          } else if (foundSmaller) {
            // Use position of immediate smaller ID
            std::string smallerPosStr = m_positionMap[smallerId];
            size_t c1 = smallerPosStr.find(',');
            size_t c2 = smallerPosStr.rfind(',');
            if (c1 == std::string::npos || c2 == std::string::npos || c1 == c2) {
              newX = x; newY = y; newZ = z; // Fallback to received
            } else {
              newX = std::stod(smallerPosStr.substr(0, c1));
              newY = std::stod(smallerPosStr.substr(c1 + 1, c2 - c1 - 1));
              newZ = std::stod(smallerPosStr.substr(c2 + 1));
            }
          } else if (foundLarger) {
            // Use position of immediate larger ID
            std::string largerPosStr = m_positionMap[largerId];
            size_t c1 = largerPosStr.find(',');
            size_t c2 = largerPosStr.rfind(',');
            if (c1 == std::string::npos || c2 == std::string::npos || c1 == c2) {
              newX = x; newY = y; newZ = z; // Fallback to received
            } else {
              newX = std::stod(largerPosStr.substr(0, c1));
              newY = std::stod(largerPosStr.substr(c1 + 1, c2 - c1 - 1));
              newZ = std::stod(largerPosStr.substr(c2 + 1));
            }
          } else {
            // No neighbors found (shouldn't happen), use received
            newX = x; newY = y; newZ = z;
          }

          // Format and store computed position
          std::ostringstream oss;
          oss << newX << "," << newY << "," << newZ;
          m_positionMap[id] = oss.str();
        }
      }
    } catch (const std::exception& e) {
      NS_LOG_WARN("Exception in parsing: " << e.what());
      delete[] msgRaw;
      continue;
    }

    delete[] msgRaw;
  }
}

} // Namespace ns3

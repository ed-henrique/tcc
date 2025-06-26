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
#include "simple-position-server.h"

#include <random>
#include <sstream>
#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("SimplePositionServerApplication");

NS_OBJECT_ENSURE_REGISTERED(SimplePositionServer);

TypeId SimplePositionServer::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::SimplePositionServer")
    .SetParent<Application>()
    .SetGroupName("Applications")
    .AddConstructor<SimplePositionServer>()
    .AddAttribute("Port", "Port on which we listen for incoming packets.",
                   UintegerValue(9),
                   MakeUintegerAccessor(&SimplePositionServer::m_port),
                   MakeUintegerChecker<uint16_t>())
    .AddTraceSource("Rx", "A packet has been received",
                     MakeTraceSourceAccessor(&SimplePositionServer::m_rxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource("RxWithAddresses", "A packet has been received",
                     MakeTraceSourceAccessor(&SimplePositionServer::m_rxTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
  ;
  return tid;
}

SimplePositionServer::SimplePositionServer() {
  NS_LOG_FUNCTION(this);
}

SimplePositionServer::~SimplePositionServer() {
  NS_LOG_FUNCTION(this);
  m_socket = 0;
  m_socket6 = 0;
}

void SimplePositionServer::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();
}

void  SimplePositionServer::StartApplication(void) {
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

  m_socket->SetRecvCallback(MakeCallback(&SimplePositionServer::HandleRead, this));
  m_socket6->SetRecvCallback(MakeCallback(&SimplePositionServer::HandleRead, this));
}

void  SimplePositionServer::StopApplication() {
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

void  SimplePositionServer::HandleRead(Ptr<Socket> socket) {
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

    size_t idSep = msg.find(" ");

    if (idSep != std::string::npos) {
      std::string posIdRaw = msg.substr(0, idSep);
      NS_LOG_INFO(posIdRaw << " OK");
    }

    delete[] msgRaw;
  }
}

} // Namespace ns3

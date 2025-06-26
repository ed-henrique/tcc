#include "ns3/log.h"
#include "ns3/pointer.h"
#include "ns3/double.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv6-address.h"
#include "ns3/nstime.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/mobility-module.h"
#include "deadreckoning-position-client.h"

#include <sstream>
#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("DeadreckoningPositionClientApplication");

NS_OBJECT_ENSURE_REGISTERED(DeadreckoningPositionClient);

TypeId DeadreckoningPositionClient::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::DeadreckoningPositionClient")
    .SetParent<Application>()
    .SetGroupName("Applications")
    .AddConstructor<DeadreckoningPositionClient>()
    .AddAttribute("Interval", 
                   "The time to wait between packets",
                   TimeValue(Seconds(1.0)),
                   MakeTimeAccessor(&DeadreckoningPositionClient::m_interval),
                   MakeTimeChecker())
    .AddAttribute("Node", 
                   "The node in which the application is installed",
                   PointerValue(nullptr),
                   MakePointerAccessor(&DeadreckoningPositionClient::m_node),
                   MakePointerChecker<Node>())
    .AddAttribute("ExtraPayloadSize", 
                   "Extra payload size to add to packets",
                   UintegerValue(0),
                   MakeUintegerAccessor(&DeadreckoningPositionClient::m_extraPayloadSize),
                   MakeUintegerChecker<uint32_t>())
    .AddAttribute("EnbNode", 
                   "The enbNode to which the node is attached to",
                   PointerValue(nullptr),
                   MakePointerAccessor(&DeadreckoningPositionClient::m_enbNode),
                   MakePointerChecker<Node>())
    .AddAttribute("Threshold", 
                   "Chance to send the packet",
                   DoubleValue(0.5),
                   MakeDoubleAccessor(&SimplePositionClient::m_threshold),
                   MakeDoubleChecker<double>())
    .AddAttribute("RemoteAddress", 
                   "The destination Address of the outbound packets",
                   AddressValue(),
                   MakeAddressAccessor(&DeadreckoningPositionClient::m_peerAddress),
                   MakeAddressChecker())
    .AddAttribute("RemotePort", 
                   "The destination port of the outbound packets",
                   UintegerValue(0),
                   MakeUintegerAccessor(&DeadreckoningPositionClient::m_peerPort),
                   MakeUintegerChecker<uint16_t>())
    .AddTraceSource("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor(&DeadreckoningPositionClient::m_txTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource("Rx", "A packet has been received",
                     MakeTraceSourceAccessor(&DeadreckoningPositionClient::m_rxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource("TxWithAddresses", "A new packet is created and is sent",
                     MakeTraceSourceAccessor(&DeadreckoningPositionClient::m_txTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
    .AddTraceSource("RxWithAddresses", "A packet has been received",
                     MakeTraceSourceAccessor(&DeadreckoningPositionClient::m_rxTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
  ;
  return tid;
}

DeadreckoningPositionClient::DeadreckoningPositionClient() {
  NS_LOG_FUNCTION(this);
  m_sent = 0;
  m_socket = 0;
  m_node = nullptr;
  m_enbNode = nullptr;
  m_nextId = 0;
  m_extraPayloadSize = 0;
  m_random = CreateObject<UniformRandomVariable>();
  m_sendEvent = EventId();
}

DeadreckoningPositionClient::~DeadreckoningPositionClient() {
  NS_LOG_FUNCTION(this);
  m_socket = 0;
  m_node = nullptr;
  m_enbNode = nullptr;
}

void DeadreckoningPositionClient::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();
}

void  DeadreckoningPositionClient::StartApplication(void) {
  NS_LOG_FUNCTION(this);

  if (m_socket == 0) {
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    m_socket = Socket::CreateSocket(GetNode(), tid);
    if (Ipv4Address::IsMatchingType(m_peerAddress) == true) {
      if (m_socket->Bind() == -1) {
        NS_FATAL_ERROR("Failed to bind socket");
      }

      m_socket->Connect(InetSocketAddress(Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
    } else if (Ipv6Address::IsMatchingType(m_peerAddress) == true) {
      if (m_socket->Bind6() == -1) {
        NS_FATAL_ERROR("Failed to bind socket");
      }

      m_socket->Connect(Inet6SocketAddress(Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
    } else if (InetSocketAddress::IsMatchingType(m_peerAddress) == true) {
      if (m_socket->Bind() == -1) {
        NS_FATAL_ERROR("Failed to bind socket");
      }

      m_socket->Connect(m_peerAddress);
    } else if (Inet6SocketAddress::IsMatchingType(m_peerAddress) == true) {
      if (m_socket->Bind6() == -1) {
        NS_FATAL_ERROR("Failed to bind socket");
      }

      m_socket->Connect(m_peerAddress);
    } else {
      NS_ASSERT_MSG(false, "Incompatible address type: " << m_peerAddress);
    }
  }

  m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  m_socket->SetAllowBroadcast(false);
  ScheduleTransmit(Seconds(0.));
}

void  DeadreckoningPositionClient::StopApplication() {
  NS_LOG_FUNCTION(this);

  if (m_socket != 0)  {
    m_socket->Close();
    m_socket = 0;
  }

  Simulator::Cancel(m_sendEvent);
}

void  DeadreckoningPositionClient::ScheduleTransmit(Time dt) {
  NS_LOG_FUNCTION(this << dt);
  m_sendEvent = Simulator::Schedule(dt, &DeadreckoningPositionClient::Send, this);
}

void  DeadreckoningPositionClient::Send(void) {
  NS_LOG_FUNCTION(this);

  NS_ASSERT(m_sendEvent.IsExpired());

  Ptr<MobilityModel> ueMobility = m_node->GetObject<MobilityModel>();
  Ptr<MobilityModel> enbMobility = m_enbNode->GetObject<MobilityModel>();

  Vector uePos = ueMobility->GetPosition();
  Vector enbPos = enbMobility->GetPosition();
  double distance = CalculateDistance(uePos, enbPos);

  NS_LOG_INFO("is " << distance << "m from eNB");

  Address localAddress;
  m_socket->GetSockName(localAddress);

  Vector uePos = ueMobility->GetPosition();

  std::ostringstream pos;
  pos << m_nextId++ << " " << uePos.x << "," << uePos.y << "," << uePos.z;
  pos << std::string(m_extraPayloadSize, '.');
  std::string msg = pos.str();

  NS_LOG_INFO("consumed 33 mJ");

  Ptr<Packet> p = Create<Packet>(
      reinterpret_cast<const uint8_t*>(msg.c_str()), 
      msg.size()
  );

  m_txTrace(p);

  if (Ipv4Address::IsMatchingType(m_peerAddress)) {
    m_txTraceWithAddresses(p, localAddress, InetSocketAddress(Ipv4Address::ConvertFrom(m_peerAddress), m_peerPort));
  } else if (Ipv6Address::IsMatchingType(m_peerAddress)) {
    m_txTraceWithAddresses(p, localAddress, Inet6SocketAddress(Ipv6Address::ConvertFrom(m_peerAddress), m_peerPort));
  }

  if (m_random->GetValue(0.0, 1.0) > m_threshold) {
    NS_LOG_INFO("Package lost");
    ++m_sent;

    ScheduleTransmit(m_interval);
    return;
  }

  m_socket->Send(p);
  ++m_sent;

  if (Ipv4Address::IsMatchingType(m_peerAddress)) {
    NS_LOG_INFO("sent '" << msg << "' to " << Ipv4Address::ConvertFrom(m_peerAddress)
                << " port " << m_peerPort);
  } else if (Ipv6Address::IsMatchingType(m_peerAddress)) {
    NS_LOG_INFO("sent '" << msg << "' to " << Ipv6Address::ConvertFrom(m_peerAddress)
                << " port " << m_peerPort);
  } else if (InetSocketAddress::IsMatchingType(m_peerAddress)) {
    NS_LOG_INFO("sent '" << msg << "' to " << InetSocketAddress::ConvertFrom(m_peerAddress).GetIpv4()
                << " port " << InetSocketAddress::ConvertFrom(m_peerAddress).GetPort());
  } else if (Inet6SocketAddress::IsMatchingType(m_peerAddress)) {
    NS_LOG_INFO("sent '" << msg << "' to " << Inet6SocketAddress::ConvertFrom(m_peerAddress).GetIpv6()
                << " port " << Inet6SocketAddress::ConvertFrom(m_peerAddress).GetPort());
  }

  ScheduleTransmit(m_interval);
}

} // Namespace ns3

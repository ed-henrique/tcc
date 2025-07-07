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
#include "gps-cbl-position-client.h"

#include <sstream>
#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("GPSCBLPositionClientApplication");

NS_OBJECT_ENSURE_REGISTERED(GPSCBLPositionClient);

TypeId GPSCBLPositionClient::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::GPSCBLPositionClient")
    .SetParent<Application>()
    .SetGroupName("Applications")
    .AddConstructor<GPSCBLPositionClient>()
    .AddAttribute("Interval", 
                   "The time to wait between packets",
                   TimeValue(Seconds(1.0)),
                   MakeTimeAccessor(&GPSCBLPositionClient::m_interval),
                   MakeTimeChecker())
    .AddAttribute("PositionInterval", 
                   "The time to wait between gathering position",
                   TimeValue(Seconds(1.0)),
                   MakeTimeAccessor(&GPSCBLPositionClient::m_positionInterval),
                   MakeTimeChecker())
    .AddAttribute("Node", 
                   "The node in which the application is installed",
                   PointerValue(nullptr),
                   MakePointerAccessor(&GPSCBLPositionClient::m_node),
                   MakePointerChecker<Node>())
    .AddAttribute("ExtraPayloadSize", 
                   "Extra payload size to add to packets",
                   UintegerValue(0),
                   MakeUintegerAccessor(&GPSCBLPositionClient::m_extraPayloadSize),
                   MakeUintegerChecker<uint32_t>())
    .AddAttribute("AmountPositionsToSend", 
                   "Amount of positions to send each time",
                   UintegerValue(10),
                   MakeUintegerAccessor(&GPSCBLPositionClient::m_amountPositionsToSend),
                   MakeUintegerChecker<uint32_t>())
    .AddAttribute("EnbNode", 
                   "The enbNode to which the node is attached to",
                   PointerValue(nullptr),
                   MakePointerAccessor(&GPSCBLPositionClient::m_enbNode),
                   MakePointerChecker<Node>())
    .AddAttribute("Range", 
                   "The enbNode range",
                   DoubleValue(0.0),
                   MakeDoubleAccessor(&GPSCBLPositionClient::m_range),
                   MakeDoubleChecker<double>())
    .AddAttribute("RemoteAddress", 
                   "The destination Address of the outbound packets",
                   AddressValue(),
                   MakeAddressAccessor(&GPSCBLPositionClient::m_peerAddress),
                   MakeAddressChecker())
    .AddAttribute("RemotePort", 
                   "The destination port of the outbound packets",
                   UintegerValue(0),
                   MakeUintegerAccessor(&GPSCBLPositionClient::m_peerPort),
                   MakeUintegerChecker<uint16_t>())
    .AddTraceSource("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor(&GPSCBLPositionClient::m_txTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource("Rx", "A packet has been received",
                     MakeTraceSourceAccessor(&GPSCBLPositionClient::m_rxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource("TxWithAddresses", "A new packet is created and is sent",
                     MakeTraceSourceAccessor(&GPSCBLPositionClient::m_txTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
    .AddTraceSource("RxWithAddresses", "A packet has been received",
                     MakeTraceSourceAccessor(&GPSCBLPositionClient::m_rxTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
  ;
  return tid;
}

GPSCBLPositionClient::GPSCBLPositionClient() {
  NS_LOG_FUNCTION(this);
  m_sent = 0;
  m_lost = 0;
  m_socket = 0;
  m_node = nullptr;
  m_enbNode = nullptr;
  m_nextId = 0;
  m_extraPayloadSize = 0;
  m_sendEvent = EventId();
}

GPSCBLPositionClient::~GPSCBLPositionClient() {
  NS_LOG_FUNCTION(this);
  m_socket = 0;
  m_node = nullptr;
  m_enbNode = nullptr;
}

void GPSCBLPositionClient::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();
}

void  GPSCBLPositionClient::StartApplication(void) {
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
  SchedulePositionGathering(Seconds(0.));
  ScheduleInside(Seconds(0.));
}

void  GPSCBLPositionClient::StopApplication() {
  NS_LOG_FUNCTION(this);

  if (m_socket != 0)  {
    m_socket->Close();
    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    m_socket = 0;
  }

  Simulator::Cancel(m_sendEvent);
  m_positionMap.clear();
}

void  GPSCBLPositionClient::ScheduleInside(Time dt) {
  NS_LOG_FUNCTION(this << dt);
  Simulator::Schedule(dt, &GPSCBLPositionClient::Inside, this);
}

void  GPSCBLPositionClient::ScheduleTransmit(Time dt) {
  NS_LOG_FUNCTION(this << dt);
  m_sendEvent = Simulator::Schedule(dt, &GPSCBLPositionClient::Send, this);
}

void  GPSCBLPositionClient::SchedulePositionGathering(Time dt) {
  NS_LOG_FUNCTION(this << dt);
  Simulator::Schedule(dt, &GPSCBLPositionClient::GatherPosition, this);
}

void  GPSCBLPositionClient::Inside(void) {
  NS_LOG_FUNCTION(this);

  Ptr<MobilityModel> ueMobility = m_node->GetObject<MobilityModel>();
  Ptr<MobilityModel> enbMobility = m_enbNode->GetObject<MobilityModel>();

  Vector uePos = ueMobility->GetPosition();
  Vector enbPos = enbMobility->GetPosition();
  double distance = CalculateDistance(uePos, enbPos);

  if (m_range <= distance) {
    NS_LOG_INFO("inside");
  } else {
    NS_LOG_INFO("outside");
  }

  Simulator::Schedule(Seconds(1.0), &GPSCBLPositionClient::Inside, this);
}

void  GPSCBLPositionClient::GatherPosition(void) {
  NS_LOG_FUNCTION(this);

  Ptr<MobilityModel> ueMobility = m_node->GetObject<MobilityModel>();
  Vector uePos = ueMobility->GetPosition();
  Vector ueVes = ueMobility->GetVelocity();
  double ueSpeed = std::sqrt(ueVes.x*ueVes.x + ueVes.y*ueVes.y);

  std::ostringstream pos;
  pos << uePos.x << "," << uePos.y << "," << uePos.z << ";" << ueSpeed;
  std::string msg = pos.str();

  m_positionMap[m_nextId++] = msg;
  NS_LOG_INFO("consumed 33 mJ");

  Simulator::Schedule(m_positionInterval, &GPSCBLPositionClient::GatherPosition, this);
}

void  GPSCBLPositionClient::Send(void) {
  NS_LOG_FUNCTION(this);

  NS_ASSERT(m_sendEvent.IsExpired());

  Ptr<MobilityModel> ueMobility = m_node->GetObject<MobilityModel>();
  Ptr<MobilityModel> enbMobility = m_enbNode->GetObject<MobilityModel>();

  Vector uePos = ueMobility->GetPosition();
  Vector enbPos = enbMobility->GetPosition();
  double distance = CalculateDistance(uePos, enbPos);

  NS_LOG_INFO("is " << distance << "m from eNB");

  if (m_positionMap.size() < m_amountPositionsToSend) {
    ScheduleTransmit(m_interval);
    return;
  }

  Address localAddress;
  m_socket->GetSockName(localAddress);

  std::ostringstream pos;
  pos << m_node->GetId() << " ";
  for (auto posPair = m_positionMap.rbegin(), next_it = posPair; posPair != m_positionMap.rend(); posPair = next_it) {
    ++next_it;
    pos << posPair->first << " " << posPair->second << "\n";
    m_positionMap.erase(posPair->first);
  }

  pos << " " << std::string(m_extraPayloadSize, '.');
  std::string msg = pos.str();

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

  if (m_range > distance) {
    NS_LOG_INFO("Package lost with " << m_amountPositionsToSend << " positions");
    ++m_lost;
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

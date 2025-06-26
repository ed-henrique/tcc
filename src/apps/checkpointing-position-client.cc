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
#include "checkpointing-position-client.h"

#include <sstream>
#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("CheckpointingPositionClientApplication");

NS_OBJECT_ENSURE_REGISTERED(CheckpointingPositionClient);

TypeId CheckpointingPositionClient::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::CheckpointingPositionClient")
    .SetParent<Application>()
    .SetGroupName("Applications")
    .AddConstructor<CheckpointingPositionClient>()
    .AddAttribute("Interval", 
                   "The time to wait between packets",
                   TimeValue(Seconds(1.0)),
                   MakeTimeAccessor(&CheckpointingPositionClient::m_interval),
                   MakeTimeChecker())
    .AddAttribute("PositionInterval", 
                   "The time to wait between gathering position",
                   TimeValue(Seconds(1.0)),
                   MakeTimeAccessor(&CheckpointingPositionClient::m_positionInterval),
                   MakeTimeChecker())
    .AddAttribute("Node", 
                   "The node in which the application is installed",
                   PointerValue(nullptr),
                   MakePointerAccessor(&CheckpointingPositionClient::m_node),
                   MakePointerChecker<Node>())
    .AddAttribute("ExtraPayloadSize", 
                   "Extra payload size to add to packets",
                   UintegerValue(0),
                   MakeUintegerAccessor(&CheckpointingPositionClient::m_extraPayloadSize),
                   MakeUintegerChecker<uint32_t>())
    .AddAttribute("AmountPositionsToSend", 
                   "Amount of positions to send each time",
                   UintegerValue(10),
                   MakeUintegerAccessor(&CheckpointingPositionClient::m_amountPositionsToSend),
                   MakeUintegerChecker<uint32_t>())
    .AddAttribute("EnbNode", 
                   "The enbNode to which the node is attached to",
                   PointerValue(nullptr),
                   MakePointerAccessor(&CheckpointingPositionClient::m_enbNode),
                   MakePointerChecker<Node>())
    .AddAttribute("Threshold", 
                   "Chance to send the packet",
                   DoubleValue(0.5),
                   MakeDoubleAccessor(&CheckpointingPositionClient::m_threshold),
                   MakeDoubleChecker<double>())
    .AddAttribute("RemoteAddress", 
                   "The destination Address of the outbound packets",
                   AddressValue(),
                   MakeAddressAccessor(&CheckpointingPositionClient::m_peerAddress),
                   MakeAddressChecker())
    .AddAttribute("RemotePort", 
                   "The destination port of the outbound packets",
                   UintegerValue(0),
                   MakeUintegerAccessor(&CheckpointingPositionClient::m_peerPort),
                   MakeUintegerChecker<uint16_t>())
    .AddTraceSource("Tx", "A new packet is created and is sent",
                     MakeTraceSourceAccessor(&CheckpointingPositionClient::m_txTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource("Rx", "A packet has been received",
                     MakeTraceSourceAccessor(&CheckpointingPositionClient::m_rxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource("TxWithAddresses", "A new packet is created and is sent",
                     MakeTraceSourceAccessor(&CheckpointingPositionClient::m_txTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
    .AddTraceSource("RxWithAddresses", "A packet has been received",
                     MakeTraceSourceAccessor(&CheckpointingPositionClient::m_rxTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
  ;
  return tid;
}

CheckpointingPositionClient::CheckpointingPositionClient() {
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

CheckpointingPositionClient::~CheckpointingPositionClient() {
  NS_LOG_FUNCTION(this);
  m_socket = 0;
  m_node = nullptr;
  m_enbNode = nullptr;
}

void CheckpointingPositionClient::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();
}

void  CheckpointingPositionClient::StartApplication(void) {
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

  m_socket->SetRecvCallback(MakeCallback(&CheckpointingPositionClient::HandleRead, this));
  m_socket->SetAllowBroadcast(false);
  ScheduleTransmit(Seconds(0.));
  SchedulePositionGathering(Seconds(0.));
}

void  CheckpointingPositionClient::StopApplication() {
  NS_LOG_FUNCTION(this);

  if (m_socket != 0)  {
    m_socket->Close();
    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    m_socket = 0;
  }

  Simulator::Cancel(m_sendEvent);
  m_positionMap.clear();
}

void  CheckpointingPositionClient::ScheduleTransmit(Time dt) {
  NS_LOG_FUNCTION(this << dt);
  m_sendEvent = Simulator::Schedule(dt, &CheckpointingPositionClient::Send, this);
}

void  CheckpointingPositionClient::SchedulePositionGathering(Time dt) {
  NS_LOG_FUNCTION(this << dt);
  Simulator::Schedule(dt, &CheckpointingPositionClient::GatherPosition, this);
}

void  CheckpointingPositionClient::GatherPosition(void) {
  NS_LOG_FUNCTION(this);

  Ptr<MobilityModel> ueMobility = m_node->GetObject<MobilityModel>();
  Vector uePos = ueMobility->GetPosition();

  std::ostringstream pos;
  pos << uePos.x << "," << uePos.y << "," << uePos.z;
  std::string msg = pos.str();

  m_positionMap[m_nextId++] = msg;
  NS_LOG_INFO("consumed 33 mJ");

  Simulator::Schedule(m_positionInterval, &CheckpointingPositionClient::GatherPosition, this);
}

void  CheckpointingPositionClient::Send(void) {
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
  for (auto posPair = m_positionMap.rbegin(); posPair != m_positionMap.rend(); ++posPair) {
    pos << posPair->first << " " << posPair->second << "\n";
  }

  pos << std::string(m_extraPayloadSize, '.');
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

void CheckpointingPositionClient::HandleRead(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);

  Ptr<Packet> packet;
  Address from;
  Address localAddress;
  while((packet = socket->RecvFrom(from))) {
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
      NS_LOG_INFO("received '" << msg << "' from " << InetSocketAddress::ConvertFrom(from).GetIpv4()
                  << " port " << InetSocketAddress::ConvertFrom(from).GetPort());
    } else if (Inet6SocketAddress::IsMatchingType(from)) {
      NS_LOG_INFO("received '" << msg << "' from " << Inet6SocketAddress::ConvertFrom(from).GetIpv6()
                  << " port " << Inet6SocketAddress::ConvertFrom(from).GetPort());
    }

    std::istringstream batch(msg);
    std::string line;

    while (std::getline(batch, line)) {
      size_t idSep = line.find(" ");

      if (idSep != std::string::npos) {
        std::string posIdRaw = line.substr(0, idSep);
        uint32_t posId = std::stoul(posIdRaw);
        m_positionMap.erase(posId);

        NS_LOG_INFO("received OK for ID " << posId);
      }
    }

    delete[] msgRaw;
  }
}

} // Namespace ns3

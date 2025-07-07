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
#include "gps-cbl-position-server.h"

#include <string>
#include <sstream>
#include <iostream>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("GPSCBLPositionServerApplication");

NS_OBJECT_ENSURE_REGISTERED(GPSCBLPositionServer);

TypeId GPSCBLPositionServer::GetTypeId(void) {
  static TypeId tid = TypeId("ns3::GPSCBLPositionServer")
    .SetParent<Application>()
    .SetGroupName("Applications")
    .AddConstructor<GPSCBLPositionServer>()
    .AddAttribute("Port", "Port on which we listen for incoming packets.",
                   UintegerValue(9),
                   MakeUintegerAccessor(&GPSCBLPositionServer::m_port),
                   MakeUintegerChecker<uint16_t>())
    .AddTraceSource("Rx", "A packet has been received",
                     MakeTraceSourceAccessor(&GPSCBLPositionServer::m_rxTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource("RxWithAddresses", "A packet has been received",
                     MakeTraceSourceAccessor(&GPSCBLPositionServer::m_rxTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
  ;
  return tid;
}

GPSCBLPositionServer::GPSCBLPositionServer() {
  NS_LOG_FUNCTION(this);
}

GPSCBLPositionServer::~GPSCBLPositionServer() {
  NS_LOG_FUNCTION(this);
  m_socket = 0;
  m_socket6 = 0;
}

void GPSCBLPositionServer::DoDispose(void) {
  NS_LOG_FUNCTION(this);
  Application::DoDispose();
}

void  GPSCBLPositionServer::StartApplication(void) {
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

  m_socket->SetRecvCallback(MakeCallback(&GPSCBLPositionServer::HandleRead, this));
  m_socket6->SetRecvCallback(MakeCallback(&GPSCBLPositionServer::HandleRead, this));
  Simulator::Schedule(Seconds(1.0), &GPSCBLPositionServer::EstimatePositions, this);
}

void  GPSCBLPositionServer::StopApplication() {
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

void GPSCBLPositionServer::EstimatePositions() {
  Time currentTime = Simulator::Now();
  
  for (auto& pair : m_vehicleStates) {
    uint32_t vehicleId = pair.first;
    VehicleState& state = pair.second;
    
    if (!state.receivedUpdate && state.lastSpeed > 0) {
      double elapsed = (currentTime - state.lastUpdate).GetSeconds();
      double distance = state.lastSpeed * elapsed;
      Vector predictedPos = state.lastPosition;
      predictedPos.x += distance * std::cos(state.lastDirection);
      predictedPos.y += distance * std::sin(state.lastDirection);
      predictedPos.x = std::max(0.0, std::min(1000.0, predictedPos.x));
      predictedPos.y = std::max(0.0, std::min(1000.0, predictedPos.y));
      state.lastPosition = predictedPos;
      std::cout << "Estimated position for vehicle " << vehicleId 
                << " at (" << predictedPos.x << ", " << predictedPos.y << ")" << std::endl;
    }
    state.receivedUpdate = false;
  }
  Simulator::Schedule(Seconds(1), &VehicleTrackingServer::EstimatePositions, this);
}

void  GPSCBLPositionServer::HandleRead(Ptr<Socket> socket) {
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

    std::istringstream batch(msg);
    std::string line;

    std::string vehIdRaw;
    std::string posIdRaw;
    double xRaw;
    double yRaw;
    double speedRaw;

    while (std::getline(batch, line)) {
      if (line[0] == '.') {
        break;
      }

      for (int i = 0; i < 3; i++) {
        size_t idSep = line.find(" ");

        if (idSep != std::string::npos) {
          if (i == 0) {
            vehIdRaw = line.substr(0, idSep);
          } else if (i == 1) {
            posIdRaw = line.substr(0, idSep);
          } else if (i == 2) {
            std::string raw = line.substr(0, idSep);
            size_t idSep1 = line.find(",");
            xRaw = raw.substr(0, idSep1);

            raw = raw.substr(idSep1 + 1, raw.length());
            size_t idSep2 = line.find(",");
            yRaw = raw.substr(0, idSep2);

            raw = raw.substr(idSep2 + 1, raw.length());
            size_t idSep3 = line.find(";");
            speedRaw = line.substr(idSep3, idSep);
          }

          line = line.substr(idSep + 1, line.length());
        }
      }
    }

    uint32_t vehicleId = static_cast<uint32_t>(std::stoul(vehIdRaw));
    double x = std::stod(xRaw);
    double y = std::stod(yRaw);
    double speed = std::stod(speedRaw);
    
    VehicleState state;
    state.lastPosition = Vector(x, y, 0);
    state.lastSpeed = speed;
    state.lastUpdate = Simulator::Now();
    state.receivedUpdate = true;
    
    if (m_vehicleStates.find(vehicleId) != m_vehicleStates.end()) {
      Vector prevPos = m_vehicleStates[vehicleId].lastPosition;
      double dx = x - prevPos.x;
      double dy = y - prevPos.y;
      state.lastDirection = std::atan2(dy, dx);
    }
    
    m_vehicleStates[vehicleId] = state;
    std::cout << "Received update from vehicle " << vehicleId 
              << " at (" << x << ", " << y << ")" << std::endl;

    delete[] msgRaw;
  }
}

} // Namespace ns3

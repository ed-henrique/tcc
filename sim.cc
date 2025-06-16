#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/csma-module.h"
#include "ns3/bridge-helper.h"
#include <iomanip>
#include <sstream>
#include <queue>
#include <cmath>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiPositionTracker");

// Server Application ========================================================
class PositionServer : public Application {
public:
    PositionServer() : m_socket(nullptr), m_port(0) {}
    virtual ~PositionServer() { m_socket = nullptr; }
    
    static TypeId GetTypeId() {
        static TypeId tid = TypeId("PositionServer")
            .SetParent<Application>()
            .AddConstructor<PositionServer>();
        return tid;
    }
    
    void Setup(uint16_t port) { m_port = port; }
    
private:
    virtual void StartApplication() {
        m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), m_port);
        if (m_socket->Bind(local) == -1) NS_FATAL_ERROR("Failed to bind socket");
        m_socket->Listen();
        m_socket->SetAcceptCallback(
            MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
            MakeCallback(&PositionServer::HandleAccept, this));
    }
    
    virtual void StopApplication() {
        if (m_socket) m_socket->Close();
        m_buffers.clear();
    }
    
    void HandleAccept(Ptr<Socket> socket, const Address& from) {
        socket->SetRecvCallback(MakeCallback(&PositionServer::HandleRead, this));
        m_buffers[socket] = "";  // Initialize buffer for this connection
    }
    
    void HandleRead(Ptr<Socket> socket) {
        Ptr<Packet> packet;
        Address from;
        
        while ((packet = socket->RecvFrom(from))) {
            uint32_t size = packet->GetSize();
            uint8_t *buffer = new uint8_t[size + 1];
            packet->CopyData(buffer, size);
            buffer[size] = '\0';
            
            // Append to socket-specific buffer
            std::string& recvBuffer = m_buffers[socket];
            recvBuffer.append(reinterpret_cast<char*>(buffer), size);
            
            // Process complete messages (delimited by newline)
            size_t pos;
            while ((pos = recvBuffer.find('\n')) != std::string::npos) {
                // Extract one complete message
                std::string message = recvBuffer.substr(0, pos);
                recvBuffer.erase(0, pos + 1);  // Remove processed message
                
                // Process the message
                if (!message.empty()) {
                    InetSocketAddress iaddr = InetSocketAddress::ConvertFrom(from);
                    NS_LOG_INFO(Simulator::Now().As(Time::S) << " Server received: " << message);
                    
                    // Send OK response
                    std::string response = "OK\n";
                    Ptr<Packet> okPacket = Create<Packet>(
                        reinterpret_cast<const uint8_t*>(response.c_str()), 
                        response.size()
                    );
                    socket->Send(okPacket);
                }
            }
            
            delete[] buffer;
        }
    }
    
    Ptr<Socket> m_socket;
    uint16_t m_port;
    std::map<Ptr<Socket>, std::string> m_buffers;  // Per-socket receive buffers
};

// Client Application with position queuing ==================================
class PositionClient : public Application {
public:
    PositionClient() : 
        m_socket(nullptr), 
        m_node(nullptr), 
        m_running(false), 
        m_connected(false),
        m_queueEnabled(false) 
    {}
    
    virtual ~PositionClient() { m_socket = nullptr; m_node = nullptr; }
    
    static TypeId GetTypeId() {
        static TypeId tid = TypeId("PositionClient")
            .SetParent<Application>()
            .AddConstructor<PositionClient>();
        return tid;
    }
    
    void Setup(Address serverAddress, Ptr<Node> node) {
        m_serverAddress = serverAddress;
        m_node = node;
    }
    
    // Enable position queueing when disconnected
    void EnableQueueing() { m_queueEnabled = true; }
    
private:
    virtual void StartApplication() {
        m_running = true;
        m_connected = false;
        
        // Reset socket if exists
        if (m_socket) {
            m_socket->Close();
            m_socket = nullptr;
        }
        
        m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
        
        // Disable packet coalescing
        m_socket->SetAttribute("TcpNoDelay", BooleanValue(true));
        
        m_socket->SetConnectCallback(
            MakeCallback(&PositionClient::ConnectionSucceeded, this),
            MakeCallback(&PositionClient::ConnectionFailed, this));
        
        // Set close callbacks to detect disconnections
        m_socket->SetCloseCallbacks(
            MakeCallback(&PositionClient::HandleNormalClose, this),
            MakeCallback(&PositionClient::HandleErrorClose, this));
        
        m_socket->Connect(m_serverAddress);
        m_socket->SetRecvCallback(MakeCallback(&PositionClient::HandleRead, this));
    }
    
    virtual void StopApplication() {
        m_running = false;
        m_connected = false;
        if (m_socket) {
            m_socket->Close();
            m_socket = nullptr;
        }
        Simulator::Cancel(m_sendEvent);
        m_positionQueue = std::queue<std::string>(); // Clear queue
    }
    
    void ConnectionSucceeded(Ptr<Socket> socket) {
        NS_LOG_INFO(Simulator::Now().As(Time::S) << " Connection succeeded");
        m_connected = true;
        
        // Send all queued positions upon reconnection
        if (m_queueEnabled && !m_positionQueue.empty()) {
            NS_LOG_INFO("Sending queued positions (" << m_positionQueue.size() << " items)");
            while (!m_positionQueue.empty()) {
                SendPacket(m_positionQueue.front());
                m_positionQueue.pop();
            }
        }
        SendPosition(); // Send current position
    }
    
    void ConnectionFailed(Ptr<Socket> socket) {
        NS_LOG_WARN("Connection failed");
        m_connected = false;
        // Retry connection after 1 second
        Simulator::Schedule(Seconds(1.0), &PositionClient::StartApplication, this);
    }
    
    void HandleNormalClose(Ptr<Socket> socket) {
        NS_LOG_INFO("Connection closed normally");
        m_connected = false;
        // Try to reconnect after 1 second
        Simulator::Schedule(Seconds(1.0), &PositionClient::StartApplication, this);
    }
    
    void HandleErrorClose(Ptr<Socket> socket) {
        NS_LOG_WARN("Connection closed with error");
        m_connected = false;
        // Try to reconnect after 1 second
        Simulator::Schedule(Seconds(1.0), &PositionClient::StartApplication, this);
    }
    
    void SendPosition() {
        if (!m_running) return;

        Ptr<MobilityModel> mobility = m_node->GetObject<MobilityModel>();
        if (!mobility) return;

        Vector position = mobility->GetPosition();
        std::ostringstream oss;
        oss << "Node " << m_node->GetId() 
            << " | Position: (" << std::fixed << std::setprecision(2) 
            << position.x << ", " << position.y << ", " << position.z << ")"
            << " | Time: " << Simulator::Now().GetSeconds() << "s";
        std::string positionStr = oss.str();
        
        // Queue or send based on connection status
        if (m_connected) {
            SendPacket(positionStr);
        } else if (m_queueEnabled) {
            NS_LOG_INFO("Queuing position: " << positionStr);
            m_positionQueue.push(positionStr);
        }
        
        // Schedule next position report
        m_sendEvent = Simulator::Schedule(Seconds(1.0), &PositionClient::SendPosition, this);
    }
    
    // Helper to send packet with error handling
    void SendPacket(const std::string& data) {
        if (!m_connected || !m_socket) return;
        
        // Add newline delimiter to separate messages
        std::string packetData = data + "\n";
        
        Ptr<Packet> packet = Create<Packet>(
            reinterpret_cast<const uint8_t*>(packetData.c_str()), 
            packetData.size()
        );
        
        int actual = m_socket->Send(packet);
        if (actual == -1) {
            NS_LOG_WARN("Failed to send packet, disconnecting");
            m_connected = false;
            // Queue the failed packet if enabled
            if (m_queueEnabled) m_positionQueue.push(data);
            // Attempt to reconnect
            Simulator::Schedule(Seconds(0.1), &PositionClient::StartApplication, this);
        } else if (static_cast<uint32_t>(actual) != packetData.size()) {
            NS_LOG_WARN("Sent incomplete packet (" << actual << "/" << packetData.size() << " bytes)");
        }
    }
    
    void HandleRead(Ptr<Socket> socket) {
        Ptr<Packet> packet;
        Address from;
        while ((packet = socket->RecvFrom(from))) {
            uint32_t size = packet->GetSize();
            uint8_t *buffer = new uint8_t[size + 1];
            packet->CopyData(buffer, size);
            buffer[size] = '\0';
            
            std::string response(reinterpret_cast<char*>(buffer));
            // Split responses by newline
            std::istringstream stream(response);
            std::string line;
            while (std::getline(stream, line)) {
                if (line == "OK") {
                    NS_LOG_INFO(Simulator::Now().As(Time::S) << " Client received OK from server");
                }
            }
            delete[] buffer;
        }
    }
    
    Ptr<Socket> m_socket;
    Address m_serverAddress;
    Ptr<Node> m_node;
    bool m_running;
    bool m_connected;
    bool m_queueEnabled;
    EventId m_sendEvent;
    std::queue<std::string> m_positionQueue;  // Stores positions during disconnections
};

// Helper function to calculate distance between two nodes
double CalculateDistance(Ptr<Node> n1, Ptr<Node> n2) {
    Ptr<MobilityModel> mob1 = n1->GetObject<MobilityModel>();
    Ptr<MobilityModel> mob2 = n2->GetObject<MobilityModel>();
    if (!mob1 || !mob2) return 1e9; // Large distance if no mobility model
    
    Vector pos1 = mob1->GetPosition();
    Vector pos2 = mob2->GetPosition();
    double dx = pos1.x - pos2.x;
    double dy = pos1.y - pos2.y;
    double dz = pos1.z - pos2.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

// Function to force STA to connect to closest AP
void ReconnectToClosestAp(Ptr<Node> staNode, NodeContainer apNodes) {
    // Find closest AP
    double minDistance = 1e9;
    Ptr<Node> closestAp = nullptr;
    
    for (uint32_t i = 0; i < apNodes.GetN(); ++i) {
        Ptr<Node> ap = apNodes.Get(i);
        double distance = CalculateDistance(staNode, ap);
        if (distance < minDistance) {
            minDistance = distance;
            closestAp = ap;
        }
    }
    
    if (!closestAp) return;
    
    // Get the WifiNetDevice and Mac
    Ptr<WifiNetDevice> wifiDev = staNode->GetDevice(0)->GetObject<WifiNetDevice>();
    if (!wifiDev) return;
    
    Ptr<StaWifiMac> staMac = wifiDev->GetMac()->GetObject<StaWifiMac>();
    if (!staMac) return;
    
    // Set SSID of the closest AP (ns-3.32 compatible method)
    Ssid ssid = Ssid("ap-" + std::to_string(closestAp->GetId()));
    staMac->SetSsid(ssid);
    
    NS_LOG_INFO(Simulator::Now().As(Time::S) << " STA " << staNode->GetId() 
        << " connecting to AP " << closestAp->GetId()
        << " (distance: " << minDistance << "m)");
}

// Helper function to trigger disconnection
void TriggerDisconnection(Ptr<Node> staNode) {
    Ptr<WifiNetDevice> wifiDev = staNode->GetDevice(0)->GetObject<WifiNetDevice>();
    if (!wifiDev) return;
    
    Ptr<StaWifiMac> staMac = wifiDev->GetMac()->GetObject<StaWifiMac>();
    if (!staMac) return;
    
    // Disconnect by setting invalid SSID
    staMac->SetSsid(Ssid("invalid-ssid"));
    
    NS_LOG_INFO(Simulator::Now().As(Time::S) << " STA " << staNode->GetId() 
        << " disassociated from current AP");
}

int main(int argc, char *argv[]) {
    // Enable logging
    Time::SetResolution(Time::NS);
    LogComponentEnable("WifiPositionTracker", LOG_LEVEL_INFO);
    // LogComponentEnable("PositionServer", LOG_LEVEL_INFO);
    // LogComponentEnable("PositionClient", LOG_LEVEL_INFO);
    
    // Simulation parameters
    double simTime = 50.0;
    
    // Create nodes
    NodeContainer remoteHostNode;
    remoteHostNode.Create(1);
    
    // Create 3 AP nodes
    NodeContainer apNodes;
    apNodes.Create(3);
    
    // Create 9 STA nodes
    NodeContainer staNodes;
    staNodes.Create(9);
    
    // Create CSMA network for backhaul
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));
    
    // Connect remote host to all APs via CSMA
    NetDeviceContainer csmaDevices = csma.Install(NodeContainer(remoteHostNode, apNodes));
    
    // Install internet stacks on all nodes
    InternetStackHelper stack;
    stack.Install(remoteHostNode);
    stack.Install(apNodes);
    stack.Install(staNodes);
    
    // Assign IP addresses to CSMA devices
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer csmaInterfaces = address.Assign(csmaDevices);
    
    // Setup WiFi
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy = YansWifiPhyHelper::Default();
    phy.SetChannel(channel.Create());
    
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", 
                                 "DataMode", StringValue("OfdmRate54Mbps"));
    
    // Setup APs with unique SSIDs
    NetDeviceContainer apDevices;
    for (uint32_t i = 0; i < apNodes.GetN(); ++i) {
        WifiMacHelper mac;
        Ssid ssid = Ssid("ap-" + std::to_string(apNodes.Get(i)->GetId()));
        mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
        NetDeviceContainer devices = wifi.Install(phy, mac, apNodes.Get(i));
        apDevices.Add(devices);
    }
    
    // Setup STAs with initial association to first AP
    NetDeviceContainer staDevices;
    for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
        WifiMacHelper mac;
        Ssid ssid = Ssid("ap-" + std::to_string(apNodes.Get(0)->GetId())); // Start associated with first AP
        mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), 
                    "ActiveProbing", BooleanValue(true));
        NetDeviceContainer device = wifi.Install(phy, mac, staNodes.Get(i));
        staDevices.Add(device);
    }
    
    // Assign IP addresses to STA devices
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);
    
    // Bridge APs' WiFi and CSMA interfaces
    BridgeHelper bridge;
    for (uint32_t i = 0; i < apNodes.GetN(); ++i) {
        NetDeviceContainer bridgeDevices;
        bridgeDevices.Add(csmaDevices.Get(i+1)); // CSMA device
        bridgeDevices.Add(apDevices.Get(i));     // WiFi device
        bridge.Install(apNodes.Get(i), bridgeDevices);
    }
    
    // Mobility model
    MobilityHelper mobility;
    
    // Fixed positions for remote host and APs (triangle formation)
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));    // Remote host
    positionAlloc->Add(Vector(0.0, 0.0, 0.0));    // AP1
    positionAlloc->Add(Vector(30.0, 0.0, 0.0));   // AP2
    positionAlloc->Add(Vector(15.0, 25.98, 0.0)); // AP3
    mobility.SetPositionAllocator(positionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(remoteHostNode);
    mobility.Install(apNodes);
    
    // Moving STAs (RandomWaypoint model)
    Ptr<RandomRectanglePositionAllocator> staPositionAlloc = CreateObject<RandomRectanglePositionAllocator>();
    staPositionAlloc->SetAttribute("X", StringValue("ns3::UniformRandomVariable[Min=-5.0|Max=35.0]"));
    staPositionAlloc->SetAttribute("Y", StringValue("ns3::UniformRandomVariable[Min=-5.0|Max=30.0]"));
    
    mobility.SetPositionAllocator(staPositionAlloc);
    mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                             "Speed", StringValue("ns3::UniformRandomVariable[Min=2|Max=6]"),
                             "Pause", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
                             "PositionAllocator", PointerValue(staPositionAlloc));
    mobility.Install(staNodes);
    
    // Setup server on remote host
    uint16_t serverPort = 5000;
    Ptr<PositionServer> serverApp = CreateObject<PositionServer>();
    serverApp->Setup(serverPort);
    remoteHostNode.Get(0)->AddApplication(serverApp);
    serverApp->SetStartTime(Seconds(0.0));
    serverApp->SetStopTime(Seconds(simTime));
    
    // Setup clients on STAs with queuing enabled
    Ipv4Address serverAddress = csmaInterfaces.GetAddress(0);
    for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
        Ptr<PositionClient> clientApp = CreateObject<PositionClient>();
        InetSocketAddress sockAddr(serverAddress, serverPort);
        clientApp->Setup(sockAddr, staNodes.Get(i));
        clientApp->EnableQueueing(); // Enable position queuing
        staNodes.Get(i)->AddApplication(clientApp);
        clientApp->SetStartTime(Seconds(1.0 + i*0.1));
        clientApp->SetStopTime(Seconds(simTime - 1));
    }
    
    // Schedule periodic disconnections and reconnections to closest AP
    for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
        Ptr<Node> sta = staNodes.Get(i);
        
        // Disconnect every 10 seconds starting at 5s
        for (double time = 5.0; time < simTime; time += 10.0) {
            // Disconnect at time
            Simulator::Schedule(Seconds(time), &TriggerDisconnection, sta);
            
            // Reconnect to closest AP at time + 2s
            Simulator::Schedule(Seconds(time + 2.0), &ReconnectToClosestAp, sta, apNodes);
        }
    }
    
    // Enable routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    
    // Print final positions and associations
    for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
        Ptr<Node> sta = staNodes.Get(i);
        Ptr<MobilityModel> mob = sta->GetObject<MobilityModel>();
        Vector pos = mob->GetPosition();
        
        // Find closest AP
        double minDistance = 1e9;
        uint32_t closestApId = 999;
        for (uint32_t j = 0; j < apNodes.GetN(); ++j) {
            double distance = CalculateDistance(sta, apNodes.Get(j));
            if (distance < minDistance) {
                minDistance = distance;
                closestApId = apNodes.Get(j)->GetId();
            }
        }
        
        NS_LOG_INFO("STA " << sta->GetId() << " final position: (" 
                   << pos.x << ", " << pos.y << ", " << pos.z << ")"
                   << " | Closest AP: " << closestApId
                   << " (distance: " << minDistance << "m)");
    }
    
    Simulator::Destroy();
    return 0;
}

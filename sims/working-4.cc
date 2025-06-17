#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include <iomanip>
#include <sstream>
#include <queue>
#include <map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WifiPositionTracker");

// Distance checker function =================================================
void CheckDistance(Ptr<Node> staNode, Ptr<Node> apNode, Ptr<Ipv4> ipv4, 
                   int32_t ifIndex, double range, double interval) {
    Ptr<MobilityModel> staMobility = staNode->GetObject<MobilityModel>();
    Ptr<MobilityModel> apMobility = apNode->GetObject<MobilityModel>();
    
    if (!staMobility || !apMobility) {
        NS_LOG_WARN("Mobility model missing for distance check");
        return;
    }
    
    Vector staPos = staMobility->GetPosition();
    Vector apPos = apMobility->GetPosition();
    double distance = CalculateDistance(staPos, apPos);
    
    NS_LOG_INFO("STA " << staNode->GetId() << " distance to AP: " 
                << std::fixed << std::setprecision(2) << distance << "m");
    
    if (distance <= range) {
        if (!ipv4->IsUp(ifIndex)) {
            NS_LOG_INFO("Bringing interface UP for STA " << staNode->GetId());
            ipv4->SetUp(ifIndex);
        }
    } else {
        if (ipv4->IsUp(ifIndex)) {
            NS_LOG_INFO("Bringing interface DOWN for STA " << staNode->GetId());
            ipv4->SetDown(ifIndex);
        }
    }
    
    // Reschedule next check
    Simulator::Schedule(Seconds(interval), &CheckDistance, 
                        staNode, apNode, ipv4, ifIndex, range, interval);
}

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
                if (pos == std::string::npos) break;
                
                // Extract one complete message
                std::string message = recvBuffer.substr(0, pos);
                recvBuffer.erase(0, pos + 1);  // Remove processed message
                
                // Process the message
                if (!message.empty()) {
                    InetSocketAddress iaddr = InetSocketAddress::ConvertFrom(from);
                    
                    // Check if this is a batch of positions
                    if (message.find("BATCH:") == 0) {
                        // Process batch of positions
                        std::string batchData = message.substr(6); // Remove "BATCH:" prefix
                        std::vector<uint32_t> receivedIds;
                        size_t start = 0;
                        size_t end = batchData.find(';');
                        
                        while (end != std::string::npos) {
                            std::string positionEntry = batchData.substr(start, end - start);
                            start = end + 1;
                            end = batchData.find(';', start);
                            
                            // Extract ID from position entry
                            size_t idPos = positionEntry.find("ID:");
                            if (idPos != std::string::npos) {
                                size_t idEnd = positionEntry.find('|', idPos);
                                if (idEnd != std::string::npos) {
                                    std::string idStr = positionEntry.substr(idPos + 3, idEnd - (idPos + 3));
                                    try {
                                        uint32_t id = std::stoul(idStr);
                                        receivedIds.push_back(id);
                                        NS_LOG_INFO(Simulator::Now().As(Time::S) 
                                            << " Server received batched position ID " << id);
                                    } catch (...) {
                                        // Ignore invalid IDs
                                    }
                                }
                            }
                        }
                        
                        // Send batch acknowledgment
                        std::ostringstream ack;
                        ack << "BATCH_OK:";
                        for (uint32_t id : receivedIds) {
                            ack << id << ",";
                        }
                        std::string response = ack.str();
                        if (response.back() == ',') {
                            response.pop_back();  // Remove trailing comma
                        }
                        response += "\n";
                        
                        Ptr<Packet> okPacket = Create<Packet>(
                            reinterpret_cast<const uint8_t*>(response.c_str()), 
                            response.size()
                        );
                        socket->Send(okPacket);
                    }
                    else {
                        // Single position message
                        NS_LOG_INFO(Simulator::Now().As(Time::S) << " Server received: " << message);
                        
                        // Extract ID from message
                        uint32_t id = 0;
                        size_t idPos = message.find("ID:");
                        if (idPos != std::string::npos) {
                            size_t idEnd = message.find('|', idPos);
                            if (idEnd != std::string::npos) {
                                std::string idStr = message.substr(idPos + 3, idEnd - (idPos + 3));
                                try {
                                    id = std::stoul(idStr);
                                } catch (...) {
                                    id = 0;
                                }
                            }
                        }
                        
                        // Send response with ID
                        std::ostringstream oss;
                        oss << "OK " << id << "\n";
                        std::string response = oss.str();
                        
                        Ptr<Packet> okPacket = Create<Packet>(
                            reinterpret_cast<const uint8_t*>(response.c_str()), 
                            response.size()
                        );
                        socket->Send(okPacket);
                    }
                }
            }
            
            delete[] buffer;
        }
    }
    
    Ptr<Socket> m_socket;
    uint16_t m_port;
    std::map<Ptr<Socket>, std::string> m_buffers;  // Per-socket receive buffers
};

// Client Application with batched position queuing ==========================
class PositionClient : public Application {
public:
    PositionClient() : 
        m_socket(nullptr), 
        m_node(nullptr), 
        m_running(false), 
        m_connected(false),
        m_queueEnabled(false),
        m_nextId(1) 
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
        m_positionQueue = std::queue<std::pair<uint32_t, std::string>>(); // Clear queue
    }
    
    void ConnectionSucceeded(Ptr<Socket> socket) {
        NS_LOG_INFO(Simulator::Now().As(Time::S) << " Connection succeeded");
        m_connected = true;
        
        // Send all queued positions in a single batch upon reconnection
        if (m_queueEnabled && !m_positionQueue.empty()) {
            NS_LOG_INFO("Sending batched positions (" << m_positionQueue.size() << " items)");
            
            // Build batch message
            std::ostringstream batch;
            batch << "BATCH:";
            while (!m_positionQueue.empty()) {
                auto& queuedPos = m_positionQueue.front();
                uint32_t id = queuedPos.first;
                std::string positionStr = queuedPos.second;
                
                // Format entry with ID
                batch << "ID:" << id << "|" << positionStr << ";";
                m_positionQueue.pop();
            }
            std::string batchMessage = batch.str();
            
            // Send the entire batch in one packet
            SendPacket(batchMessage);
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
        std::string baseStr = oss.str();
        uint32_t currentId = m_nextId++;
        
        // Queue or send based on connection status
        if (m_connected) {
            // Format message with ID
            std::ostringstream idOss;
            idOss << "ID:" << currentId << "|" << baseStr;
            SendPacket(idOss.str());
        } else if (m_queueEnabled) {
            NS_LOG_INFO("Queuing position ID " << currentId << ": " << baseStr);
            m_positionQueue.push(std::make_pair(currentId, baseStr));
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
                if (line.find("OK ") == 0) {
                    std::string idStr = line.substr(3);
                    try {
                        uint32_t id = std::stoul(idStr);
                        NS_LOG_INFO(Simulator::Now().As(Time::S) << " Node "
                                    << m_node->GetId() << " received OK for ID " << id);
                    } catch (...) {
                        NS_LOG_WARN("Invalid ID in OK response: " << line);
                    }
                }
                else if (line.find("BATCH_OK:") == 0) {
                    std::string idList = line.substr(9);
                    NS_LOG_INFO(Simulator::Now().As(Time::S) << " Node "
                                << m_node->GetId() << " received batch ack for IDs: " << idList);
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
    uint32_t m_nextId;
    EventId m_sendEvent;
    std::queue<std::pair<uint32_t, std::string>> m_positionQueue;  // Stores ID and position during disconnections
};

// Main Function =============================================================
int main(int argc, char *argv[]) {
    // Enable logging
    Time::SetResolution(Time::NS);
    LogComponentEnable("WifiPositionTracker", LOG_LEVEL_INFO);
    
    // Simulation parameters
    double simTime = 300.0;
    double commRange = 3.0; // Communication range in meters
    double checkInterval = 1.0; // Distance check interval in seconds
    
    // Create nodes
    NodeContainer remoteHostNode, apNode, staNodes;
    remoteHostNode.Create(1);
    apNode.Create(1);
    staNodes.Create(2);  // Two mobile stations
    
    // Create P2P link between remote host and AP
    PointToPointHelper p2pHelper;
    p2pHelper.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2pHelper.SetChannelAttribute("Delay", StringValue("2ms"));
    NetDeviceContainer p2pDevices = p2pHelper.Install(remoteHostNode.Get(0), apNode.Get(0));
    
    // Setup WiFi
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy = YansWifiPhyHelper::Default();
    phy.SetChannel(channel.Create());
    
    WifiHelper wifi;
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", 
                                 "DataMode", StringValue("OfdmRate54Mbps"));
    
    WifiMacHelper mac;
    Ssid ssid = Ssid("wifi-network");
    
    // Setup AP
    mac.SetType("ns3::ApWifiMac", "Ssid", SsidValue(ssid));
    NetDeviceContainer apDevice = wifi.Install(phy, mac, apNode.Get(0));
    
    // Setup STAs
    mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(ssid), 
                "ActiveProbing", BooleanValue(true));
    NetDeviceContainer staDevices = wifi.Install(phy, mac, staNodes);
    
    // Mobility model
    MobilityHelper mobility;
    
    // Fixed position for AP and remote host
    Ptr<ListPositionAllocator> fixedPositionAlloc = CreateObject<ListPositionAllocator>();
    fixedPositionAlloc->Add(Vector(0.0, 0.0, 0.0));   // Remote host
    fixedPositionAlloc->Add(Vector(10.0, 0.0, 0.0));  // AP
    mobility.SetPositionAllocator(fixedPositionAlloc);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(remoteHostNode);
    mobility.Install(apNode);
    
    // Moving STAs (RandomWaypoint model)
    Ptr<RandomRectanglePositionAllocator> staPositionAlloc = CreateObject<RandomRectanglePositionAllocator>();
    staPositionAlloc->SetAttribute("X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=20.0]"));
    staPositionAlloc->SetAttribute("Y", StringValue("ns3::UniformRandomVariable[Min=-10.0|Max=10.0]"));
    
    mobility.SetPositionAllocator(staPositionAlloc);
    mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                             "Speed", StringValue("ns3::UniformRandomVariable[Min=2|Max=8]"),
                             "Pause", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
                             "PositionAllocator", PointerValue(staPositionAlloc));
    mobility.Install(staNodes);
    
    // Install internet stacks
    InternetStackHelper stack;
    stack.Install(remoteHostNode);
    stack.Install(apNode);
    stack.Install(staNodes);
    
    // Assign IP addresses
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer p2pInterfaces = address.Assign(p2pDevices);
    
    address.SetBase("192.168.1.0", "255.255.255.0");
    Ipv4InterfaceContainer apInterface = address.Assign(apDevice);
    Ipv4InterfaceContainer staInterfaces = address.Assign(staDevices);
    
    // Setup server on remote host
    uint16_t serverPort = 5000;
    Ptr<PositionServer> serverApp = CreateObject<PositionServer>();
    serverApp->Setup(serverPort);
    remoteHostNode.Get(0)->AddApplication(serverApp);
    serverApp->SetStartTime(Seconds(0.0));
    serverApp->SetStopTime(Seconds(simTime));
    
    // Setup clients on STAs with queuing enabled
    for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
        Ptr<PositionClient> clientApp = CreateObject<PositionClient>();
        InetSocketAddress serverAddress(p2pInterfaces.GetAddress(0), serverPort);
        clientApp->Setup(serverAddress, staNodes.Get(i));
        clientApp->EnableQueueing(); // Enable position queuing
        staNodes.Get(i)->AddApplication(clientApp);
        clientApp->SetStartTime(Seconds(1.0 + i*0.2));
        clientApp->SetStopTime(Seconds(simTime - 1));
    }
    
    // Enable distance-based interface control for STAs
    for (uint32_t i = 0; i < staNodes.GetN(); i++) {
        Ptr<Ipv4> ipv4 = staNodes.Get(i)->GetObject<Ipv4>();
        int32_t ifIndex = ipv4->GetInterfaceForDevice(staDevices.Get(i));
        
        // Schedule periodic distance checks
        Simulator::Schedule(Seconds(1.0), &CheckDistance, 
                            staNodes.Get(i), apNode.Get(0), 
                            ipv4, ifIndex, commRange, checkInterval);
    }
    
    // Enable routing
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    
    // Print final positions
    for (uint32_t i = 0; i < staNodes.GetN(); ++i) {
        Ptr<MobilityModel> mob = staNodes.Get(i)->GetObject<MobilityModel>();
        Vector pos = mob->GetPosition();
        NS_LOG_INFO("STA " << i << " final position: (" 
                   << pos.x << ", " << pos.y << ", " << pos.z << ")");
    }
    
    Simulator::Destroy();
    return 0;
}

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/simple-position-client.h"
#include "ns3/simple-position-server.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/random-variable-stream.h"
#include "ns3/lte-module.h"
#include "ns3/utilities-module.h"
#include <ns3/winner-plus-propagation-loss-model.h>

#include <chrono>
#include <iomanip>
#include <stdlib.h>
#include <ctime>    
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TCC");

int main(int argc, char *argv[]) {
  LogComponentEnableAll(LOG_PREFIX_TIME);
  LogComponentEnableAll(LOG_PREFIX_NODE);
  LogComponentEnable("TCC", LOG_LEVEL_INFO);
  LogComponentEnable("SimplePositionClientApplication", LOG_LEVEL_INFO);
  LogComponentEnable("SimplePositionServerApplication", LOG_LEVEL_INFO);

  int seed = 1;
  uint8_t worker = 0;
  std::string mobilityFile;
  std::string simName = "test";
  double cellsize = 1000;
  int packetsize_app_a = 49; // 32 Bytes 5G mMTC payload + 4 Bytes CoAP Header + 13 Bytes DTLS Header
  int payloadSize;
  double syncFrequency;
  double packetChance = 0.3;
  bool edt = false;

  CommandLine cmd(__FILE__);
  cmd.AddValue("mobilityFile", "Mobility file", mobilityFile);
  cmd.AddValue("packetChance", "Packet chance", packetChance);
  cmd.AddValue("simName", "Total duration of the simulation", simName);
  cmd.AddValue("payloadSize", "Size of the payload", payloadSize);
  cmd.AddValue("syncFrequency", "Frequency of position gathering", syncFrequency);
  cmd.AddValue("worker", "worker id when using multithreading to not confuse logging", worker);
  cmd.AddValue("randomSeed", "randomSeed", seed);
  cmd.AddValue("edt", "Early Data Transmission", edt);
  cmd.Parse(argc, argv);

  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults();

  Ns2NodeUtility ns2Utility(mobilityFile);

  uint64_t ues_to_consider = ns2Utility.GetNNodes();
  Time simTime = Seconds(ns2Utility.GetSimulationTime());

  NodeContainer ueNodes;
  ueNodes.Create(ues_to_consider);
  Ns2MobilityHelper sumoTrace(mobilityFile);
  sumoTrace.Install();

  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  lteHelper->SetEpcHelper(epcHelper);
  lteHelper->EnableRrcLogging();
  lteHelper->SetEnbAntennaModelType("ns3::IsotropicAntennaModel");
  lteHelper->SetUeAntennaModelType("ns3::IsotropicAntennaModel");
  lteHelper->SetAttribute("PathlossModel", StringValue("ns3::WinnerPlusPropagationLossModel"));
  lteHelper->SetPathlossModelAttribute("HeightBasestation", DoubleValue(50));
  lteHelper->SetPathlossModelAttribute("Environment", EnumValue(UMaEnvironment));
  lteHelper->SetPathlossModelAttribute("LineOfSight", BooleanValue(false));
  Config::SetDefault("ns3::LteHelper::UseIdealRrc", BooleanValue(false));
  Config::SetDefault("ns3::LteSpectrumPhy::CtrlErrorModelEnabled", BooleanValue(false));
  Config::SetDefault("ns3::LteSpectrumPhy::DataErrorModelEnabled", BooleanValue(false));

  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  Ptr<Node> remoteHost = remoteHostContainer.Get(0);

  Ptr<Node> pgw = epcHelper->GetPgwNode();
  InternetStackHelper internet;
  internet.Install(remoteHostContainer);

  // Create the Internet
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gb/s")));
  p2ph.SetDeviceAttribute("Mtu", UintegerValue(1500));
  p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(10)));
  NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
  // interface 0 is localhost, 1 is the p2p device
  Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress(1);

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  NodeContainer enbNodes;
  enbNodes.Create(1);
  // Install Mobility Model
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator>();
  positionAlloc->Add(Vector(cellsize/2, cellsize/2, 25)); // Place our single eNb right in the center of the cell

  MobilityHelper mobilityEnb;
  mobilityEnb.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobilityEnb.SetPositionAllocator(positionAlloc);
  mobilityEnb.Install(enbNodes);

  // Install LTE Devices to the nodes
  NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice(ueNodes);

  // Install the IP stack on the UEs
  internet.Install(ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueLteDevs));
  // Assign IP address to UEs, and install applications
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u) {
    Ptr<Node> ueNode = ueNodes.Get(u);
    // Set the default gateway for the UE
    Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ueNode->GetObject<Ipv4>());
    ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  }
  RngSeedManager::SetSeed(seed);
  Ptr<UniformRandomVariable> RaUeUniformVariable = CreateObject<UniformRandomVariable>();


  // Install and start applications on UEs and remote host
  uint16_t ulPort = 2000;
  ApplicationContainer clientApps;

  Ptr<SimplePositionServer> serverApp = CreateObject<SimplePositionServer>();
  serverApp->SetAttribute("Port", UintegerValue(ulPort));
  remoteHost->AddApplication(serverApp);
  serverApp->SetStartTime(MilliSeconds(50));
  serverApp->SetStopTime(simTime);

  InetSocketAddress serverAddress(remoteHostAddr, ulPort);
  
  for (uint16_t i = 0; i < ues_to_consider; i++) {
    lteHelper->AttachSuspendedNb(ueLteDevs.Get(i), enbLteDevs.Get(0));

    Ptr<LteUeNetDevice> ueLteDevice = ueLteDevs.Get(i)->GetObject<LteUeNetDevice>();
    Ptr<LteUeRrc> ueRrc = ueLteDevice->GetRrc();
    ueRrc->EnableLogging();
    ueRrc->SetAttribute("CIoT-Opt", BooleanValue(false));
    if(edt == true){
      //std::cout << "EDT" << std::endl;
      ueRrc->SetAttribute("EDT", BooleanValue(true));
    }
    else{
      ueRrc->SetAttribute("EDT", BooleanValue(false));
    }

    Ptr<SimplePositionClient> clientApp = CreateObject<SimplePositionClient>();
    clientApp->SetAttribute("RemoteAddress", AddressValue(remoteHostAddr));
    clientApp->SetAttribute("RemotePort", UintegerValue(ulPort));
    clientApp->SetAttribute("Threshold", DoubleValue(packetChance));
    clientApp->SetAttribute("Node", PointerValue(ueNodes.Get(i)));
    clientApp->SetAttribute("EnbNode", PointerValue(enbNodes.Get(0)));
    clientApp->SetAttribute("ExtraPayloadSize", UintegerValue(packetsize_app_a + payloadSize));
    // clientApp->SetAttribute("PositionInterval", TimeValue(positionInterval));
    clientApp->SetAttribute("Interval", TimeValue(Seconds(syncFrequency)));
    ueNodes.Get(i)->AddApplication(clientApp);
    clientApps.Add(clientApp);

    clientApps.Get(i)->SetStartTime(Seconds(ns2Utility.GetEntryTimeForNode(i)));
    clientApps.Get(i)->SetStopTime(Seconds(ns2Utility.GetExitTimeForNode(i)));
  }

  auto start = std::chrono::system_clock::now(); 
  std::time_t start_time = std::chrono::system_clock::to_time_t(start);
  std::cout << "started computation at " << std::ctime(&start_time);
     std::string logdir = "logs/";
  std::string makedir = "mkdir -p ";
  std::string techdir = makedir;

  techdir += logdir;
  int z = std::system(techdir.c_str());
  std::cout << z;
  techdir += "/";
  techdir += simName;
  techdir += "/";
  z = std::system(techdir.c_str());
  std::cout << z;
  logdir += simName;
  logdir += "/";
  logdir += std::to_string(ueNodes.GetN());
  logdir += "_";
  logdir += std::to_string(simTime.GetInteger());
  logdir += "_";
  logdir += std::to_string(edt);
  
  std::string top_dirmakedir = makedir+logdir; 
  int a = std::system(top_dirmakedir.c_str());
  std::cout << a << std::endl;
  logdir += "/";
  
  auto tm = *std::localtime(&start_time);
  std::stringstream ss;
  ss << std::put_time(&tm, "%d_%m_%Y_%H_%M_%S");
  logdir += ss.str();
  logdir += "_";
  logdir += std::to_string(worker);
  logdir += "_";
  logdir += std::to_string(seed);
  logdir += "_";  

  for (uint16_t i = 0; i < ueNodes.GetN(); i++) {
    Ptr<LteUeNetDevice> ueLteDevice = ueLteDevs.Get(i)->GetObject<LteUeNetDevice>();
    Ptr<LteUeRrc> ueRrc = ueLteDevice->GetRrc();
    Ptr<LteUeMac> ueMac = ueLteDevice->GetMac();
    ueRrc->SetLogDir(logdir);
    ueMac->SetLogDir(logdir);
  }

  Ptr<LteEnbNetDevice> enbLteDevice = enbLteDevs.Get(0)->GetObject<LteEnbNetDevice>();
  Ptr<LteEnbRrc> enbRrc = enbLteDevice->GetRrc();
  enbRrc->SetLogDir(logdir);

  Simulator::Stop(simTime);
  Simulator::Run();
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = end-start;
  std::time_t end_time = std::chrono::system_clock::to_time_t(end);
  std::cout << "finished computation at " << std::ctime(&end_time)
              << "elapsed time: " << elapsed_seconds.count() << "s\n";
  Simulator::Destroy();
  return 0;
}


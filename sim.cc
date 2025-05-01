#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/random-variable-stream.h"
#include "ns3/lte-module.h"
#include <ns3/winner-plus-propagation-loss-model.h>
#include <chrono>
#include <iomanip>
#include <stdlib.h>
#include <ctime>
#include <cmath>
#include <fstream>
using namespace ns3;


NS_LOG_COMPONENT_DEFINE ("LenaNb5G");

void PrintEveryMinute() {
  // Print the current simulation time in minutes
  std::cout << "Simulated minute passed at " << Simulator::Now().As(Time::S) << std::endl;

  // Schedule the next print in 60 seconds (1 minute)
  Simulator::Schedule(Seconds(60.0), &PrintEveryMinute);
}

int main (int argc, char *argv[]) {
  Time simTime = Minutes(60);
  uint64_t ues_to_consider = 0;

  uint8_t worker = 0;
  int seed = 1;
  std::string simName = "lena-nb";
  double cellsize = 1000; // in meters
  int num_ues = 1;
  int packetsize = 200; // 32 Bytes 5G mMTC payload + 4 Bytes CoAP Header + 13 Bytes DTLS Header (UDP Header and IP Header are added by NS-3)
  Time packetinterval = Seconds(1.0);
  bool ciot = false;
  bool edt = true;
  // Command line arguments
  CommandLine cmd (__FILE__);
  cmd.AddValue ("simTime", "Total duration of the simulation", simTime);
  cmd.AddValue ("simName", "Total duration of the simulation", simName);
  cmd.AddValue ("worker", "worker id when using multithreading to not confuse logging", worker);
  cmd.AddValue ("randomSeed", "randomSeed",seed);
  cmd.AddValue ("numUes", "Number of UEs", num_ues);
  cmd.AddValue ("ciot", "Cellular IoT Optimization",ciot);
  cmd.AddValue ("edt", "Early Data Transmission",edt);
  cmd.Parse (argc, argv);
  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults ();

  // parse again so you can override default values from the command line

  Ptr<LteHelper> lteHelper = CreateObject<LteHelper> ();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper> ();
  lteHelper->SetEpcHelper (epcHelper);
  lteHelper->EnableRrcLogging ();
  lteHelper->SetEnbAntennaModelType ("ns3::IsotropicAntennaModel");
  lteHelper->SetUeAntennaModelType ("ns3::IsotropicAntennaModel");
  lteHelper->SetAttribute ("PathlossModel", StringValue ("ns3::WinnerPlusPropagationLossModel")); // Note that the Winner+ pathloss model isn't available in the current release of ns3. It can be downloaded at https://github.com/tudo-cni/ns3-propagation-winner-plus
  lteHelper->SetPathlossModelAttribute ("HeightBasestation", DoubleValue (50));
  lteHelper->SetPathlossModelAttribute ("Environment", EnumValue (UMaEnvironment));
  lteHelper->SetPathlossModelAttribute ("LineOfSight", BooleanValue (false));
  // lteHelper->SetPathlossModelAttribute ("MaxRange", DoubleValue (75*std::sqrt(5)));
  Config::SetDefault ("ns3::LteHelper::UseIdealRrc", BooleanValue (false));
  Config::SetDefault ("ns3::LteSpectrumPhy::CtrlErrorModelEnabled", BooleanValue (false));
  Config::SetDefault ("ns3::LteSpectrumPhy::DataErrorModelEnabled", BooleanValue (false));


  // Calculate UES to consider
  ues_to_consider = num_ues;
  // std::cout << "UEs to consider: " << ues_to_consider<< std::endl;

  Ptr<Node> pgw = epcHelper->GetPgwNode ();
   // Create a single RemoteHost
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);
  InternetStackHelper internet;
  internet.Install (remoteHostContainer);

  // Create the Internet
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("100Gb/s")));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (1500));
  p2ph.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (10)));
  NetDeviceContainer internetDevices = p2ph.Install (pgw, remoteHost);
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (internetDevices);
  // interface 0 is localhost, 1 is the p2p device
  Ipv4Address remoteHostAddr = internetIpIfaces.GetAddress (1);

  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting (remoteHost->GetObject<Ipv4> ());
  remoteHostStaticRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"), 1);

  NodeContainer enbNodes;
  enbNodes.Create (3);

  // Install Mobility Model
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  Vector firstTower(cellsize/4, cellsize/4, 25);
  Vector midTower(cellsize/2, 3*cellsize/4, 25);
  Vector lastTower(3*cellsize/4, cellsize/4, 25);

  positionAlloc->Add (firstTower);
  positionAlloc->Add (midTower);
  positionAlloc->Add (lastTower);

  MobilityHelper mobilityEnb;
  mobilityEnb.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobilityEnb.SetPositionAllocator(positionAlloc);
  mobilityEnb.Install(enbNodes);

  /*
  For all scenarios, 3*X minutes of simulation time are simulated, but only the intermediate X minutes are evaluated.
  The first X minutes produce no significant results since devices at the beginning are scheduled in an empty cell and experience
  very good transmission conditions. After X minutes, new devices will find ongoing transmissions of previous devices, which enables
  a more realistic situation and produces significant results. Since devices that have started transmissions within the intermediate X
  minutes of the simulation may not complete their transmissions in this intermediate time slot, additional X minutes are simulated
  with more new transmissions to keep the channels busy and let the intermediate devices complete their transmissions.
  */
  NodeContainer ueNodes;
  ueNodes.Create (ues_to_consider*3); // Pre-Run, Run, Post-Run.

  MobilityHelper mobilityUe;
  mobilityUe.SetMobilityModel ("ns3::WaypointMobilityModel");
  mobilityUe.Install (ueNodes);

  Time X_sec = simTime * 60;  // Convert to seconds
  Time phaseDuration = X_sec;

  for (uint32_t ueId = 0; ueId < ueNodes.GetN(); ++ueId) { // Pre-Run, Run, Post-Run.
    Ptr<Node> ue = ueNodes.Get(ueId);
    Ptr<WaypointMobilityModel> mobility = ue->GetObject<WaypointMobilityModel>();

    // Determine UE group and movement timing
    uint32_t group = ueId / ues_to_consider;
    Time startTime = group * X_sec;
    
    // Determine path direction (50% first->last, 50% last->first)
    bool firstToLast = (ueId % 2 == 0);

    // Set waypoints with UE height at 1.5m
    Vector startPos, midPos, endPos;
    if (firstToLast) {
        startPos = Vector(firstTower.x, firstTower.y, 1.5);
        endPos = Vector(lastTower.x, lastTower.y, 1.5);
    } else {
        startPos = Vector(lastTower.x, lastTower.y, 1.5);
        endPos = Vector(firstTower.x, firstTower.y, 1.5);
    }
    midPos = Vector(midTower.x, midTower.y, 1.5);

    // Add waypoints (start -> mid -> end)
    mobility->AddWaypoint(Waypoint(startTime, startPos));
    mobility->AddWaypoint(Waypoint(startTime + phaseDuration/2, midPos));
    mobility->AddWaypoint(Waypoint(startTime + phaseDuration, endPos));
  }

  // Install LTE Devices to the nodes
  NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice (enbNodes);
  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice (ueNodes);

  // Install the IP stack on the UEs
  internet.Install (ueNodes);
  Ipv4InterfaceContainer ueIpIface;
  ueIpIface = epcHelper->AssignUeIpv4Address (NetDeviceContainer (ueLteDevs));
  // Assign IP address to UEs, and install applications
  for (uint32_t u = 0; u < ueNodes.GetN (); ++u)
    {
      Ptr<Node> ueNode = ueNodes.Get (u);
      // Set the default gateway for the UE
      Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting (ueNode->GetObject<Ipv4> ());
      ueStaticRouting->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (), 1);
    }
  RngSeedManager::SetSeed (seed);
  Ptr<UniformRandomVariable> RaUeUniformVariable = CreateObject<UniformRandomVariable> ();

  // Install and start applications on UEs and remote host
  uint16_t ulPort = 2000;
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;

  // Set up the data transmission for the Pre-Run
  for (uint16_t i = 0; i < ues_to_consider; i++)
    {
      int access = RaUeUniformVariable->GetInteger (50, simTime.GetMilliSeconds());
      lteHelper->AttachSuspendedNb(ueLteDevs.Get(i), enbLteDevs.Get(0));

      Ptr<LteUeNetDevice> ueLteDevice = ueLteDevs.Get(i)->GetObject<LteUeNetDevice> ();
      Ptr<LteUeRrc> ueRrc = ueLteDevice->GetRrc();
      if(ciot == true){
        //std::cout << "ciot" << std::endl;
        ueRrc->SetAttribute("CIoT-Opt", BooleanValue(true));
      }
      else{
        ueRrc->SetAttribute("CIoT-Opt", BooleanValue(false));
      }
      if(edt == true){
        //std::cout << "EDT" << std::endl;
        ueRrc->SetAttribute("EDT", BooleanValue(true));
      }
      else{
        ueRrc->SetAttribute("EDT", BooleanValue(false));
      }

      ++ulPort;
      UdpEchoServerHelper server (ulPort);
      serverApps.Add(server.Install (remoteHost));
      //
      // Create a UdpEchoClient application to send UDP datagrams from node zero to
      // node one.
      //
      if (i < num_ues){
        uint packetsize = packetsize;
        UdpEchoClientHelper ulClient (remoteHostAddr, ulPort);
        ulClient.SetAttribute ("Interval", TimeValue (packetinterval));
        ulClient.SetAttribute ("MaxPackets", UintegerValue (1000000));
        ulClient.SetAttribute ("PacketSize", UintegerValue(packetsize));
        clientApps.Add (ulClient.Install (ueNodes.Get(i)));

        serverApps.Get(i)->SetStartTime (MilliSeconds (access));
        clientApps.Get(i)->SetStartTime (MilliSeconds (access));
      }
    }


  // Set up the data transmission for the UEs to be considered in the results
  for (uint16_t i = ues_to_consider; i < ues_to_consider*2; i++)
    {
      int access = RaUeUniformVariable->GetInteger (simTime.GetMilliSeconds(), 2*simTime.GetMilliSeconds());
      lteHelper->AttachSuspendedNb(ueLteDevs.Get(i), enbLteDevs.Get(0));

      Ptr<LteUeNetDevice> ueLteDevice = ueLteDevs.Get(i)->GetObject<LteUeNetDevice> ();
      Ptr<LteUeRrc> ueRrc = ueLteDevice->GetRrc();
      ueRrc->EnableLogging();
      if(ciot == true){
        //std::cout << "ciot" << std::endl;
        ueRrc->SetAttribute("CIoT-Opt", BooleanValue(true));
      }
      else{
        ueRrc->SetAttribute("CIoT-Opt", BooleanValue(false));
      }
      if(edt == true){
        //std::cout << "EDT" << std::endl;
        ueRrc->SetAttribute("EDT", BooleanValue(true));
      }
      else{
        ueRrc->SetAttribute("EDT", BooleanValue(false));
      }

      ++ulPort;
      UdpEchoServerHelper server (ulPort);
      serverApps.Add(server.Install (remoteHost));
      //
      // Create a UdpEchoClient application to send UDP datagrams from node zero to
      // node one.
      //

      if (i < ues_to_consider+num_ues){
        uint packetsize = packetsize;
        UdpEchoClientHelper ulClient (remoteHostAddr, ulPort);
        ulClient.SetAttribute ("Interval", TimeValue (packetinterval));
        ulClient.SetAttribute ("MaxPackets", UintegerValue (1000000));
        ulClient.SetAttribute ("PacketSize", UintegerValue(packetsize));
        clientApps.Add (ulClient.Install (ueNodes.Get(i)));

        serverApps.Get(i)->SetStartTime (MilliSeconds (access));
        clientApps.Get(i)->SetStartTime (MilliSeconds (access));
      }
    }
  


  // Set up the data transmission for the Post-Run
  for (uint16_t i = ues_to_consider*2; i < ues_to_consider*3; i++)
    {
      int access = RaUeUniformVariable->GetInteger (simTime.GetMilliSeconds()*2, simTime.GetMilliSeconds()*3);
      lteHelper->AttachSuspendedNb(ueLteDevs.Get(i), enbLteDevs.Get(0));

      Ptr<LteUeNetDevice> ueLteDevice = ueLteDevs.Get(i)->GetObject<LteUeNetDevice> ();
      Ptr<LteUeRrc> ueRrc = ueLteDevice->GetRrc();
      if(ciot == true){
        //std::cout << "ciot" << std::endl;
        ueRrc->SetAttribute("CIoT-Opt", BooleanValue(true));
      }
      else{
        ueRrc->SetAttribute("CIoT-Opt", BooleanValue(false));
      }
      if(edt == true){
        //std::cout << "EDT" << std::endl;
        ueRrc->SetAttribute("EDT", BooleanValue(true));
      }
      else{
        ueRrc->SetAttribute("EDT", BooleanValue(false));
      }

      ++ulPort;
      UdpEchoServerHelper server (ulPort);
      serverApps.Add(server.Install (remoteHost));
      //
      // Create a UdpEchoClient application to send UDP datagrams from node zero to
      // node one.
      //
      if (i < ues_to_consider*2 + num_ues){
        uint packetsize = packetsize;
        UdpEchoClientHelper ulClient (remoteHostAddr, ulPort);
        ulClient.SetAttribute ("Interval", TimeValue (packetinterval));
        ulClient.SetAttribute ("MaxPackets", UintegerValue (1000000));
        ulClient.SetAttribute ("PacketSize", UintegerValue(packetsize));
        clientApps.Add (ulClient.Install (ueNodes.Get(i)));

        serverApps.Get(i)->SetStartTime (MilliSeconds (access));
        clientApps.Get(i)->SetStartTime (MilliSeconds (access));
      }
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
  logdir += std::to_string(ciot);
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

  for (uint16_t i = 0; i < ueNodes.GetN(); i++){
    Ptr<LteUeNetDevice> ueLteDevice = ueLteDevs.Get(i)->GetObject<LteUeNetDevice> ();
    Ptr<LteUeRrc> ueRrc = ueLteDevice->GetRrc();
    Ptr<LteUeMac> ueMac = ueLteDevice->GetMac();
    ueRrc->SetLogDir(logdir);
    ueMac->SetLogDir(logdir);
  }

  Ptr<LteEnbNetDevice> enbLteDevice = enbLteDevs.Get(0)->GetObject<LteEnbNetDevice>();
  Ptr<LteEnbRrc> enbRrc = enbLteDevice->GetRrc();
  enbRrc->SetLogDir(logdir);

  // lteHelper->EnableTraces ();
  // p2ph.EnablePcapAll("lena-simple-epc");

  Simulator::Schedule(Seconds(60.0), &PrintEveryMinute);
  Simulator::Stop (3*simTime); // Pre-Run, Run, Post-Run
  Simulator::Run ();
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = end-start;
  std::time_t end_time = std::chrono::system_clock::to_time_t(end);
  std::cout << "finished computation at " << std::ctime(&end_time)
            << "elapsed time: " << elapsed_seconds.count() << "s\n";
  Simulator::Destroy ();
  return 0;
}


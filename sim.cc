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

NS_LOG_COMPONENT_DEFINE("Tcc");

// 32 Bytes 5G mMTC payload + 4 Bytes CoAP Header + 13 Bytes DTLS Header (Headers de UDP e IP são adicionados pelo ns3)
const int headerSize = 32 + 4 + 13;
const double cellsize = 1000.0; // em metros

// Funções auxiliares
// PrintEveryMinute serve para acompanhar quanto tempo já foi passado na simulação 
void PrintEveryMinute() {
  std::cout << "Simulated minute passed at " << Simulator::Now().As(Time::S) << std::endl;
  Simulator::Schedule(Seconds(60.0), &PrintEveryMinute);
}

// Função principal
int main(int argc, char *argv[]) {
  // Parâmetros ajustáveis
  std::string simName = "Default";   // Nome da simulação
  Time simTime = Minutes(60.0);      // Tempo de simulação
  int seed = 0;                      // Semente para geração pseudo-aleatória de números
  double coverage = 40.0;            // Cobertura das torres de transmissão
  uint64_t numUes = 0;               // Quantidade de dispositivos de rastreamento
  int payloadSize = 200;             // Tamanho do payload enviado
  Time syncFrequency = Seconds(1.0); // Frequência de coleta dos dados de rastreamento
  bool edt = true;                   // Usar modo de transmissão EDT

  // Parâmetros adicionais
  uint8_t worker = 0; // Usado para simulações em multithreading

  // Sobrescreve parâmetros ajustáveis pelos fornecidos via CLI, se existirem
  CommandLine cmd(__FILE__);
  cmd.AddValue("simName", "Nome da simulação", simName);
  cmd.AddValue("simTime", "Duração total da simulação", simTime);
  cmd.AddValue("seed", "Semente para geração pseudo-aleatória de números", seed);
  cmd.AddValue("coverage", "Cobertura das torres de transmissão", coverage);
  cmd.AddValue("numUes", "Quantidade de dispositivos de rastreamento", numUes);
  cmd.AddValue("payloadSize", "Tamanho do payload enviado", payloadSize);
  cmd.AddValue("syncFrequency", "Frequência de coleta dos dados de rastreamento", syncFrequency);
  cmd.AddValue("edt", "Ativar Modo de transmissão EDT (Early Data Transmission)", edt);
  cmd.AddValue("worker", "ID da thread responsável para não confundir os logs", worker);
  cmd.Parse (argc, argv);
  ConfigStore inputConfig;
  inputConfig.ConfigureDefaults();

  // Cálculos de tempo
  Time phaseDuration = Seconds(simTime);  // Converte para segundos

  // Configuração de rede
  NodeContainer ueNodes, enbNodes, serverNode;
  enbNodes.Create(3);
  ueNodes.Create(numUes*3); // Eles são criados conforme as três fases
  serverNode.Create(1);

  // Configuração do LTE e EPC
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  lteHelper->SetEpcHelper(epcHelper);
  lteHelper->EnableRrcLogging();
  lteHelper->SetEnbAntennaModelType("ns3::IsotropicAntennaModel");
  lteHelper->SetUeAntennaModelType("ns3::IsotropicAntennaModel");
  lteHelper->SetAttribute ("PathlossModel", StringValue ("ns3::WinnerPlusPropagationLossModel"));
  lteHelper->SetPathlossModelAttribute("HeightBasestation", DoubleValue(50));
  lteHelper->SetPathlossModelAttribute("Environment", EnumValue(UMaEnvironment));
  lteHelper->SetPathlossModelAttribute("LineOfSight", BooleanValue(false));
  Config::SetDefault("ns3::LteHelper::UseIdealRrc", BooleanValue(false));
  Config::SetDefault("ns3::LteSpectrumPhy::CtrlErrorModelEnabled", BooleanValue(false));
  Config::SetDefault("ns3::LteSpectrumPhy::DataErrorModelEnabled", BooleanValue(false));

  // Configuração da mobilidade
  MobilityHelper mobility;
  SetupMobility(ueNodes, enbNodes);

  // Instala LTE nos nós
  NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);

  // Configuração de internet
  InstallInternetStack(epcHelper, enbNodes, serverNode);

  // Configuração das aplicações
  InstallApplications(ueNodes, serverNode, payloadSize, syncFrequency, phaseDuration);
  
  // Configuração do modo de transmissão
  ConfigureEdt(ueDevs, edt);

  // Inicia contagem do tempo
  auto start = std::chrono::system_clock::now(); 
  std::time_t start_time = std::chrono::system_clock::to_time_t(start);
  std::cout << "started computation at " << std::ctime(&start_time);

  // Configuração dos logs
  SetupLogging(simName, seed);

  // Execução da simulação
  Simulator::Schedule(Seconds(60.0), &PrintEveryMinute);
  Simulator::Stop(3*simTime); // Contempla as três fases
  Simulator::Run();

  // Finaliza contagem do tempo
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = end-start;
  std::time_t end_time = std::chrono::system_clock::to_time_t(end);
  std::cout << "finished computation at " << std::ctime(&end_time)
            << "elapsed time: " << elapsed_seconds.count() << "s\n";

  // Fim da simulação
  Simulator::Destroy();
  return 0;
}

// Funções de configuração
void SetupMobility(NodeContainer& ueNodes, NodeContainer& enbNodes) {
    // Posição das torres de transmissão
    Ptr<ListPositionAllocator> enbPositions = CreateObject<ListPositionAllocator>();
    enbPositions->Add(Vector(0, 0, 25));
    enbPositions->Add(Vector(500, 500, 25));
    enbPositions->Add(Vector(1000, 0, 25));

    MobilityHelper enbMobility;
    enbMobility.SetPositionAllocator(enbPositions);
    enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    enbMobility.Install(enbNodes);

    // Mobilidade das UEs considerando coverage
    MobilityHelper ueMobility;
    ObjectFactory posFactory;
    posFactory.SetTypeId("ns3::RandomRectanglePositionAllocator");
    posFactory.Set("X", StringValue("ns3::UniformRandomVariable[Min=0|Max=1000]"));
    posFactory.Set("Y", StringValue("ns3::UniformRandomVariable[Min=0|Max=1000]"));
    
    Ptr<PositionAllocator> posAlloc = posFactory.Create()->GetObject<PositionAllocator>();
    ueMobility.SetMobilityModel("ns3::WaypointMobilityModel");
    ueMobility.SetPositionAllocator(posAlloc);
    ueMobility.Install(ueNodes);
}

void InstallInternetStack(Ptr<PointToPointEpcHelper> epcHelper,
                         NodeContainer& enbNodes,
                         NodeContainer& serverNode) 
{
    // Instala a configuração de internet nos eNBs e no servidor
    InternetStackHelper internet;
    internet.Install(enbNodes);
    internet.Install(serverNode);

    // Criar links P2P entre servidor e eNBs
    PointToPointHelper p2ph;
    p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("1Gbps")));
    p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(5)));
    
    // Cria um segmento de rede por eNB
    std::vector<Ipv4InterfaceContainer> enbServerInterfaces;
    for (uint32_t i = 0; i < enbNodes.GetN(); ++i) {
        NetDeviceContainer devices = p2ph.Install(enbNodes.Get(i), serverNode.Get(0));
        
        // Fornece endereços IP (rede 10.0.i.0/24)
        std::ostringstream subnet;
        subnet << "10.0." << i << ".0";
        Ipv4AddressHelper ipv4h;
        ipv4h.SetBase(subnet.str().c_str(), "255.255.255.0");
        enbServerInterfaces.push_back(ipv4h.Assign(devices));
    }

    // Configura roteamento dos eNBs
    Ipv4StaticRoutingHelper ipv4RoutingHelper;
    for (uint32_t i = 0; i < enbNodes.GetN(); ++i) {
        Ptr<Node> enb = enbNodes.Get(i);
        Ptr<Ipv4StaticRouting> enbRouting = ipv4RoutingHelper.GetStaticRouting(
            enb->GetObject<Ipv4>());
        
        // Route all non-local traffic through server link
        enbRouting->AddNetworkRouteTo(
            Ipv4Address("0.0.0.0"), 
            Ipv4Mask("0.0.0.0"),
            enbServerInterfaces[i].GetAddress(1), // Server-side IP
            1);                                   // Interface index
    }

    // 4. Configure Server routing
    Ptr<Ipv4StaticRouting> serverRouting = ipv4RoutingHelper.GetStaticRouting(
        serverNode.Get(0)->GetObject<Ipv4>());
    
    // Route UE network traffic back through eNodeBs
    for (uint32_t i = 0; i < enbNodes.GetN(); ++i) {
        serverRouting->AddNetworkRouteTo(
            Ipv4Address("7.0.0.0"),      // UE network
            Ipv4Mask("255.0.0.0"),
            enbServerInterfaces[i].GetAddress(0), // eNodeB-side IP
            i+1);                        // Interface index (offset by 1)
    }

    // 5. Configure EPC for LTE-only UE communication
    epcHelper->SetAttribute("S1uLinkDataRate", DataRateValue(DataRate("10Mbps")));
    epcHelper->SetAttribute("S1uLinkDelay", TimeValue(MilliSeconds(2)));
}

void InstallApplications(NodeContainer& ueNodes, NodeContainer& serverNode,
                         uint32_t payloadSize, double syncFrequency, Time phaseDuration) {
    // Aplicação do servidor
    Ptr<ServerApplication> serverApp = CreateObject<ServerApplication>();
    serverNode.Get(0)->AddApplication(serverApp);
    serverApp->SetStartTime(Seconds(0));

    // Aplicação dos dispositivos de rastreamento
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) {
        Ptr<DrApplication> drApp = CreateObject<DrApplication>();
        drApp->SetAttribute("PayloadSize", UintegerValue(payloadSize));
        drApp->SetAttribute("Interval", TimeValue(Seconds(syncFrequency)));
        drApp->SetRemote(serverNode.Get(0)->GetObject<Ipv4>()->GetAddress(1,0).GetLocal());
        
        ueNodes.Get(i)->AddApplication(drApp);
        drApp->SetStartTime(CalculateStartTime(i, phaseDuration));
    }
}

void ConfigureEdt(NetDeviceContainer& ueDevs, bool edt) {
    for (uint32_t i = 0; i < ueDevs.GetN(); ++i) {
        Ptr<LteUeNetDevice> ueDev = ueDevs.Get(i)->GetObject<LteUeNetDevice>();
        Ptr<LteUeRrc> ueRrc = ueDev->GetRrc();

        ueRrc->SetAttribute("EDT", BooleanValue(edt));
    }
}



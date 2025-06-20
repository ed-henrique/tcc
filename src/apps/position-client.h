#ifndef POSITION_CLIENT_H
#define POSITION_CLIENT_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include "ns3/traced-callback.h"

namespace ns3 {

class Socket;
class Packet;

class PositionClient : public Application {
public:
  static TypeId GetTypeId(void);
  PositionClient();
  virtual ~PositionClient();

protected:
  virtual void DoDispose(void);

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void ScheduleTransmit(Time dt);
  void SchedulePositionGathering(Time dt);
  void GatherPosition(void);
  void Send(void);

  void HandleRead(Ptr<Socket> socket);

  Ptr<Node> m_node;
  Ptr<Node> m_enbNode;
  std::map<uint32_t, std::string> m_positionMap;
  uint32_t m_nextId;
  double m_range;

  Time m_interval;
  Time m_positionInterval;
  uint32_t m_extraPayloadSize;
  uint32_t m_amountPositionsToSend;

  uint32_t m_sent;
  Ptr<Socket> m_socket;
  Address m_peerAddress;
  uint16_t m_peerPort;
  EventId m_sendEvent;

  TracedCallback<Ptr<const Packet>> m_txTrace;
  TracedCallback<Ptr<const Packet>> m_rxTrace;
  TracedCallback<Ptr<const Packet>, const Address &, const Address &> m_txTraceWithAddresses;
  TracedCallback<Ptr<const Packet>, const Address &, const Address &> m_rxTraceWithAddresses;
};

} // namespace ns3

#endif /* POSITION_CLIENT_H */

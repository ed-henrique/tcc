#ifndef CHECKPOINTING_POSITION_CLIENT_H
#define CHECKPOINTING_POSITION_CLIENT_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include "ns3/traced-callback.h"
#include "ns3/random-variable-stream.h"

namespace ns3 {

class Socket;
class Packet;

class CheckpointingPositionClient : public Application {
public:
  static TypeId GetTypeId(void);
  CheckpointingPositionClient();
  virtual ~CheckpointingPositionClient();

protected:
  virtual void DoDispose(void);

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void ScheduleInside(Time dt);
  void ScheduleTransmit(Time dt);
  void SchedulePositionGathering(Time dt);
  void Inside(void);
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
  uint32_t m_lost;
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

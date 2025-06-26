#ifndef DEADRECKONING_POSITION_SERVER_H
#define DEADRECKONING_POSITION_SERVER_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/address.h"
#include "ns3/traced-callback.h"

namespace ns3 {

class Socket;
class Packet;

class SimplePositionServer : public Application {
public:
  static TypeId GetTypeId(void);
  SimplePositionServer();
  virtual ~SimplePositionServer();

protected:
  virtual void DoDispose(void);

private:
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void HandleRead(Ptr<Socket> socket);

  uint16_t m_port;
  Ptr<Socket> m_socket;
  Ptr<Socket> m_socket6;
  std::map<std::string, std::map<uint32_t, std::string>> m_positionMap;
  
  Address m_local;

  TracedCallback<Ptr<const Packet>> m_rxTrace;
  TracedCallback<Ptr<const Packet>, const Address &, const Address &> m_rxTraceWithAddresses;
};

} // namespace ns3

#endif /* POSITION_SERVER_H */


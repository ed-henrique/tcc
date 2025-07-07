#ifndef GPS_CBL_POSITION_SERVER_H
#define GPS_CBL_POSITION_SERVER_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/address.h"
#include "ns3/traced-callback.h"

namespace ns3 {

class Socket;
class Packet;

class GPSCBLPositionServer : public Application {
public:
  static TypeId GetTypeId(void);
  GPSCBLPositionServer();
  virtual ~GPSCBLPositionServer();

protected:
  virtual void DoDispose(void);

private:
  struct VehicleState {
    uint32_t id;
    Vector lastPosition;
    double lastSpeed = 0;
    double lastDirection = 0;
    Time lastUpdate;
    bool receivedUpdate = false;
  };
  
  virtual void StartApplication(void);
  virtual void StopApplication(void);

  void EstimatePositions(void);
  void HandleRead(Ptr<Socket> socket);

  uint16_t m_port;
  Ptr<Socket> m_socket;
  Ptr<Socket> m_socket6;
  Address m_local;
  std::map<uint32_t, VehicleState> m_vehicleStates;

  TracedCallback<Ptr<const Packet>> m_rxTrace;
  TracedCallback<Ptr<const Packet>, const Address &, const Address &> m_rxTraceWithAddresses;
};

} // namespace ns3

#endif /* POSITION_SERVER_H */


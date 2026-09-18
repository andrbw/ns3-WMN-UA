/*
 * AdhocAlohaMac extended with carrier sensing to implement p-persistent CSMA.
 */

#include "adhoc-csma-mac.h"

#include "wifi-mpdu.h"

#include "ns3/channel-access-manager.h"
#include "ns3/double.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/wifi-phy.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("AdhocCsmaMac");

NS_OBJECT_ENSURE_REGISTERED(AdhocCsmaMac);

TypeId
AdhocCsmaMac::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::AdhocCsmaMac")
            .SetParent<AdhocAlohaMac>()
            .SetGroupName("Wifi")
            .AddConstructor<AdhocCsmaMac>()
            .AddAttribute("Slot",
                          "Duration of a contention slot, i.e. the interval at the end of which a "
                          "station that decided not to transmit senses the channel again. It "
                          "should be at least maximum propagation delay across the network, so "
                          "that every station has learnt the outcome of the previous slot.",
                          TimeValue(MicroSeconds(9)),
                          MakeTimeAccessor(&AdhocCsmaMac::m_slot),
                          MakeTimeChecker(Time(0)))
            .AddAttribute("TransmitProbability",
                          "Probability p with which a station transmits in an idle contention slot.",
                          DoubleValue(0.5),
                          MakeDoubleAccessor(&AdhocCsmaMac::m_p),
                          MakeDoubleChecker<double>(0.0, 1.0));
    return tid;
}

AdhocCsmaMac::AdhocCsmaMac()
    : m_uniform(CreateObject<UniformRandomVariable>())
{
    NS_LOG_FUNCTION(this);
}

AdhocCsmaMac::~AdhocCsmaMac()
{
    NS_LOG_FUNCTION(this);
}

void
AdhocCsmaMac::AttemptTransmission(Ptr<WifiMpdu> mpdu)
{
    NS_LOG_FUNCTION(this << *mpdu);

    // Carrier sense is already maintained by the channel
    // access manager (note that it includes the NAV).
    if (!GetChannelAccessManager()->IsBusy())
    {
        // If channel becomes idle - transmit with probability m_p
        if (m_uniform->GetValue() < m_p)
        {
            Transmit(mpdu);
            return;
        }
        NS_LOG_DEBUG("Channel idle, but the frame is withheld for one slot");
        Defer(m_slot, mpdu);
        return;
    }

    // Channel is busy. CSMA persists: it keeps listening and senses again as soon as the 
    // channel may have cleared (GetDelayUntilIdle()). 
    // Sensing is retried, falling back to once per slot.
    Time untilIdle = GetWifiPhy()->GetDelayUntilIdle();
    Defer(untilIdle.IsZero() ? GetWifiPhy()->GetSlot() : untilIdle, mpdu);
}

void
AdhocCsmaMac::Defer(Time delay, Ptr<WifiMpdu> mpdu)
{
    NS_LOG_FUNCTION(this << delay << *mpdu);

    NS_ASSERT(!m_pendingEvent.IsPending());
    m_pendingEvent = Simulator::Schedule(delay, &AdhocCsmaMac::AttemptTransmission, this, mpdu);
}

} // namespace ns3

/*
 * Modified copy of adhoc-wifi-mac.cc
 */

#include "adhoc-aloha-mac.h"

#include "qos-txop.h"
#include "wifi-mac-queue.h"

#include "ns3/eht-capabilities.h"
#include "ns3/he-capabilities.h"
#include "ns3/ht-capabilities.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/vht-capabilities.h"

#include "ns3/abort.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/frame-exchange-manager.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/wifi-phy.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("AdhocAlohaMac");

NS_OBJECT_ENSURE_REGISTERED(AdhocAlohaMac);

TypeId
AdhocAlohaMac::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::AdhocAlohaMac")
            .SetParent<WifiMac>()
            .SetGroupName("Wifi")
            .AddConstructor<AdhocAlohaMac>()
            .AddAttribute("EnableAck",
                          "If true, frames are individually addressed, acknowledged by means of "
                          "the regular Wi-Fi Ack frame and retransmitted after a random delay if "
                          "no Ack is received. If false (pure Aloha), frames are broadcast and "
                          "hence neither acknowledged nor retransmitted.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&AdhocAlohaMac::m_useAck),
                          MakeBooleanChecker())
            .AddAttribute("MaxRetries",
                          "Maximum number of retransmissions of a frame before it is given up "
                          "on. Only used when EnableAck is true.",
                          UintegerValue(4),
                          MakeUintegerAccessor(&AdhocAlohaMac::m_maxRetries),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("RetransmissionDelay",
                          "Random time interval (in seconds) between a missed Ack and the "
                          "retransmission of the frame, as in classical Aloha. Only used when "
                          "EnableAck is true. If left unset, it defaults to "
                          "UniformRandomVariable[Min=0.0|Max=0.01]. Note that it is deliberately "
                          "not instantiated unless EnableAck is true, so that enabling this "
                          "feature does not shift the RNG stream assignment of a pure Aloha run.",
                          PointerValue(),
                          MakePointerAccessor(&AdhocAlohaMac::m_retxDelay),
                          MakePointerChecker<RandomVariableStream>());
    return tid;
}

AdhocAlohaMac::AdhocAlohaMac()
    : m_retryCount(0)
{
    NS_LOG_FUNCTION(this);
    // Let the lower layers know that we are acting in an IBSS
    SetTypeOfStation(ADHOC_STA);
}

AdhocAlohaMac::~AdhocAlohaMac()
{
    NS_LOG_FUNCTION(this);
}

void
AdhocAlohaMac::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    // Keep track of the MPDUs the Wi-Fi's frame exchange manager gives up on. The simulation is
    // expected to set retry limit to 1, so that the Wi-Fi layer performs a single transmission attempt 
    // and notifies us as soon as an Ack times out. Retransmissions are then entirely up to Aloha.
    NS_ABORT_UNLESS(
        TraceConnectWithoutContext("DroppedMpdu",
                                   MakeCallback(&AdhocAlohaMac::NotifyMpduDropped, this)));

    if (m_useAck && !m_retxDelay)
    {
        // Created here rather than as an attribute default, so that a pure Aloha run does not
        // consume an RNG stream index for a random variable it never uses.
        Ptr<UniformRandomVariable> delay = CreateObject<UniformRandomVariable>();
        delay->SetAttribute("Min", DoubleValue(0.0));
        delay->SetAttribute("Max", DoubleValue(0.01));
        m_retxDelay = delay;
    }

    WifiMac::DoInitialize();
}

void
AdhocAlohaMac::DoCompleteConfig()
{
    NS_LOG_FUNCTION(this);
}

void
AdhocAlohaMac::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_pendingEvent.Cancel();
    WifiMac::DoDispose();
}

bool
AdhocAlohaMac::CanForwardPacketsTo(Mac48Address to) const
{
    return true;
}

bool
AdhocAlohaMac::IsBusy() const
{
    // The pending frame stays in the Txop queue until it is either acknowledged or given up
    // on by the frame exchange manager. In between two attempts it is held by the pending
    // transmission attempt event instead.
    return GetWifiPhy()->IsStateTx() || m_pendingEvent.IsPending() ||
           GetTxop()->GetWifiMacQueue()->GetNPackets() > 0;
}

void
AdhocAlohaMac::Enqueue(Ptr<WifiMpdu> mpdu, Mac48Address to, Mac48Address from)
{
    NS_LOG_FUNCTION(this << *mpdu << to << from);

    // if the previous frame is not disposed of yet - drop packet (internal collision)
    // for simplicity, new packets while old one is pending are not bufferred
    if (IsBusy())
    {
        NS_LOG_DEBUG("A frame is still pending, drop packet");
        return;
    }

    // WifiMac::Enqueue() has already created the MPDU and set the frame type, so only the
    // address fields are left to fill in here.
    auto& hdr = mpdu->GetHeader();

    // An Ack is only ever requested for individually addressed frames, hence the actual
    // destination address is used when acknowledgements are enabled and the broadcast
    // address (which the receiver does not answer) is used for pure Aloha.
    hdr.SetAddr1(m_useAck ? to : Mac48Address::GetBroadcast());
    hdr.SetAddr2(GetAddress());
    hdr.SetAddr3(GetBssid(SINGLE_LINK_OP_ID));
    hdr.SetDsNotFrom();
    hdr.SetDsNotTo();

    m_retryCount = 0;
    AttemptTransmission(mpdu);
}

void
AdhocAlohaMac::AttemptTransmission(Ptr<WifiMpdu> mpdu)
{
    NS_LOG_FUNCTION(this << *mpdu);

    // Aloha does not sense the channel: every attempt is made straight away.
    Transmit(mpdu);
}

void
AdhocAlohaMac::Transmit(Ptr<WifiMpdu> mpdu)
{
    NS_LOG_FUNCTION(this << *mpdu);

    // Pure Aloha: the frame is handed over to the PHY right away, without carrier sensing
    // and without backoff.
    if (GetWifiPhy()->IsStateTx())
    {
        // the PHY is busy sending Ack, this attempt is lost
        NS_LOG_DEBUG("PHY is transmitting, cannot start a new transmission");
        ScheduleRetransmission(mpdu);
        return;
    }

    // Deliberately bypasses Txop::Queue(), which would request channel access from the
    // ChannelAccessManager
    GetTxop()->GetWifiMacQueue()->Enqueue(mpdu);
    auto width = GetWifiPhy()->GetChannelWidth();

    // Aloha grants itself the channel and drives the frame exchange
    // manager directly.
    GetFrameExchangeManager()->StartTransmission(GetTxop(), width);
}

void
AdhocAlohaMac::NotifyMpduDropped(WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu)
{
    NS_LOG_FUNCTION(this << reason << *mpdu);

    if (!m_useAck || reason != WIFI_MAC_DROP_REACHED_RETRY_LIMIT)
    {
        return;
    }

    // The Ack timed out -> Aloha schedules retransmission.
    ScheduleRetransmission(ConstCast<WifiMpdu>(mpdu));
}

void
AdhocAlohaMac::ScheduleRetransmission(Ptr<WifiMpdu> mpdu)
{
    NS_LOG_FUNCTION(this << *mpdu);

    if (!m_useAck || m_retryCount >= m_maxRetries)
    {
        NS_LOG_DEBUG("No retransmissions enabled or retry limit reached, drop frame");
        return;
    }
    m_retryCount++;

    // Keep the sequence number of the original frame and flag the frame as a retransmission,
    // so that a duplicate caused by a lost Ack is filtered out by the receiver.
    mpdu->GetHeader().SetRetry();

    Time delay = Seconds(m_retxDelay->GetValue());
    NS_LOG_DEBUG("Retransmission " << m_retryCount << " scheduled in " << delay.As(Time::MS));
    m_pendingEvent = Simulator::Schedule(delay, &AdhocAlohaMac::AttemptTransmission, this, mpdu);
}

void
AdhocAlohaMac::SetLinkUpCallback(Callback<void> linkUp)
{
    NS_LOG_FUNCTION(this << &linkUp);
    WifiMac::SetLinkUpCallback(linkUp);

    linkUp();
}

void
AdhocAlohaMac::Receive(Ptr<const WifiMpdu> mpdu, uint8_t linkId)
{
    NS_LOG_FUNCTION(this << *mpdu << +linkId);
    const WifiMacHeader* hdr = &mpdu->GetHeader();
    NS_ASSERT(!hdr->IsCtl());
    Mac48Address from = hdr->GetAddr2();
    Mac48Address to = hdr->GetAddr1();

    // In pure Aloha mode data frames are broadcast, hence every station receives them and
    // only the sink (the first node of the topology) is meant to pass them up. When
    // acknowledgements are enabled frames are individually addressed, so checking the
    // receiver address is enough.
    if (hdr->IsData() &&
        (to == GetAddress() || GetAddress() == Mac48Address("00:00:00:00:00:01")))
    {
        ForwardUp(mpdu->GetPacket()->Copy(), from, to);
        return;
    }

    // Invoke the receive handler of our parent class to deal with any
    // other frames.
    WifiMac::Receive(mpdu, linkId);
}

} // namespace ns3

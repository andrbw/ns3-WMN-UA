/*
 * Modified copy of adhoc-wifi-mac.h
 */

#ifndef ADHOC_ALOHA_MAC_H
#define ADHOC_ALOHA_MAC_H

#include "wifi-mac.h"

#include "ns3/event-id.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

/**
 * @ingroup wifi
 *
 * @brief Wifi MAC high model implementing the Aloha protocol
 */
class AdhocAlohaMac : public WifiMac
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    AdhocAlohaMac();
    ~AdhocAlohaMac() override;

    void SetLinkUpCallback(Callback<void> linkUp) override;
    bool CanForwardPacketsTo(Mac48Address to) const override;

  protected:
    void DoInitialize() override;
    void DoCompleteConfig() override;
    void DoDispose() override;

    /**
     * Hand the given MPDU over to the channel access procedure. Aloha transmits right away,
     * so this simply forwards to Transmit()
     *
     * @param mpdu the MPDU to transmit
     */
    virtual void AttemptTransmission(Ptr<WifiMpdu> mpdu);
    /**
     * Perform one transmission attempt of the given MPDU
     *
     * @param mpdu the MPDU to transmit
     */
    void Transmit(Ptr<WifiMpdu> mpdu);
    /**
     * @return true if a frame is being transmitted, waiting for its Ack or waiting for its
     *         next transmission attempt
     */
    bool IsBusy() const;

    //! pending deferred retransmission
    EventId m_pendingEvent;

  private:
    void Receive(Ptr<const WifiMpdu> mpdu, uint8_t linkId) override;
    void Enqueue(Ptr<WifiMpdu> mpdu, Mac48Address to, Mac48Address from) override;

    /**
     * Schedule a retransmission of the given MPDU after a random time interval, as in
     * classical Aloha, unless the retry limit has been reached (in which case the frame
     * is given up on).
     *
     * @param mpdu the MPDU to retransmit
     */
    void ScheduleRetransmission(Ptr<WifiMpdu> mpdu);
    /**
     * Called when the frame exchange manager of Wi-Fi module gives up on an MPDU. For ALOHA, Wi-Fi
     * retry limit should be set to 1, so this happens as soon as an Ack times out.
     *
     * @param reason the reason why the MPDU was dropped
     * @param mpdu the dropped MPDU
     */
    void NotifyMpduDropped(WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu);

    bool m_useAck;         //!< whether frames are acknowledged and retransmitted
    uint32_t m_maxRetries; //!< maximum number of retransmissions of a frame
    Ptr<RandomVariableStream> m_retxDelay; //!< random delay (seconds) before a retransmission
    uint32_t m_retryCount; //!< retransmissions performed so far for the pending frame
};

} // namespace ns3

#endif /* ADHOC_ALOHA_MAC_H */

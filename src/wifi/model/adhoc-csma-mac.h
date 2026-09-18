/*
 * AdhocAlohaMac extended with carrier sensing to implement p-persistent CSMA.
 */

#ifndef ADHOC_CSMA_MAC_H
#define ADHOC_CSMA_MAC_H

#include "adhoc-aloha-mac.h"

namespace ns3
{

/**
 * @ingroup wifi
 *
 * @brief Wifi MAC high model implementing the p-persistent CSMA protocol
 *
 * Everything but the channel access procedure is inherited
 * unchanged, so the whole difference between Aloha and CSMA is
 * confined to AttemptTransmission().
 */
class AdhocCsmaMac : public AdhocAlohaMac
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    AdhocCsmaMac();
    ~AdhocCsmaMac() override;

  protected:
    void AttemptTransmission(Ptr<WifiMpdu> mpdu) override;

  private:
    /**
     * Hold on to the given MPDU and sense the channel again after the given delay. The event
     * is stored in AdhocAlohaMac::m_pendingEvent, so a frame waiting for the channel makes
     * IsBusy() report true just like a frame waiting for its retransmission does.
     *
     * @param delay the delay after which the channel is sensed again
     * @param mpdu the MPDU whose transmission is deferred
     */
    void Defer(Time delay, Ptr<WifiMpdu> mpdu);

    Time m_slot;                          //!< contention slot duration
    double m_p;                           //!< per-slot transmission probability
    Ptr<UniformRandomVariable> m_uniform; //!< draws the per-slot decision
};

} // namespace ns3

#endif /* ADHOC_CSMA_MAC_H */

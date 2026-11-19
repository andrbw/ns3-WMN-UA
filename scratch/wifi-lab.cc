// Wi-Fi lab for upstream ns-3.48. Copy to scratch/wifi-lab.cc.
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <unordered_set>
#include <vector>

using namespace ns3;

namespace
{
double appStart = 2.0;
double appStop;
std::ofstream association;
std::vector<uint64_t> windowBytes;
struct MacCounters
{
    uint64_t dataPpdus = 0, dataMpdus = 0, retryMpdus = 0, acked = 0;
    uint64_t retryDrops = 0, queueDrops = 0, expired = 0, otherDrops = 0;
    uint64_t ackTx = 0, blockAckTx = 0, aggregates = 0, aggregateMpdus = 0;
    std::unordered_set<uint64_t> offered, admitted, rejectedBeforeQueue;
    double signal = 0, noise = 0;
    uint64_t samples = 0;
};
std::vector<MacCounters> counters;
std::map<std::tuple<uint32_t, std::string, uint64_t>, uint64_t> modes;

bool CohortTime()
{
    return Simulator::Now().GetSeconds() >= appStart;
}
void MacOffered(uint32_t node, Ptr<const Packet> packet)
{
    if (CohortTime()) counters.at(node).offered.insert(packet->GetUid());
}
void MacRejectedBeforeQueue(uint32_t node, Ptr<const Packet> packet)
{
    if (CohortTime()) counters.at(node).rejectedBeforeQueue.insert(packet->GetUid());
}
void QueueAdmitted(uint32_t node, Ptr<const WifiMpdu> mpdu)
{
    if (CohortTime() && mpdu->GetHeader().IsData())
        counters.at(node).admitted.insert(mpdu->GetPacket()->GetUid());
}
void Associated(uint32_t node, Mac48Address bssid)
{
    association << Simulator::Now().GetSeconds() << ',' << node << ",assoc," << bssid << '\n';
}
void Disassociated(uint32_t node, Mac48Address bssid)
{
    association << Simulator::Now().GetSeconds() << ',' << node << ",deassoc," << bssid << '\n';
}
void Received(uint32_t user, Ptr<const Packet> packet, const Address&)
{
    const double now = Simulator::Now().GetSeconds();
    if (now >= appStart && now < appStop)
        windowBytes.at(user) += packet->GetSize();
}
void Acked(uint32_t node, Ptr<const WifiMpdu> mpdu)
{
    if (CohortTime() && mpdu->GetHeader().IsData())
        ++counters.at(node).acked;
}
void Dropped(uint32_t node, WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu)
{
    if (!CohortTime() || !mpdu->GetHeader().IsData())
        return;
    auto& c = counters.at(node);
    switch (reason)
    {
    case WIFI_MAC_DROP_REACHED_RETRY_LIMIT: ++c.retryDrops; break;
    // Count admission failures by packet UID below: the NS-3.48 scheduler can
    // reject a new MPDU without firing the legacy DropBeforeEnqueue trace.
    case WIFI_MAC_DROP_FAILED_ENQUEUE: break;
    case WIFI_MAC_DROP_EXPIRED_LIFETIME: ++c.expired; break;
    default: ++c.otherDrops; break;
    }
}
void PsduTx(uint32_t node, WifiConstPsduMap psdus, WifiTxVector vector, double)
{
    if (!CohortTime())
        return;
    auto& c = counters.at(node);
    for (const auto& [staId, psdu] : psdus)
    {
        uint32_t data = 0;
        for (const auto& mpdu : *psdu)
        {
            const auto& h = mpdu->GetHeader();
            if (h.IsAck()) ++c.ackTx;
            if (h.IsBlockAck()) ++c.blockAckTx;
            if (!h.IsData()) continue;
            ++data;
            ++c.dataMpdus;
            if (h.IsRetry()) ++c.retryMpdus;
        }
        if (!data) continue;
        ++c.dataPpdus;
        if (data > 1)
        {
            ++c.aggregates;
            c.aggregateMpdus += data;
        }
        ++modes[{node, vector.GetMode(staId).GetUniqueName(),
                 vector.GetMode(staId).GetDataRate(vector, staId)}];
    }
}
void SnifferRx(uint32_t node, Ptr<const Packet> packet, uint16_t, WifiTxVector,
               MpduInfo, SignalNoiseDbm signalNoise, uint16_t)
{
    WifiMacHeader h;
    packet->PeekHeader(h);
    if (!CohortTime() || !h.IsData() || h.GetAddr1().IsGroup()) return;
    auto& c = counters.at(node);
    c.signal += signalNoise.signal;
    c.noise += signalNoise.noise;
    ++c.samples;
}
}

int main(int argc, char* argv[])
{
    std::string mode = "infra", traffic = "uplink", prefix = "results/baseline";
    uint32_t nStations = 1, mcs = 7, retryLimit = 7, ampdu = 0, amsdu = 0;
    uint32_t payload = 1200, queuePackets = 200;
    double distance = 5.0, rateMbps = 1.0, duration = 10.0;
    double exponent = 3.0, txPower = 16.0, minimumRssi = -82.0;
    bool pcap = false, activeProbing = false, badSsid = false;
    bool adaptive = false, slowStation = false, shortGi = false;
    CommandLine cmd(__FILE__);
    cmd.AddValue("mode", "infra or adhoc", mode);
    cmd.AddValue("traffic", "uplink to central receiver or peer (two STAs)", traffic);
    cmd.AddValue("nStations", "Number of transmitting STAs; peer requires 2", nStations);
    cmd.AddValue("mcs", "Fixed HT MCS 0..7", mcs);
    cmd.AddValue("distance", "STA radius from central node in metres (>=1)", distance);
    cmd.AddValue("rateMbps", "Offered UDP payload rate per source in Mbit/s", rateMbps);
    cmd.AddValue("payload", "UDP payload bytes, <=1472 (no IP fragmentation)", payload);
    cmd.AddValue("retryLimit", "WifiMac::FrameRetryLimit: total attempts incl. first", retryLimit);
    cmd.AddValue("ampdu", "BE_MaxAmpduSize bytes; 0 disables A-MPDU", ampdu);
    cmd.AddValue("amsdu", "BE_MaxAmsduSize bytes; 0 disables A-MSDU", amsdu);
    cmd.AddValue("duration", "Source measurement window in seconds", duration);
    cmd.AddValue("exponent", "Log-distance path-loss exponent", exponent);
    cmd.AddValue("txPower", "Transmit power in dBm", txPower);
    cmd.AddValue("minimumRssi", "Minimum RSSI for preamble detection in dBm", minimumRssi);
    cmd.AddValue("pcap", "Write radiotap captures on all Wi-Fi devices", pcap);
    cmd.AddValue("activeProbing", "STAs use active probing", activeProbing);
    cmd.AddValue("badSsid", "Use a mismatched STA SSID in infrastructure mode", badSsid);
    cmd.AddValue("adaptive", "Use MinstrelHtWifiManager instead of fixed MCS", adaptive);
    cmd.AddValue("slowStation", "Last STA transmits at MCS 0, others at selected MCS", slowStation);
    cmd.AddValue("shortGi", "Allow 400 ns guard interval instead of 800 ns", shortGi);
    cmd.AddValue("prefix", "Unique output prefix, parent directories auto-created", prefix);
    cmd.Parse(argc, argv);
    NS_ABORT_MSG_IF(mode != "infra" && mode != "adhoc", "Unknown mode");
    NS_ABORT_MSG_IF(traffic != "uplink" && traffic != "peer", "Unknown traffic");
    NS_ABORT_MSG_IF(nStations < 1 || nStations > 100 || mcs > 7 || distance < 1 ||
                    rateMbps <= 0 || duration <= 0 || payload < 64 ||
                    payload > 1472 || retryLimit < 1 || retryLimit > 65535 ||
                    ampdu > 65535 || amsdu > 7935 || queuePackets < 1 || exponent <= 0 ||
                    !std::isfinite(minimumRssi),
                    "Invalid experiment parameter");
    NS_ABORT_MSG_IF(traffic == "peer" && nStations != 2, "Peer mode requires two STAs");
    NS_ABORT_MSG_IF(slowStation && (adaptive || traffic == "peer" || nStations < 2),
                    "slowStation requires fixed-rate uplink with >=2 STAs");
    NS_ABORT_MSG_IF(badSsid && mode != "infra", "badSsid is only meaningful for infra");
    std::filesystem::path parent = std::filesystem::path(prefix).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    appStop = appStart + duration;
    const double simulationStop = appStop + 10.1;
    counters.resize(nStations + 1);
    windowBytes.resize(nStations, 0);
    association.open(prefix + "-assoc.csv");
    association << "time_s,node_id,event,bssid\n";
    Config::SetDefault("ns3::WifiMac::FrameRetryLimit", UintegerValue(retryLimit));
    Config::SetDefault("ns3::WifiMacQueue::MaxSize", QueueSizeValue(QueueSize(std::to_string(queuePackets) + "p")));
    Config::SetDefault("ns3::WifiMacQueue::MaxDelay", TimeValue(MilliSeconds(500)));
    Config::SetDefault("ns3::HtConfiguration::ShortGuardIntervalSupported", BooleanValue(shortGi));
    Config::SetDefault("ns3::HtConfiguration::LdpcSupported", BooleanValue(false));

    NodeContainer central, stations, all;
    central.Create(1);
    stations.Create(nStations);
    all.Add(central);
    all.Add(stations);
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(all);
    central.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0, 0, 0));
    for (uint32_t i = 0; i < nStations; ++i)
    {
        double angle = 2 * 3.141592653589793 * i / nStations;
        stations.Get(i)->GetObject<MobilityModel>()->SetPosition(
            Vector(distance * std::cos(angle), distance * std::sin(angle), 0));
    }
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
        "ReferenceDistance", DoubleValue(1.0), "ReferenceLoss", DoubleValue(46.73),
        "Exponent", DoubleValue(exponent));
    YansWifiPhyHelper phy;
    phy.SetPreambleDetectionModel("ns3::ThresholdPreambleDetectionModel",
        "MinimumRssi", DoubleValue(minimumRssi));
    phy.SetChannel(channel.Create());
    phy.Set("ChannelSettings", StringValue("{36, 20, BAND_5GHZ, 0}"));
    phy.Set("TxPowerStart", DoubleValue(txPower));
    phy.Set("TxPowerEnd", DoubleValue(txPower));
    phy.Set("RxNoiseFigure", DoubleValue(7.0));
    phy.Set("Antennas", UintegerValue(1));
    phy.Set("MaxSupportedTxSpatialStreams", UintegerValue(1));
    phy.Set("MaxSupportedRxSpatialStreams", UintegerValue(1));
    phy.SetErrorRateModel("ns3::TableBasedErrorRateModel");
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211n);
    // Let excess UDP load be dropped at the Wi-Fi MAC, for visible queue-drop accounting.
    wifi.DisableFlowControl();
    if (adaptive)
        wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
    else
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
            "DataMode", StringValue("HtMcs" + std::to_string(mcs)),
            "ControlMode", StringValue("OfdmRate6Mbps"));
    Config::SetDefault("ns3::WifiRemoteStationManager::RtsCtsThreshold", UintegerValue(65535));
    WifiMacHelper mac;
    mac.SetType(mode == "infra" ? "ns3::ApWifiMac" : "ns3::AdhocWifiMac",
                "Ssid", SsidValue(Ssid("wifi-lab")),
                "BE_MaxAmpduSize", UintegerValue(ampdu),
                "BE_MaxAmsduSize", UintegerValue(amsdu));
    NetDeviceContainer devices = wifi.Install(phy, mac, central);
    if (mode == "infra")
        mac.SetType("ns3::StaWifiMac", "Ssid", SsidValue(Ssid(badSsid ? "wrong" : "wifi-lab")),
                    "ActiveProbing", BooleanValue(activeProbing),
                    "BE_MaxAmpduSize", UintegerValue(ampdu),
                    "BE_MaxAmsduSize", UintegerValue(amsdu));
    devices.Add(wifi.Install(phy, mac, stations));
    wifi.AssignStreams(devices, 0);
    if (slowStation)
        DynamicCast<WifiNetDevice>(devices.Get(nStations))->GetRemoteStationManager()->SetAttribute(
            "DataMode", StringValue("HtMcs0"));
    for (uint32_t node = 0; node <= nStations; ++node)
    {
        auto dev = DynamicCast<WifiNetDevice>(devices.Get(node));
        dev->GetMac()->TraceConnectWithoutContext("MacTx", MakeBoundCallback(&MacOffered, node));
        dev->GetMac()->TraceConnectWithoutContext("MacTxDrop", MakeBoundCallback(&MacRejectedBeforeQueue, node));
        dev->GetMac()->GetQosTxop(AC_BE)->GetWifiMacQueue()->TraceConnectWithoutContext(
            "Enqueue", MakeBoundCallback(&QueueAdmitted, node));
        dev->GetMac()->TraceConnectWithoutContext("AckedMpdu", MakeBoundCallback(&Acked, node));
        dev->GetMac()->TraceConnectWithoutContext("DroppedMpdu", MakeBoundCallback(&Dropped, node));
        dev->GetPhy()->TraceConnectWithoutContext("PhyTxPsduBegin", MakeBoundCallback(&PsduTx, node));
        dev->GetPhy()->TraceConnectWithoutContext("MonitorSnifferRx", MakeBoundCallback(&SnifferRx, node));
        if (auto sta = DynamicCast<StaWifiMac>(dev->GetMac()))
        {
            sta->TraceConnectWithoutContext("Assoc", MakeBoundCallback(&Associated, node));
            sta->TraceConnectWithoutContext("DeAssoc", MakeBoundCallback(&Disassociated, node));
        }
    }
    if (pcap)
    {
        phy.SetPcapDataLinkType(WifiPhyHelper::DLT_IEEE802_11_RADIO);
        phy.EnablePcap(prefix, devices, true);
    }
    InternetStackHelper internet;
    internet.Install(all);
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    auto interfaces = address.Assign(devices);
    // Isolate Wi-Fi delivery from startup ARP-resolution buffering and failures.
    NeighborCacheHelper neighbors;
    neighbors.PopulateNeighborCache(interfaces);
    uint32_t nSources = traffic == "peer" ? 1 : nStations;
    uint32_t destination = traffic == "peer" ? 2 : 0;
    auto phase = CreateObject<UniformRandomVariable>();
    phase->SetStream(10000);
    for (uint32_t i = 0; i < nSources; ++i)
    {
        uint16_t port = 5000 + i;
        PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
        auto sinks = sink.Install(all.Get(destination));
        sinks.Start(Seconds(0));
        sinks.Stop(Seconds(simulationStop));
        sinks.Get(0)->TraceConnectWithoutContext("Rx", MakeBoundCallback(&Received, i));
        double interval = 8.0 * payload / (rateMbps * 1e6);
        UdpClientHelper client(interfaces.GetAddress(destination), port);
        client.SetAttribute("MaxPackets", UintegerValue(std::numeric_limits<uint32_t>::max()));
        client.SetAttribute("PacketSize", UintegerValue(payload));
        client.SetAttribute("Interval", TimeValue(Seconds(interval)));
        auto apps = client.Install(stations.Get(i));
        apps.Start(Seconds(appStart + phase->GetValue(0, interval)));
        apps.Stop(Seconds(appStop));
    }
    FlowMonitorHelper flowHelper;
    auto monitor = flowHelper.InstallAll();
    Simulator::Stop(Seconds(simulationStop));
    Simulator::Run();
    monitor->CheckForLostPackets();
    monitor->SerializeToXmlFile(prefix + "-flow.xml", true, true);
    auto classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::ofstream flows(prefix + "-flows.csv");
    flows << "user,source,destination,port,tx_packets,rx_packets,lost_packets,unreceived_packets,"
          << "rx_ip_bytes,window_payload_bytes,goodput_mbps,ip_throughput_mbps,pdr,mean_delay_ms\n";
    flows << std::setprecision(10);
    for (const auto& [id, st] : monitor->GetFlowStats())
    {
        auto tuple = classifier->FindFlow(id);
        if (tuple.protocol != 17 || tuple.destinationAddress != interfaces.GetAddress(destination) ||
            tuple.destinationPort < 5000 || tuple.destinationPort >= 5000 + nSources) continue;
        uint32_t i = tuple.destinationPort - 5000;
        flows << i << ',' << tuple.sourceAddress << ',' << tuple.destinationAddress << ','
              << tuple.destinationPort << ',' << st.txPackets << ',' << st.rxPackets << ','
              << st.lostPackets << ',' << (st.txPackets - st.rxPackets) << ',' << st.rxBytes << ','
              << windowBytes[i] << ',' << windowBytes[i] * 8.0 / duration / 1e6 << ','
              << st.rxBytes * 8.0 / duration / 1e6 << ',';
        if (st.txPackets) flows << double(st.rxPackets) / st.txPackets;
        flows << ',';
        if (st.rxPackets) flows << st.delaySum.GetSeconds() * 1000 / st.rxPackets;
        flows << '\n';
    }
    // Include a zero-goodput user if no packet reached the IP measurement point.
    // analyze.py fills such users from the metadata n_sources field.
    std::ofstream macOut(prefix + "-mac.csv");
    macOut << "node_id,role,data_ppdus,data_mpdu_attempts,retry_mpdu_attempts,acked_data_mpdus,"
           << "retry_limit_drops,queue_full_drops,lifetime_drops,other_drops,ack_tx,block_ack_tx,"
           << "aggregate_ppdus,mpdus_in_aggregates,mean_signal_dbm,mean_noise_dbm\n";
    for (uint32_t i = 0; i <= nStations; ++i)
    {
        auto& c = counters[i];
        // MacTx and Enqueue are synchronous for this lab's associated,
        // single-link BE traffic. Packet UID survives the LLC/MAC header copy.
        // Exclude MAC admission failures (e.g. wrong SSID) from queue-full drops.
        for (const auto uid : c.offered)
            if (!c.admitted.count(uid) && !c.rejectedBeforeQueue.count(uid))
                ++c.queueDrops;
        macOut << i << ',' << (i == 0 ? "central" : "sta") << ',' << c.dataPpdus << ','
               << c.dataMpdus << ',' << c.retryMpdus << ',' << c.acked << ',' << c.retryDrops << ','
               << c.queueDrops << ',' << c.expired << ',' << c.otherDrops << ',' << c.ackTx << ','
               << c.blockAckTx << ',' << c.aggregates << ',' << c.aggregateMpdus << ',';
        if (c.samples) macOut << c.signal / c.samples;
        macOut << ',';
        if (c.samples) macOut << c.noise / c.samples;
        macOut << '\n';
    }
    std::ofstream modeOut(prefix + "-modes.csv");
    modeOut << "node_id,mode,phy_rate_bps,data_ppdus\n";
    for (const auto& [key, count] : modes)
        modeOut << std::get<0>(key) << ',' << std::get<1>(key) << ',' << std::get<2>(key) << ',' << count << '\n';
    std::ofstream meta(prefix + "-config.csv");
    meta << "mode,traffic,n_stations,n_sources,mcs,distance_m,rate_mbps,payload_bytes,retry_limit,"
         << "ampdu_bytes,amsdu_bytes,adaptive,slow_station,short_gi,exponent,tx_power_dbm,"
         << "duration_s,simulation_stop_s,queue_packets,active_probing,bad_ssid,pcap,preamble_min_rssi_dbm,seed,run\n";
    meta << mode << ',' << traffic << ',' << nStations << ',' << nSources << ',' << mcs << ','
         << distance << ',' << rateMbps << ',' << payload << ',' << retryLimit << ',' << ampdu << ','
         << amsdu << ',' << adaptive << ',' << slowStation << ',' << shortGi << ',' << exponent << ','
         << txPower << ',' << duration << ',' << simulationStop << ',' << queuePackets << ','
         << activeProbing << ',' << badSsid << ',' << pcap << ',' << minimumRssi << ','
         << RngSeedManager::GetSeed() << ',' << RngSeedManager::GetRun() << '\n';
    association.close();
    Simulator::Destroy();
}

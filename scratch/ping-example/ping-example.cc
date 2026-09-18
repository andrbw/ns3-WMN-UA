/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

// Example to demonstrate the working of the Ping Application
// Network topology: three Ad-Hoc Wi-Fi nodes n0, n1, and n2.
//
// By default, this program will send 5 pings from node n0 to node n1. The output will look like this:
//
// The Ping application in this example starts at simulation time 1 and will
// stop either at 50s or once 'Count' pings have been responded
// to, whichever comes first.
//
// The example program will also produce pcap traces (one for each
// NetDevice in the scenario)
//

#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-apps-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/multi-model-spectrum-channel.h"
#include "ns3/propagation-loss-model.h"
#include "ns3/neighbor-cache-helper.h"

// WiFiViz lives in an optional contrib module (contrib/wifiviz)
// alongside ns-3. Pull it in only when its header is available.
// The lab builds even when the module is missing.
#if __has_include("ns3/wifiviz.h")
#include "ns3/wifiviz.h"
#define HAS_WIFIVIZ 1
#endif


#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PingExample");

int
main(int argc, char* argv[])
{
    Time interPacketInterval{Seconds(1.0)};
    uint32_t size{56};
    uint32_t count{5};
    uint32_t srcIdx{0};
    uint32_t dstIdx{1};

    bool enableViz = false;
    bool launchViewer = false;

    GlobalValue::Bind("ChecksumEnabled", BooleanValue(true));

    CommandLine cmd;
    cmd.AddValue("interval", "The time to wait between two packets", interPacketInterval);
    cmd.AddValue("size", "Data bytes to be sent, per-packet", size);
    cmd.AddValue("count", "Number of packets to be sent", count);
    cmd.AddValue("srcIdx",
                 "End node index that is ping source, e.g., if 0 the src IP is \"10.1.1.1\"",
                 srcIdx);
    cmd.AddValue("dstIdx",
                 "End node index that is ping destination, e.g., if 1 the src IP is \"10.1.1.2\"",
                 dstIdx);

    cmd.AddValue ("enableViz", "collect WiFiViz records for the timeline viewer "
                             "(requires the wifiviz module in contrib)", enableViz);
    cmd.AddValue ("launchViewer", "start the WiFiViz viewer together with the run "
                                "(requires the wifiviz module in contrib)", launchViewer);

    cmd.Parse(argc, argv);

    NodeContainer endNodes;
    endNodes.Create(3);

    //configure Wi-Fi parameters
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);
    SpectrumWifiPhyHelper wifiPhy;
    Ptr<MultiModelSpectrumChannel> channel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<FriisPropagationLossModel> lossModel = CreateObject<FriisPropagationLossModel> ();
    channel->AddPropagationLossModel (lossModel);
    wifiPhy.SetChannel(channel);

    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac", "QosSupported", BooleanValue(false));
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate6Mbps"));

    //insert Wi-Fi NICs into nodes
    NetDeviceContainer endNodeDevices = wifi.Install(wifiPhy, wifiMac, endNodes);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
    positionAlloc->Add (Vector (0.0, 0.0, 0.0));
    mobility.SetPositionAllocator (positionAlloc);
    mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
    mobility.Install (endNodes);

    InternetStackHelper internet;
    internet.Install(endNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(endNodeDevices);

    NeighborCacheHelper neighborCache;
    neighborCache.PopulateNeighborCache ();

    //Create Ping application and installing on node
    PingHelper pingHelper(interfaces.GetAddress (dstIdx), interfaces.GetAddress (srcIdx));
    pingHelper.SetAttribute("Interval", TimeValue(interPacketInterval));
    pingHelper.SetAttribute("Size", UintegerValue(size));
    pingHelper.SetAttribute("Count", UintegerValue(count));
    ApplicationContainer apps = pingHelper.Install(endNodes.Get(srcIdx));
    apps.Start(Seconds(1));
    apps.Stop(Seconds(50));

    wifiPhy.EnablePcapAll("ping-example");

    //Record on every device.
#ifdef HAS_WIFIVIZ
  Ptr<SniffUtils> viz =
    WiFiVizHelper::MaybeEnableVisualizer (enableViz, endNodeDevices, 60, launchViewer);
#else
  if (enableViz || launchViewer)
    {
      std::cerr << "WiFiViz is not available: clone the wifiviz module into contrib/ and "
                   "re-run ./ns3 configure to use --enableViz/--launchViewer" << std::endl;
    }
#endif

    Simulator::Stop(Seconds(60.0));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}

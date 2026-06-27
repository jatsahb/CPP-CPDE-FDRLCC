/**
 * fdrlcc-topology-management.cpp
 * 
 * Topology management functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-topology-management.hpp"
#include "fdrlcc-types.hpp"
#include "src_cpp/apps/fdrl-consumer.hpp"
#include "src_cpp/apps/fdrl-producer.hpp"
#include "ns3/node.h"
#include "ns3/names.h"
#include "ns3/node-container.h"
#include "ns3/application-container.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"
#include "ns3/ndnSIM/helper/ndn-app-helper.hpp"
#include "ns3/ndnSIM/helper/ndn-global-routing-helper.hpp"
#include "ns3/log.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>
#include <vector>
#include <map>
#include <algorithm>
#include <functional>
#include <stdexcept>

NS_LOG_COMPONENT_DEFINE("FdrlccTopologyManagement");

namespace ns3 {
namespace ndn {
namespace fdrl {

TopologyInfo ParseTopologyFromLoadedNodes()
{
  TopologyInfo info;
  std::set<std::string> uniqueRegions;
  
  // Get all nodes from the loaded topology
  NodeContainer allNodes = NodeContainer::GetGlobal();
  
  for (uint32_t i = 0; i < allNodes.GetN(); i++) {
    Ptr<Node> node = allNodes.Get(i);
    std::string nodeName = Names::FindName(node);
    
    // Parse node name: format is {region}-{identifier}
    // Examples: A-C1, A-P1, A-ER1, N1-C1, N1-ERC1, N1-SR1
    size_t dashPos = nodeName.find('-');
    if (dashPos == std::string::npos) {
      continue;  // Skip nodes without '-' separator (shouldn't happen in our topology)
    }
    
    std::string region = nodeName.substr(0, dashPos);  // "A" or "N1"
    std::string identifier = nodeName.substr(dashPos + 1);  // "C1", "P1", "ER1", "ERC1", etc.
    
    // Extract region (track unique regions)
    uniqueRegions.insert(region);
    
    // Classify node type based on identifier pattern
    if (identifier.length() > 0 && identifier[0] == 'C') {
      // Consumer: C1, C2, C10, etc.
      info.consumers[region].push_back(nodeName);
    } else if (identifier.length() > 0 && identifier[0] == 'P') {
      // Producer: P1, P2, etc.
      info.producers[region].push_back(nodeName);
    } else {
      // Router: ER1, ERC1, ERP1, SR1, CR1, IR1, etc. (anything else)
      info.routers[region].push_back(nodeName);
    }
  }
  
  // Convert set to sorted vector for consistent ordering
  info.regions.assign(uniqueRegions.begin(), uniqueRegions.end());
  std::sort(info.regions.begin(), info.regions.end());
  
  return info;
}
std::string
GenerateTimestampedDirectory(uint32_t scenario, double simTime, uint32_t runNumber, const std::string& algorithm)
{
  // PhD DEFENSE FRAMEWORK: Use scenario_X/run_Y/ directory structure
  // Format: results/scenario_X/run_Y/
  std::ostringstream oss;
  oss << "src/ndnSIM/fdrlcc/results/simulation_results/scenario_" << scenario << "/run_" << runNumber;
  
  return oss.str();
}
void
InstallProducers(ns3::ndn::GlobalRoutingHelper& routingHelper, const TopologyInfo& topoInfo)
{
  // PHASE-1: Dynamic Producer Installation
  // =======================================
  // Producers serve dynamic content (not static payload reuse)
  // Some producers start later than consumers to create demand imbalance
  // Each producer serves a unique prefix to enable path diversity
  
  // Producer start times (staggered to create demand imbalance)
  // First producer in each region starts immediately, second starts later
  double producerStartTimes[] = {1.0, 15.0};  // First: 1s, Second: 15s
  
  // Install producers for all auto-detected regions
  for (const auto& region : topoInfo.regions) {
    std::string prefix = "/region" + region;
    
    // Get all producer nodes for this region from auto-detected topology
    auto prodIt = topoInfo.producers.find(region);
    if (prodIt == topoInfo.producers.end()) {
      NS_LOG_WARN("No producers found for region: " << region);
      continue;
    }
    
    const auto& producerNames = prodIt->second;
    for (size_t prodIdx = 0; prodIdx < producerNames.size(); prodIdx++) {
      const auto& producerName = producerNames[prodIdx];
      Ptr<Node> producer = Names::Find<Node>(producerName);
      
      if (producer) {
        // PHASE-1: Producers serve the BASE prefix (consumers request this)
        // Multiple producers serving the same prefix enables load distribution
        // Path diversity comes from routing, not from different prefixes
        ns3::ndn::AppHelper producerHelper("ns3::ndn::fdrl::FdrlProducer");
        producerHelper.SetPrefix(prefix);  // Use base prefix, not unique prefix
        
        // PHASE-1: Dynamic content - vary payload size to simulate different content types
        // This prevents static payload reuse (cache behavior varies)
        // Each producer has different payload size for content diversity
        uint32_t payloadSize = 1024 + (prodIdx * 256);  // 1024, 1280, 1536, etc.
        producerHelper.SetAttribute("PayloadSize", UintegerValue(payloadSize));
        
        ApplicationContainer apps = producerHelper.Install(producer);
        
        // PHASE-1: Time-staggered producer activation
        // First producer starts at 1s, second at 15s (creates demand imbalance)
        double startTime = (prodIdx < 2) ? producerStartTimes[prodIdx] : 1.0;
        apps.Start(Seconds(startTime));
        
        // Add origin to routing (enables path discovery)
        // All producers serve the same prefix, routing distributes Interests
        routingHelper.AddOrigins(prefix, producer);
        
        NS_LOG_INFO("PHASE-1: Installed producer on " << producerName 
                    << " with prefix " << prefix
                    << " | Payload: " << payloadSize << " bytes"
                    << " | Start: " << startTime << "s");
      } else {
        NS_LOG_WARN("Producer node not found: " << producerName);
      }
    }
  }
}
std::vector<Ptr<FdrlConsumer>>
InstallConsumers(const TopologyInfo& topoInfo, CCAlgorithm algorithm, double trafficMultiplier)
{
  std::vector<Ptr<FdrlConsumer>> consumers;
  // PHASE-1: Store ApplicationContainers for stop/restart scheduling
  static std::vector<ApplicationContainer> g_consumerApps;  // Persistent storage for apps
  
  // PHASE-1: Dynamic Consumer Groups with Heterogeneous Rates
  // =========================================================
  // Consumer Rate Groups:
  //   - Low:    10-20 Interests/sec (background traffic)
  //   - Medium: 50-80 Interests/sec (normal traffic)
  //   - High:   100+ Interests/sec (bursty/on-off traffic)
  //
  // Time-Staggered Activation:
  //   - Group 1: Start at 5s  (early adopters)
  //   - Group 2: Start at 20s (main traffic)
  //   - Group 3: Start at 40s (late joiners)
  //   - Group 4: Start at 70s (delayed traffic)
  //
  // Stop/Restart Pattern:
  //   - Some consumers stop at 100s, restart at 150s (simulate user behavior)
  
  // Define consumer rate groups (interests per second)
  struct ConsumerGroup {
    double minRate;
    double maxRate;
    double startTime;
    double stopTime;    // -1 = never stop
    double restartTime; // -1 = never restart
  };
  
  ConsumerGroup groups[] = {
    {10.0,  20.0,  5.0,  -1.0, -1.0},  // Low: 10-20/s, start 5s, no stop
    {50.0,  80.0,  20.0, -1.0, -1.0},  // Medium: 50-80/s, start 20s, no stop
    {100.0, 150.0, 40.0, 100.0, 150.0}, // High: 100-150/s, start 40s, stop 100s, restart 150s
    {80.0,  120.0, 70.0, -1.0, -1.0}   // Medium-High: 80-120/s, start 70s, no stop
  };
  
  // Base frequencies per region (for backward compatibility)
  // PHASE-1: These are now used as region-level baselines, individual consumers vary
  std::map<std::string, double> baseFrequencies;
  for (size_t i = 0; i < topoInfo.regions.size(); i++) {
    const std::string& region = topoInfo.regions[i];
    
    // Reduced base frequencies - individual consumers will have varied rates
    if (region.length() == 1 && region[0] >= 'A' && region[0] <= 'Z') {
      if (region == "A") baseFrequencies[region] = 50.0;   // Baseline for region A
      else if (region == "B") baseFrequencies[region] = 45.0;
      else if (region == "C") baseFrequencies[region] = 40.0;
      else if (region == "D") baseFrequencies[region] = 55.0;
      else if (region == "E") baseFrequencies[region] = 35.0;
      else baseFrequencies[region] = 50.0;
    } else {
      double freqLevels[] = {50.0, 45.0, 40.0, 55.0, 35.0, 48.0, 42.0};
      baseFrequencies[region] = freqLevels[i % 7];
    }
  }

  // Install consumers for all auto-detected regions
  for (const auto& region : topoInfo.regions) {
    std::string prefix = "/region" + region;
    
    // Get all consumer nodes for this region from auto-detected topology
    auto consIt = topoInfo.consumers.find(region);
    if (consIt == topoInfo.consumers.end()) {
      NS_LOG_WARN("No consumers found for region: " << region);
      continue;
    }
    
    const auto& consumerNames = consIt->second;
    // baseFreq variable removed - not used in PHASE-1 (individual consumers have varied rates)
    
    // PHASE-1: Install consumers with dynamic behavior
    // Distribute consumers across rate groups and start times
    for (size_t idx = 0; idx < consumerNames.size(); idx++) {
      const auto& consumerName = consumerNames[idx];
      Ptr<Node> consumerNode = Names::Find<Node>(consumerName);
      
      if (consumerNode) {
        // All consumers request from their own region
        std::string targetPrefix = prefix;
        
        // PHASE-1: Assign consumer to a rate group based on index
        // Distribute consumers across 4 groups for diversity
        int groupIdx = idx % 4;
        ConsumerGroup& group = groups[groupIdx];
        
        // PHASE-1: Calculate actual rate within group range
        // Use consumer index to create variation within group
        double rateRange = group.maxRate - group.minRate;
        double rateOffset = (idx % 5) * (rateRange / 4.0);  // Vary within group
        double groupRate = group.minRate + rateOffset;
        
        // Apply traffic multiplier (user-specified scaling)
        double actualFreq = groupRate * trafficMultiplier;
        
        ns3::ndn::AppHelper consumerHelper("ns3::ndn::fdrl::FdrlConsumer");
        consumerHelper.SetPrefix(targetPrefix);
        consumerHelper.SetAttribute("Frequency", DoubleValue(actualFreq));
        
        ApplicationContainer apps = consumerHelper.Install(consumerNode);
        
        // PHASE-1: Store apps container for stop/restart scheduling
        size_t appIdx = g_consumerApps.size();
        g_consumerApps.push_back(apps);
        
        // PHASE-1: Time-staggered activation
        apps.Start(Seconds(group.startTime));
        
        // Get FdrlConsumer pointer
        Ptr<Application> app = apps.Get(0);
        Ptr<FdrlConsumer> fdrlConsumer = DynamicCast<FdrlConsumer>(app);
        
        if (fdrlConsumer) {
          // Set the actual frequency (may differ from initial attribute)
          fdrlConsumer->SetBaseFrequency(actualFreq);
          
          // PHASE-1: Dynamic content pool (not static payload reuse)
          // Each consumer has limited content pool to create cache hits/misses
          fdrlConsumer->SetContentPoolSize(50 + (idx % 50));  // 50-100 items per consumer
          
          // CONDITIONAL: Set algorithm based on selection
          if (algorithm == CCAlgorithm::FDRLCC) {
            fdrlConsumer->EnableFDRLCC(true);  // AI CC mode
          } else {
            fdrlConsumer->EnableFDRLCC(false);  // Classical CC mode
            
            // Set specific classical algorithm
            if (algorithm == CCAlgorithm::AIMD) {
              fdrlConsumer->SetClassicalCCAlgorithm(ClassicalCCAlgorithm::AIMD);
              fdrlConsumer->ConfigureClassicalCC(0.1, 0.5, 0.1, 2.0, 10);
            } else if (algorithm == CCAlgorithm::BIC) {
              fdrlConsumer->SetClassicalCCAlgorithm(ClassicalCCAlgorithm::BIC);
              fdrlConsumer->ConfigureClassicalCC(0.1, 0.5, 0.1, 2.0, 10);
            } else if (algorithm == CCAlgorithm::CUBIC) {
              fdrlConsumer->SetClassicalCCAlgorithm(ClassicalCCAlgorithm::CUBIC);
              fdrlConsumer->ConfigureClassicalCC(0.1, 0.5, 0.1, 2.0, 10);
            }
          }
          
          // ENHANCEMENT: Assign unique random streams to ensure different seeds produce different results
          // Calculate unique stream ID based on seed, region, and consumer index
          // Handle both letter regions (A, B, C) and numeric regions (N1, N2, etc.)
          int regionOffset = 0;
          if (region.length() == 1 && region[0] >= 'A' && region[0] <= 'Z') {
            // Letter region: A=0, B=1000, C=2000, etc.
            regionOffset = (region[0] - 'A') * 1000;
          } else {
            // Numeric region: use hash of region name
            std::hash<std::string> hasher;
            regionOffset = static_cast<int>(hasher(region) % 10000) * 100;
          }
          
          // Extract consumer number from name (e.g., "C1" -> 1, "C10" -> 10)
          size_t dashPos = consumerName.find('-');
          if (dashPos != std::string::npos) {
            std::string identifier = consumerName.substr(dashPos + 1);
            if (identifier.length() > 1 && identifier[0] == 'C') {
              try {
                int suffixNum = std::stoi(identifier.substr(1));
                // Incorporate seed to ensure different seeds produce different streams
                int64_t streamId = (g_randomSeed % 1000) * 10000 + regionOffset + suffixNum;
                fdrlConsumer->AssignRandomStreams(streamId);
              } catch (...) {
                // Fallback: use index
                int64_t streamId = (g_randomSeed % 1000) * 10000 + regionOffset + idx;
                fdrlConsumer->AssignRandomStreams(streamId);
              }
            } else {
              int64_t streamId = (g_randomSeed % 1000) * 10000 + regionOffset + idx;
              fdrlConsumer->AssignRandomStreams(streamId);
            }
          } else {
            int64_t streamId = (g_randomSeed % 1000) * 10000 + regionOffset + idx;
            fdrlConsumer->AssignRandomStreams(streamId);
          }
          
          // PHASE-1: Enable traffic variation for randomness (bursty behavior)
          // Higher variation for high-rate consumers (more bursty)
          double variationFactor = (groupIdx >= 2) ? 0.40 : 0.25;  // High-rate: 40%, others: 25%
          fdrlConsumer->SetTrafficVariation(variationFactor);
          
          // PHASE-1: Schedule stop/restart for consumers in group 2 (high-rate, bursty)
          // Use ApplicationContainer methods (Stop/Start) via persistent storage
          if (group.stopTime > 0 && group.restartTime > 0) {
            // Schedule stop using ApplicationContainer (capture app index)
            Simulator::Schedule(Seconds(group.stopTime), [appIdx]() {
              NS_LOG_INFO("Stopping consumer (simulating user pause)");
              if (appIdx < g_consumerApps.size()) {
                g_consumerApps[appIdx].Stop(Seconds(0));
              }
            });
            
            // Schedule restart using ApplicationContainer (capture app index)
            Simulator::Schedule(Seconds(group.restartTime), [appIdx]() {
              NS_LOG_INFO("Restarting consumer (simulating user resume)");
              if (appIdx < g_consumerApps.size()) {
                g_consumerApps[appIdx].Start(Seconds(0));
              }
            });
          }
          
          consumers.push_back(fdrlConsumer);
          
          // Track which region this consumer belongs to
          g_consumerRegions[fdrlConsumer] = region;
          
          NS_LOG_INFO("PHASE-1: Installed consumer on " << consumerName 
                      << " with prefix " << targetPrefix
                      << " (region " << region << ")"
                      << " | Rate: " << std::fixed << std::setprecision(1) << actualFreq << " Hz"
                      << " | Group: " << (groupIdx + 1)
                      << " | Start: " << group.startTime << "s"
                      << (group.stopTime > 0 ? " | Stop: " + std::to_string((int)group.stopTime) + "s, Restart: " + std::to_string((int)group.restartTime) + "s" : ""));
        } else {
          NS_LOG_WARN("Failed to cast to FdrlConsumer on " << consumerName);
        }
      } else {
        NS_LOG_WARN("Consumer node not found: " << consumerName);
      }
    }
  }
  
  return consumers;
}


} // namespace fdrl
} // namespace ndn
} // namespace ns3


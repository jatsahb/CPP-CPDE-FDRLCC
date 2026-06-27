/**
 * Python-callable simulation for real-time DRL training
 * 
 * This allows Python to:
 * 1. Start/stop simulation
 * 2. Step simulation by time increments
 * 3. Get state and apply actions between steps
 */

#include "py_fdrlcc_simulation.hpp"

#include "../controller/fdrl-controller.hpp"
// REFACTORED: MetricStore deleted - using MetricEngine instead
#include "src_cpp/metrics/metric-engine.hpp"
#include "../apps/fdrl-consumer.hpp"
#include "../apps/fdrl-producer.hpp"
#include "../apps/fdrl-router.hpp"
#include "../helpers/fdrl-controller-helper.hpp"
#include "../helpers/fdrl-results-logger.hpp"

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/random-variable-stream.h"

#include "ns3/log.h"
#include <memory>
#include <atomic>

NS_LOG_COMPONENT_DEFINE("ndn.PyFdrlccSimulation");

namespace ns3 {
namespace ndn {
namespace fdrl {

// Global simulation state for Python access
static Ptr<Controller> g_pythonController = nullptr;
static std::vector<Ptr<FdrlConsumer>> g_pythonConsumers;
static bool g_simulationInitialized = false;
static std::atomic<bool> g_simulationRunning{false};
static double g_simEndTime = 0.0;

/**
 * Setup the simulation topology
 */
static void SetupTopology(double consumerFreq, uint32_t seed)
{
    // Create nodes
    NodeContainer consumers;
    consumers.Create(4);
    
    NodeContainer routers;
    routers.Create(2);
    
    NodeContainer producers;
    producers.Create(1);
    
    // Setup links
    PointToPointHelper consumerLink;
    consumerLink.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    consumerLink.SetChannelAttribute("Delay", StringValue("5ms"));
    consumerLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("50p"));
    
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("10ms"));
    bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("20p"));
    
    PointToPointHelper producerLink;
    producerLink.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    producerLink.SetChannelAttribute("Delay", StringValue("2ms"));
    
    // Connect topology
    for (uint32_t i = 0; i < consumers.GetN(); ++i) {
        consumerLink.Install(consumers.Get(i), routers.Get(0));
    }
    bottleneckLink.Install(routers.Get(0), routers.Get(1));
    producerLink.Install(routers.Get(1), producers.Get(0));
    
    // Install NDN stack
    ns3::ndn::StackHelper ndnHelper;
    ndnHelper.SetDefaultRoutes(true);
    ndnHelper.setCsSize(100);
    ndnHelper.InstallAll();
    
    ns3::ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/best-route");
    
    // Install controller on router
    FdrlControllerHelper controllerHelper;
    controllerHelper.SetUpdateInterval(Seconds(0.2));
    controllerHelper.SetSamplingInterval(Seconds(0.1));
    g_pythonController = controllerHelper.Install(routers.Get(0));
    
    // Create consumers
    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    rng->SetAttribute("Stream", IntegerValue(seed));
    
    for (uint32_t i = 0; i < consumers.GetN(); ++i) {
        Ptr<FdrlConsumer> consumer = CreateObject<FdrlConsumer>();
        consumer->SetAttribute("Prefix", StringValue("/prefix"));
        
        double freq = consumerFreq * (0.8 + rng->GetValue() * 0.4);
        consumer->SetBaseFrequency(freq);
        consumer->SetContentPoolSize(50);
        consumer->SetTrafficVariation(0.2);
        
        consumers.Get(i)->AddApplication(consumer);
        consumer->SetStartTime(Seconds(1.0 + i * 0.2));
        
        g_pythonConsumers.push_back(consumer);
    }
    
    // Create producer
    Ptr<FdrlProducer> producer = CreateObject<FdrlProducer>();
    producer->SetAttribute("Prefix", StringValue("/prefix"));
    producer->SetAttribute("PayloadSize", UintegerValue(1024));
    producers.Get(0)->AddApplication(producer);
    producer->SetStartTime(Seconds(0.5));
    
    // Create router app
    Ptr<FdrlRouter> router = CreateObject<FdrlRouter>();
    routers.Get(0)->AddApplication(router);
    
    // Attach to controller
    controllerHelper.AttachApplications(g_pythonController, g_pythonConsumers[0], 
                                        router, producer);
    
    g_simulationInitialized = true;
    NS_LOG_INFO("Topology setup complete. Controller installed on router 0.");
}

/**
 * Implementation of RunFdrlccSimulation
 * Runs the full simulation with periodic Python callbacks
 */
int RunFdrlccSimulationImpl(double simTime, 
                            double updateInterval,
                            double samplingInterval,
                            double consumerFreq,
                            uint32_t seed)
{
    NS_LOG_INFO("Starting FDRLCC simulation...");
    
    // Clear any previous simulation
    Simulator::Destroy();
    g_pythonConsumers.clear();
    g_pythonController = nullptr;
    g_simulationInitialized = false;
    
    // Set random seed
    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(seed + 1);
    
    // Setup topology
    SetupTopology(consumerFreq, seed);
    
    if (!g_simulationInitialized || !g_pythonController) {
        NS_LOG_ERROR("Failed to initialize simulation");
        return -1;
    }
    
    g_simEndTime = simTime;
    g_simulationRunning = true;
    
    // Run simulation
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    
    g_simulationRunning = false;
    
    // Cleanup
    Simulator::Destroy();
    
    NS_LOG_INFO("Simulation completed.");
    return 0;
}

// ============================================================================
// Python-accessible functions for step-by-step simulation control
// ============================================================================

bool InitializeSimulation(double simTime, double consumerFreq, uint32_t seed)
{
    // Clear any previous simulation
    Simulator::Destroy();
    g_pythonConsumers.clear();
    g_pythonController = nullptr;
    g_simulationInitialized = false;
    
    // Set random seed
    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(seed + 1);
    
    // Setup topology
    SetupTopology(consumerFreq, seed);
    
    g_simEndTime = simTime;
    
    return g_simulationInitialized && g_pythonController != nullptr;
}

bool StepSimulation(double stepSize)
{
    if (!g_simulationInitialized) {
        NS_LOG_ERROR("Simulation not initialized");
        return false;
    }
    
    Time currentTime = Simulator::Now();
    Time targetTime = currentTime + Seconds(stepSize);
    
    if (targetTime.GetSeconds() > g_simEndTime) {
        NS_LOG_INFO("Simulation reached end time");
        return false;
    }
    
    g_simulationRunning = true;
    
    // Schedule a stop at target time
    Simulator::Stop(targetTime - currentTime);
    Simulator::Run();
    
    // Check if simulation ended
    if (Simulator::Now().GetSeconds() >= g_simEndTime) {
        g_simulationRunning = false;
        return false;
    }
    
    return true;
}

std::vector<double> GetCurrentState()
{
    if (!g_pythonController) {
        return std::vector<double>(9, 0.0);
    }
    
    // REFACTORED: MetricStore deleted - MetricEngine::GetLatestSnapshot() requires region parameter
    auto engine = g_pythonController->GetMetricStore();  // Returns Ptr<MetricEngine> now
    auto features = g_pythonController->GetStateFeatures();
    
    if (!engine || !features) {
        return std::vector<double>(9, 0.0);
    }
    
    // TODO: Get actual region from controller/context - for now use default
    MetricSnapshot snapshot = engine->GetLatestSnapshot("default");  // MetricEngine requires region
    return features->ExtractFeatures(snapshot);
}

void ApplyAction(double rateFactor, double queueFactor)
{
    if (!g_pythonController) {
        NS_LOG_WARN("No controller available");
        return;
    }
    
    // Apply to controller
    ActionVector action;
    action.interestRateFactor = std::max(0.1, std::min(2.0, rateFactor));
    action.queueThresholdFactor = std::max(0.1, std::min(2.0, queueFactor));
    action.forwardingWeightDelta = 0.0;
    action.cacheAdjustment = 0.0;
    
    g_pythonController->ApplyAction(action);
    
    // Also apply rate factor to consumers
    for (auto& consumer : g_pythonConsumers) {
        if (consumer) {
            consumer->ApplyRateFactor(rateFactor);
        }
    }
}

double GetCurrentReward()
{
    if (!g_pythonController) {
        return 0.0;
    }
    return g_pythonController->GetCurrentReward();
}

MetricSnapshot GetMetricSnapshot()
{
    if (!g_pythonController) {
        return MetricSnapshot{};
    }
    
    // REFACTORED: MetricStore deleted - MetricEngine::GetLatestSnapshot() requires region parameter
    auto engine = g_pythonController->GetMetricStore();  // Returns Ptr<MetricEngine> now
    if (!engine) {
        return MetricSnapshot{};
    }
    
    // TODO: Get actual region from controller/context - for now use default
    return engine->GetLatestSnapshot("default");  // MetricEngine requires region
}

bool IsSimulationRunning()
{
    return g_simulationRunning.load();
}

double GetSimulationTime()
{
    return Simulator::Now().GetSeconds();
}

void DestroySimulation()
{
    g_simulationRunning = false;
    g_pythonConsumers.clear();
    g_pythonController = nullptr;
    g_simulationInitialized = false;
    Simulator::Destroy();
}

Ptr<Controller> GetPythonController()
{
    return g_pythonController;
}

int
RunFdrlccSimulation(double simTime, 
                    double updateInterval,
                    double samplingInterval,
                    double consumerFreq,
                    uint32_t seed)
{
    NS_LOG_UNCOND("Running FDRLCC simulation from Python...");
    NS_LOG_UNCOND("  Simulation time: " << simTime << " seconds");
    NS_LOG_UNCOND("  Update interval: " << updateInterval << " seconds");
    NS_LOG_UNCOND("  Sampling interval: " << samplingInterval << " seconds");
    NS_LOG_UNCOND("  Consumer frequency: " << consumerFreq << " Hz");
    NS_LOG_UNCOND("  Random seed: " << seed);
    
    return RunFdrlccSimulationImpl(simTime, updateInterval, samplingInterval, consumerFreq, seed);
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

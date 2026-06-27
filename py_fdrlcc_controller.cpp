#include "py_fdrlcc_controller.hpp"
#include "py_fdrlcc_interface.hpp"
#include "py_fdrlcc_simulation.hpp"

#include "../controller/fdrl-controller.hpp"
// REFACTORED: MetricStore deleted - using MetricEngine instead
#include "src_cpp/metrics/metric-engine.hpp"
#include "../controller/fdrl-state-features.hpp"
#include "../controller/fdrl-action-policy.hpp"
#include "../controller/fdrl-federation-coordinator.hpp"

#include "ns3/node.h"
#include "ns3/node-list.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"

#include <pybind11/pybind11.h>
#include <map>
#include <mutex>
#include <exception>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/functional.h>

namespace py = pybind11;
using namespace ns3;
using namespace ns3::ndn;
using namespace ns3::ndn::fdrl;

NS_LOG_COMPONENT_DEFINE("ndn.PyFdrlccController");

namespace {

// Global registry for controllers (allows access across process boundaries via shared memory concept)
// In practice, this works within the same process when Python is embedded or simulation runs in Python
std::map<uint32_t, Ptr<Controller>> g_controllerRegistry;
std::mutex g_registryMutex;

void
RegisterController(uint32_t nodeId, Ptr<Controller> controller)
{
  std::lock_guard<std::mutex> lock(g_registryMutex);
  g_controllerRegistry[nodeId] = controller;
  NS_LOG_DEBUG("Registered controller for node " << nodeId);
}

Ptr<Controller>
GetControllerFromNode(uint32_t nodeId)
{
  // First try global registry (for embedded Python or same-process scenarios)
  {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    auto it = g_controllerRegistry.find(nodeId);
    if (it != g_controllerRegistry.end() && it->second) {
      NS_LOG_DEBUG("Controller found in registry for node " << nodeId);
      return it->second;
    }
  }
  
  // Fallback: Try to get from NodeList (for same-process scenarios)
  // This will only work if simulation and Python are in the same process
  // Note: NodeList::GetNode() will fail if called from a different process
  try {
    if (NodeList::GetNNodes() > 0) {
      Ptr<Node> node = NodeList::GetNode(nodeId);
      if (node) {
        Ptr<Controller> controller = node->GetObject<Controller>();
        if (controller) {
          // Cache it in registry for future access
          RegisterController(nodeId, controller);
          NS_LOG_DEBUG("Controller found on node " << nodeId << " via NodeList");
          return controller;
        }
      }
    }
  } catch (const std::exception& e) {
    NS_LOG_DEBUG("Exception accessing NodeList: " << e.what());
  }
  
  {
    std::lock_guard<std::mutex> lock(g_registryMutex);
    NS_LOG_DEBUG("Controller not found for node " << nodeId 
                 << " (registry size: " << g_controllerRegistry.size() 
                 << ", NodeList size: " << NodeList::GetNNodes() << ")");
  }
  return nullptr;
}

} // namespace

PYBIND11_MODULE(py_fdrlcc_controller, m)
{
  m.doc() = "FDRLCC Python bindings for controller interaction";

  // ============================================================================
  // MetricSnapshot binding
  // ============================================================================
  // REFACTORED: MetricSnapshot binding - many fields removed, simplified to 5D state metrics
  py::class_<MetricSnapshot>(m, "MetricSnapshot")
    .def(py::init<>())
    // REFACTORED: Only bind fields that exist in new MetricSnapshot
    .def_readwrite("total_interests_sent", &MetricSnapshot::totalInterestsSent)
    .def_readwrite("total_data_received", &MetricSnapshot::totalDataReceived)
    .def_readwrite("total_bytes_received", &MetricSnapshot::totalBytesReceived)
    .def_readwrite("total_packets_dropped", &MetricSnapshot::totalPacketsDropped)
    .def_readwrite("pending_interests", &MetricSnapshot::pendingInterests)
    .def_readwrite("cache_hits", &MetricSnapshot::cacheHits)
    .def_readwrite("cache_misses", &MetricSnapshot::cacheMisses)
    .def_readwrite("queue_occupancy", &MetricSnapshot::queueOccupancy)
    .def_readwrite("throughput_mbps", &MetricSnapshot::throughputMbps)
    .def_readwrite("avg_delay_ms", &MetricSnapshot::avgDelayMs)  // REFACTORED: avgDelayMs instead of rttMeanMs
    .def_readwrite("cache_hit_ratio", &MetricSnapshot::cacheHitRatio)
    .def_readwrite("pending_interests_norm", &MetricSnapshot::pendingInterestsNorm)
    .def_readwrite("throughput_norm", &MetricSnapshot::throughputNorm)
    .def_readwrite("avg_delay_norm", &MetricSnapshot::avgDelayNorm)
    // REFACTORED: Provide computed properties for backward compatibility
    .def_property_readonly("rtt_mean_ms", 
         [](const MetricSnapshot& self) { return self.avgDelayMs; })  // Alias for backward compatibility
    .def_property_readonly("packet_loss_rate",
         [](const MetricSnapshot& self) {
           if (self.totalInterestsSent > 0) {
             return static_cast<double>(self.totalPacketsDropped) / static_cast<double>(self.totalInterestsSent);
           }
           return 0.0;
         })
    // REFACTORED: Removed fields - provide computed properties
    .def_property_readonly("queue_drop_ratio",
         [](const MetricSnapshot& self) {
           // Calculate from totalPacketsDropped if needed
           if (self.totalInterestsSent > 0) {
             return static_cast<double>(self.totalPacketsDropped) / static_cast<double>(self.totalInterestsSent);
           }
           return 0.0;
         })
    .def_property_readonly("interest_rate_pps",
         [](const MetricSnapshot& self) {
           // Approximate from pendingInterests (not exact, but for compatibility)
           return static_cast<double>(self.pendingInterests);  // Placeholder
         })
    .def_property_readonly("data_rate_pps",
         [](const MetricSnapshot& self) {
           // Approximate from throughput (not exact, but for compatibility)
           return self.throughputMbps * 1e6 / (1024.0 * 8.0);  // Convert Mbps to pps
         })
    .def_property_readonly("congestion_level",
         [](const MetricSnapshot& self) {
           return self.queueOccupancy;  // Use queueOccupancy as congestion indicator
         });

  // ============================================================================
  // ActionVector binding
  // ============================================================================
  py::class_<ActionVector>(m, "ActionVector")
    .def(py::init<>())
    .def_readwrite("interest_rate_factor", &ActionVector::interestRateFactor)
    .def_readwrite("queue_threshold_factor", &ActionVector::queueThresholdFactor)
    .def_readwrite("forwarding_weight_delta", &ActionVector::forwardingWeightDelta)
    .def_readwrite("cache_adjustment", &ActionVector::cacheAdjustment);

  // ============================================================================
  // StateFeatures binding
  // ============================================================================
  py::class_<StateFeatures, std::shared_ptr<StateFeatures>>(m, "StateFeatures")
    .def(py::init<>())
    .def("extract_features",
         [](StateFeatures& self, const MetricSnapshot& snapshot) {
           auto features = self.ExtractFeatures(snapshot);
           // Convert to numpy array
           return py::cast(features);
         },
         "Extract normalized feature vector from metric snapshot",
         py::arg("snapshot"))
    .def("get_normalization_config", &StateFeatures::GetNormalizationConfig)
    .def("set_normalization_config", &StateFeatures::SetNormalizationConfig);

  // NormalizationConfig binding
  py::class_<StateFeatures::NormalizationConfig>(m, "NormalizationConfig")
    .def(py::init<>())
    .def_readwrite("max_queue_occupancy", &StateFeatures::NormalizationConfig::maxQueueOccupancy)
    .def_readwrite("max_queue_drop", &StateFeatures::NormalizationConfig::maxQueueDrop)
    .def_readwrite("max_interest_rate", &StateFeatures::NormalizationConfig::maxInterestRate)
    .def_readwrite("max_data_rate", &StateFeatures::NormalizationConfig::maxDataRate)
    .def_readwrite("max_throughput_mbps", &StateFeatures::NormalizationConfig::maxThroughputMbps)
    .def_readwrite("max_rtt_ms", &StateFeatures::NormalizationConfig::maxRttMs)
    .def_readwrite("max_loss_rate", &StateFeatures::NormalizationConfig::maxLossRate)
    .def_readwrite("max_nack_rate", &StateFeatures::NormalizationConfig::maxNackRate)
    .def_readwrite("max_cache_utilization", &StateFeatures::NormalizationConfig::maxCacheUtilization);

  // ============================================================================
  // MetricEngine binding (REFACTORED: MetricStore deleted)
  // ============================================================================
  // REFACTORED: MetricStore deleted - using MetricEngine instead
  // Note: Bind similar to Controller - without Object base class for pybind11 compatibility
  py::class_<MetricEngine>(m, "MetricEngine")
    .def(py::init<>())
    .def("start",
         [](Ptr<MetricEngine> self, double seconds) {
           if (self) self->Start(Seconds(seconds));
         },
         py::arg("interval_seconds") = 1.0)
    .def("stop",
         [](Ptr<MetricEngine> self) {
           if (self) self->Stop();
         })
    .def("get_latest_snapshot",
         [](Ptr<MetricEngine> self, const std::string& region) {
           if (!self) return MetricSnapshot{};
           return self->GetLatestSnapshot(region);
         },
         py::arg("region") = "default",
         "Get latest metric snapshot for a region")
    // DEPRECATED: Keep these methods for backward compatibility
    .def("configure_sampling_interval",
         [](Ptr<MetricEngine> self, double seconds) {
           NS_LOG_WARN("configure_sampling_interval() is deprecated - use start(interval) instead");
         },
         py::arg("seconds"))
    .def("get_sampling_interval",
         [](Ptr<MetricEngine> self) {
           return 1.0;  // Default interval
         })
    .def("get_history",
         [](Ptr<MetricEngine> self) {
           NS_LOG_WARN("get_history() is deprecated - MetricEngine doesn't maintain history");
           return std::vector<MetricSnapshot>{};
         });
  
  // DEPRECATED: Alias MetricStore for backward compatibility (same as MetricEngine)
  py::class_<MetricEngine>(m, "MetricStore")
    .def(py::init<>())
    .def("start",
         [](Ptr<MetricEngine> self, double seconds) {
           if (self) self->Start(Seconds(seconds));
         },
         py::arg("interval_seconds") = 1.0)
    .def("stop",
         [](Ptr<MetricEngine> self) {
           if (self) self->Stop();
         })
    .def("get_latest_snapshot",
         [](Ptr<MetricEngine> self, const std::string& region) {
           if (!self) return MetricSnapshot{};
           return self->GetLatestSnapshot(region);
         },
         py::arg("region") = "default");

  // ============================================================================
  // ActionPolicy binding
  // ============================================================================
  py::class_<ActionPolicy, std::shared_ptr<ActionPolicy>>(m, "ActionPolicy")
    .def(py::init<>())
    .def("validate", &ActionPolicy::Validate)
    .def("apply", &ActionPolicy::Apply)
    .def("set_interest_rate_bounds", &ActionPolicy::SetInterestRateBounds)
    .def("set_queue_threshold_bounds", &ActionPolicy::SetQueueThresholdBounds)
    .def("set_forwarding_weight_bounds", &ActionPolicy::SetForwardingWeightBounds)
    .def("set_cache_bounds", &ActionPolicy::SetCacheBounds)
    .def("set_interest_rate_handler",
         [](ActionPolicy& self, std::function<void(double)> handler) {
           self.SetInterestRateHandler(handler);
         })
    .def("set_queue_threshold_handler",
         [](ActionPolicy& self, std::function<void(double)> handler) {
           self.SetQueueThresholdHandler(handler);
         })
    .def("set_forwarding_weight_handler",
         [](ActionPolicy& self, std::function<void(double)> handler) {
           self.SetForwardingWeightHandler(handler);
         })
    .def("set_cache_adjustment_handler",
         [](ActionPolicy& self, std::function<void(double)> handler) {
           self.SetCacheAdjustmentHandler(handler);
         });

  // ============================================================================
  // FederationCoordinator binding
  // ============================================================================
  py::class_<FederationCoordinator, std::shared_ptr<FederationCoordinator>>(m, "FederationCoordinator")
    .def(py::init<>())
    .def("start", &FederationCoordinator::Start)
    .def("stop", &FederationCoordinator::Stop)
    .def("trigger_local_update", &FederationCoordinator::TriggerLocalUpdate)
    .def("trigger_federated_round", &FederationCoordinator::TriggerFederatedRound)
    .def("set_round_interval",
         [](FederationCoordinator& self, double seconds) {
           self.SetRoundInterval(Seconds(seconds));
         },
         py::arg("seconds"))
    .def("get_round_interval",
         [](FederationCoordinator& self) {
           return self.GetRoundInterval().GetSeconds();
         })
    .def("set_on_request_weights",
         [](FederationCoordinator& self, std::function<WeightBuffer()> callback) {
           self.SetOnRequestWeights(callback);
         })
    .def("set_on_receive_global",
         [](FederationCoordinator& self, std::function<void(const WeightBuffer&)> callback) {
           self.SetOnReceiveGlobal(callback);
         })
    .def("set_on_dispatch_global",
         [](FederationCoordinator& self, std::function<void(const WeightBuffer&)> callback) {
           self.SetOnDispatchGlobal(callback);
         });

  // WeightBuffer binding (std::vector<uint8_t>)
  py::class_<WeightBuffer>(m, "WeightBuffer")
    .def(py::init<>())
    .def("__len__", [](const WeightBuffer& b) { return b.size(); })
    .def("__getitem__", [](const WeightBuffer& b, size_t i) { return b[i]; })
    .def("__setitem__", [](WeightBuffer& b, size_t i, uint8_t v) { b[i] = v; })
    .def("append", [](WeightBuffer& b, uint8_t v) { b.push_back(v); })
    .def("clear", &WeightBuffer::clear)
    .def("data", [](WeightBuffer& b) {
      return py::memoryview::from_memory(b.data(), b.size());
    });

  // ============================================================================
  // Controller binding
  // ============================================================================
  // Note: Bind without Object base class to avoid pybind11 type registration issues
  // The functionality is preserved as we only expose the interface methods
  py::class_<Controller>(m, "Controller")
    .def(py::init<>())
    .def("initialize", &Controller::Initialize)
    .def("shutdown", &Controller::Shutdown)
    .def("get_update_interval",
         [](Controller& self) {
           return self.GetUpdateInterval().GetSeconds();
         })
    .def("set_update_interval",
         [](Controller& self, double seconds) {
           self.SetUpdateInterval(Seconds(seconds));
         },
         py::arg("seconds"))
    .def("get_metric_store", &Controller::GetMetricStore)
    .def("set_metric_store", &Controller::SetMetricStore)
    .def("get_state_features", &Controller::GetStateFeatures)
    .def("set_state_features", &Controller::SetStateFeatures)
    .def("get_action_policy", &Controller::GetActionPolicy)
    .def("set_action_policy", &Controller::SetActionPolicy)
    .def("get_federation_coordinator", &Controller::GetFederationCoordinator)
    .def("set_federation_coordinator", &Controller::SetFederationCoordinator)
    // Convenience methods for Python agent interaction
    .def("get_current_state",
         [](Controller& self) -> py::object {
           // REFACTORED: MetricEngine::GetLatestSnapshot() requires region parameter
           auto engine = self.GetMetricStore();  // Returns Ptr<MetricEngine> now
           auto features = self.GetStateFeatures();
           if (!engine || !features) {
             return py::none();
           }
           // TODO: Get actual region from controller/context - for now use default
           auto snapshot = engine->GetLatestSnapshot("default");  // MetricEngine requires region
           auto state_vector = features->ExtractFeatures(snapshot);
           return py::cast(state_vector);
         },
         "Get current normalized state vector for DRL agent")
    .def("apply_action",
         &Controller::ApplyAction,
         "Apply action vector from DRL agent",
         py::arg("action"))
    .def("get_current_reward",
         &Controller::GetCurrentReward,
         "Get current reward value (computed in last control step)")
    .def("set_python_action_callback",
         [](Controller& self, py::object callback) {
           // Wrap Python callable in C++ function
           self.SetPythonActionCallback([callback](const std::vector<double>& features) -> ActionVector {
             try {
               py::object result = callback(py::cast(features));
               ActionVector action;
               if (py::hasattr(result, "interest_rate_factor")) {
                 action.interestRateFactor = result.attr("interest_rate_factor").cast<double>();
               }
               if (py::hasattr(result, "queue_threshold_factor")) {
                 action.queueThresholdFactor = result.attr("queue_threshold_factor").cast<double>();
               }
               if (py::hasattr(result, "forwarding_weight_delta")) {
                 action.forwardingWeightDelta = result.attr("forwarding_weight_delta").cast<double>();
               }
               if (py::hasattr(result, "cache_adjustment")) {
                 action.cacheAdjustment = result.attr("cache_adjustment").cast<double>();
               }
               return action;
             } catch (const std::exception& e) {
               NS_LOG_ERROR("Python action callback failed: " << e.what());
               return ActionVector{};  // Return default action
             }
           });
         },
         "Set Python callback for action selection",
         py::arg("callback"))
    .def("set_python_reward_callback",
         [](Controller& self, py::object callback) {
           // Wrap Python callable in C++ function
           self.SetPythonRewardCallback([callback](const MetricSnapshot& snapshot) -> double {
             try {
               py::object result = callback(py::cast(snapshot));
               return result.cast<double>();
             } catch (const std::exception& e) {
               NS_LOG_ERROR("Python reward callback failed: " << e.what());
               return 0.0;
             }
           });
         },
         "Set Python callback for reward computation",
         py::arg("callback"))
    .def("has_python_action_callback", &Controller::HasPythonActionCallback)
    .def("has_python_reward_callback", &Controller::HasPythonRewardCallback)
    .def("set_python_weight_request_callback",
         [](Controller& self, py::object callback) {
           self.SetPythonWeightRequestCallback([callback]() -> WeightBuffer {
             try {
               py::object result = callback();
               // Convert Python bytes/bytearray to WeightBuffer
               if (py::isinstance<py::bytes>(result)) {
                 std::string bytes_str = result.cast<std::string>();
                 return WeightBuffer(bytes_str.begin(), bytes_str.end());
               }
               else if (py::hasattr(result, "__iter__")) {
                 WeightBuffer buffer;
                 for (auto item : result) {
                   buffer.push_back(py::cast<uint8_t>(item));
                 }
                 return buffer;
               }
               return WeightBuffer();
             } catch (const std::exception& e) {
               NS_LOG_ERROR("Python weight request callback failed: " << e.what());
               return WeightBuffer();
             }
           });
         },
         "Set Python callback for requesting model weights for federation",
         py::arg("callback"))
    .def("set_python_weight_receive_callback",
         [](Controller& self, py::object callback) {
           self.SetPythonWeightReceiveCallback([callback](const WeightBuffer& weights) {
             try {
               // Convert WeightBuffer to Python bytes
               py::bytes py_weights(reinterpret_cast<const char*>(weights.data()), weights.size());
               callback(py_weights);
             } catch (const std::exception& e) {
               NS_LOG_ERROR("Python weight receive callback failed: " << e.what());
             }
           });
         },
         "Set Python callback for receiving global aggregated weights",
         py::arg("callback"));

  // ============================================================================
  // Helper functions
  // ============================================================================
  m.def("register_controller",
        [](uint32_t nodeId, Controller* controller) {
          if (controller) {
            RegisterController(nodeId, Ptr<Controller>(controller, false));
          }
        },
        "Register controller instance for a node ID (for cross-process access)",
        py::arg("node_id"),
        py::arg("controller"));
  
  m.def("get_controller",
        [](uint32_t nodeId) -> Controller* {
          Ptr<Controller> ctrl = GetControllerFromNode(nodeId);
          if (!ctrl) {
            return nullptr;
          }
          // Return raw pointer - pybind11 will manage lifetime via Controller binding
          return PeekPointer(ctrl);
        },
        "Get controller instance from node ID",
        py::arg("node_id"),
        py::return_value_policy::reference_internal);

  // Time conversion helpers
  m.def("seconds", [](double s) { return Seconds(s); });
  m.def("milliseconds", [](double ms) { return MilliSeconds(ms); });

  // ============================================================================
  // PyBindingInterface - Stateful interface for real-time RL training
  // ============================================================================
  // Note: Bind without Object base class to avoid pybind11 type registration issues
  py::class_<PyBindingInterface>(m, "PyBindingInterface")
    .def(py::init<>())
    .def("set_controller", &PyBindingInterface::SetController,
         "Set the controller instance",
         py::arg("controller"))
    .def("get_controller", &PyBindingInterface::GetController,
         "Get the controller instance",
         py::return_value_policy::reference_internal)
    .def("get_state",
         [](PyBindingInterface& self) {
           return self.GetState();
         },
         "Get current 9-dimensional state vector for RL agent")
    .def("send_action",
         &PyBindingInterface::SendAction,
         "Send action (rate, queue_weight) to controller",
         py::arg("a_rate"), py::arg("a_qweight"))
    .def("log_transition",
         &PyBindingInterface::LogTransition,
         "Log a state transition (s, a, r, s')",
         py::arg("state"), py::arg("action"), py::arg("reward"), py::arg("next_state"))
    .def("is_simulation_running",
         &PyBindingInterface::IsSimulationRunning,
         "Check if simulation is currently running")
    .def("get_throughput", &PyBindingInterface::GetThroughput,
         "Get current throughput in Mbps")
    .def("get_rtt", &PyBindingInterface::GetRtt,
         "Get current RTT in milliseconds")
    .def("get_queue_size", &PyBindingInterface::GetQueueSize,
         "Get current queue size (normalized)")
    .def("get_satisfaction_ratio", &PyBindingInterface::GetSatisfactionRatio,
         "Get current satisfaction ratio (0-1)")
    .def("get_nack_rate", &PyBindingInterface::GetNackRate,
         "Get current NACK rate")
    .def("get_metric_snapshot", &PyBindingInterface::GetMetricSnapshot,
         "Get full metric snapshot");

  // Helper function to create interface from controller
  m.def("create_interface",
        [](Ptr<Controller> controller) {
          Ptr<PyBindingInterface> interface = CreateObject<PyBindingInterface>();
          interface->SetController(controller);
          return interface;
        },
        "Create PyBindingInterface from controller",
        py::arg("controller"),
        py::return_value_policy::reference_internal);

  // ============================================================================
  // Simulation Control Functions - For Python-driven DRL training
  // ============================================================================
  
  // Forward declarations from py_fdrlcc_simulation.hpp
  m.def("initialize_simulation",
        [](double simTime, double consumerFreq, uint32_t seed) {
          return InitializeSimulation(simTime, consumerFreq, seed);
        },
        "Initialize simulation without running",
        py::arg("sim_time"),
        py::arg("consumer_freq") = 10.0,
        py::arg("seed") = 42);
  
  m.def("step_simulation",
        [](double stepSize) {
          return StepSimulation(stepSize);
        },
        "Advance simulation by one step",
        py::arg("step_size") = 0.2);
  
  m.def("get_state",
        []() {
          return GetCurrentState();
        },
        "Get current state vector (9 dimensions)");
  
  m.def("apply_action",
        [](double rateFactor, double queueFactor) {
          ApplyAction(rateFactor, queueFactor);
        },
        "Apply action from RL agent",
        py::arg("rate_factor"),
        py::arg("queue_factor") = 1.0);
  
  m.def("get_reward",
        []() {
          return GetCurrentReward();
        },
        "Get current reward value");
  
  m.def("get_metrics",
        []() {
          return GetMetricSnapshot();
        },
        "Get full metric snapshot");
  
  m.def("is_running",
        []() {
          return IsSimulationRunning();
        },
        "Check if simulation is running");
  
  m.def("get_sim_time",
        []() {
          return GetSimulationTime();
        },
        "Get current simulation time");
  
  m.def("destroy_simulation",
        []() {
          DestroySimulation();
        },
        "Clean up simulation");
  
  m.def("run_simulation",
        [](double simTime, double updateInterval, double samplingInterval,
           double consumerFreq, uint32_t seed) {
          return RunFdrlccSimulation(simTime, updateInterval, samplingInterval,
                                     consumerFreq, seed);
        },
        "Run complete simulation (blocking)",
        py::arg("sim_time") = 60.0,
        py::arg("update_interval") = 0.2,
        py::arg("sampling_interval") = 0.1,
        py::arg("consumer_freq") = 10.0,
        py::arg("seed") = 42);
}

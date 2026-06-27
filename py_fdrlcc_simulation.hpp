#ifndef PY_FDRLCC_SIMULATION_HPP_
#define PY_FDRLCC_SIMULATION_HPP_

/**
 * Python-callable simulation functions for FDRLCC
 * 
 * These functions allow Python to control the simulation for real-time DRL training:
 * - InitializeSimulation: Set up topology without running
 * - StepSimulation: Advance simulation by small time increments
 * - GetCurrentState: Get state vector for RL agent
 * - ApplyAction: Apply RL agent's action
 * - GetCurrentReward: Get reward from controller
 * - DestroySimulation: Clean up
 */

// REFACTORED: MetricStore deleted - using MetricEngine instead
#include "src_cpp/metrics/metric-engine.hpp"
#include <vector>
#include <cstdint>

namespace ns3 {
namespace ndn {
namespace fdrl {

class Controller;

/**
 * Run a complete FDRLCC simulation (blocking)
 * @param simTime Total simulation time in seconds
 * @param updateInterval Controller update interval
 * @param samplingInterval Metric sampling interval
 * @param consumerFreq Consumer base frequency in Hz
 * @param seed Random seed
 * @return 0 on success, -1 on error
 */
int RunFdrlccSimulation(double simTime, 
                        double updateInterval,
                        double samplingInterval,
                        double consumerFreq,
                        uint32_t seed);

// ============================================================================
// Step-by-step simulation control for real-time Python training
// ============================================================================

/**
 * Initialize simulation without running
 * @param simTime Maximum simulation time
 * @param consumerFreq Base consumer frequency
 * @param seed Random seed
 * @return true if initialization successful
 */
bool InitializeSimulation(double simTime, double consumerFreq, uint32_t seed);

/**
 * Advance simulation by one step
 * @param stepSize Time to advance in seconds
 * @return true if simulation can continue, false if ended
 */
bool StepSimulation(double stepSize);

/**
 * Get current state vector (9 dimensions)
 * @return Normalized state vector for RL agent
 */
std::vector<double> GetCurrentState();

/**
 * Apply action from RL agent
 * @param rateFactor Interest rate factor (0.1-2.0)
 * @param queueFactor Queue threshold factor (0.1-2.0)
 */
void ApplyAction(double rateFactor, double queueFactor);

/**
 * Get current reward value
 * @return Reward computed by controller
 */
double GetCurrentReward();

/**
 * Get full metric snapshot
 * @return MetricSnapshot with all current metrics
 */
MetricSnapshot GetMetricSnapshot();

/**
 * Check if simulation is currently running
 * @return true if running
 */
bool IsSimulationRunning();

/**
 * Get current simulation time
 * @return Current time in seconds
 */
double GetSimulationTime();

/**
 * Clean up simulation
 */
void DestroySimulation();

/**
 * Get the Python-accessible controller
 * @return Controller pointer (may be null)
 */
Ptr<Controller> GetPythonController();

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // PY_FDRLCC_SIMULATION_HPP_

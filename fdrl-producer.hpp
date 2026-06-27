#ifndef FDRLCC_APPLICATIONS_FDRL_PRODUCER_HPP_
#define FDRLCC_APPLICATIONS_FDRL_PRODUCER_HPP_

#include "ns3/ndnSIM/apps/ndn-producer.hpp"

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * \brief Producer extension with hooks for FDRLCC experiments.
 */
class FdrlProducer : public Producer
{
public:
  static TypeId GetTypeId();

  FdrlProducer();
  ~FdrlProducer() override;

  void SetContentFreshness(Time freshness);
  Time GetContentFreshness() const;

protected:
  void OnInterest(shared_ptr<const Interest> interest) override;

private:
  Time m_contentFreshness;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_APPLICATIONS_FDRL_PRODUCER_HPP_


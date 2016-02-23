#ifndef STOPREMOTETRANSACTION_H_
#define STOPREMOTETRANSACTION_H_

#include "MantidAPI/Algorithm.h"

namespace Mantid {
namespace RemoteAlgorithms {

class DLLExport StopRemoteTransaction : public Mantid::API::Algorithm {
public:
  /// (Empty) Constructor
  StopRemoteTransaction() : Mantid::API::Algorithm() {}
  /// Virtual destructor
  ~StopRemoteTransaction() override {}
  /// Algorithm's name
  const std::string name() const override { return "StopRemoteTransaction"; }
  /// Summary of algorithms purpose
  const std::string summary() const override {
    return "Stop a job transaction on a remote compute resource.";
  }

  /// Algorithm's version
  int version() const override { return (1); }
  /// Algorithm's category for identification
  const std::string category() const override { return "Remote"; }

private:
  void init() override;
  /// Execution code
  void exec() override;
};

} // end namespace RemoteAlgorithms
} // end namespace Mantid
#endif /*STOPREMOTETRANSACTION_H_*/

#ifndef STARTREMOTETRANSACTION_H_
#define STARTREMOTETRANSACTION_H_

#include "MantidAPI/Algorithm.h"

namespace Mantid {
namespace RemoteAlgorithms {

class DLLExport StartRemoteTransaction : public Mantid::API::Algorithm {
public:
  /// (Empty) Constructor
  StartRemoteTransaction() : Mantid::API::Algorithm() {}
  /// Virtual destructor
  ~StartRemoteTransaction() override {}
  /// Algorithm's name
  const std::string name() const override { return "StartRemoteTransaction"; }
  /// Summary of algorithms purpose
  const std::string summary() const override {
    return "Start a job transaction on a remote compute resource.";
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
#endif /*STARTREMOTETRANSACTION_H_*/

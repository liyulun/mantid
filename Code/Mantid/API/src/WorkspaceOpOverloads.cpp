//----------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------
#include "MantidAPI/WorkspaceOpOverloads.h"
#include "MantidAPI/Algorithm.h"
#include "MantidAPI/AlgorithmManager.h"
#include "MantidKernel/Property.h"
#include "MantidKernel/Exception.h"
#include "MantidAPI/IWorkspaceProperty.h"
#include "MantidAPI/WorkspaceFactory.h"
#include <numeric>

namespace Mantid
{
namespace API
{

/** Performs a binary operation on two workspaces
 *  @param algorithmName The name of the binary operation to perform
 *  @param lhs left hand side workspace shared pointer
 *  @param rhs left hand side workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
static Workspace_sptr executeBinaryOperation(const std::string algorithmName, const Workspace_sptr lhs, const Workspace_sptr rhs, bool lhsAsOutput = false)
{
  Algorithm_sptr alg = AlgorithmManager::Instance().createUnmanaged(algorithmName);
  alg->setChild(true);
  alg->initialize();

  alg->setProperty<Workspace_sptr>("InputWorkspace_1",lhs);
  alg->setProperty<Workspace_sptr>("InputWorkspace_2",rhs);
  
  // Have to set a text name for the output workspace even though it will not be used.
  //   This satisfies the validation.
  alg->setPropertyValue("OutputWorkspace","��NotApplicable");
  // If calling from a compound assignment operator, set the left-hand operand to be the output workspace
  if (lhsAsOutput) alg->setProperty<Workspace_sptr>("OutputWorkspace",lhs);

  alg->execute();

  if (alg->isExecuted())
  {
    //Get the output workspace property
    return alg->getProperty("OutputWorkspace");
  }
  else
  {
    throw std::runtime_error("Error while executing operation algorithm: algorithmName");
  }

  throw Kernel::Exception::NotFoundError("Required output workspace property not found on sub algorithm" ,"OutputWorkspace");

  //Horendous code inclusion to satisfy compilers that all code paths return a value
  // in reality the above code should either throw or return successfully.
  Workspace_sptr retVal = WorkspaceFactory::Instance().create("Workspace2D");
  return retVal;
}

/** Creates a temporary single value workspace the error is set to sqrt(value)
 *  @param rhsValue the value to use
 *  @returns The value in a workspace shared pointer
 */
static Workspace_sptr createWorkspaceSingleValue(const double& rhsValue)
{
  Workspace_sptr retVal = WorkspaceFactory::Instance().create("WorkspaceSingleValue");
  retVal->dataY(0)[0]=rhsValue;

  return retVal;
}

/** Adds two workspaces
 *  @param lhs left hand side workspace shared pointer
 *  @param rhs left hand side workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator+(const Workspace_sptr lhs, const Workspace_sptr rhs)
{
  return executeBinaryOperation("Plus",lhs,rhs);
}

/** Adds a workspace to a single value
 *  @param lhs      left hand side workspace shared pointer
 *  @param rhsValue the single value
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator+(const Workspace_sptr lhs, const double& rhsValue)
{
  return executeBinaryOperation("Plus",lhs,createWorkspaceSingleValue(rhsValue));
}

/** Subtracts two workspaces
 *  @param lhs left hand side workspace shared pointer
 *  @param rhs left hand side workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator-(const Workspace_sptr lhs, const Workspace_sptr rhs)
{
  return executeBinaryOperation("Minus",lhs,rhs);
}

/** Subtracts  a single value from a workspace
 *  @param lhs      left hand side workspace shared pointer
 *  @param rhsValue the single value
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator-(const Workspace_sptr lhs, const double& rhsValue)
{
  return executeBinaryOperation("Minus",lhs,createWorkspaceSingleValue(rhsValue));
}

/** Multiply two workspaces
 *  @param lhs left hand side workspace shared pointer
 *  @param rhs left hand side workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator*(const Workspace_sptr lhs, const Workspace_sptr rhs)
{
  return executeBinaryOperation("Multiply",lhs,rhs);
}

/** Multiply a workspace and a single value
 *  @param lhs      left hand side workspace shared pointer
 *  @param rhsValue the single value
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator*(const Workspace_sptr lhs, const double& rhsValue)
{
  return executeBinaryOperation("Multiply",lhs,createWorkspaceSingleValue(rhsValue));
}

/** Multiply a workspace and a single value. Allows you to write, e.g., 2*workspace.
 *  @param lhsValue the single value
 *  @param rhs      workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator*(const double& lhsValue, const Workspace_sptr rhs)
{
  return executeBinaryOperation("Multiply",createWorkspaceSingleValue(lhsValue),rhs);
}

/** Divide two workspaces
 *  @param lhs left hand side workspace shared pointer
 *  @param rhs left hand side workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator/(const Workspace_sptr lhs, const Workspace_sptr rhs)
{
  return executeBinaryOperation("Divide",lhs,rhs);
}

/** Divide a workspace by a single value
 *  @param lhs      left hand side workspace shared pointer
 *  @param rhsValue the single value
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator/(const Workspace_sptr lhs, const double& rhsValue)
{
  return executeBinaryOperation("Divide",lhs,createWorkspaceSingleValue(rhsValue));
}

/** Adds two workspaces
 *  @param lhs left hand side workspace shared pointer
 *  @param rhs left hand side workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator+=(const Workspace_sptr lhs, const Workspace_sptr rhs)
{
  return executeBinaryOperation("Plus",lhs,rhs,true);
}

/** Adds a single value to a workspace
 *  @param lhs      workspace shared pointer
 *  @param rhsValue the single value
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator+=(const Workspace_sptr lhs, const double& rhsValue)
{
  return executeBinaryOperation("Plus",lhs,createWorkspaceSingleValue(rhsValue),true);
}

/** Subtracts two workspaces
 *  @param lhs left hand side workspace shared pointer
 *  @param rhs left hand side workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator-=(const Workspace_sptr lhs, const Workspace_sptr rhs)
{
  return executeBinaryOperation("Minus",lhs,rhs,true);
}

/** Subtracts a single value from a workspace
 *  @param lhs      workspace shared pointer
 *  @param rhsValue the single value
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator-=(const Workspace_sptr lhs, const double& rhsValue)
{
  return executeBinaryOperation("Minus",lhs,createWorkspaceSingleValue(rhsValue),true);
}

/** Multiply two workspaces
 *  @param lhs left hand side workspace shared pointer
 *  @param rhs left hand side workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator*=(const Workspace_sptr lhs, const Workspace_sptr rhs)
{
  return executeBinaryOperation("Multiply",lhs,rhs,true);
}

/** Multiplies a workspace by a single value
 *  @param lhs      workspace shared pointer
 *  @param rhsValue the single value
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator*=(const Workspace_sptr lhs, const double& rhsValue)
{
  return executeBinaryOperation("Multiply",lhs,createWorkspaceSingleValue(rhsValue),true);
}

/** Divide two workspaces
 *  @param lhs left hand side workspace shared pointer
 *  @param rhs left hand side workspace shared pointer
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator/=(const Workspace_sptr lhs, const Workspace_sptr rhs)
{
  return executeBinaryOperation("Divide",lhs,rhs,true);
}

/** Divides a workspace by a single value
 *  @param lhs      workspace shared pointer
 *  @param rhsValue the single value
 *  @returns The result in a workspace shared pointer
 */
Workspace_sptr operator/=(const Workspace_sptr lhs, const double& rhsValue)
{
  return executeBinaryOperation("Divide",lhs,createWorkspaceSingleValue(rhsValue),true);
}

//----------------------------------------------------------------------
// Now the WorkspaceHelpers methods
//----------------------------------------------------------------------

/** Checks whether a workspace has common bins (or values) in X
 *  @param WS The workspace to check
 *  @return True if the bins match
 */
const bool WorkspaceHelpers::commonBoundaries(const Workspace_const_sptr WS)
{
  if ( !WS->blocksize() || WS->getNumberHistograms() < 2) return true;
  // Quickest check is to see if they are actually all the same vector
  if ( sharedXData(WS) ) return true;

  // But even if they're not they could still match...
  const double commonSum = std::accumulate(WS->readX(0).begin(),WS->readX(0).end(),0.);
  const int numHist = WS->getNumberHistograms();
  for (int j = 1; j < numHist; ++j)
  {
    const double sum = std::accumulate(WS->readX(j).begin(),WS->readX(j).end(),0.);
    if ( std::abs(commonSum-sum)/std::max<double>(commonSum,sum) > 1.0E-7 ) return false;
  }
  return true;
}

/** Checks whether the bins (X values) of two workspace are the same
 *  @param ws1 The first workspace
 *  @param ws2 The second workspace
 *  @param firstOnly If true, only the first spectrum is checked. If false (the default), all spectra
 *                   are checked and the two workspaces must have the same number of spectra
 *  @return True if the test passes
 */
const bool WorkspaceHelpers::matchingBins(const Workspace_const_sptr ws1,
                                          const Workspace_const_sptr ws2, const bool firstOnly)
{
  // First of all, the first vector must be the same size
  if ( ws1->readX(0).size() != ws2->readX(0).size() ) return false;

  // Now check the first spectrum
  const double firstWS = std::accumulate(ws1->readX(0).begin(),ws1->readX(0).end(),0.);
  const double secondWS = std::accumulate(ws2->readX(0).begin(),ws2->readX(0).end(),0.);
  if ( std::abs(firstWS-secondWS)/std::max<double>(firstWS,secondWS) > 1.0E-7 ) return false;

  // If we were only asked to check the first spectrum, return now
  if (firstOnly) return true;

  // Check that total size of workspace is the same
  if ( ws1->size() != ws2->size() ) return false;
  // If that passes then check whether all the X vectors are shared
  if ( sharedXData(ws1) && sharedXData(ws2) ) return true;

  // If that didn't pass then explicitly check 1 in 10 of the vectors (min 10, max 100)
  const int numHist = ws1->getNumberHistograms();
  int numberToCheck = numHist / 10;
  if (numberToCheck<10) numberToCheck = 10;
  if (numberToCheck>100) numberToCheck = 100;
  int step = numHist / numberToCheck;
  if (!step) step=1;
  for (int i = step; i < numHist; i+=step)
  {
    const double firstWS = std::accumulate(ws1->readX(i).begin(),ws1->readX(i).end(),0.);
    const double secondWS = std::accumulate(ws2->readX(i).begin(),ws2->readX(i).end(),0.);
    if ( std::abs(firstWS-secondWS)/std::max<double>(firstWS,secondWS) > 1.0E-7 ) return false;
  }

  return true;
}

/// Checks whether all the X vectors in a workspace are the same one underneath
const bool WorkspaceHelpers::sharedXData(const Workspace_const_sptr WS)
{
  const double& first = WS->readX(0)[0];
  const int numHist = WS->getNumberHistograms();
  for (int i = 1; i < numHist; ++i)
  {
    if ( &first != &(WS->readX(i)[0]) ) return false;
  }
  return true;
}

/** Divides the data in a workspace by the bin width to make it a distribution.
 *  Can also reverse this operation (i.e. multiply by the bin width).
 *  Sets the isDistribution() flag accordingly.
 *  @param workspace The workspace on which to carry out the operation
 *  @param forwards If true (the default) divides by bin width, if false multiplies
 */
void WorkspaceHelpers::makeDistribution(Workspace_sptr workspace, const bool forwards)
{
  // Check workspace isn't already in the correct state - do nothing if it is
  if ( workspace->isDistribution() == forwards ) return;

  const int numberOfSpectra = workspace->getNumberHistograms();
  for (int i = 0; i < numberOfSpectra; ++i)
  {
    const int size = workspace->blocksize();
    for (int j = 0; j < size; ++j)
    {
      double width = std::abs( workspace->readX(i)[j+1] - workspace->readX(i)[j] );
      if (!forwards) width = 1.0/width;
      workspace->dataY(i)[j] /= width;
      workspace->dataE(i)[j] /= width;
    }
  }
  workspace->isDistribution(forwards);
}

} // namespace API
} // namespace Mantid

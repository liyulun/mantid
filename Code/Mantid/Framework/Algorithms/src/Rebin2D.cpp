//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------    
#include "MantidAlgorithms/Rebin2D.h"
#include "MantidKernel/ArrayProperty.h"
#include "MantidKernel/RebinParamsValidator.h"
#include "MantidKernel/VectorHelper.h"
#include "MantidAPI/WorkspaceProperty.h"
#include "MantidAPI/NumericAxis.h"
#include "MantidGeometry/Math/ConvexPolygon.h"
#include "MantidGeometry/Math/PolygonIntersection.h"

namespace Mantid
{
  namespace Algorithms
  {

    // Register the algorithm into the AlgorithmFactory
    DECLARE_ALGORITHM(Rebin2D)

    using namespace API;
    using Geometry::ConvexPolygon;
    using Geometry::Vertex2DList;
    using Kernel::V2D;

    //--------------------------------------------------------------------------
    // Public methods
    //--------------------------------------------------------------------------
    /**
     * Sets documentation strings for this algorithm
     */
    void Rebin2D::initDocs()
    {
      this->setWikiSummary("Rebins both axes of a 2D workspace.");
      this->setOptionalMessage("Rebins both axes of a 2D workspace using the given parameters");
      this->setWikiDescription("The input bin parameters are used to form an output grid. The overlap "
                               "of the polygons is tested to compute the required signal weight for the "
                               "new bin on the workspace");
    }

    //--------------------------------------------------------------------------
    // Private methods
    //--------------------------------------------------------------------------
    /** 
     * Initialize the algorithm's properties.
     */
    void Rebin2D::init()
    {      
      using Kernel::ArrayProperty;
      using Kernel::Direction;
      using Kernel::RebinParamsValidator;
      using API::WorkspaceProperty;
      declareProperty(new WorkspaceProperty<>("InputWorkspace","",Direction::Input), "An input workspace.");
      declareProperty(new WorkspaceProperty<>("OutputWorkspace","",Direction::Output), "An output workspace.");
      const std::string docString = 
        "A comma separated list of first bin boundary, width, last bin boundary. Optionally\n"
        "this can be followed by a comma and more widths and last boundary pairs.\n"
        "Negative width values indicate logarithmic binning.";
      declareProperty(new ArrayProperty<double>("Axis1Binning", new RebinParamsValidator), docString);
      declareProperty(new ArrayProperty<double>("Axis2Binning", new RebinParamsValidator), docString);
    }

    /** 
     * Execute the algorithm.
     */
    void Rebin2D::exec()
    {
      // Information to form input grid
      MatrixWorkspace_const_sptr inputWS = getProperty("InputWorkspace");
      NumericAxis *oldAxis2 = dynamic_cast<API::NumericAxis*>(inputWS->getAxis(1));
      if( !oldAxis2 ) 
      {
        throw std::invalid_argument("Vertical axis is not a numeric axis, cannot rebin. "
                                    "If it is a spectra axis try running ConvertSpectrumAxis first.");
      }
      const std::vector<double> oldYBins = oldAxis2->createBinBoundaries();

      // Output grid and workspace. Fills in the new X and Y bin vectors
      MantidVecPtr newXBins;
      MantidVec newYBins;
      MatrixWorkspace_sptr outputWS = createOutputWorkspace(inputWS, newXBins.access(), newYBins);
      const int64_t numHist = static_cast<int64_t>(outputWS->getNumberHistograms());
      const int64_t numSig = static_cast<int64_t>(outputWS->blocksize());
      Axis* newAxis2 = outputWS->getAxis(1);

      Progress progress(this,0.0,1.0, numHist);
      // Find the intersection of the each new 'spectra' with the old
      PARALLEL_FOR2(inputWS,outputWS)
      for(int64_t i = 0; i < numHist; ++i)
      {
        PARALLEL_START_INTERUPT_REGION

        outputWS->setX(i, newXBins);
        // References to the Y and E data
        MantidVec & outY = outputWS->dataY(i);
        MantidVec & outE = outputWS->dataE(i);
        // The Y axis value is the bin centre of the new Y bins
        const double y_i = newYBins[i];
        const double y_ip1 = newYBins[i+1];
        newAxis2->setValue(i, 0.5*(y_i + y_ip1));

        // For each bin on the new workspace, test compute overlap
        for(int64_t j = 0; j < numSig; ++j)
        {
          ConvexPolygon newBin = ConvexPolygon((*newXBins)[j],(*newXBins)[j+1], y_i, y_ip1);
          std::pair<double,double> contrib = calculateYE(inputWS, oldYBins, newBin);
          outY[j] = contrib.first;
          outE[j] = contrib.second;
        }
        progress.report();

        PARALLEL_END_INTERUPT_REGION
      }
      PARALLEL_CHECK_INTERUPT_REGION

      outputWS->isDistribution(inputWS->isDistribution());
      setProperty("OutputWorkspace", outputWS);
    }

    /**
     * Calculate the Y and E values for the given possible overlap
     * @param inputWS :: A pointer to the inputWS
     * @param oldYBins :: A reference to the set of Y bin boundaries on the input WS
     * @param newBin :: A reference to a polygon to test for overlap
     * @returns A pair of Y and E values
     */
    std::pair<double,double> 
    Rebin2D::calculateYE(API::MatrixWorkspace_const_sptr inputWS,
                         const MantidVec & oldYBins, const ConvexPolygon & newBin) const
    {
      const MantidVec & oldXBins = inputWS->readX(0);
      // Build a list intersection locations in terms of workspace indices
      // along with corresponding weights from that location
      std::vector<BinWithWeight> overlaps = findIntersections(oldXBins, oldYBins, newBin);
      std::pair<double,double> binValues(0,0);
      if( inputWS->isDistribution() )
      {
        const double newWidth = newBin[1].X() - newBin[0].X(); // For distribution
        binValues = calculateDistYE(inputWS, overlaps, newWidth);
      }
      else
      {
        binValues = calculateYE(inputWS, overlaps);
      }
      return binValues;
    }

    /**
     * Calculate the Y and E values from the given overlaps
     * @param inputWS :: A pointer to the inputWS
     * @param overlaps :: A list of overlap locations and weights
     * @returns A pair of Y and E values
     */
    std::pair<double,double> Rebin2D::calculateYE(API::MatrixWorkspace_const_sptr inputWS,
                                                  const std::vector<BinWithWeight> & overlaps) const
    {
      double totalY(0.0), totalE(0.0);
      std::vector<BinWithWeight>::const_iterator iend = overlaps.end();
      for(std::vector<BinWithWeight>::const_iterator itr = overlaps.begin();
          itr != iend; ++itr)
      {
        const size_t yIndex = itr->yIndex;
        const size_t xIndex = itr->xIndex;
        const MantidVec & inputY = inputWS->readY(yIndex);
        const MantidVec & inputE = inputWS->readE(yIndex);
        totalY += inputY[xIndex] * itr->weight;
        totalE += std::pow(inputE[xIndex] * itr->weight, 2);
      }
      return std::make_pair(totalY,std::sqrt(totalE));
    }

    /**
     * Calculate the Y and E values from the given intersections for an input distribution
     * @param inputWS :: A pointer to the inputWS
     * @param overlaps :: A list of overlap locations and weights
     * @param newBinWidth :: The width of the new bin
     * @returns A pair of Y and E values
     */
    std::pair<double,double> Rebin2D::calculateDistYE(API::MatrixWorkspace_const_sptr inputWS,
                                                      const std::vector<BinWithWeight> & overlaps,
                                                      const double newBinWidth) const
    {
      const MantidVec & oldXBins = inputWS->readX(0);
      double totalY(0.0), totalE(0.0);
      std::vector<BinWithWeight>::const_iterator iend = overlaps.end();
      for(std::vector<BinWithWeight>::const_iterator itr = overlaps.begin();
          itr != iend; ++itr)
      {
        const size_t yIndex = itr->yIndex;
        const size_t xIndex = itr->xIndex;
        const MantidVec & inputY = inputWS->readY(yIndex);
        const MantidVec & inputE = inputWS->readE(yIndex);
        double oldWidth = oldXBins[xIndex+1] - oldXBins[xIndex];
        totalY += inputY[xIndex] * oldWidth * itr->weight;
        totalE += std::pow(inputE[xIndex]*oldWidth*itr->weight,2);
      }
      return std::make_pair(totalY/newBinWidth,std::sqrt(totalE)/newBinWidth);
    }

    /**
     * Find the overlap of the inputWS with the given polygon
     * @param oldAxis1 :: Axis 1 bin boundaries from the input grid
     * @param oldAxis2 :: Axis 2 bin boundaries from the input grid
     * @param newPoly :: The new polygon to test
     * @returns A list of intersection locations with weights of the overlap
     */
    std::vector<Rebin2D::BinWithWeight> 
    Rebin2D::findIntersections(const MantidVec & oldAxis1, const MantidVec & oldAxis2,
                               const Geometry::ConvexPolygon & newPoly) const
    {
      std::vector<BinWithWeight> overlaps;
      overlaps.reserve(5); // Have a guess at a posible limit

      const size_t nxpoints(oldAxis1.size()-1), nypoints(oldAxis2.size()-1);
      Vertex2DList vertices(4);
      for(size_t i = 0; i < nypoints; ++i)
      {
        const double yo_lo(oldAxis2[i]), yo_hi(oldAxis2[i+1]);
        // Check if there is a possibility of overlap
        if( yo_hi < newPoly[0].Y() || yo_lo > newPoly[2].Y() ) continue;
        for(size_t j = 0; j < nxpoints; ++j)
        {
          const double xo_lo(oldAxis1[j]), xo_hi(oldAxis1[j+1]);
          // Check if there is a possibility of overlap
          if( xo_hi < newPoly[0].X() || xo_lo > newPoly[1].X() ) continue;
          ConvexPolygon oldPoly(xo_lo, xo_hi, yo_lo, yo_hi);
          try
          {
            ConvexPolygon overlap = chasingEdgeIntersect(newPoly, oldPoly);
            overlaps.push_back(BinWithWeight(i,j,overlap.area()/oldPoly.area()));
          }
          catch(std::runtime_error &)
          {}            
        }
      }
      return overlaps;
    }

    /**
     * Setup the output workspace 
     * @param parent :: A pointer to the input workspace
     * @param newXBins[out] :: An output vector to be filled with the new X bin boundaries
     * @param newYBins[out] :: An output vector to be filled with the new Y bin boundaries
     * @return A pointer to the output workspace
     */
    MatrixWorkspace_sptr Rebin2D::createOutputWorkspace(MatrixWorkspace_const_sptr parent,
                                                        MantidVec & newXBins, 
                                                        MantidVec & newYBins) const
    {
      using Kernel::VectorHelper::createAxisFromRebinParams;
      // First create the two sets of bin boundaries
      const int newXSize = createAxisFromRebinParams(getProperty("Axis1Binning"), newXBins);
      const int newYSize = createAxisFromRebinParams(getProperty("Axis2Binning"), newYBins);
      // and now the workspace
      return WorkspaceFactory::Instance().create(parent,newYSize-1,newXSize,newXSize-1);
    }
    
  } // namespace Algorithms
} // namespace Mantid


#ifndef CONVERTUNITSTEST_H_
#define CONVERTUNITSTEST_H_

#include <cxxtest/TestSuite.h>

#include "MantidAlgorithms/ConvertUnits.h"
#include "MantidKernel/UnitFactory.h"
#include "MantidAPI/AnalysisDataService.h"
#include "MantidAPI/WorkspaceFactory.h"
#include "MantidAPI/SpectraDetectorMap.h"
#include "MantidDataObjects/Workspace2D.h"
#include "MantidDataHandling/LoadInstrument.h"

using namespace Mantid::Kernel;
using namespace Mantid::API;
using namespace Mantid::Algorithms;
using namespace Mantid::DataObjects;

class ConvertUnitsTest : public CxxTest::TestSuite
{
public:
  
  ConvertUnitsTest()
  {   
    // Set up a small workspace for testing
    Workspace_sptr space = WorkspaceFactory::Instance().create("Workspace2D",2584,11,10);
    Workspace2D_sptr space2D = boost::dynamic_pointer_cast<Workspace2D>(space);
    std::vector<double> x(11);
    for (int i = 0; i < 11; ++i) 
    {
      x[i]=i*1000;
    }
    std::vector<double> a(10);
    std::vector<double> e(10);
    for (int i = 0; i < 10; ++i)
    {
      a[i]=i;
      e[i]=sqrt(double(i));
    }
    int forSpecDetMap[2584];
    for (int j = 0; j < 2584; ++j) {
      space2D->setX(j, x);
      space2D->setData(j, a, e);
      // Just set the spectrum number to match the index
      space2D->getAxis(1)->spectraNo(j) = j;
      forSpecDetMap[j] = j;
    }
    
    // Register the workspace in the data service
    inputSpace = "testWorkspace";
    AnalysisDataService::Instance().add(inputSpace, space);

    // Load the instrument data
    Mantid::DataHandling::LoadInstrument loader;
    loader.initialize();
    // Path to test input file assumes Test directory checked out from SVN
    std::string inputFile = "../../../../Test/Instrument/HET_Definition.xml";
    loader.setPropertyValue("Filename", inputFile);
    loader.setPropertyValue("Workspace", inputSpace);
    loader.execute();
    
    // Populate the spectraDetectorMap with fake data to make spectrum number = detector id = workspace index
    space->getSpectraMap()->populate(forSpecDetMap, forSpecDetMap, 2584, space->getInstrument().get() );
    
    space->getAxis(0)->unit() = UnitFactory::Instance().create("TOF");
  }
  
  void testInit()
  {
    TS_ASSERT_THROWS_NOTHING(alg.initialize());    
    TS_ASSERT( alg.isInitialized() );    
    
    // Set the properties
    alg.setPropertyValue("InputWorkspace",inputSpace);
    outputSpace = "outWorkspace";
    alg.setPropertyValue("OutputWorkspace",outputSpace);
    alg.setPropertyValue("Target","Wavelength");
  }
  
  void testExec()
  {
    if ( !alg.isInitialized() ) alg.initialize();
    TS_ASSERT_THROWS_NOTHING( alg.execute());
    TS_ASSERT( alg.isExecuted() );

    // Get back the saved workspace
    Workspace_sptr output;
    TS_ASSERT_THROWS_NOTHING(output = AnalysisDataService::Instance().retrieve(outputSpace));
    Workspace_sptr input;
    TS_ASSERT_THROWS_NOTHING(input = AnalysisDataService::Instance().retrieve(inputSpace));
    
    Workspace2D_sptr output2D = boost::dynamic_pointer_cast<Workspace2D>(output);
    Workspace2D_sptr input2D = boost::dynamic_pointer_cast<Workspace2D>(input);
    // Test that y & e data is unchanged
    std::vector<double> y = output2D->dataY(101);
    std::vector<double> e = output2D->dataE(101);
    unsigned int ten = 10;
    TS_ASSERT_EQUALS( y.size(), ten );
    TS_ASSERT_EQUALS( e.size(), ten );
    std::vector<double> yIn = input2D->dataY(101);
    std::vector<double> eIn = input2D->dataE(101);
    TS_ASSERT_EQUALS( y[0], yIn[0] );
    TS_ASSERT_EQUALS( y[4], yIn[4] );
    TS_ASSERT_EQUALS( e[1], eIn[1] );
    // Test that spectra that should have been zeroed have been
    std::vector<double> x = output2D->dataX(2300);
    y = output2D->dataY(2408);
    e = output2D->dataE(2276);
    TS_ASSERT_EQUALS( x[7], 0 );
    TS_ASSERT_EQUALS( y[1], 0 );
    TS_ASSERT_EQUALS( e[9], 0 );
    // Check that the data has truly been copied (i.e. isn't a reference to the same
    //    vector in both workspaces)
    double test[10] = {11, 22, 33, 44, 55, 66, 77, 88, 99, 1010};
    std::vector<double> tester(test, test+10);
    output2D->setData(1837, tester);
    y = output2D->dataY(1837);
    TS_ASSERT_EQUALS( y[3], 44.0);
    yIn = input2D->dataY(1837);
    TS_ASSERT_EQUALS( yIn[3], 3.0);
    
    // Check that a couple of x bin boundaries have been correctly converted
    x = output2D->dataX(103);
    TS_ASSERT_DELTA( x[5], 1.5808, 0.001 );
    TS_ASSERT_DELTA( x[10], 3.1617, 0.001 );
    // Just check that an input bin boundary is unchanged
    std::vector<double> xIn = input2D->dataX(2066);
    TS_ASSERT_EQUALS( xIn[4], 4000.0 );
  }
  
  
private:
  ConvertUnits alg;
  std::string inputSpace;
  std::string outputSpace;
};

#endif /*CONVERTUNITSTEST_H_*/

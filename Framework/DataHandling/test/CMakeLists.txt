if ( CXXTEST_FOUND )
  include_directories ( SYSTEM ${CXXTEST_INCLUDE_DIR} ${GMOCK_INCLUDE_DIR} ${GTEST_INCLUDE_DIR} )

  include_directories ( ../../TestHelpers/inc )
  # This variable is used within the cxxtest_add_test macro to build this helper class into the test executable.
  # It will go out of scope at the end of this file so doesn't need un-setting
  set ( TESTHELPER_SRCS ../../TestHelpers/src/TearDownWorld.cpp
                        ../../TestHelpers/src/ComponentCreationHelper.cpp
                        ../../TestHelpers/src/ScopedFileHelper.cpp
                        ../../TestHelpers/src/StartFrameworkManager.cpp
                        ../../TestHelpers/src/InstrumentCreationHelper.cpp
                        ../../TestHelpers/src/WorkspaceCreationHelper.cpp
                        NXcanSASTestHelper.cpp
      )

  cxxtest_add_test ( DataHandlingTest ${TEST_FILES} )
  target_link_libraries( DataHandlingTest LINK_PRIVATE ${TCMALLOC_LIBRARIES_LINKTIME} 
            API
            DataHandling
            DataObjects
            Geometry
            Kernel
            Nexus
            ${NEXUS_LIBRARIES}
            ${Boost_LIBRARIES}
            ${POCO_LIBRARIES}
            ${HDF5_LIBRARIES} )
  add_dependencies ( DataHandlingTest Algorithms MDAlgorithms )
  add_dependencies ( FrameworkTests DataHandlingTest )
  # Test data
  add_dependencies ( DataHandlingTest StandardTestData )

  # Add to the 'FrameworkTests' group in VS
  set_property ( TARGET DataHandlingTest PROPERTY FOLDER "UnitTests" )
endif ()

#ifndef MANTID_CUSTOMINTERFACES_GENERICDATAPROCESSORPRESENTERTEST_H
#define MANTID_CUSTOMINTERFACES_GENERICDATAPROCESSORPRESENTERTEST_H

#include <cxxtest/TestSuite.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "MantidAPI/FrameworkManager.h"
#include "MantidAPI/TableRow.h"
#include "MantidGeometry/Instrument.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorAppendRowCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorClearSelectedCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorCopySelectedCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorCutSelectedCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorDeleteRowCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorExpandCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorExportTableCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorGroupRowsCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorImportTableCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorNewTableCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorOpenTableCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorOptionsCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorPasteSelectedCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorPlotGroupCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorPlotRowCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorPrependRowCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorProcessCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorSaveTableAsCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorSaveTableCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/DataProcessorSeparatorCommand.h"
#include "MantidQtCustomInterfaces/Reflectometry/GenericDataProcessorPresenter.h"
#include "MantidTestHelpers/WorkspaceCreationHelper.h"
#include "ProgressableViewMockObject.h"
#include "DataProcessorMockObjects.h"

using namespace MantidQt::CustomInterfaces;
using namespace Mantid::API;
using namespace Mantid::Kernel;
using namespace testing;

//=====================================================================================
// Functional tests
//=====================================================================================
class GenericDataProcessorPresenterTest : public CxxTest::TestSuite {

private:
  DataProcessorWhiteList createReflectometryWhiteList() {

    DataProcessorWhiteList whitelist;
    whitelist.addElement("Run(s)", "InputWorkspace");
    whitelist.addElement("Angle", "ThetaIn");
    whitelist.addElement("Transmission Run(s)", "FirstTransmissionRun");
    whitelist.addElement("Q min", "MomentumTransferMinimum");
    whitelist.addElement("Q max", "MomentumTransferMaximum");
    whitelist.addElement("dQ/Q", "MomentumTransferStep");
    whitelist.addElement("Scale", "ScaleFactor");
    return whitelist;
  }

  std::map<std::string, DataProcessorPreprocessingAlgorithm>
  createReflectometryPreprocessMap() {

    return std::map<std::string, DataProcessorPreprocessingAlgorithm>{
        {"Run(s)", DataProcessorPreprocessingAlgorithm()},
        {"Transmission Run(s)",
         DataProcessorPreprocessingAlgorithm(
             "CreateTransmissionWorkspaceAuto", "TRANS_",
             std::set<std::string>{"FirstTransmissionRun",
                                   "SecondTransmissionRun", "OutputWorkspace"},
             false)}};
  }

  DataProcessorProcessingAlgorithm createReflectometryProcessor() {

    return DataProcessorProcessingAlgorithm(
        "ReflectometryReductionOneAuto",
        std::vector<std::string>{"IvsQ_", "IvsLam_"},
        std::set<std::string>{"ThetaIn", "ThetaOut", "InputWorkspace",
                              "OutputWorkspace", "OutputWorkspaceWavelength",
                              "FirstTransmissionRun", "SecondTransmissionRun"});
  }

  DataProcessorPostprocessingAlgorithm createReflectometryPostprocessor() {

    return DataProcessorPostprocessingAlgorithm();
  }

  ITableWorkspace_sptr
  createWorkspace(const std::string &wsName,
                  const DataProcessorWhiteList &whitelist) {
    ITableWorkspace_sptr ws = WorkspaceFactory::Instance().createTable();

    const int ncols = static_cast<int>(whitelist.size());

    for (int col = 0; col < ncols - 2; col++) {
      auto column = ws->addColumn("str", whitelist.colNameFromColIndex(col));
      column->setPlotType(0);
    }
    auto colGroup =
        ws->addColumn("int", whitelist.colNameFromColIndex(GroupCol));
    auto colOptions =
        ws->addColumn("str", whitelist.colNameFromColIndex(OptionsCol));
    colGroup->setPlotType(0);
    colOptions->setPlotType(0);

    if (wsName.length() > 0)
      AnalysisDataService::Instance().addOrReplace(wsName, ws);

    return ws;
  }

  void createTOFWorkspace(const std::string &wsName,
                          const std::string &runNumber = "") {
    auto tinyWS =
        WorkspaceCreationHelper::create2DWorkspaceWithReflectometryInstrument();
    auto inst = tinyWS->getInstrument();

    inst->getParameterMap()->addDouble(inst.get(), "I0MonitorIndex", 1.0);
    inst->getParameterMap()->addDouble(inst.get(), "PointDetectorStart", 1.0);
    inst->getParameterMap()->addDouble(inst.get(), "PointDetectorStop", 1.0);
    inst->getParameterMap()->addDouble(inst.get(), "LambdaMin", 0.0);
    inst->getParameterMap()->addDouble(inst.get(), "LambdaMax", 10.0);
    inst->getParameterMap()->addDouble(inst.get(), "MonitorBackgroundMin", 0.0);
    inst->getParameterMap()->addDouble(inst.get(), "MonitorBackgroundMax",
                                       10.0);
    inst->getParameterMap()->addDouble(inst.get(), "MonitorIntegralMin", 0.0);
    inst->getParameterMap()->addDouble(inst.get(), "MonitorIntegralMax", 10.0);

    tinyWS->mutableRun().addLogData(
        new PropertyWithValue<double>("Theta", 0.12345));
    if (!runNumber.empty())
      tinyWS->mutableRun().addLogData(
          new PropertyWithValue<std::string>("run_number", runNumber));

    AnalysisDataService::Instance().addOrReplace(wsName, tinyWS);
  }

  ITableWorkspace_sptr
  createPrefilledWorkspace(const std::string &wsName,
                           const DataProcessorWhiteList &whitelist) {
    auto ws = createWorkspace(wsName, whitelist);
    TableRow row = ws->appendRow();
    row << "12345"
        << "0.5"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1" << 0 << "";
    row = ws->appendRow();
    row << "12346"
        << "1.5"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1" << 0 << "";
    row = ws->appendRow();
    row << "24681"
        << "0.5"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1" << 1 << "";
    row = ws->appendRow();
    row << "24682"
        << "1.5"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1" << 1 << "";
    return ws;
  }

public:
  // This pair of boilerplate methods prevent the suite being created
  // statically
  // This means the constructor isn't called when running other tests
  static GenericDataProcessorPresenterTest *createSuite() {
    return new GenericDataProcessorPresenterTest();
  }
  static void destroySuite(GenericDataProcessorPresenterTest *suite) {
    delete suite;
  }

  GenericDataProcessorPresenterTest() { FrameworkManager::Instance(); }

  void testConstructor() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    MockProgressableView mockProgress;

    // We don't the view we will handle yet, so none of the methods below should
    // be
    // called
    EXPECT_CALL(mockDataProcessorView, setTableList(_)).Times(0);
    EXPECT_CALL(mockDataProcessorView, setOptionsHintStrategy(_, _)).Times(0);
    // Constructor
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());

    // Verify expectations
    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));

    // Check that the presenter updates the whitelist adding columns 'Group' and
    // 'Options'
    auto whitelist = presenter.getWhiteList();
    TS_ASSERT_EQUALS(whitelist.size(), 9);
    TS_ASSERT_EQUALS(whitelist.colNameFromColIndex(7), "Group");
    TS_ASSERT_EQUALS(whitelist.colNameFromColIndex(8), "Options");
  }

  void testPresenterAcceptsViews() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    MockProgressableView mockProgress;

    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());

    // When the presenter accepts the views, expect the following:
    // Expect that the list of settings is populated
    EXPECT_CALL(mockDataProcessorView, loadSettings(_)).Times(Exactly(1));
    // Expect that the list of tables is populated
    EXPECT_CALL(mockDataProcessorView, setTableList(_)).Times(Exactly(1));
    // Expect that the layout containing pre-processing, processing and
    // post-processing options is created
    std::vector<std::string> stages = {"Pre-process", "Pre-process", "Process",
                                       "Post-process"};
    std::vector<std::string> algorithms = {
        "Plus", "CreateTransmissionWorkspaceAuto",
        "ReflectometryReductionOneAuto", "Stitch1DMany"};

    EXPECT_CALL(mockDataProcessorView, setGlobalOptions(stages, algorithms, _))
        .Times(Exactly(1));
    // Expect that the autocompletion hints are populated
    EXPECT_CALL(mockDataProcessorView, setOptionsHintStrategy(_, 8))
        .Times(Exactly(1));
    // Now accept the views
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    // Verify expectations
    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testSaveNew() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;

    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    presenter.notify(DataProcessorPresenter::NewTableFlag);

    EXPECT_CALL(mockDataProcessorView, askUserString(_, _, "Workspace"))
        .Times(1)
        .WillOnce(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::SaveFlag);

    TS_ASSERT(AnalysisDataService::Instance().doesExist("TestWorkspace"));
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testSaveExisting() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    EXPECT_CALL(mockDataProcessorView, askUserString(_, _, "Workspace"))
        .Times(0);
    presenter.notify(DataProcessorPresenter::SaveFlag);

    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testSaveAs() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    // The user hits "save as" but cancels when choosing a name
    EXPECT_CALL(mockDataProcessorView, askUserString(_, _, "Workspace"))
        .Times(1)
        .WillOnce(Return(""));
    presenter.notify(DataProcessorPresenter::SaveAsFlag);

    // The user hits "save as" and and enters "Workspace" for a name
    EXPECT_CALL(mockDataProcessorView, askUserString(_, _, "Workspace"))
        .Times(1)
        .WillOnce(Return("Workspace"));
    presenter.notify(DataProcessorPresenter::SaveAsFlag);

    TS_ASSERT(AnalysisDataService::Instance().doesExist("Workspace"));

    AnalysisDataService::Instance().remove("TestWorkspace");
    AnalysisDataService::Instance().remove("Workspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testAppendRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "append row" twice with no rows selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    presenter.notify(DataProcessorPresenter::AppendRowFlag);
    presenter.notify(DataProcessorPresenter::AppendRowFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // Check that the table has been modified correctly
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 6);
    TS_ASSERT_EQUALS(ws->String(4, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(5, RunCol), "");
    TS_ASSERT_EQUALS(ws->Int(0, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(2, GroupCol), 1);
    TS_ASSERT_EQUALS(ws->Int(3, GroupCol), 1);
    TS_ASSERT_EQUALS(ws->Int(4, GroupCol), 2);
    TS_ASSERT_EQUALS(ws->Int(5, GroupCol), 3);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testAppendRowSpecify() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "append row" twice, with the second row selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::AppendRowFlag);
    presenter.notify(DataProcessorPresenter::AppendRowFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // Check that the table has been modified correctly
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 6);
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "");
    TS_ASSERT_EQUALS(ws->Int(0, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(2, GroupCol), 3);
    TS_ASSERT_EQUALS(ws->Int(3, GroupCol), 2);
    TS_ASSERT_EQUALS(ws->Int(4, GroupCol), 1);
    TS_ASSERT_EQUALS(ws->Int(5, GroupCol), 1);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testAppendRowSpecifyPlural() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);
    rowlist.insert(2);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "append row" once, with the second, third, and fourth row
    // selected.
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::AppendRowFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // Check that the table was modified correctly
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 5);
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "");
    TS_ASSERT_EQUALS(ws->Int(0, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(2, GroupCol), 1);
    TS_ASSERT_EQUALS(ws->Int(3, GroupCol), 2);
    TS_ASSERT_EQUALS(ws->Int(4, GroupCol), 1);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPrependRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "prepend row" twice with no rows selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    presenter.notify(DataProcessorPresenter::PrependRowFlag);
    presenter.notify(DataProcessorPresenter::PrependRowFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // Check that the table has been modified correctly
    ITableWorkspace_sptr ws =
        AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
            "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 6);
    TS_ASSERT_EQUALS(ws->Int(0, GroupCol), 3);
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 2);
    TS_ASSERT_EQUALS(ws->Int(2, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(3, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(4, GroupCol), 1);
    TS_ASSERT_EQUALS(ws->Int(5, GroupCol), 1);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPrependRowSpecify() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "prepend row" twice, with the second row selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::PrependRowFlag);
    presenter.notify(DataProcessorPresenter::PrependRowFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // Check that the table has been modified correctly
    ITableWorkspace_sptr ws =
        AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
            "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 6);
    TS_ASSERT_EQUALS(ws->Int(0, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 3);
    TS_ASSERT_EQUALS(ws->Int(2, GroupCol), 2);
    TS_ASSERT_EQUALS(ws->Int(3, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(4, GroupCol), 1);
    TS_ASSERT_EQUALS(ws->Int(5, GroupCol), 1);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPrependRowSpecifyPlural() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);
    rowlist.insert(2);
    rowlist.insert(3);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "prepend row" once, with the second, third, and fourth row
    // selected.
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::PrependRowFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // Check that the table was modified correctly
    ITableWorkspace_sptr ws =
        AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
            "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 5);
    TS_ASSERT_EQUALS(ws->Int(0, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 2);
    TS_ASSERT_EQUALS(ws->Int(2, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(3, GroupCol), 1);
    TS_ASSERT_EQUALS(ws->Int(4, GroupCol), 1);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testDeleteRowNone() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "delete row" with no rows selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    presenter.notify(DataProcessorPresenter::DeleteRowFlag);

    // The user hits save
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // Check that the table has not lost any rows
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testDeleteRowSingle() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "delete row" with the second row selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::DeleteRowFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 3);
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 1);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testDeleteRowPlural() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(0);
    rowlist.insert(1);
    rowlist.insert(2);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "delete row" with the first three rows selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::DeleteRowFlag);

    // The user hits save
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // Check the rows were deleted as expected
    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 1);
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "24682");
    TS_ASSERT_EQUALS(ws->Int(0, GroupCol), 1);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testProcess() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(0);
    rowlist.insert(1);

    createTOFWorkspace("TOF_12345", "12345");
    createTOFWorkspace("TOF_12346", "12346");

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first two rows selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(mockDataProcessorView, getProcessingOptions("Plus"))
        .Times(2)
        .WillRepeatedly(Return(""));
    EXPECT_CALL(mockDataProcessorView,
                getProcessingOptions("CreateTransmissionWorkspaceAuto"))
        .Times(0);
    EXPECT_CALL(mockDataProcessorView,
                getProcessingOptions("ReflectometryReductionOneAuto"))
        .Times(2)
        .WillRepeatedly(Return(""));
    EXPECT_CALL(mockDataProcessorView, getProcessingOptions("Stitch1DMany"))
        .Times(1)
        .WillOnce(Return("Params = \"0.1\""));
    EXPECT_CALL(mockDataProcessorView, getEnableNotebook())
        .Times(1)
        .WillRepeatedly(Return(false));
    EXPECT_CALL(mockDataProcessorView, requestNotebookPath()).Times(0);

    presenter.notify(DataProcessorPresenter::ProcessFlag);

    // Check output workspaces were created as expected
    TS_ASSERT(AnalysisDataService::Instance().doesExist("IvsQ_TOF_12345"));
    TS_ASSERT(AnalysisDataService::Instance().doesExist("IvsLam_TOF_12345"));
    TS_ASSERT(AnalysisDataService::Instance().doesExist("TOF_12345"));
    TS_ASSERT(AnalysisDataService::Instance().doesExist("IvsQ_TOF_12346"));
    TS_ASSERT(AnalysisDataService::Instance().doesExist("IvsLam_TOF_12346"));
    TS_ASSERT(AnalysisDataService::Instance().doesExist("TOF_12346"));
    TS_ASSERT(
        AnalysisDataService::Instance().doesExist("IvsQ_TOF_12345_TOF_12346"));

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");
    AnalysisDataService::Instance().remove("IvsQ_TOF_12345");
    AnalysisDataService::Instance().remove("IvsLam_TOF_12345");
    AnalysisDataService::Instance().remove("TOF_12345");
    AnalysisDataService::Instance().remove("IvsQ_TOF_12346");
    AnalysisDataService::Instance().remove("IvsLam_TOF_12346");
    AnalysisDataService::Instance().remove("TOF_12346");
    AnalysisDataService::Instance().remove("IvsQ_TOF_12345_TOF_12346");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testProcessWithNotebook() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(0);
    rowlist.insert(1);

    createTOFWorkspace("TOF_12345", "12345");
    createTOFWorkspace("TOF_12346", "12346");

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first two rows selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(mockDataProcessorView, getProcessingOptions("Plus"))
        .Times(2)
        .WillRepeatedly(Return(""));
    EXPECT_CALL(mockDataProcessorView,
                getProcessingOptions("CreateTransmissionWorkspaceAuto"))
        .Times(0);
    EXPECT_CALL(mockDataProcessorView,
                getProcessingOptions("ReflectometryReductionOneAuto"))
        .Times(2)
        .WillRepeatedly(Return(""));
    EXPECT_CALL(mockDataProcessorView, getProcessingOptions("Stitch1DMany"))
        .Times(1)
        .WillOnce(Return("Params = \"0.1\""));
    EXPECT_CALL(mockDataProcessorView, getEnableNotebook())
        .Times(1)
        .WillRepeatedly(Return(true));
    EXPECT_CALL(mockDataProcessorView, requestNotebookPath()).Times(1);
    presenter.notify(DataProcessorPresenter::ProcessFlag);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");
    AnalysisDataService::Instance().remove("IvsQ_TOF_12345");
    AnalysisDataService::Instance().remove("IvsLam_TOF_12345");
    AnalysisDataService::Instance().remove("TOF_12345");
    AnalysisDataService::Instance().remove("IvsQ_TOF_12346");
    AnalysisDataService::Instance().remove("IvsLam_TOF_12346");
    AnalysisDataService::Instance().remove("TOF_12346");
    AnalysisDataService::Instance().remove("IvsQ_TOF_12345_TOF_12346");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  /*
   * Test processing workspaces with non-standard names, with
   * and without run_number information in the sample log.
   */
  void testProcessCustomNames() {

    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    auto ws = createWorkspace("TestWorkspace", presenter.getWhiteList());
    TableRow row = ws->appendRow();
    row << "dataA"
        << "0.7"
        << ""
        << "0.1"
        << "1.6"
        << "0.04"
        << "1" << 1;
    row = ws->appendRow();
    row << "dataB"
        << "2.3"
        << ""
        << "1.4"
        << "2.9"
        << "0.04"
        << "1" << 1;

    createTOFWorkspace("dataA");
    createTOFWorkspace("dataB");

    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(0);
    rowlist.insert(1);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits the "process" button with the first two rows selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(mockDataProcessorView, getProcessingOptions("Plus"))
        .Times(2)
        .WillRepeatedly(Return(""));
    EXPECT_CALL(mockDataProcessorView,
                getProcessingOptions("CreateTransmissionWorkspaceAuto"))
        .Times(0);
    EXPECT_CALL(mockDataProcessorView,
                getProcessingOptions("ReflectometryReductionOneAuto"))
        .Times(2)
        .WillRepeatedly(Return(""));
    EXPECT_CALL(mockDataProcessorView, getProcessingOptions("Stitch1DMany"))
        .Times(1)
        .WillOnce(Return("Params = \"0.1\""));

    presenter.notify(DataProcessorPresenter::ProcessFlag);

    // Check output workspaces were created as expected
    TS_ASSERT(AnalysisDataService::Instance().doesExist("IvsQ_TOF_dataA"));
    TS_ASSERT(AnalysisDataService::Instance().doesExist("IvsQ_TOF_dataB"));
    TS_ASSERT(AnalysisDataService::Instance().doesExist("IvsLam_TOF_dataA"));
    TS_ASSERT(AnalysisDataService::Instance().doesExist("IvsLam_TOF_dataB"));
    TS_ASSERT(
        AnalysisDataService::Instance().doesExist("IvsQ_TOF_dataA_TOF_dataB"));

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");
    AnalysisDataService::Instance().remove("dataA");
    AnalysisDataService::Instance().remove("dataB");
    AnalysisDataService::Instance().remove("IvsQ_TOF_dataA");
    AnalysisDataService::Instance().remove("IvsQ_TOF_dataB");
    AnalysisDataService::Instance().remove("IvsLam_TOF_dataA");
    AnalysisDataService::Instance().remove("IvsLam_TOF_dataB");
    AnalysisDataService::Instance().remove("IvsQ_TOF_dataA_TOF_dataB");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testBadWorkspaceType() {
    ITableWorkspace_sptr ws = WorkspaceFactory::Instance().createTable();

    // Wrong types
    ws->addColumn("str", "Run(s)");
    ws->addColumn("str", "ThetaIn");
    ws->addColumn("str", "TransRun(s)");
    ws->addColumn("str", "Qmin");
    ws->addColumn("str", "Qmax");
    ws->addColumn("str", "dq/q");
    ws->addColumn("str", "Scale");
    ws->addColumn("str", "StitchGroup");
    ws->addColumn("str", "Options");

    AnalysisDataService::Instance().addOrReplace("TestWorkspace", ws);

    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    // We should receive an error
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(1);

    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testBadWorkspaceLength() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    // Because we to open twice, get an error twice
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(2);
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(2)
        .WillRepeatedly(Return("TestWorkspace"));

    ITableWorkspace_sptr ws = WorkspaceFactory::Instance().createTable();
    ws->addColumn("str", "Run(s)");
    ws->addColumn("str", "ThetaIn");
    ws->addColumn("str", "TransRun(s)");
    ws->addColumn("str", "Qmin");
    ws->addColumn("str", "Qmax");
    ws->addColumn("str", "dq/q");
    ws->addColumn("double", "Scale");
    ws->addColumn("int", "StitchGroup");
    AnalysisDataService::Instance().addOrReplace("TestWorkspace", ws);

    // Try to open with too few columns
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    ws->addColumn("str", "OptionsA");
    ws->addColumn("str", "OptionsB");
    AnalysisDataService::Instance().addOrReplace("TestWorkspace", ws);

    // Try to open with too many columns
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPromptSaveAfterAppendRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    // User hits "append row"
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    presenter.notify(DataProcessorPresenter::AppendRowFlag);

    // The user will decide not to discard their changes
    EXPECT_CALL(mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(false));

    // Then hits "new table" without having saved
    presenter.notify(DataProcessorPresenter::NewTableFlag);

    // The user saves
    EXPECT_CALL(mockDataProcessorView, askUserString(_, _, "Workspace"))
        .Times(1)
        .WillOnce(Return("Workspace"));
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // The user tries to create a new table again, and does not get bothered
    EXPECT_CALL(mockDataProcessorView, askUserYesNo(_, _)).Times(0);
    presenter.notify(DataProcessorPresenter::NewTableFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPromptSaveAfterDeleteRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    // User hits "append row" a couple of times
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    presenter.notify(DataProcessorPresenter::AppendRowFlag);
    presenter.notify(DataProcessorPresenter::AppendRowFlag);

    // The user saves
    EXPECT_CALL(mockDataProcessorView, askUserString(_, _, "Workspace"))
        .Times(1)
        .WillOnce(Return("Workspace"));
    presenter.notify(DataProcessorPresenter::SaveFlag);

    //...then deletes the 2nd row
    std::set<int> rows;
    rows.insert(1);
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rows));
    presenter.notify(DataProcessorPresenter::DeleteRowFlag);

    // The user will decide not to discard their changes when asked
    EXPECT_CALL(mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(false));

    // Then hits "new table" without having saved
    presenter.notify(DataProcessorPresenter::NewTableFlag);

    // The user saves
    presenter.notify(DataProcessorPresenter::SaveFlag);

    // The user tries to create a new table again, and does not get bothered
    EXPECT_CALL(mockDataProcessorView, askUserYesNo(_, _)).Times(0);
    presenter.notify(DataProcessorPresenter::NewTableFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPromptSaveAndDiscard() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    // User hits "append row" a couple of times
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(std::set<int>()));
    presenter.notify(DataProcessorPresenter::AppendRowFlag);
    presenter.notify(DataProcessorPresenter::AppendRowFlag);

    // Then hits "new table", and decides to discard
    EXPECT_CALL(mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(true));
    presenter.notify(DataProcessorPresenter::NewTableFlag);

    // These next two times they don't get prompted - they have a new table
    presenter.notify(DataProcessorPresenter::NewTableFlag);
    presenter.notify(DataProcessorPresenter::NewTableFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPromptSaveOnOpen() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());

    // User hits "append row"
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    presenter.notify(DataProcessorPresenter::AppendRowFlag);

    // and tries to open a workspace, but gets prompted and decides not to
    // discard
    EXPECT_CALL(mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(false));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    // the user does it again, but discards
    EXPECT_CALL(mockDataProcessorView, askUserYesNo(_, _))
        .Times(1)
        .WillOnce(Return(true));
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    // the user does it one more time, and is not prompted
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    EXPECT_CALL(mockDataProcessorView, askUserYesNo(_, _)).Times(0);
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testExpandSelection() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    auto ws = createWorkspace("TestWorkspace", presenter.getWhiteList());
    TableRow row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 0 << ""; // Row 0
    row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 1 << ""; // Row 1
    row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 1 << ""; // Row 2
    row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 2 << ""; // Row 3
    row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 2 << ""; // Row 4
    row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 2 << ""; // Row 5
    row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 3 << ""; // Row 6
    row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 4 << ""; // Row 7
    row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 4 << ""; // Row 8
    row = ws->appendRow();
    row << ""
        << ""
        << ""
        << ""
        << ""
        << ""
        << "1" << 5 << ""; // Row 9

    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    std::set<int> selection;
    std::set<int> expected;

    selection.insert(0);
    expected.insert(0);

    // With row 0 selected, we shouldn't expand at all
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(mockDataProcessorView, setSelection(ContainerEq(expected)))
        .Times(1);
    presenter.notify(DataProcessorPresenter::ExpandSelectionFlag);

    // With 0,1 selected, we should finish with 0,1,2 selected
    selection.clear();
    selection.insert(0);
    selection.insert(1);

    expected.clear();
    expected.insert(0);
    expected.insert(1);
    expected.insert(2);

    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(mockDataProcessorView, setSelection(ContainerEq(expected)))
        .Times(1);
    presenter.notify(DataProcessorPresenter::ExpandSelectionFlag);

    // With 1,6 selected, we should finish with 1,2,6 selected
    selection.clear();
    selection.insert(1);
    selection.insert(6);

    expected.clear();
    expected.insert(1);
    expected.insert(2);
    expected.insert(6);

    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(mockDataProcessorView, setSelection(ContainerEq(expected)))
        .Times(1);
    presenter.notify(DataProcessorPresenter::ExpandSelectionFlag);

    // With 4,8 selected, we should finish with 3,4,5,7,8 selected
    selection.clear();
    selection.insert(4);
    selection.insert(8);

    expected.clear();
    expected.insert(3);
    expected.insert(4);
    expected.insert(5);
    expected.insert(7);
    expected.insert(8);

    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(mockDataProcessorView, setSelection(ContainerEq(expected)))
        .Times(1);
    presenter.notify(DataProcessorPresenter::ExpandSelectionFlag);

    // With nothing selected, we should finish with nothing selected
    selection.clear();
    expected.clear();

    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(selection));
    EXPECT_CALL(mockDataProcessorView, setSelection(ContainerEq(expected)))
        .Times(1);
    presenter.notify(DataProcessorPresenter::ExpandSelectionFlag);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testClearRows() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);
    rowlist.insert(2);

    // We should not receive any errors
    EXPECT_CALL(mockDataProcessorView, giveUserCritical(_, _)).Times(0);

    // The user hits "clear selected" with the second and third rows selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::ClearSelectedFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the group ids have been set correctly
    TS_ASSERT_EQUALS(ws->Int(0, GroupCol), 0);
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 2);
    TS_ASSERT_EQUALS(ws->Int(2, GroupCol), 3);
    TS_ASSERT_EQUALS(ws->Int(3, GroupCol), 1);

    // Make sure the selected rows are clear
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "");
    TS_ASSERT_EQUALS(ws->String(1, ThetaCol), "");
    TS_ASSERT_EQUALS(ws->String(2, ThetaCol), "");
    TS_ASSERT_EQUALS(ws->String(1, TransCol), "");
    TS_ASSERT_EQUALS(ws->String(2, TransCol), "");
    TS_ASSERT_EQUALS(ws->String(1, QMinCol), "");
    TS_ASSERT_EQUALS(ws->String(2, QMinCol), "");
    TS_ASSERT_EQUALS(ws->String(1, QMaxCol), "");
    TS_ASSERT_EQUALS(ws->String(2, QMaxCol), "");
    TS_ASSERT_EQUALS(ws->String(1, DQQCol), "");
    TS_ASSERT_EQUALS(ws->String(2, DQQCol), "");
    TS_ASSERT_EQUALS(ws->String(1, ScaleCol), "");
    TS_ASSERT_EQUALS(ws->String(2, ScaleCol), "");

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testCopyRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);

    const std::string expected = "12346\t1.5\t\t1.4\t2.9\t0.04\t1\t0\t";

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(mockDataProcessorView, setClipboard(expected));
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::CopySelectedFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testCopyRows() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(0);
    rowlist.insert(1);
    rowlist.insert(2);
    rowlist.insert(3);

    const std::string expected = "12345\t0.5\t\t0.1\t1.6\t0.04\t1\t0\t\n"
                                 "12346\t1.5\t\t1.4\t2.9\t0.04\t1\t0\t\n"
                                 "24681\t0.5\t\t0.1\t1.6\t0.04\t1\t1\t\n"
                                 "24682\t1.5\t\t1.4\t2.9\t0.04\t1\t1\t";

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(mockDataProcessorView, setClipboard(expected));
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::CopySelectedFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testCutRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);

    const std::string expected = "12346\t1.5\t\t1.4\t2.9\t0.04\t1\t0\t";

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(mockDataProcessorView, setClipboard(expected));
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::CutSelectedFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 3);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24682");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testCutRows() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(0);
    rowlist.insert(1);
    rowlist.insert(2);

    const std::string expected = "12345\t0.5\t\t0.1\t1.6\t0.04\t1\t0\t\n"
                                 "12346\t1.5\t\t1.4\t2.9\t0.04\t1\t0\t\n"
                                 "24681\t0.5\t\t0.1\t1.6\t0.04\t1\t1\t";

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(mockDataProcessorView, setClipboard(expected));
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::CutSelectedFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 1);
    // Check the only unselected row is left behind
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "24682");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPasteRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);

    const std::string clipboard = "123\t0.5\t456\t1.2\t3.4\t3.14\t5\t6\tabc";

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return(clipboard));
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::PasteSelectedFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the values were pasted correctly
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "123");
    TS_ASSERT_EQUALS(ws->String(1, ThetaCol), "0.5");
    TS_ASSERT_EQUALS(ws->String(1, TransCol), "456");
    TS_ASSERT_EQUALS(ws->String(1, QMinCol), "1.2");
    TS_ASSERT_EQUALS(ws->String(1, QMaxCol), "3.4");
    TS_ASSERT_EQUALS(ws->String(1, DQQCol), "3.14");
    TS_ASSERT_EQUALS(ws->String(1, ScaleCol), "5");
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 6);
    TS_ASSERT_EQUALS(ws->String(1, OptionsCol), "abc");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPasteNewRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    const std::string clipboard = "123\t0.5\t456\t1.2\t3.4\t3.14\t5\t6\tabc";

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return(clipboard));
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    presenter.notify(DataProcessorPresenter::PasteSelectedFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 5);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "12346");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the values were pasted correctly
    TS_ASSERT_EQUALS(ws->String(4, RunCol), "123");
    TS_ASSERT_EQUALS(ws->String(4, ThetaCol), "0.5");
    TS_ASSERT_EQUALS(ws->String(4, TransCol), "456");
    TS_ASSERT_EQUALS(ws->String(4, QMinCol), "1.2");
    TS_ASSERT_EQUALS(ws->String(4, QMaxCol), "3.4");
    TS_ASSERT_EQUALS(ws->String(4, DQQCol), "3.14");
    TS_ASSERT_EQUALS(ws->String(4, ScaleCol), "5");
    TS_ASSERT_EQUALS(ws->Int(4, GroupCol), 6);
    TS_ASSERT_EQUALS(ws->String(4, OptionsCol), "abc");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPasteRows() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(1);
    rowlist.insert(2);

    const std::string clipboard = "123\t0.5\t456\t1.2\t3.4\t3.14\t5\t6\tabc\n"
                                  "345\t2.7\t123\t2.1\t4.3\t2.17\t3\t2\tdef";

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return(clipboard));
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::PasteSelectedFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 4);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the values were pasted correctly
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "123");
    TS_ASSERT_EQUALS(ws->String(1, ThetaCol), "0.5");
    TS_ASSERT_EQUALS(ws->String(1, TransCol), "456");
    TS_ASSERT_EQUALS(ws->String(1, QMinCol), "1.2");
    TS_ASSERT_EQUALS(ws->String(1, QMaxCol), "3.4");
    TS_ASSERT_EQUALS(ws->String(1, DQQCol), "3.14");
    TS_ASSERT_EQUALS(ws->String(1, ScaleCol), "5");
    TS_ASSERT_EQUALS(ws->Int(1, GroupCol), 6);
    TS_ASSERT_EQUALS(ws->String(1, OptionsCol), "abc");

    TS_ASSERT_EQUALS(ws->String(2, RunCol), "345");
    TS_ASSERT_EQUALS(ws->String(2, ThetaCol), "2.7");
    TS_ASSERT_EQUALS(ws->String(2, TransCol), "123");
    TS_ASSERT_EQUALS(ws->String(2, QMinCol), "2.1");
    TS_ASSERT_EQUALS(ws->String(2, QMaxCol), "4.3");
    TS_ASSERT_EQUALS(ws->String(2, DQQCol), "2.17");
    TS_ASSERT_EQUALS(ws->String(2, ScaleCol), "3");
    TS_ASSERT_EQUALS(ws->Int(2, GroupCol), 2);
    TS_ASSERT_EQUALS(ws->String(2, OptionsCol), "def");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPasteNewRows() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    const std::string clipboard = "123\t0.5\t456\t1.2\t3.4\t3.14\t5\t6\tabc\n"
                                  "345\t2.7\t123\t2.1\t4.3\t2.17\t3\t2\tdef";

    // The user hits "copy selected" with the second and third rows selected
    EXPECT_CALL(mockDataProcessorView, getClipboard())
        .Times(1)
        .WillRepeatedly(Return(clipboard));
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(std::set<int>()));
    presenter.notify(DataProcessorPresenter::PasteSelectedFlag);

    // The user hits "save"
    presenter.notify(DataProcessorPresenter::SaveFlag);

    auto ws = AnalysisDataService::Instance().retrieveWS<ITableWorkspace>(
        "TestWorkspace");
    TS_ASSERT_EQUALS(ws->rowCount(), 6);
    // Check the unselected rows were unaffected
    TS_ASSERT_EQUALS(ws->String(0, RunCol), "12345");
    TS_ASSERT_EQUALS(ws->String(1, RunCol), "12346");
    TS_ASSERT_EQUALS(ws->String(2, RunCol), "24681");
    TS_ASSERT_EQUALS(ws->String(3, RunCol), "24682");

    // Check the values were pasted correctly
    TS_ASSERT_EQUALS(ws->String(4, RunCol), "123");
    TS_ASSERT_EQUALS(ws->String(4, ThetaCol), "0.5");
    TS_ASSERT_EQUALS(ws->String(4, TransCol), "456");
    TS_ASSERT_EQUALS(ws->String(4, QMinCol), "1.2");
    TS_ASSERT_EQUALS(ws->String(4, QMaxCol), "3.4");
    TS_ASSERT_EQUALS(ws->String(4, DQQCol), "3.14");
    TS_ASSERT_EQUALS(ws->String(4, ScaleCol), "5");
    TS_ASSERT_EQUALS(ws->Int(4, GroupCol), 6);
    TS_ASSERT_EQUALS(ws->String(4, OptionsCol), "abc");

    TS_ASSERT_EQUALS(ws->String(5, RunCol), "345");
    TS_ASSERT_EQUALS(ws->String(5, ThetaCol), "2.7");
    TS_ASSERT_EQUALS(ws->String(5, TransCol), "123");
    TS_ASSERT_EQUALS(ws->String(5, QMinCol), "2.1");
    TS_ASSERT_EQUALS(ws->String(5, QMaxCol), "4.3");
    TS_ASSERT_EQUALS(ws->String(5, DQQCol), "2.17");
    TS_ASSERT_EQUALS(ws->String(5, ScaleCol), "3");
    TS_ASSERT_EQUALS(ws->Int(5, GroupCol), 2);
    TS_ASSERT_EQUALS(ws->String(5, OptionsCol), "def");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testImportTable() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    NiceMock<MockProgressableView> mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);
    EXPECT_CALL(mockDataProcessorView, showImportDialog());
    presenter.notify(DataProcessorPresenter::ImportTableFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testExportTable() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    MockProgressableView mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);
    EXPECT_CALL(mockDataProcessorView, showAlgorithmDialog("SaveTBL"));
    presenter.notify(DataProcessorPresenter::ExportTableFlag);

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPlotRowWarn() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    MockProgressableView mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    createTOFWorkspace("TOF_12345", "12345");
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));

    // We should be warned
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(0);

    // We should be warned
    EXPECT_CALL(mockDataProcessorView, giveUserWarning(_, _));
    // The presenter calls plotWorkspaces
    EXPECT_CALL(mockDataProcessorView, plotWorkspaces(_)).Times(1);
    // The user hits "plot rows" with the first row selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::PlotRowFlag);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");
    AnalysisDataService::Instance().remove("TOF_12345");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }
  void testPlotEmptyRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    MockProgressableView mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);
    std::set<int> rowlist;
    rowlist.insert(0);
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(rowlist));
    EXPECT_CALL(mockDataProcessorView, giveUserWarning(_, _));
    // Append an empty row to our table
    presenter.notify(DataProcessorPresenter::AppendRowFlag);
    // Attempt to plot the empty row (should result in critical warning)
    presenter.notify(DataProcessorPresenter::PlotRowFlag);
    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }
  void testPlotGroupWithEmptyRow() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    MockProgressableView mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    createTOFWorkspace("TOF_12345", "12345");
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    std::set<int> rowList;
    rowList.insert(0);
    rowList.insert(1);
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(2)
        .WillRepeatedly(Return(rowList));
    EXPECT_CALL(mockDataProcessorView, giveUserWarning(_, _));
    // Open up our table with one row
    presenter.notify(DataProcessorPresenter::OpenTableFlag);
    // Append an empty row to the table
    presenter.notify(DataProcessorPresenter::AppendRowFlag);
    // Attempt to plot the group (should result in critical warning)
    presenter.notify(DataProcessorPresenter::PlotGroupFlag);
    AnalysisDataService::Instance().remove("TestWorkspace");
    AnalysisDataService::Instance().remove("TOF_12345");
    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPlotGroupWarn() {
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    MockProgressableView mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    createPrefilledWorkspace("TestWorkspace", presenter.getWhiteList());
    createTOFWorkspace("TOF_12345", "12345");
    createTOFWorkspace("TOF_12346", "12346");
    EXPECT_CALL(mockDataProcessorView, getWorkspaceToOpen())
        .Times(1)
        .WillRepeatedly(Return("TestWorkspace"));
    presenter.notify(DataProcessorPresenter::OpenTableFlag);

    std::set<int> rowlist;
    rowlist.insert(0);

    // We should be warned
    EXPECT_CALL(mockDataProcessorView, giveUserWarning(_, _));
    // the presenter calls plotWorkspaces
    EXPECT_CALL(mockDataProcessorView, plotWorkspaces(_));
    // The user hits "plot groups" with the first row selected
    EXPECT_CALL(mockDataProcessorView, getSelectedRows())
        .Times(1)
        .WillRepeatedly(Return(rowlist));
    presenter.notify(DataProcessorPresenter::PlotGroupFlag);

    // Tidy up
    AnalysisDataService::Instance().remove("TestWorkspace");
    AnalysisDataService::Instance().remove("TOF_12345");
    AnalysisDataService::Instance().remove("TOF_12346");

    TS_ASSERT(Mock::VerifyAndClearExpectations(&mockDataProcessorView));
  }

  void testPublishCommands() {
    // The mock view is not needed for this test
    // We just want to test the list of commands returned by the presenter
    NiceMock<MockDataProcessorView> mockDataProcessorView;
    MockProgressableView mockProgress;
    GenericDataProcessorPresenter presenter(
        createReflectometryWhiteList(), createReflectometryPreprocessMap(),
        createReflectometryProcessor(), createReflectometryPostprocessor());
    presenter.acceptViews(&mockDataProcessorView, &mockProgress);

    // Actions (commands)
    auto commands = presenter.publishCommands();
    TS_ASSERT_EQUALS(commands.size(), 26);

    TS_ASSERT(dynamic_cast<DataProcessorOpenTableCommand *>(commands[0].get()));
    TS_ASSERT(dynamic_cast<DataProcessorNewTableCommand *>(commands[1].get()));
    TS_ASSERT(dynamic_cast<DataProcessorSaveTableCommand *>(commands[2].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorSaveTableAsCommand *>(commands[3].get()));
    TS_ASSERT(dynamic_cast<DataProcessorSeparatorCommand *>(commands[4].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorImportTableCommand *>(commands[5].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorExportTableCommand *>(commands[6].get()));
    TS_ASSERT(dynamic_cast<DataProcessorSeparatorCommand *>(commands[7].get()));
    TS_ASSERT(dynamic_cast<DataProcessorOptionsCommand *>(commands[8].get()));
    TS_ASSERT(dynamic_cast<DataProcessorSeparatorCommand *>(commands[9].get()));
    TS_ASSERT(dynamic_cast<DataProcessorProcessCommand *>(commands[10].get()));
    TS_ASSERT(dynamic_cast<DataProcessorExpandCommand *>(commands[11].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorSeparatorCommand *>(commands[12].get()));
    TS_ASSERT(dynamic_cast<DataProcessorPlotRowCommand *>(commands[13].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorPlotGroupCommand *>(commands[14].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorSeparatorCommand *>(commands[15].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorAppendRowCommand *>(commands[16].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorPrependRowCommand *>(commands[17].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorSeparatorCommand *>(commands[18].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorGroupRowsCommand *>(commands[19].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorCopySelectedCommand *>(commands[20].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorCutSelectedCommand *>(commands[21].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorPasteSelectedCommand *>(commands[22].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorClearSelectedCommand *>(commands[23].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorSeparatorCommand *>(commands[24].get()));
    TS_ASSERT(
        dynamic_cast<DataProcessorDeleteRowCommand *>(commands[25].get()));
  }
};

#endif /* MANTID_CUSTOMINTERFACES_GENERICDATAPROCESSORPRESENTERTEST_H */

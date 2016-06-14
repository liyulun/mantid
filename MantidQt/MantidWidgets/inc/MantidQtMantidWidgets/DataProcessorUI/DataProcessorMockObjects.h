#ifndef MANTID_MANTIDWIDGETS_DATAPROCESSORVIEWMOCKOBJECTS_H
#define MANTID_MANTIDWIDGETS_DATAPROCESSORVIEWMOCKOBJECTS_H

#include "MantidKernel/make_unique.h"
#include "MantidQtMantidWidgets/DataProcessorUI/DataProcessorAppendRowCommand.h"
#include "MantidQtMantidWidgets/DataProcessorUI/DataProcessorView.h"
#include "MantidQtMantidWidgets/DataProcessorUI/QDataProcessorTableModel.h"
#include <gmock/gmock.h>

using namespace MantidQt::MantidWidgets;
using namespace Mantid::API;

// Clean column ids for use within tests
const int RunCol = 0;
const int ThetaCol = 1;
const int TransCol = 2;
const int QMinCol = 3;
const int QMaxCol = 4;
const int DQQCol = 5;
const int ScaleCol = 6;
const int GroupCol = 7;
const int OptionsCol = 8;

class MockDataProcessorView : public DataProcessorView {
public:
  MockDataProcessorView(){};
  ~MockDataProcessorView() override {}

  // Prompts
  MOCK_METHOD3(askUserString,
               std::string(const std::string &prompt, const std::string &title,
                           const std::string &defaultValue));
  MOCK_METHOD2(askUserYesNo, bool(std::string, std::string));
  MOCK_METHOD2(giveUserCritical, void(std::string, std::string));
  MOCK_METHOD2(giveUserWarning, void(std::string, std::string));
  MOCK_METHOD0(requestNotebookPath, std::string());
  MOCK_METHOD0(showImportDialog, void());
  MOCK_METHOD1(showAlgorithmDialog, void(const std::string &));

  MOCK_METHOD1(plotWorkspaces, void(const std::set<std::string> &));

  // IO
  MOCK_CONST_METHOD0(getWorkspaceToOpen, std::string());
  MOCK_CONST_METHOD0(getSelectedRows, std::set<int>());
  MOCK_CONST_METHOD0(getClipboard, std::string());
  MOCK_CONST_METHOD1(getProcessingOptions, std::string(const std::string &));
  MOCK_METHOD0(getEnableNotebook, bool());
  MOCK_METHOD1(setSelection, void(const std::set<int> &rows));
  MOCK_METHOD1(setClipboard, void(const std::string &text));

  MOCK_METHOD1(setModel, void(const std::string &));
  MOCK_METHOD1(setTableList, void(const std::set<std::string> &));
  MOCK_METHOD2(setInstrumentList,
               void(const std::vector<std::string> &, const std::string &));
  MOCK_METHOD2(setOptionsHintStrategy,
               void(MantidQt::MantidWidgets::HintStrategy *, int));
  MOCK_METHOD3(
      setGlobalOptions,
      void(const std::vector<std::string> &stages,
           const std::vector<std::string> &algNames,
           const std::vector<std::map<std::string, std::string>> &hints));

  // Settings
  MOCK_METHOD1(loadSettings, void(std::map<std::string, QVariant> &));

  // Calls we don't care about
  void showTable(QDataProcessorTableModel_sptr) override{};
  void saveSettings(const std::map<std::string, QVariant> &) override{};
  std::string getProcessInstrument() const override { return "FAKE"; }

  boost::shared_ptr<DataProcessorPresenter> getTablePresenter() const override {
    return boost::shared_ptr<DataProcessorPresenter>();
  }
};

class MockDataProcessorPresenter : public DataProcessorPresenter {

public:
  MockDataProcessorPresenter(){};
  ~MockDataProcessorPresenter() override {}

  MOCK_METHOD1(notify, void(DataProcessorPresenter::Flag));
  MOCK_METHOD1(setModel, void(std::string name));
  MOCK_METHOD1(accept, void(WorkspaceReceiver *));

private:
  // Calls we don't care about
  const std::map<std::string, QVariant> &options() const override {
    return m_options;
  };
  std::vector<DataProcessorCommand_uptr> publishCommands() override {
    std::vector<DataProcessorCommand_uptr> commands;
    for (size_t i = 0; i < 26; i++)
      commands.push_back(
          Mantid::Kernel::make_unique<DataProcessorAppendRowCommand>(this));
    return commands;
  };
  std::set<std::string> getTableList() const {
    return std::set<std::string>();
  };
  // Calls we don't care about
  void setOptions(const std::map<std::string, QVariant> &) override{};
  void
  transfer(const std::vector<std::map<std::string, std::string>> &) override{};
  void setInstrumentList(const std::vector<std::string> &,
                         const std::string &) override{};
  // void accept(WorkspaceReceiver *) {};
  void acceptViews(DataProcessorView *, ProgressableView *) override{};

  std::map<std::string, QVariant> m_options;
};

#endif /*MANTID_MANTIDWIDGETS_DATAPROCESSORVIEWMOCKOBJECTS_H*/

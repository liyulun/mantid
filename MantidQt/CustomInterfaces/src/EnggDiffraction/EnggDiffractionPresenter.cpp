#include "MantidAPI/ITableWorkspace.h"
#include "MantidAPI/MatrixWorkspace.h"
#include "MantidAPI/TableRow.h"
#include "MantidAPI/WorkspaceFactory.h"
#include "MantidKernel/Property.h"
#include <MantidKernel/StringTokenizer.h>
#include "MantidQtAPI/PythonRunner.h"
// #include "MantidQtCustomInterfaces/EnggDiffraction/EnggDiffractionModel.h"
#include "MantidQtCustomInterfaces/EnggDiffraction/EnggDiffractionPresenter.h"
#include "MantidQtCustomInterfaces/EnggDiffraction/EnggDiffractionPresWorker.h"
#include "MantidQtCustomInterfaces/EnggDiffraction/IEnggDiffractionView.h"
#include "MantidQtCustomInterfaces/Muon/ALCHelper.h"

#include <fstream>

#include <boost/lexical_cast.hpp>

#include "Poco/DirectoryIterator.h"
#include <Poco/File.h>

#include <QThread>
#include <MantidAPI/AlgorithmManager.h>

using namespace Mantid::API;
using namespace MantidQt::CustomInterfaces;

namespace MantidQt {
namespace CustomInterfaces {

/**
 * Parameters from a GSAS calibration. They define a conversion of
 * units time-of-flight<->d-spacing that can be calculated with the
 * algorithm AlignDetectors for example.
 */
struct GSASCalibrationParms {
  GSASCalibrationParms(size_t bid, double dc, double da, double tz)
      : bankid(bid), difc(dc), difa(da), tzero(tz) {}

  size_t bankid{0};
  double difc{0};
  double difa{0};
  double tzero{0};
};

namespace {
Mantid::Kernel::Logger g_log("EngineeringDiffractionGUI");
}

const std::string EnggDiffractionPresenter::g_enginxStr = "ENGINX";

const std::string EnggDiffractionPresenter::g_shortMsgRBNumberRequired =
    "A valid RB Number is required";
const std::string EnggDiffractionPresenter::g_msgRBNumberRequired =
    std::string("An experiment reference number (or so called \"RB "
                "number\" at ISIS) is "
                "required to effectively use this interface. \n") +
    "The output calibration, focusing and fitting results will be "
    "saved in directories named using the RB number entered.";

const std::string EnggDiffractionPresenter::g_runNumberErrorStr =
    " cannot be empty, must be an integer number, valid ENGINX run number/s "
    "or "
    "valid directory/directories.";

// discouraged at the moment
const bool EnggDiffractionPresenter::g_askUserCalibFilename = false;

const std::string EnggDiffractionPresenter::g_vanIntegrationWSName =
    "engggui_vanadium_integration_ws";
const std::string EnggDiffractionPresenter::g_vanCurvesWSName =
    "engggui_vanadium_curves_ws";

const std::string EnggDiffractionPresenter::g_focusedFittingWSName =
    "engggui_fitting_focused_ws";

const std::string EnggDiffractionPresenter::g_calibBanksParms =
    "engggui_calibration_banks_parameters";

bool EnggDiffractionPresenter::g_useAlignDetectors = true;
int EnggDiffractionPresenter::g_croppedCounter = 0;
int EnggDiffractionPresenter::g_plottingCounter = 0;
bool EnggDiffractionPresenter::g_abortThread = false;
std::string EnggDiffractionPresenter::g_lastValidRun = "";
std::string EnggDiffractionPresenter::g_calibCropIdentifier = "SpectrumNumbers";
std::string EnggDiffractionPresenter::g_sumOfFilesFocus = "";

EnggDiffractionPresenter::EnggDiffractionPresenter(IEnggDiffractionView *view)
    : m_workerThread(NULL), m_calibFinishedOK(false), m_focusFinishedOK(false),
      m_rebinningFinishedOK(false), m_fittingFinishedOK(false),
      m_view(view) /*, m_model(new EnggDiffractionModel()), */ {
  if (!m_view) {
    throw std::runtime_error(
        "Severe inconsistency found. Presenter created "
        "with an empty/null view (engineeering diffraction interface). "
        "Cannot continue.");
  }
}

EnggDiffractionPresenter::~EnggDiffractionPresenter() { cleanup(); }

/**
* Close open sessions, kill threads etc., save settings, etc. for a
* graceful window close/destruction
*/
void EnggDiffractionPresenter::cleanup() {
  // m_model->cleanup();

  // this may still be running
  if (m_workerThread) {
    if (m_workerThread->isRunning()) {
      g_log.notice() << "A calibration process is currently running, shutting "
                        "it down immediately..." << std::endl;
      m_workerThread->wait(10);
    }
    delete m_workerThread;
    m_workerThread = NULL;
  }
}

void EnggDiffractionPresenter::notify(
    IEnggDiffractionPresenter::Notification notif) {

  switch (notif) {

  case IEnggDiffractionPresenter::Start:
    processStart();
    break;

  case IEnggDiffractionPresenter::LoadExistingCalib:
    processLoadExistingCalib();
    break;

  case IEnggDiffractionPresenter::CalcCalib:
    processCalcCalib();
    break;

  case IEnggDiffractionPresenter::CropCalib:
    ProcessCropCalib();
    break;

  case IEnggDiffractionPresenter::FocusRun:
    processFocusBasic();
    break;

  case IEnggDiffractionPresenter::FocusCropped:
    processFocusCropped();
    break;

  case IEnggDiffractionPresenter::FocusTexture:
    processFocusTexture();
    break;

  case IEnggDiffractionPresenter::ResetFocus:
    processResetFocus();
    break;

  case IEnggDiffractionPresenter::RebinTime:
    processRebinTime();
    break;

  case IEnggDiffractionPresenter::RebinMultiperiod:
    processRebinMultiperiod();
    break;

  case IEnggDiffractionPresenter::FittingRunNo:
    fittingRunNoChanged();
    break;

  case IEnggDiffractionPresenter::FitPeaks:
    processFitPeaks();

    break;

  case IEnggDiffractionPresenter::LogMsg:
    processLogMsg();
    break;

  case IEnggDiffractionPresenter::InstrumentChange:
    processInstChange();
    break;

  case IEnggDiffractionPresenter::RBNumberChange:
    processRBNumberChange();
    break;

  case IEnggDiffractionPresenter::ShutDown:
    processShutDown();
    break;

  case IEnggDiffractionPresenter::StopFocus:
    processStopFocus();
    break;
  }
}

void EnggDiffractionPresenter::processStart() {
  EnggDiffCalibSettings cs = m_view->currentCalibSettings();
  m_view->showStatus("Ready");

  updateNewCalib(m_view->currentCalibFile());
}

void EnggDiffractionPresenter::processLoadExistingCalib() {
  EnggDiffCalibSettings cs = m_view->currentCalibSettings();

  std::string fname = m_view->askExistingCalibFilename();
  if (fname.empty()) {
    return;
  }

  updateNewCalib(fname);
}

/**
 * Grab a calibration from a (GSAS calibration) file
 * (.prm/.par/.iparm) and set/use it as current calibration.
 *
 * @param fname name/path of the calibration file
 */

void EnggDiffractionPresenter::updateNewCalib(const std::string &fname) {
  if (fname.empty()) {
    return;
  }

  std::string instName, vanNo, ceriaNo;
  try {
    parseCalibrateFilename(fname, instName, vanNo, ceriaNo);
  } catch (std::invalid_argument &ia) {
    m_view->userWarning("Invalid calibration filename : " + fname, ia.what());
    return;
  }

  try {
    grabCalibParms(fname);
    updateCalibParmsTable();
    m_view->newCalibLoaded(vanNo, ceriaNo, fname);
  } catch (std::runtime_error &rexc) {
    m_view->userWarning("Problem while updating calibration.", rexc.what());
  }
}

/**
 * Get from a calibration file (GSAS instrument parameters file) the
 * DIFC, DIFA, TZERO calibration parameters used for units
 * conversions. Normally this is used on the ...all_banks.prm file
 * which has the parameters for every bank included in the calibration
 * process.
 *
 * @param fname name of the calibration/GSAS iparm file
 */
void EnggDiffractionPresenter::grabCalibParms(const std::string &fname) {
  std::vector<GSASCalibrationParms> parms;

  // To grab the bank indices, lines like "INS   BANK     2"
  // To grab the difc,difa,tzero parameters, lines like:
  // "INS  2 ICONS  18388.00    0.00    -6.76"
  try {
    std::ifstream prmFile(fname);
    std::string line;
    int opts = Mantid::Kernel::StringTokenizer::TOK_TRIM +
               Mantid::Kernel::StringTokenizer::TOK_IGNORE_EMPTY;
    while (std::getline(prmFile, line)) {
      if (line.find("ICONS") != std::string::npos) {
        Mantid::Kernel::StringTokenizer tokenizer(line, " ", opts);
        const size_t numElems = 6;
        if (tokenizer.count() == numElems) {
          try {
            size_t bid = boost::lexical_cast<size_t>(tokenizer[1]);
            double difc = boost::lexical_cast<double>(tokenizer[3]);
            double difa = boost::lexical_cast<double>(tokenizer[4]);
            double tzero = boost::lexical_cast<double>(tokenizer[5]);
            parms.emplace_back(GSASCalibrationParms(bid, difc, difa, tzero));
          } catch (std::runtime_error &rexc) {
            g_log.warning()
                << "Error when trying to extract parameters from this line:  "
                << line << ". This calibration file may not load correctly. "
                           "Error details: " << rexc.what() << std::endl;
          }
        } else {
          g_log.warning() << "Could not parse correctly a parameters "
                             "definition line in this calibration file    ("
                          << fname << "). Did not find  " << numElems
                          << " elements as expected. The calibration may not "
                             "load correctly" << std::endl;
        }
      }
    }

  } catch (std::runtime_error &rexc) {
    g_log.error()
        << "Error while loading calibration / GSAS IPARM file (" << fname
        << "). Could not parse the file. Please check its contents. Details: "
        << rexc.what() << std::endl;
  }

  m_currentCalibParms = parms;
}

/**
 * Puts in a table workspace, visible in the ADS, the per-bank
 * calibration parameters for the current calibration.
 */
void EnggDiffractionPresenter::updateCalibParmsTable() {
  if (m_currentCalibParms.empty()) {
    return;
  }

  ITableWorkspace_sptr parmsTbl;
  AnalysisDataServiceImpl &ADS = Mantid::API::AnalysisDataService::Instance();
  if (ADS.doesExist(g_calibBanksParms)) {
    parmsTbl = ADS.retrieveWS<ITableWorkspace>(g_calibBanksParms);
    parmsTbl->setRowCount(0);
  } else {
    auto alg = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
        "CreateEmptyTableWorkspace");
    alg->initialize();
    alg->setPropertyValue("OutputWorkspace", g_calibBanksParms);
    alg->execute();

    parmsTbl = ADS.retrieveWS<ITableWorkspace>(g_calibBanksParms);
    parmsTbl->addColumn("int", "bankid");
    parmsTbl->addColumn("double", "difc");
    parmsTbl->addColumn("double", "difa");
    parmsTbl->addColumn("double", "tzero");
  }

  for (const auto &parms : m_currentCalibParms) {
    // ADS.remove(FocusedFitPeaksTableName);
    TableRow row = parmsTbl->appendRow();
    row << static_cast<int>(parms.bankid) << parms.difc << parms.difa
        << parms.tzero;
  }
}

void EnggDiffractionPresenter::processCalcCalib() {
  const std::string vanNo = isValidRunNumber(m_view->newVanadiumNo());
  const std::string ceriaNo = isValidRunNumber(m_view->newCeriaNo());
  try {
    inputChecksBeforeCalibrate(vanNo, ceriaNo);
  } catch (std::invalid_argument &ia) {
    m_view->userWarning("Error in the inputs required for calibrate",
                        ia.what());
    return;
  }
  g_log.notice() << "EnggDiffraction GUI: starting new calibration. This may "
                    "take a few seconds... " << std::endl;

  const std::string outFilename = outputCalibFilename(vanNo, ceriaNo);

  m_view->showStatus("Calculating calibration...");
  m_view->enableCalibrateAndFocusActions(false);
  // alternatively, this would be GUI-blocking:
  // doNewCalibration(outFilename, vanNo, ceriaNo, specNos);
  // calibrationFinished()
  startAsyncCalibWorker(outFilename, vanNo, ceriaNo, "");
}

void EnggDiffractionPresenter::ProcessCropCalib() {
  const std::string vanNo = isValidRunNumber(m_view->newVanadiumNo());
  const std::string ceriaNo = isValidRunNumber(m_view->newCeriaNo());
  int specNoNum = m_view->currentCropCalibBankName();
  enum BankMode { SPECNOS = 0, NORTH = 1, SOUTH = 2 };

  try {
    inputChecksBeforeCalibrate(vanNo, ceriaNo);
    if (m_view->currentCalibSpecNos().empty() &&
        specNoNum == BankMode::SPECNOS) {
      throw std::invalid_argument(
          "The Spectrum Nos cannot be empty, must be a"
          "valid range or a Bank Name can be selected instead");
    }
  } catch (std::invalid_argument &ia) {
    m_view->userWarning("Error in the inputs required for cropped calibration",
                        ia.what());
    return;
  }

  g_log.notice()
      << "EnggDiffraction GUI: starting cropped calibration. This may "
         "take a few seconds... " << std::endl;

  const std::string outFilename = outputCalibFilename(vanNo, ceriaNo);

  std::string specNo = "";
  if (specNoNum == BankMode::NORTH) {
    specNo = "North";
    g_calibCropIdentifier = "Bank";

  } else if (specNoNum == BankMode::SOUTH) {
    specNo = "South";
    g_calibCropIdentifier = "Bank";

  } else if (specNoNum == BankMode::SPECNOS) {
    g_calibCropIdentifier = "SpectrumNumbers";
    specNo = m_view->currentCalibSpecNos();
  }

  m_view->showStatus("Calculating cropped calibration...");
  m_view->enableCalibrateAndFocusActions(false);
  // alternatively, this would be GUI-blocking:
  // doNewCalibration(outFilename, vanNo, ceriaNo, specNo/bankName);
  // calibrationFinished()
  startAsyncCalibWorker(outFilename, vanNo, ceriaNo, specNo);
}

void EnggDiffractionPresenter::processFocusBasic() {
  std::vector<std::string> multi_RunNo =
      isValidMultiRunNumber(m_view->focusingRunNo());
  const std::vector<bool> banks = m_view->focusingBanks();

  // reset global values
  g_abortThread = false;
  g_sumOfFilesFocus = "";
  g_plottingCounter = 0;

  // check if valid run number provided before focusin
  try {
    inputChecksBeforeFocusBasic(multi_RunNo, banks);
  } catch (std::invalid_argument &ia) {
    m_view->userWarning("Error in the inputs required to focus a run",
                        ia.what());
    return;
  }

  int focusMode = m_view->currentMultiRunMode();
  if (focusMode == 0) {
    g_log.debug() << " focus mode selected Individual Run Files Separately "
                  << std::endl;

    // start focusing
    startFocusing(multi_RunNo, banks, "", "");

  } else if (focusMode == 1) {
    g_log.debug() << " focus mode selected Focus Sum Of Files " << std::endl;
    g_sumOfFilesFocus = "basic";
    std::vector<std::string> firstRun;
    firstRun.push_back(multi_RunNo[0]);

    // to avoid multiple loops, use firstRun instead as the
    // multi-run number is not required for sumOfFiles
    startFocusing(firstRun, banks, "", "");
  }
}

void EnggDiffractionPresenter::processFocusCropped() {
  const std::vector<std::string> multi_RunNo =
      isValidMultiRunNumber(m_view->focusingCroppedRunNo());
  const std::vector<bool> banks = m_view->focusingBanks();
  const std::string specNos = m_view->focusingCroppedSpectrumNos();

  // reset global values
  g_abortThread = false;
  g_sumOfFilesFocus = "";
  g_plottingCounter = 0;

  // check if valid run number provided before focusin
  try {
    inputChecksBeforeFocusCropped(multi_RunNo, banks, specNos);
  } catch (std::invalid_argument &ia) {
    m_view->userWarning(
        "Error in the inputs required to focus a run (in cropped mode)",
        ia.what());
    return;
  }

  int focusMode = m_view->currentMultiRunMode();
  if (focusMode == 0) {
    g_log.debug() << " focus mode selected Individual Run Files Separately "
                  << std::endl;

    startFocusing(multi_RunNo, banks, specNos, "");

  } else if (focusMode == 1) {
    g_log.debug() << " focus mode selected Focus Sum Of Files " << std::endl;
    g_sumOfFilesFocus = "cropped";
    std::vector<std::string> firstRun{multi_RunNo[0]};

    // to avoid multiple loops, use firstRun instead as the
    // multi-run number is not required for sumOfFiles
    startFocusing(firstRun, banks, specNos, "");
  }
}

void EnggDiffractionPresenter::processFocusTexture() {
  const std::vector<std::string> multi_RunNo =
      isValidMultiRunNumber(m_view->focusingTextureRunNo());
  const std::string dgFile = m_view->focusingTextureGroupingFile();

  // reset global values
  g_abortThread = false;
  g_sumOfFilesFocus = "";
  g_plottingCounter = 0;

  // check if valid run number provided before focusing
  try {
    inputChecksBeforeFocusTexture(multi_RunNo, dgFile);
  } catch (std::invalid_argument &ia) {
    m_view->userWarning(
        "Error in the inputs required to focus a run (in texture mode)",
        ia.what());
    return;
  }

  int focusMode = m_view->currentMultiRunMode();
  if (focusMode == 0) {
    g_log.debug() << " focus mode selected Individual Run Files Separately "
                  << std::endl;
    startFocusing(multi_RunNo, std::vector<bool>(), "", dgFile);

  } else if (focusMode == 1) {
    g_log.debug() << " focus mode selected Focus Sum Of Files " << std::endl;
    g_sumOfFilesFocus = "texture";
    std::vector<std::string> firstRun{multi_RunNo[0]};

    // to avoid multiple loops, use firstRun instead as the
    // multi-run number is not required for sumOfFiles
    startFocusing(firstRun, std::vector<bool>(), "", dgFile);
  }
}

/**
* Starts a focusing worker, for different modes depending on the
* inputs provided. Assumes that the inputs have been checked by the
* respective specific processFocus methods (for normal, cropped,
* texture, etc. focusing).
*
* @param multi_RunNo vector of run/file number to focus
* @param banks banks to include in the focusing, processed one at a time
*
* @param specNos list of spectra to use when focusing. If not empty
* this implies focusing in cropped mode.
*
* @param dgFile detector grouping file to define banks (texture). If
* not empty, this implies focusing in texture mode.
*/
void EnggDiffractionPresenter::startFocusing(
    const std::vector<std::string> &multi_RunNo, const std::vector<bool> &banks,
    const std::string &specNos, const std::string &dgFile) {

  std::string optMsg = "";
  if (!specNos.empty()) {
    optMsg = " (cropped)";
  } else if (!dgFile.empty()) {
    optMsg = " (texture)";
  }
  g_log.notice() << "EnggDiffraction GUI: starting new focusing" << optMsg
                 << ". This may take some seconds... " << std::endl;

  const std::string focusDir = m_view->focusingDir();

  m_view->showStatus("Focusing...");
  m_view->enableCalibrateAndFocusActions(false);
  // GUI-blocking alternative:
  // doFocusRun(focusDir, outFilenames, runNo, banks, specNos, dgFile)
  // focusingFinished()
  startAsyncFocusWorker(focusDir, multi_RunNo, banks, specNos, dgFile);
}

void EnggDiffractionPresenter::processResetFocus() { m_view->resetFocus(); }

void EnggDiffractionPresenter::processRebinTime() {

  const std::string runNo = isValidRunNumber(m_view->currentPreprocRunNo());
  double bin = m_view->rebinningTimeBin();

  try {
    inputChecksBeforeRebinTime(runNo, bin);
  } catch (std::invalid_argument &ia) {
    m_view->userWarning(
        "Error in the inputs required to pre-process (rebin) a run", ia.what());
    return;
  }

  const std::string outWSName = "engggui_preproc_time_ws";
  g_log.notice() << "EnggDiffraction GUI: starting new pre-processing "
                    "(re-binning) with a TOF bin into workspace '" +
                        outWSName + "'. This "
                                    "may take some seconds... " << std::endl;

  m_view->showStatus("Rebinning by time...");
  m_view->enableCalibrateAndFocusActions(false);
  // GUI-blocking alternative:
  // doRebinningTime(runNo, bin, outWSName)
  // rebinningFinished()
  startAsyncRebinningTimeWorker(runNo, bin, outWSName);
}

void EnggDiffractionPresenter::processRebinMultiperiod() {
  const std::string runNo = isValidRunNumber(m_view->currentPreprocRunNo());
  size_t nperiods = m_view->rebinningPulsesNumberPeriods();
  double timeStep = m_view->rebinningPulsesTime();

  try {
    inputChecksBeforeRebinPulses(runNo, nperiods, timeStep);
  } catch (std::invalid_argument &ia) {
    m_view->userWarning("Error in the inputs required to pre-process (rebin) a "
                        "run by pulse times",
                        ia.what());
    return;
  }
  const std::string outWSName = "engggui_preproc_by_pulse_time_ws";
  g_log.notice() << "EnggDiffraction GUI: starting new pre-processing "
                    "(re-binning) by pulse times into workspace '" +
                        outWSName + "'. This "
                                    "may take some seconds... " << std::endl;

  m_view->showStatus("Rebinning by pulses...");
  m_view->enableCalibrateAndFocusActions(false);
  // GUI-blocking alternative:
  // doRebinningPulses(runNo, nperiods, timeStep, outWSName)
  // rebinningFinished()
  startAsyncRebinningPulsesWorker(runNo, nperiods, timeStep, outWSName);
}

// Fitting Tab Run Number & Bank handling here
void MantidQt::CustomInterfaces::EnggDiffractionPresenter::
    fittingRunNoChanged() {

  try {
    std::string strFocusedFile = m_view->getFittingRunNo();
    QString focusedFile = QString::fromStdString(strFocusedFile);
    // file name
    Poco::Path selectedfPath(strFocusedFile);
    Poco::Path bankDir;

    // handling of vectors
    auto runnoDirVector = m_view->getFittingRunNumVec();
    runnoDirVector.clear();

    std::string strFPath = selectedfPath.toString();
    // returns empty if no directory is found
    std::vector<std::string> splitBaseName =
        m_view->splitFittingDirectory(strFPath);
    // runNo when single focused file selected
    std::vector<std::string> runNoVec;

    if (selectedfPath.isFile() && !splitBaseName.empty()) {

#ifdef __unix__
      bankDir = selectedfPath.parent();
#else
      bankDir = (bankDir).expand(selectedfPath.parent().toString());
#endif
      if (!splitBaseName.empty() && splitBaseName.size() > 3) {
        std::string foc_file = splitBaseName[0] + "_" + splitBaseName[1] + "_" +
                               splitBaseName[2] + "_" + splitBaseName[3];
        std::string strBankDir = bankDir.toString();

        if (strBankDir.empty()) {
          m_view->userWarning(
              "Invalid Input",
              "Please check that a valid directory is "
              "set for Output Folder under Focusing Settings on the "
              "settings tab. "
              "Please try again");
        } else {

          updateFittingDirVec(strBankDir, foc_file, false, runnoDirVector);
          m_view->setFittingRunNumVec(runnoDirVector);

          // add bank to the combo-box and list view
          m_view->addBankItems(splitBaseName, focusedFile);
          runNoVec.clear();
          runNoVec.push_back(splitBaseName[1]);
          auto fittingMultiRunMode = m_view->getFittingMultiRunMode();
          if (!fittingMultiRunMode)
            m_view->addRunNoItem(runNoVec, false);
        }
      }
      // assuming that no directory is found so look for number
      // if run number length greater
    } else if (focusedFile.count() > 4) {
      if (strFocusedFile.find("-") != std::string::npos) {
        std::vector<std::string> firstLastRunNoVec;
        boost::split(firstLastRunNoVec, strFocusedFile, boost::is_any_of("-"));
        std::string firstRun;
        std::string lastRun;
        if (!firstLastRunNoVec.empty()) {
          firstRun = firstLastRunNoVec[0];
          lastRun = firstLastRunNoVec[1];

          m_view->setFittingMultiRunMode(true);
          enableMultiRun(firstRun, lastRun, runnoDirVector);
        }

      } else {
        // if given a single run number instead
        auto focusDir = m_view->getFocusDir();

        if (focusDir.empty()) {
          m_view->userWarning(
              "Invalid Input",
              "Please check that a valid directory is "
              "set for Output Folder under Focusing Settings on the "
              "settings tab. "
              "Please try again");
        } else {

          updateFittingDirVec(focusDir, strFocusedFile, false, runnoDirVector);
          m_view->setFittingRunNumVec(runnoDirVector);

          // add bank to the combo-box and list view
          m_view->addBankItems(splitBaseName, focusedFile);
          runNoVec.clear();
          runNoVec.push_back(strFocusedFile);

          auto fittingMultiRunMode = m_view->getFittingMultiRunMode();
          if (!fittingMultiRunMode)
            m_view->addRunNoItem(runNoVec, false);
        }
      }
    }
    // set the directory here to the first in the vector if its not empty
    if (!runnoDirVector.empty() && !selectedfPath.isFile()) {
      QString firstDir = QString::fromStdString(runnoDirVector[0]);
      m_view->setFittingRunNo(firstDir);

    } else if (m_view->getFittingRunNo().empty()) {
      m_view->userWarning("Invalid Input",
                          "Invalid directory or run number given. "
                          "Please try again");
    }

  } catch (std::runtime_error &re) {
    m_view->userWarning("Invalid file",
                        "Unable to select the file; " +
                            static_cast<std::string>(re.what()));
    return;
  }
}

void EnggDiffractionPresenter::updateFittingDirVec(
    const std::string &bankDir, const std::string &focusedFile, bool multi_run,
    std::vector<std::string> &fittingRunNoDirVec) {

  try {
    std::string cwd(bankDir);
    Poco::DirectoryIterator it(cwd);
    Poco::DirectoryIterator end;
    while (it != end) {
      if (it->isFile()) {
        std::string itFilePath = it->path();
        Poco::Path itBankfPath(itFilePath);

        std::string itbankFileName = itBankfPath.getBaseName();
        // check if it not any other file.. e.g: texture
        if (itbankFileName.find(focusedFile) != std::string::npos) {
          fittingRunNoDirVec.push_back(itFilePath);
          if (multi_run)
            return;
        }
      }
      ++it;
    }
  } catch (std::runtime_error &re) {
    m_view->userWarning("Invalid file",
                        "File not found in the following directory; " +
                            bankDir + ". " +
                            static_cast<std::string>(re.what()));
  }
}

void EnggDiffractionPresenter::enableMultiRun(
    std::string firstRun, std::string lastRun,
    std::vector<std::string> &fittingRunNoDirVec) {

  bool firstDig = m_view->isDigit(firstRun);
  bool lastDig = m_view->isDigit(lastRun);

  std::vector<std::string> RunNumberVec;
  if (firstDig && lastDig) {
    int firstNum = std::stoi(firstRun);
    int lastNum = std::stoi(lastRun);

    if ((lastNum - firstNum) > 200) {
      m_view->userWarning(
          "Please try again",
          "The specified run number range is "
          "far to big, please try a smaller range of consecutive run numbers.");
    }

    else if (firstNum <= lastNum) {

      for (int i = firstNum; i <= lastNum; i++) {
        RunNumberVec.push_back(std::to_string(i));
      }

      auto focusDir = m_view->getFocusDir();
      if (focusDir.empty()) {
        m_view->userWarning(
            "Invalid Input",
            "Please check that a valid directory is "
            "set for Output Folder under Focusing Settings on the "
            "settings tab. "
            "Please try again");
      } else {
        // if given a single run number instead
        for (size_t i = 0; i < RunNumberVec.size(); i++) {
          updateFittingDirVec(focusDir, RunNumberVec[i], true,
                              fittingRunNoDirVec);
        }
        int diff = (lastNum - firstNum) + 1;
        auto global_vec_size = fittingRunNoDirVec.size();
        if (size_t(diff) == global_vec_size) {

          m_view->addRunNoItem(RunNumberVec, true);

          m_view->setBankEmit();
        }
      }
    } else {
      m_view->userWarning("Invalid Run Number",
                          "One or more run file not found "
                          "from the specified range of runs."
                          "Please try again");
    }
  } else {
    m_view->userWarning("Invalid Run Number",
                        "The specfied range of run number "
                        "entered is invalid. Please try again");
  }
}

// Process Fitting Peaks begins here

void EnggDiffractionPresenter::processFitPeaks() {
  const std::string focusedRunNo = m_view->getFittingRunNo();
  const std::string fitPeaksData = m_view->fittingPeaksData();

  g_log.debug() << "the expected peaks are: " << fitPeaksData << std::endl;

  try {
    inputChecksBeforeFitting(focusedRunNo, fitPeaksData);
  } catch (std::invalid_argument &ia) {
    m_view->userWarning("Error in the inputs required for fitting", ia.what());
    return;
  }

  const std::string outWSName = "engggui_fitting_fit_peak_ws";
  g_log.notice() << "EnggDiffraction GUI: starting new "
                    "single peak fits into workspace '" +
                        outWSName + "'. This "
                                    "may take some seconds... " << std::endl;

  m_view->showStatus("Fitting single peaks...");
  // disable GUI to avoid any double threads
  m_view->enableCalibrateAndFocusActions(false);
  // startAsyncFittingWorker
  // doFitting()
  startAsyncFittingWorker(focusedRunNo, fitPeaksData);
}

void EnggDiffractionPresenter::inputChecksBeforeFitting(
    const std::string &focusedRunNo, const std::string &ExpectedPeaks) {
  if (focusedRunNo.size() == 0) {
    throw std::invalid_argument(
        "Focused Run "
        "cannot be empty and must be a valid directory");
  }

  Poco::File file(focusedRunNo);
  if (!file.exists()) {
    throw std::invalid_argument("The focused workspace file for single peak "
                                "fitting could not be found: " +
                                focusedRunNo);
  }

  if (ExpectedPeaks.empty()) {
    g_log.warning() << "Expected peaks were not passed, via fitting interface, "
                       "the default list of "
                       "expected peaks will be utilised instead." << std::endl;
  }
  bool contains_non_digits =
      ExpectedPeaks.find_first_not_of("0123456789,. ") != std::string::npos;
  if (contains_non_digits) {
    throw std::invalid_argument("The expected peaks provided " + ExpectedPeaks +
                                " is invalid, "
                                "fitting process failed. Please try again!");
  }
}

void EnggDiffractionPresenter::startAsyncFittingWorker(
    const std::string &focusedRunNo, const std::string &ExpectedPeaks) {

  delete m_workerThread;
  m_workerThread = new QThread(this);
  EnggDiffWorker *worker =
      new EnggDiffWorker(this, focusedRunNo, ExpectedPeaks);
  worker->moveToThread(m_workerThread);

  connect(m_workerThread, SIGNAL(started()), worker, SLOT(fitting()));
  connect(worker, SIGNAL(finished()), this, SLOT(fittingFinished()));
  // early delete of thread and worker
  connect(m_workerThread, SIGNAL(finished()), m_workerThread,
          SLOT(deleteLater()), Qt::DirectConnection);
  connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
  m_workerThread->start();
}

void EnggDiffractionPresenter::setDifcTzero(MatrixWorkspace_sptr wks) const {
  size_t bankID = 1;
  // attempt to guess bankID - this should be done in code that is currently
  // in the view
  auto fittingFilename = m_view->getFittingRunNo();
  Poco::File fittingFile(fittingFilename);
  if (fittingFile.exists()) {
    Poco::Path path(fittingFile.path());
    auto name = path.getBaseName();
    std::vector<std::string> chunks;
    boost::split(chunks, name, boost::is_any_of("_"));
    if (!chunks.empty()) {
      try {
        bankID = boost::lexical_cast<size_t>(chunks.back());
      } catch (std::runtime_error &) {
      }
    }
  }

  const std::string units = "none";
  auto &run = wks->mutableRun();

  if (m_currentCalibParms.empty()) {
    run.addProperty<int>("bankid", 1, units, true);
    run.addProperty<double>("difc", 18400.0, units, true);
    run.addProperty<double>("difa", 0.0, units, true);
    run.addProperty<double>("tzero", 4.0, units, true);
  } else {
    GSASCalibrationParms parms(0, 0.0, 0.0, 0.0);
    for (const auto &p : m_currentCalibParms) {
      if (p.bankid == bankID) {
        parms = p;
        break;
      }
    }
    if (0 == parms.difc)
      parms = m_currentCalibParms.front();

    run.addProperty<int>("bankid", static_cast<int>(parms.bankid), units, true);
    run.addProperty<double>("difc", parms.difc, units, true);
    run.addProperty<double>("difa", parms.difa, units, true);
    run.addProperty<double>("tzero", parms.tzero, units, true);
  }
}

void EnggDiffractionPresenter::doFitting(const std::string &focusedRunNo,
                                         const std::string &ExpectedPeaks) {
  g_log.notice() << "EnggDiffraction GUI: starting new fitting with file "
                 << focusedRunNo << ". This may take a few seconds... "
                 << std::endl;

  MatrixWorkspace_sptr focusedWS;
  m_fittingFinishedOK = false;

  // load the focused workspace file to perform single peak fits
  try {
    auto load =
        Mantid::API::AlgorithmManager::Instance().createUnmanaged("Load");
    load->initialize();
    load->setPropertyValue("Filename", focusedRunNo);
    load->setPropertyValue("OutputWorkspace", g_focusedFittingWSName);
    load->execute();

    AnalysisDataServiceImpl &ADS = Mantid::API::AnalysisDataService::Instance();
    focusedWS = ADS.retrieveWS<MatrixWorkspace>(g_focusedFittingWSName);
  } catch (std::runtime_error &re) {
    g_log.error()
        << "Error while loading focused data. "
           "Could not run the algorithm Load succesfully for the Fit "
           "peaks (file name: " +
               focusedRunNo + "). Error description: " + re.what() +
               " Please check also the previous log messages for details.";
    return;
  }

  setDifcTzero(focusedWS);

  // run the algorithm EnggFitPeaks with workspace loaded above
  // requires unit in Time of Flight
  auto enggFitPeaks =
      Mantid::API::AlgorithmManager::Instance().createUnmanaged("EnggFitPeaks");
  const std::string focusedFitPeaksTableName =
      "engggui_fitting_fitpeaks_params";

  // delete existing table workspace to avoid confusion
  AnalysisDataServiceImpl &ADS = Mantid::API::AnalysisDataService::Instance();
  if (ADS.doesExist(focusedFitPeaksTableName)) {
    ADS.remove(focusedFitPeaksTableName);
  }

  try {
    enggFitPeaks->initialize();
    enggFitPeaks->setProperty("InputWorkspace", focusedWS);
    if (!ExpectedPeaks.empty()) {
      enggFitPeaks->setProperty("ExpectedPeaks", ExpectedPeaks);
    }
    enggFitPeaks->setProperty("FittedPeaks", focusedFitPeaksTableName);
    enggFitPeaks->execute();
  } catch (std::exception &re) {
    g_log.error() << "Could not run the algorithm EnggFitPeaks "
                     "successfully for bank, "
                     // bank name
                     "Error description: " +
                         static_cast<std::string>(re.what()) +
                         " Please check also the log message for detail."
                  << std::endl;
  }

  try {
    runFittingAlgs(focusedFitPeaksTableName, g_focusedFittingWSName);

  } catch (std::invalid_argument &ia) {
    g_log.error() << "Error, Fitting could not finish off correctly, " +
                         std::string(ia.what()) << std::endl;
    return;
  }
}

void EnggDiffractionPresenter::runFittingAlgs(
    std::string focusedFitPeaksTableName, std::string focusedWSName) {
  // retrieve the table with parameters
  auto &ADS = Mantid::API::AnalysisDataService::Instance();
  if (!ADS.doesExist(focusedFitPeaksTableName)) {
    // convert units so valid dSpacing peaks can still be added to gui
    if (ADS.doesExist(g_focusedFittingWSName)) {
      convertUnits(g_focusedFittingWSName);
    }

    throw std::invalid_argument(
        focusedFitPeaksTableName +
        " workspace could not be found. "
        "Please check the log messages for more details.");
  }

  auto table = ADS.retrieveWS<ITableWorkspace>(focusedFitPeaksTableName);
  size_t rowCount = table->rowCount();
  const std::string single_peak_out_WS = "engggui_fitting_single_peaks";
  std::string currentPeakOutWS;

  std::string Bk2BkExpFunctionStr;
  std::string startX = "";
  std::string endX = "";
  for (size_t i = 0; i < rowCount; i++) {
    // get the functionStrFactory to generate the string for function
    // property, returns the string with i row from table workspace
    // table is just passed so it works?
    Bk2BkExpFunctionStr =
        functionStrFactory(table, focusedFitPeaksTableName, i, startX, endX);

    g_log.debug() << "startX: " + startX + " . endX: " + endX << std::endl;

    currentPeakOutWS = "__engggui_fitting_single_peaks" + std::to_string(i);

    // run EvaluateFunction algorithm with focused workspace to produce
    // the correct fit function
    // focusedWSName is not going to change as its always going to be from
    // single workspace
    runEvaluateFunctionAlg(Bk2BkExpFunctionStr, focusedWSName, currentPeakOutWS,
                           startX, endX);

    // crop workspace so only the correct workspace index is plotted
    runCropWorkspaceAlg(currentPeakOutWS);

    // apply the same binning as a focused workspace
    runRebinToWorkspaceAlg(currentPeakOutWS);

    // if the first peak
    if (i == size_t(0)) {

      // create a workspace clone of bank focus file
      // this will import all information of the previous file
      runCloneWorkspaceAlg(focusedWSName, single_peak_out_WS);

      setDataToClonedWS(currentPeakOutWS, single_peak_out_WS);
      ADS.remove(currentPeakOutWS);
    } else {
      const std::string currentPeakClonedWS =
          "__engggui_fitting_cloned_peaks" + std::to_string(i);

      runCloneWorkspaceAlg(focusedWSName, currentPeakClonedWS);

      setDataToClonedWS(currentPeakOutWS, currentPeakClonedWS);

      // append all peaks in to single workspace & remove
      runAppendSpectraAlg(single_peak_out_WS, currentPeakClonedWS);
      ADS.remove(currentPeakOutWS);
      ADS.remove(currentPeakClonedWS);
    }
  }

  convertUnits(g_focusedFittingWSName);

  // convert units for both workspaces to dSpacing from ToF
  if (rowCount > size_t(0)) {
    auto swks = ADS.retrieveWS<MatrixWorkspace>(single_peak_out_WS);
    setDifcTzero(swks);
    convertUnits(single_peak_out_WS);
  } else {
    g_log.error() << "The engggui_fitting_fitpeaks_params table produced is"
                     "empty. Please try again!" << std::endl;
  }

  m_fittingFinishedOK = true;
}

std::string EnggDiffractionPresenter::functionStrFactory(
    Mantid::API::ITableWorkspace_sptr &paramTableWS, std::string tableName,
    size_t row, std::string &startX, std::string &endX) {
  const double windowLeft = 9;
  const double windowRight = 12;

  AnalysisDataServiceImpl &ADS = Mantid::API::AnalysisDataService::Instance();
  paramTableWS = ADS.retrieveWS<ITableWorkspace>(tableName);

  double A0 = paramTableWS->cell<double>(row, size_t(1));
  double A1 = paramTableWS->cell<double>(row, size_t(3));
  double I = paramTableWS->cell<double>(row, size_t(13));
  double A = paramTableWS->cell<double>(row, size_t(7));
  double B = paramTableWS->cell<double>(row, size_t(9));
  double X0 = paramTableWS->cell<double>(row, size_t(5));
  double S = paramTableWS->cell<double>(row, size_t(11));

  startX = boost::lexical_cast<std::string>(X0 - (windowLeft * S));
  endX = boost::lexical_cast<std::string>(X0 + (windowRight * S));

  std::string functionStr =
      "name=LinearBackground,A0=" + boost::lexical_cast<std::string>(A0) +
      ",A1=" + boost::lexical_cast<std::string>(A1) +
      ";name=BackToBackExponential,I=" + boost::lexical_cast<std::string>(I) +
      ",A=" + boost::lexical_cast<std::string>(A) + ",B=" +
      boost::lexical_cast<std::string>(B) + ",X0=" +
      boost::lexical_cast<std::string>(X0) + ",S=" +
      boost::lexical_cast<std::string>(S);

  return functionStr;
}

void EnggDiffractionPresenter::runEvaluateFunctionAlg(
    const std::string &bk2BkExpFunction, const std::string &InputName,
    const std::string &OutputName, const std::string &startX,
    const std::string &endX) {

  auto evalFunc = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
      "EvaluateFunction");
  g_log.notice() << "EvaluateFunction algorithm has started" << std::endl;
  try {
    evalFunc->initialize();
    evalFunc->setProperty("Function", bk2BkExpFunction);
    evalFunc->setProperty("InputWorkspace", InputName);
    evalFunc->setProperty("OutputWorkspace", OutputName);
    evalFunc->setProperty("StartX", startX);
    evalFunc->setProperty("EndX", endX);
    evalFunc->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Could not run the algorithm EvaluateFunction, "
                     "Error description: " +
                         static_cast<std::string>(re.what()) << std::endl;
  }
}

void EnggDiffractionPresenter::runCropWorkspaceAlg(std::string workspaceName) {
  auto cropWS = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
      "CropWorkspace");
  try {
    cropWS->initialize();
    cropWS->setProperty("InputWorkspace", workspaceName);
    cropWS->setProperty("OutputWorkspace", workspaceName);
    cropWS->setProperty("StartWorkspaceIndex", 1);
    cropWS->setProperty("EndWorkspaceIndex", 1);
    cropWS->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Could not run the algorithm CropWorkspace, "
                     "Error description: " +
                         static_cast<std::string>(re.what()) << std::endl;
  }
}

void EnggDiffractionPresenter::runAppendSpectraAlg(std::string workspace1Name,
                                                   std::string workspace2Name) {
  auto appendSpec = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
      "AppendSpectra");
  try {
    appendSpec->initialize();
    appendSpec->setProperty("InputWorkspace1", workspace1Name);
    appendSpec->setProperty("InputWorkspace2", workspace2Name);
    appendSpec->setProperty("OutputWorkspace", workspace1Name);
    appendSpec->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Could not run the algorithm AppendWorkspace, "
                     "Error description: " +
                         static_cast<std::string>(re.what()) << std::endl;
  }
}

void EnggDiffractionPresenter::runRebinToWorkspaceAlg(
    std::string workspaceName) {
  auto RebinToWs = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
      "RebinToWorkspace");
  try {
    RebinToWs->initialize();
    RebinToWs->setProperty("WorkspaceToRebin", workspaceName);
    RebinToWs->setProperty("WorkspaceToMatch", g_focusedFittingWSName);
    RebinToWs->setProperty("OutputWorkspace", workspaceName);
    RebinToWs->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Could not run the algorithm RebinToWorkspace, "
                     "Error description: " +
                         static_cast<std::string>(re.what()) << std::endl;
  }
}

/**
 * Converts from time-of-flight to d-spacing
 *
 * @param workspaceName name of the workspace to convert (in place)
 */
void EnggDiffractionPresenter::convertUnits(std::string workspaceName) {
  // Here using the GSAS (DIFC, TZERO) parameters seems preferred
  if (g_useAlignDetectors) {
    runAlignDetectorsAlg(workspaceName);
  } else {
    runConvertUnitsAlg(workspaceName);
  }
}

void EnggDiffractionPresenter::getDifcTzero(MatrixWorkspace_const_sptr wks,
                                            double &difc, double &difa,
                                            double &tzero) const {

  try {
    const auto run = wks->run();
    // long, step by step way:
    // auto propC = run.getLogData("difc");
    // auto doubleC =
    //     dynamic_cast<Mantid::Kernel::PropertyWithValue<double> *>(propC);
    // if (!doubleC)
    //   throw Mantid::Kernel::Exception::NotFoundError(
    //       "Required difc property not found in workspace.", "difc");
    difc = run.getPropertyValueAsType<double>("difc");
    difa = run.getPropertyValueAsType<double>("difa");
    tzero = run.getPropertyValueAsType<double>("tzero");

  } catch (std::runtime_error &rexc) {
    // fallback to something reasonable / approximate values so
    // the fitting tab can work minimally
    difa = tzero = 0.0;
    difc = 18400;
    g_log.warning()
        << "Could not retrieve the DIFC, DIFA, TZERO values from the workspace "
        << wks->name() << ". Using default, which is not adjusted for this "
                          "workspace/run: DIFA: " << difa << ", DIFC: " << difc
        << ", TZERO: " << tzero << ". Error details: " << rexc.what()
        << std::endl;
  }
}

/**
 * Converts units from time-of-flight to d-spacing, using
 * AlignDetectors.  This is the GSAS-style alternative to using the
 * algorithm ConvertUnits.  Needs to make sure that the workspace is
 * not of distribution type (and use the algorithm
 * ConvertFromDistribution if it is). This is a requirement of
 * AlignDetectors.
 *
 * @param workspaceName name of the workspace to convert
 */
void EnggDiffractionPresenter::runAlignDetectorsAlg(std::string workspaceName) {
  const std::string targetUnit = "dSpacing";
  const std::string algName = "AlignDetectors";

  const auto &ADS = Mantid::API::AnalysisDataService::Instance();
  auto inputWS = ADS.retrieveWS<MatrixWorkspace>(workspaceName);
  if (!inputWS)
    return;

  double difc, difa, tzero;
  getDifcTzero(inputWS, difc, difa, tzero);

  // create a table with the GSAS calibration parameters
  ITableWorkspace_sptr difcTable;
  try {
    difcTable = Mantid::API::WorkspaceFactory::Instance().createTable();
    if (!difcTable) {
      return;
    }
    difcTable->addColumn("int", "detid");
    difcTable->addColumn("double", "difc");
    difcTable->addColumn("double", "difa");
    difcTable->addColumn("double", "tzero");
    TableRow row = difcTable->appendRow();
    auto spec = inputWS->getSpectrum(0);
    if (!spec)
      return;
    Mantid::detid_t detID = *(spec->getDetectorIDs().cbegin());

    row << detID << difc << difa << tzero;
  } catch (std::runtime_error &rexc) {
    g_log.error() << "Failed to prepare calibration table input to convert "
                     "units with the algorithm " << algName
                  << ". Error details: " << rexc.what() << std::endl;
    return;
  }

  // AlignDetectors doesn't take distribution workspaces (it enforces
  // RawCountValidator)
  if (inputWS->isDistribution()) {
    try {
      auto alg = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
          "ConvertFromDistribution");
      alg->initialize();
      alg->setProperty("Workspace", workspaceName);
      alg->execute();
    } catch (std::runtime_error &rexc) {
      g_log.error() << "Could not run ConvertFromDistribution. Error: "
                    << rexc.what() << std::endl;
      return;
    }
  }

  try {
    auto alg =
        Mantid::API::AlgorithmManager::Instance().createUnmanaged(algName);
    alg->initialize();
    alg->setProperty("InputWorkspace", workspaceName);
    alg->setProperty("OutputWorkspace", workspaceName);
    alg->setProperty("CalibrationWorkspace", difcTable);
    alg->execute();
  } catch (std::runtime_error &rexc) {
    g_log.error() << "Could not run the algorithm " << algName
                  << " to convert workspace to " << targetUnit
                  << ", Error details: " + static_cast<std::string>(rexc.what())
                  << std::endl;
  }
}

void EnggDiffractionPresenter::runConvertUnitsAlg(std::string workspaceName) {
  const std::string targetUnit = "dSpacing";
  auto ConvertUnits =
      Mantid::API::AlgorithmManager::Instance().createUnmanaged("ConvertUnits");
  try {
    ConvertUnits->initialize();
    ConvertUnits->setProperty("InputWorkspace", workspaceName);
    ConvertUnits->setProperty("OutputWorkspace", workspaceName);
    ConvertUnits->setProperty("Target", targetUnit);
    ConvertUnits->setPropertyValue("EMode", "Elastic");
    ConvertUnits->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Could not run the algorithm ConvertUnits to convert "
                     "workspace to " << targetUnit
                  << ", Error description: " +
                         static_cast<std::string>(re.what()) << std::endl;
  }
}

void EnggDiffractionPresenter::runCloneWorkspaceAlg(
    std::string inputWorkspace, const std::string &outputWorkspace) {

  auto cloneWorkspace =
      Mantid::API::AlgorithmManager::Instance().createUnmanaged(
          "CloneWorkspace");
  try {
    cloneWorkspace->initialize();
    cloneWorkspace->setProperty("InputWorkspace", inputWorkspace);
    cloneWorkspace->setProperty("OutputWorkspace", outputWorkspace);
    cloneWorkspace->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Could not run the algorithm CreateWorkspace, "
                     "Error description: " +
                         static_cast<std::string>(re.what()) << std::endl;
  }
}

void EnggDiffractionPresenter::setDataToClonedWS(std::string &current_WS,
                                                 const std::string &cloned_WS) {
  AnalysisDataServiceImpl &ADS = Mantid::API::AnalysisDataService::Instance();
  auto currentPeakWS = ADS.retrieveWS<MatrixWorkspace>(current_WS);
  auto currentClonedWS = ADS.retrieveWS<MatrixWorkspace>(cloned_WS);
  currentClonedWS->getSpectrum(0)
      ->setData(currentPeakWS->readY(0), currentPeakWS->readE(0));
}

void EnggDiffractionPresenter::plotFitPeaksCurves() {
  AnalysisDataServiceImpl &ADS = Mantid::API::AnalysisDataService::Instance();
  std::string singlePeaksWs = "engggui_fitting_single_peaks";

  if (!ADS.doesExist(singlePeaksWs) && !ADS.doesExist(g_focusedFittingWSName)) {
    g_log.error() << "Fitting results could not be plotted as there is no " +
                         singlePeaksWs + " or " + g_focusedFittingWSName +
                         " workspace found." << std::endl;
    m_view->showStatus("Error while fitting peaks");
    return;
  }

  try {
    auto focusedPeaksWS =
        ADS.retrieveWS<MatrixWorkspace>(g_focusedFittingWSName);
    auto focusedData = ALCHelper::curveDataFromWs(focusedPeaksWS);
    m_view->setDataVector(focusedData, true, m_fittingFinishedOK);

    if (m_fittingFinishedOK) {
      g_log.debug() << "single peaks fitting being plotted now." << std::endl;
      auto singlePeaksWS = ADS.retrieveWS<MatrixWorkspace>(singlePeaksWs);
      auto singlePeaksData = ALCHelper::curveDataFromWs(singlePeaksWS);
      m_view->setDataVector(singlePeaksData, false, true);
      m_view->showStatus("Peaks fitted successfully");

    } else {
      g_log.notice() << "Focused workspace has been plotted to the "
                        "graph; further peaks can be adding using Peak Tools."
                     << std::endl;
      g_log.warning() << "Peaks could not be plotted as the fitting process "
                         "did not finish correctly." << std::endl;
      m_view->showStatus("No peaks could be fitted");
    }

  } catch (std::runtime_error) {
    g_log.error()
        << "Unable to finish of the plotting of the graph for "
           "engggui_fitting_focused_fitpeaks  workspace. Error "
           "description. Please check also the log message for detail.";

    m_view->showStatus("Error while plotting the peaks fitted");
    throw;
  }
}

void EnggDiffractionPresenter::fittingFinished() {
  if (!m_view)
    return;

  if (!m_fittingFinishedOK) {
    g_log.warning() << "The single peak fitting did not finish correctly."
                    << std::endl;
    if (m_workerThread) {
      delete m_workerThread;
      m_workerThread = NULL;
    }

    m_view->showStatus(
        "Single peak fitting process did not complete successfully");
  } else {
    g_log.notice() << "The single peak fitting finished - the output "
                      "workspace is ready." << std::endl;

    m_view->showStatus("Single peak fittin process finished. Ready");
    if (m_workerThread) {
      delete m_workerThread;
      m_workerThread = NULL;
    }
  }

  try {
    // should now plot the focused workspace when single peak fitting
    // process fails
    plotFitPeaksCurves();

  } catch (std::runtime_error &re) {
    g_log.error() << "Unable to finish the plotting of the graph for "
                     "engggui_fitting_focused_fitpeaks workspace. Error "
                     "description: " +
                         static_cast<std::string>(re.what()) +
                         " Please check also the log message for detail.";
    throw;
  }
  g_log.notice() << "EnggDiffraction GUI: plotting of peaks for single peak "
                    "fits has completed. " << std::endl;

  // enable the GUI
  m_view->enableCalibrateAndFocusActions(true);
}

void EnggDiffractionPresenter::processLogMsg() {
  std::vector<std::string> msgs = m_view->logMsgs();
  for (size_t i = 0; i < msgs.size(); i++) {
    g_log.information() << msgs[i] << std::endl;
  }
}

void EnggDiffractionPresenter::processInstChange() {
  const std::string err = "Changing instrument is not supported!";
  g_log.error() << err << std::endl;
  m_view->userError("Fatal error", err);
}

void EnggDiffractionPresenter::processRBNumberChange() {
  const std::string rbn = m_view->getRBNumber();
  auto valid = validateRBNumber(rbn);
  m_view->enableTabs(valid);
  m_view->splashMessage(!valid, g_shortMsgRBNumberRequired,
                        g_msgRBNumberRequired);
  if (!valid) {
    m_view->showStatus("Valid RB number required");
  } else {
    m_view->showStatus("Ready");
  }
}

void EnggDiffractionPresenter::processShutDown() {
  m_view->showStatus("Closing...");
  m_view->saveSettings();
  cleanup();
}

void EnggDiffractionPresenter::processStopFocus() {
  if (m_workerThread) {
    if (m_workerThread->isRunning()) {
      g_log.notice() << "A focus process is currently running, shutting "
                        "it down as soon as possible..." << std::endl;

      g_abortThread = true;
      g_log.warning() << "Focus Stop has been clicked, please wait until "
                         "current focus run process has been completed. "
                      << std::endl;
    }
  }
}

/**
* Check if an RB number is valid to work with it (retrieve data,
* calibrate, focus, etc.). For now this will accept any non-empty
* string. Later on we might be more strict about valid RB numbers /
* experiment IDs.
*
* @param rbn RB number as given by the user
*/
bool EnggDiffractionPresenter::validateRBNumber(const std::string &rbn) const {
  return !rbn.empty();
}

/**
* Checks if the provided run number is valid and if a directory is
* provided it will convert it to a run number. It also records the
* paths the user has browsed to.
*
* @param userPaths the input/directory given by the user via the "browse"
* button
*
* @return run_number 6 character string of a run number
*/
std::string EnggDiffractionPresenter::isValidRunNumber(
    const std::vector<std::string> &userPaths) {

  std::string run_number;
  if (userPaths.empty() || userPaths.front().empty()) {
    return run_number;
  }

  for (const auto &path : userPaths) {
    run_number = "";
    try {
      if (Poco::File(path).exists()) {
        Poco::Path inputDir = path;

        // get file name via poco::path
        std::string filename = inputDir.getFileName();

        // convert to int or assign it to size_t
        for (size_t i = 0; i < filename.size(); i++) {
          char *str = &filename[i];
          if (std::isdigit(*str)) {
            run_number += filename[i];
          }
        }
        run_number.erase(0, run_number.find_first_not_of('0'));

        // The path of this file needs to be added in the search
        // path as the user can browse to any path not included in
        // the interface settings
        recordPathBrowsedTo(inputDir.toString());
      }

    } catch (std::runtime_error &re) {
      throw std::invalid_argument("Error while checking the selected file: " +
                                  static_cast<std::string>(re.what()));
    } catch (Poco::FileNotFoundException &rexc) {
      throw std::invalid_argument("Error while checking the selected file. "
                                  "There was a problem with the file: " +
                                  std::string(rexc.what()));
    }
  }

  g_log.debug() << "Run number inferred from browse path (" << userPaths.front()
                << ") is: " << run_number << std::endl;

  return run_number;
}

/**
* Checks if the provided run number is valid and if a directory is provided
*
* @param paths takes the input/paths of the user
*
* @return vector of string multi_run_number, 6 character string of a run number
*/
std::vector<std::string> EnggDiffractionPresenter::isValidMultiRunNumber(
    const std::vector<std::string> &paths) {

  std::vector<std::string> multi_run_number;
  if (paths.empty() || paths.front().empty())
    return multi_run_number;

  for (auto path : paths) {
    std::string run_number;
    try {
      if (Poco::File(path).exists()) {
        Poco::Path inputDir = path;

        // get file name name via poco::path
        std::string filename = inputDir.getFileName();

        // convert to int or assign it to size_t
        for (size_t i = 0; i < filename.size(); i++) {
          char *str = &filename[i];
          if (std::isdigit(*str)) {
            run_number += filename[i];
          }
        }
        run_number.erase(0, run_number.find_first_not_of('0'));

        recordPathBrowsedTo(inputDir.toString());
      }
    } catch (std::runtime_error &re) {
      throw std::invalid_argument(
          "Error while looking for a run number in the files selected: " +
          static_cast<std::string>(re.what()));
    } catch (Poco::FileNotFoundException &rexc) {
      throw std::invalid_argument("Error while looking for a run number in the "
                                  "files selected. There was a problem with "
                                  "the file(s): " +
                                  std::string(rexc.what()));
    }

    multi_run_number.push_back(run_number);
  }

  g_log.debug()
      << "First and last run number inferred from a multi-run selection: "
      << multi_run_number.front() << " ... " << multi_run_number.back()
      << std::endl;

  return multi_run_number;
}

/**
* Does several checks on the current inputs and settings. This should
* be done before starting any calibration work. The message return
* should in principle be shown to the user as a visible message
* (pop-up, error log, etc.)
*
* @param newVanNo number of the Vanadium run for the new calibration
* @param newCeriaNo number of the Ceria run for the new calibration
*
* @throws std::invalid_argument with an informative message.
*/
void EnggDiffractionPresenter::inputChecksBeforeCalibrate(
    const std::string &newVanNo, const std::string &newCeriaNo) {
  if (newVanNo.empty()) {
    throw std::invalid_argument("The Vanadium number" + g_runNumberErrorStr);
  }
  if (newCeriaNo.empty()) {
    throw std::invalid_argument("The Ceria number" + g_runNumberErrorStr);
  }

  EnggDiffCalibSettings cs = m_view->currentCalibSettings();
  const std::string pixelCalib = cs.m_pixelCalibFilename;
  if (pixelCalib.empty()) {
    throw std::invalid_argument(
        "You need to set a pixel (full) calibration in settings.");
  }
  const std::string templGSAS = cs.m_templateGSAS_PRM;
  if (templGSAS.empty()) {
    throw std::invalid_argument(
        "You need to set a template calibration file for GSAS in settings.");
  }
}

/**
 * What should be the name of the output GSAS calibration file, given
 * the Vanadium and Ceria runs
 *
 * @param vanNo number of the Vanadium run, which is normally part of the name
 * @param ceriaNo number of the Ceria run, which is normally part of the name
 * @param bankName bank name when generating a file for an individual
 * bank. Leave empty to use a generic name for all banks
 *
 * @return filename (without the full path)
 */
std::string
EnggDiffractionPresenter::outputCalibFilename(const std::string &vanNo,
                                              const std::string &ceriaNo,
                                              const std::string &bankName) {
  std::string outFilename = "";
  const std::string sugg =
      buildCalibrateSuggestedFilename(vanNo, ceriaNo, bankName);
  if (!g_askUserCalibFilename) {
    outFilename = sugg;
  } else {
    outFilename = m_view->askNewCalibrationFilename(sugg);
    if (!outFilename.empty()) {
      // make sure it follows the rules
      try {
        std::string inst, van, ceria;
        parseCalibrateFilename(outFilename, inst, van, ceria);
      } catch (std::invalid_argument &ia) {
        m_view->userWarning(
            "Invalid output calibration filename: " + outFilename, ia.what());
        outFilename = "";
      }
    }
  }

  return outFilename;
}

/**
* Parses the name of a calibration file and guesses the instrument,
* vanadium and ceria run numbers, assuming that the name has been
* build with buildCalibrateSuggestedFilename().
*
* @todo this is currently based on the filename. This method should
* do a basic sanity check that the file is actually a calibration
* file (IPARM file for GSAS), and get the numbers from the file not
* from the name of the file. Leaving this as a TODO for now, as the
* file template and contents are still subject to change.
*
* @param path full path to a calibration file
* @param instName instrument used in the file
* @param vanNo number of the Vanadium run used for this calibration file
* @param ceriaNo number of the Ceria run
*
* @throws invalid_argument with an informative message if tha name does
* not look correct (does not follow the conventions).
*/
void EnggDiffractionPresenter::parseCalibrateFilename(const std::string &path,
                                                      std::string &instName,
                                                      std::string &vanNo,
                                                      std::string &ceriaNo) {
  instName = "";
  vanNo = "";
  ceriaNo = "";

  Poco::Path fullPath(path);
  const std::string filename = fullPath.getFileName();
  if (filename.empty()) {
    return;
  }

  const std::string explMsg =
      "Expected a file name like 'INSTR_vanNo_ceriaNo_....par', "
      "where INSTR is the instrument name and vanNo and ceriaNo are the "
      "numbers of the Vanadium and calibration sample (Ceria, CeO2) runs.";
  std::vector<std::string> parts;
  boost::split(parts, filename, boost::is_any_of("_"));
  if (parts.size() < 4) {
    throw std::invalid_argument(
        "Failed to find at least the 4 required parts of the file name.\n\n" +
        explMsg);
  }

  // check the rules on the file name
  if (g_enginxStr != parts[0]) {
    throw std::invalid_argument("The first component of the file name is not "
                                "the expected instrument name: " +
                                g_enginxStr + ".\n\n" + explMsg);
  }
  const std::string castMsg =
      "It is not possible to interpret as an integer number ";
  try {
    boost::lexical_cast<int>(parts[1]);
  } catch (std::runtime_error &) {
    throw std::invalid_argument(
        castMsg + "the Vanadium number part of the file name.\n\n" + explMsg);
  }
  try {
    boost::lexical_cast<int>(parts[2]);
  } catch (std::runtime_error &) {
    throw std::invalid_argument(
        castMsg + "the Ceria number part of the file name.\n\n" + explMsg);
  }

  instName = parts[0];
  vanNo = parts[1];
  ceriaNo = parts[2];
}

/**
* Start the calibration work without blocking the GUI. This uses
* connect for Qt signals/slots so that it runs well with the Qt event
* loop. Because of that this class needs to be a Q_OBJECT.
*
* @param outFilename name for the output GSAS calibration file
* @param vanNo vanadium run number
* @param ceriaNo ceria run number
* @param specNos SpecNos or bank name to be passed
*/
void EnggDiffractionPresenter::startAsyncCalibWorker(
    const std::string &outFilename, const std::string &vanNo,
    const std::string &ceriaNo, const std::string &specNos) {
  delete m_workerThread;
  m_workerThread = new QThread(this);
  EnggDiffWorker *worker =
      new EnggDiffWorker(this, outFilename, vanNo, ceriaNo, specNos);
  worker->moveToThread(m_workerThread);

  connect(m_workerThread, SIGNAL(started()), worker, SLOT(calibrate()));
  connect(worker, SIGNAL(finished()), this, SLOT(calibrationFinished()));
  // early delete of thread and worker
  connect(m_workerThread, SIGNAL(finished()), m_workerThread,
          SLOT(deleteLater()), Qt::DirectConnection);
  connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
  m_workerThread->start();
}

/**
* Calculate a new calibration. This is what threads/workers should
* use to run the calculations in response to the user clicking
* 'calibrate' or similar.
*
* @param outFilename name for the output GSAS calibration file
* @param vanNo vanadium run number
* @param ceriaNo ceria run number
* @param specNos SpecNos or bank name to be passed
*/
void EnggDiffractionPresenter::doNewCalibration(const std::string &outFilename,
                                                const std::string &vanNo,
                                                const std::string &ceriaNo,
                                                const std::string &specNos) {
  g_log.notice() << "Generating new calibration file: " << outFilename
                 << std::endl;

  EnggDiffCalibSettings cs = m_view->currentCalibSettings();
  Mantid::Kernel::ConfigServiceImpl &conf =
      Mantid::Kernel::ConfigService::Instance();
  const std::vector<std::string> tmpDirs = conf.getDataSearchDirs();
  // in principle, the run files will be found from 'DirRaw', and the
  // pre-calculated Vanadium corrections from 'DirCalib'
  if (!cs.m_inputDirCalib.empty() && Poco::File(cs.m_inputDirCalib).exists()) {
    conf.appendDataSearchDir(cs.m_inputDirCalib);
  }
  if (!cs.m_inputDirRaw.empty() && Poco::File(cs.m_inputDirRaw).exists())
    conf.appendDataSearchDir(cs.m_inputDirRaw);
  for (const auto &browsed : m_browsedToPaths) {
    conf.appendDataSearchDir(browsed);
  }

  try {
    m_calibFinishedOK = false;
    doCalib(cs, vanNo, ceriaNo, outFilename, specNos);
    m_calibFinishedOK = true;
  } catch (std::runtime_error &rexc) {
    g_log.error()
        << "The calibration calculations failed. One of the "
           "algorithms did not execute correctly. See log messages for "
           "further details. Error: " +
               std::string(rexc.what()) << std::endl;
  } catch (std::invalid_argument &) {
    g_log.error()
        << "The calibration calculations failed. Some input properties "
           "were not valid. See log messages for details. " << std::endl;
  }
  // restore normal data search paths
  conf.setDataSearchDirs(tmpDirs);
}

/**
* Method to call when the calibration work has finished, either from
* a separate thread or not (as in this presenter's' test).
*/
void EnggDiffractionPresenter::calibrationFinished() {
  if (!m_view)
    return;

  m_view->enableCalibrateAndFocusActions(true);
  if (!m_calibFinishedOK) {
    g_log.warning() << "The calibration did not finish correctly. Please "
                       "check previous log messages for details." << std::endl;
    m_view->showStatus("Calibration didn't finish succesfully. Ready");
  } else {
    const std::string vanNo = isValidRunNumber(m_view->newVanadiumNo());

    const std::string ceriaNo = isValidRunNumber(m_view->newCeriaNo());
    updateCalibParmsTable();
    m_view->newCalibLoaded(vanNo, ceriaNo, m_calibFullPath);
    g_log.notice() << "Calibration finished and ready as 'current calibration'."
                   << std::endl;
    m_view->showStatus("Calibration finished succesfully. Ready");
  }
  if (m_workerThread) {
    delete m_workerThread;
    m_workerThread = nullptr;
  }
}

/**
 * Build a suggested name for a new calibration, by appending instrument name,
 * relevant run numbers, etc., like: ENGINX_241391_236516_all_banks.par
 *
 * @param vanNo number of the Vanadium run
 * @param ceriaNo number of the Ceria run
 * @param bankName bank name when generating a file for an individual
 * bank. Leave empty to use a generic name for all banks
 *
 * @return Suggested name for a new calibration file, following
 * ENGIN-X practices
 */
std::string EnggDiffractionPresenter::buildCalibrateSuggestedFilename(
    const std::string &vanNo, const std::string &ceriaNo,
    const std::string &bankName) const {
  // default and only one supported instrument for now
  std::string instStr = g_enginxStr;
  std::string nameAppendix = "_all_banks";
  if (!bankName.empty()) {
    nameAppendix = "_" + bankName;
  }
  std::string curInst = m_view->currentInstrument();
  if ("ENGIN-X" != curInst && "ENGINX" != curInst) {
    instStr = "UNKNOWNINST";
    nameAppendix = "_calibration";
  }

  // default extension for calibration files
  const std::string calibExt = ".prm";
  std::string sugg =
      instStr + "_" + vanNo + "_" + ceriaNo + nameAppendix + calibExt;

  return sugg;
}

/**
* Calculate a calibration, responding the the "new calibration"
* action/button.
*
* @param cs user settings
* @param vanNo Vanadium run number
* @param ceriaNo Ceria run number
* @param outFilename output filename chosen by the user
* @param specNos SpecNos or bank name to be passed
*/
void EnggDiffractionPresenter::doCalib(const EnggDiffCalibSettings &cs,
                                       const std::string &vanNo,
                                       const std::string &ceriaNo,
                                       const std::string &outFilename,
                                       const std::string &specNos) {
  ITableWorkspace_sptr vanIntegWS;
  MatrixWorkspace_sptr vanCurvesWS;
  MatrixWorkspace_sptr ceriaWS;

  // save vanIntegWS and vanCurvesWS as open genie
  // see where spec number comes from

  loadOrCalcVanadiumWorkspaces(vanNo, cs.m_inputDirCalib, vanIntegWS,
                               vanCurvesWS, cs.m_forceRecalcOverwrite, specNos);

  const std::string instStr = m_view->currentInstrument();
  try {
    auto load =
        Mantid::API::AlgorithmManager::Instance().createUnmanaged("Load");
    load->initialize();
    load->setPropertyValue("Filename", instStr + ceriaNo);
    const std::string ceriaWSName = "engggui_calibration_sample_ws";
    load->setPropertyValue("OutputWorkspace", ceriaWSName);
    load->execute();

    AnalysisDataServiceImpl &ADS = Mantid::API::AnalysisDataService::Instance();
    ceriaWS = ADS.retrieveWS<MatrixWorkspace>(ceriaWSName);
  } catch (std::runtime_error &re) {
    g_log.error()
        << "Error while loading calibration sample data. "
           "Could not run the algorithm Load succesfully for the "
           "calibration "
           "sample (run number: " +
               ceriaNo + "). Error description: " + re.what() +
               " Please check also the previous log messages for details.";
    throw;
  }

  // Bank 1 and 2 - ENGIN-X
  // bank 1 - loops once & used for cropped calibration
  // bank 2 - loops twice, one with each bank & used for new calibration
  std::vector<double> difc, tzero;
  // for the names of the output files
  std::vector<std::string> bankNames;

  bool specNumUsed = specNos != "";
  // TODO: this needs sanitizing, it should be simpler
  if (specNumUsed) {
    constexpr size_t bankNo1 = 1;

    difc.resize(bankNo1);
    tzero.resize(bankNo1);
    int selection = m_view->currentCropCalibBankName();
    if (0 == selection) {
      // user selected "custom" name
      const std::string customName = m_view->currentCalibCustomisedBankName();
      if (customName.empty()) {
        bankNames.emplace_back("cropped");
      } else {
        bankNames.push_back(customName);
      }
    } else if (1 == selection) {
      bankNames.push_back("North");
    } else {
      bankNames.push_back("South");
    }
  } else {
    constexpr size_t bankNo2 = 2;

    difc.resize(bankNo2);
    tzero.resize(bankNo2);
    bankNames = {"North", "South"};
  }

  for (size_t i = 0; i < difc.size(); i++) {
    auto alg = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
        "EnggCalibrate");
    try {
      alg->initialize();
      alg->setProperty("InputWorkspace", ceriaWS);
      alg->setProperty("VanIntegrationWorkspace", vanIntegWS);
      alg->setProperty("VanCurvesWorkspace", vanCurvesWS);
      if (specNumUsed) {
        alg->setPropertyValue(g_calibCropIdentifier,
                              boost::lexical_cast<std::string>(specNos));
      } else {
        alg->setPropertyValue("Bank", boost::lexical_cast<std::string>(i + 1));
      }
      const std::string outFitParamsTblName =
          outFitParamsTblNameGenerator(specNos, i);
      alg->setPropertyValue("FittedPeaks", outFitParamsTblName);
      alg->setPropertyValue("OutputParametersTableName", outFitParamsTblName);
      alg->execute();
    } catch (std::runtime_error &re) {
      g_log.error() << "Error in calibration. ",
          "Could not run the algorithm EnggCalibrate succesfully for bank " +
              boost::lexical_cast<std::string>(i) + ". Error description: " +
              re.what() + " Please check also the log messages for details.";
      throw;
    }

    try {
      difc[i] = alg->getProperty("DIFC");
      tzero[i] = alg->getProperty("TZERO");
    } catch (std::runtime_error &rexc) {
      g_log.error() << "Error in calibration. ",
          "The calibration algorithm EnggCalibrate run succesfully but could "
          "not retrieve the outputs DIFC and TZERO. Error description: " +
              std::string(rexc.what()) +
              " Please check also the log messages for additional details.";
      throw;
    }

    g_log.notice() << " * Bank " << i + 1 << " calibrated, "
                   << "difc: " << difc[i] << ", zero: " << tzero[i]
                   << std::endl;
  }

  // Creates appropriate output directory
  const std::string calibrationComp = "Calibration";
  auto saveDir = outFilesUserDir(calibrationComp);
  Poco::Path outFullPath(saveDir);
  outFullPath.append(outFilename);

  // Double horror: 1st use a python script
  // 2nd: because runPythonCode does this by emitting a signal that goes to
  // MantidPlot, it has to be done in the view (which is a UserSubWindow).
  // First write the all banks parameters file
  m_calibFullPath = outFullPath.toString();
  writeOutCalibFile(m_calibFullPath, difc, tzero, bankNames, ceriaNo, vanNo);
  copyToGeneral(outFullPath, calibrationComp);
  m_currentCalibParms.clear();

  // Then write one individual file per bank, using different templates and the
  // specific bank name as suffix
  for (size_t bankIdx = 0; bankIdx < difc.size(); ++bankIdx) {
    Poco::Path bankOutputFullPath(saveDir);
    const std::string bankFilename = buildCalibrateSuggestedFilename(
        vanNo, ceriaNo, "bank_" + bankNames[bankIdx]);
    bankOutputFullPath.append(bankFilename);
    std::string templateFile = "template_ENGINX_241391_236516_North_bank.prm";
    if (1 == bankIdx) {
      templateFile = "template_ENGINX_241391_236516_South_bank.prm";
    }

    const std::string outPathName = bankOutputFullPath.toString();
    writeOutCalibFile(outPathName, {difc[bankIdx]}, {tzero[bankIdx]},
                      {bankNames[bankIdx]}, ceriaNo, vanNo, templateFile);
    copyToGeneral(bankOutputFullPath, calibrationComp);
    m_currentCalibParms.emplace_back(
        GSASCalibrationParms(bankIdx, difc[bankIdx], 0.0, tzero[bankIdx]));
    if (1 == difc.size()) {
      // it is a  single bank or cropped calibration, so take its specific name
      m_calibFullPath = outPathName;
    }
  }
  g_log.notice() << "Calibration file written as " << m_calibFullPath
                 << std::endl;

  // plots the calibrated workspaces.
  g_plottingCounter++;
  plotCalibWorkspace(difc, tzero, specNos);
}

/**
* Perform checks specific to normal/basic run focusing in addition to
* the general checks for any focusing (as done by
* inputChecksBeforeFocus() which is called from this method). Use
* always before running 'Focus'
*
* @param multi_RunNo vector of run number to focus
* @param banks which banks to consider in the focusing
*
* @throws std::invalid_argument with an informative message.
*/
void EnggDiffractionPresenter::inputChecksBeforeFocusBasic(
    const std::vector<std::string> &multi_RunNo,
    const std::vector<bool> &banks) {
  if (multi_RunNo.size() == 0) {
    const std::string msg = "The sample run number" + g_runNumberErrorStr;
    throw std::invalid_argument(msg);
  }

  inputChecksBanks(banks);

  inputChecksBeforeFocus();
}

/**
* Perform checks specific to focusing in "cropped" mode, in addition
* to the general checks for any focusing (as done by
* inputChecksBeforeFocus() which is called from this method). Use
* always before running 'FocusCropped'
*
* @param multi_RunNo vector of run number to focus
* @param banks which banks to consider in the focusing
* @param specNos list of spectra (as usual csv list of spectra in Mantid)
*
* @throws std::invalid_argument with an informative message.
*/
void EnggDiffractionPresenter::inputChecksBeforeFocusCropped(
    const std::vector<std::string> &multi_RunNo, const std::vector<bool> &banks,
    const std::string &specNos) {
  if (multi_RunNo.size() == 0) {
    throw std::invalid_argument("To focus cropped the sample run number" +
                                g_runNumberErrorStr);
  }

  if (specNos.empty()) {
    throw std::invalid_argument("The list of spectrum Nos cannot be empty when "
                                "focusing in 'cropped' mode.");
  }

  inputChecksBanks(banks);

  inputChecksBeforeFocus();
}

/**
* Perform checks specific to focusing in "texture" mode, in addition
* to the general checks for any focusing (as done by
* inputChecksBeforeFocus() which is called from this method). Use
* always before running 'FocusCropped'
*
* @param multi_RunNo vector of run number to focus
* @param dgFile file with detector grouping info
*
* @throws std::invalid_argument with an informative message.
*/
void EnggDiffractionPresenter::inputChecksBeforeFocusTexture(
    const std::vector<std::string> &multi_RunNo, const std::string &dgFile) {
  if (multi_RunNo.size() == 0) {
    throw std::invalid_argument("To focus texture banks the sample run number" +
                                g_runNumberErrorStr);
  }

  if (dgFile.empty()) {
    throw std::invalid_argument("A detector grouping file needs to be "
                                "specified when focusing texture banks.");
  }
  Poco::File dgf(dgFile);
  if (!dgf.exists()) {
    throw std::invalid_argument(
        "The detector grouping file coult not be found: " + dgFile);
  }

  inputChecksBeforeFocus();
}

void EnggDiffractionPresenter::inputChecksBanks(
    const std::vector<bool> &banks) {
  if (0 == banks.size()) {
    const std::string msg =
        "Error in specification of banks found when starting the "
        "focusing process. Cannot continue.";
    g_log.error() << msg << std::endl;
    throw std::invalid_argument(msg);
  }
  if (banks.end() == std::find(banks.begin(), banks.end(), true)) {
    const std::string msg =
        "EnggDiffraction GUI: not focusing, as none of the banks "
        "have been selected. You probably forgot to select at least one.";
    g_log.warning() << msg << std::endl;
    throw std::invalid_argument(msg);
  }
}

/**
* Performs several checks on the current focusing inputs and
* settings. This should be done before starting any focus work. The
* message return should be shown to the user as a visible message
* (pop-up, error log, etc.)
*
* @throws std::invalid_argument with an informative message.
*/
void EnggDiffractionPresenter::inputChecksBeforeFocus() {
  EnggDiffCalibSettings cs = m_view->currentCalibSettings();
  const std::string pixelCalib = cs.m_pixelCalibFilename;
  if (pixelCalib.empty()) {
    throw std::invalid_argument(
        "You need to set a pixel (full) calibration in settings.");
  }
}

/**
* Builds the names of the output focused files (one per bank), given
* the sample run number and which banks should be focused.
*
* @param runNo number of the run for which we want a focused output
* file name
*
* @param banks for every bank, (true/false) to consider it or not for
* the focusing
*
* @return filenames (without the full path)
*/
std::vector<std::string>
EnggDiffractionPresenter::outputFocusFilenames(const std::string &runNo,
                                               const std::vector<bool> &banks) {
  const std::string instStr = m_view->currentInstrument();
  std::vector<std::string> res;
  res.reserve(banks.size());
  std::string prefix = instStr + "_" + runNo + "_focused_bank_";
  for (size_t b = 1; b <= banks.size(); b++) {
    res.emplace_back(prefix + boost::lexical_cast<std::string>(b) + ".nxs");
  }
  return res;
}

std::string
EnggDiffractionPresenter::outputFocusCroppedFilename(const std::string &runNo) {
  const std::string instStr = m_view->currentInstrument();

  return instStr + "_" + runNo + "_focused_cropped.nxs";
}

std::vector<std::string> EnggDiffractionPresenter::sumOfFilesLoadVec() {
  std::vector<std::string> multi_RunNo;

  if (g_sumOfFilesFocus == "basic")
    multi_RunNo = isValidMultiRunNumber(m_view->focusingRunNo());
  else if (g_sumOfFilesFocus == "cropped")
    multi_RunNo = isValidMultiRunNumber(m_view->focusingCroppedRunNo());
  else if (g_sumOfFilesFocus == "texture")
    multi_RunNo = isValidMultiRunNumber(m_view->focusingTextureRunNo());

  return multi_RunNo;
}

std::vector<std::string> EnggDiffractionPresenter::outputFocusTextureFilenames(
    const std::string &runNo, const std::vector<size_t> &bankIDs) {
  const std::string instStr = m_view->currentInstrument();

  std::vector<std::string> res;
  res.reserve(bankIDs.size());
  std::string prefix = instStr + "_" + runNo + "_focused_texture_bank_";
  for (size_t b = 0; b < bankIDs.size(); b++) {
    res.emplace_back(prefix + boost::lexical_cast<std::string>(bankIDs[b]) +
                     ".nxs");
  }

  return res;
}

/**
* Start the focusing algorithm(s) without blocking the GUI. This is
* based on Qt connect / signals-slots so that it goes in sync with
* the Qt event loop. For that reason this class needs to be a
* Q_OBJECT.
*
* @param dir directory (full path) for the focused output files
* @param multi_RunNo input vector of run number
* @param banks instrument bank to focus
* @param specNos list of spectra (as usual csv list of spectra in Mantid)
* @param dgFile detector grouping file name
*/
void EnggDiffractionPresenter::startAsyncFocusWorker(
    const std::string &dir, const std::vector<std::string> &multi_RunNo,
    const std::vector<bool> &banks, const std::string &dgFile,
    const std::string &specNos) {

  delete m_workerThread;
  m_workerThread = new QThread(this);
  EnggDiffWorker *worker =
      new EnggDiffWorker(this, dir, multi_RunNo, banks, dgFile, specNos);
  worker->moveToThread(m_workerThread);
  connect(m_workerThread, SIGNAL(started()), worker, SLOT(focus()));
  connect(worker, SIGNAL(finished()), this, SLOT(focusingFinished()));
  // early delete of thread and worker
  connect(m_workerThread, SIGNAL(finished()), m_workerThread,
          SLOT(deleteLater()), Qt::DirectConnection);
  connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
  m_workerThread->start();
}

/**
* Produce a new focused output file. This is what threads/workers
* should use to run the calculations required to process a 'focus'
* push or similar from the user.
*
* @param dir directory (full path) for the output focused files
* @param runNo input run number
*
* @param specNos list of spectra to use when focusing. Not empty
* implies focusing in cropped mode.
*
* @param dgFile detector grouping file to define banks (texture). Not
* empty implies focusing in texture mode.
*
* @param banks for every bank, (true/false) to consider it or not for
* the focusing
*/
void EnggDiffractionPresenter::doFocusRun(const std::string &dir,
                                          const std::string &runNo,
                                          const std::vector<bool> &banks,
                                          const std::string &specNos,
                                          const std::string &dgFile) {

  if (g_abortThread) {
    return;
  }

  // to track last valid run
  g_lastValidRun = runNo;

  g_log.notice() << "Generating new focusing workspace(s) and file(s) into "
                    "this directory: " << dir << std::endl;

  // TODO: this is almost 100% common with doNewCalibrate() - refactor
  EnggDiffCalibSettings cs = m_view->currentCalibSettings();
  Mantid::Kernel::ConfigServiceImpl &conf =
      Mantid::Kernel::ConfigService::Instance();
  const std::vector<std::string> tmpDirs = conf.getDataSearchDirs();
  // in principle, the run files will be found from 'DirRaw', and the
  // pre-calculated Vanadium corrections from 'DirCalib'
  if (!cs.m_inputDirCalib.empty() && Poco::File(cs.m_inputDirCalib).exists()) {
    conf.appendDataSearchDir(cs.m_inputDirCalib);
  }
  if (!cs.m_inputDirRaw.empty() && Poco::File(cs.m_inputDirRaw).exists()) {
    conf.appendDataSearchDir(cs.m_inputDirRaw);
  }
  for (const auto &browsed : m_browsedToPaths) {
    conf.appendDataSearchDir(browsed);
  }

  // Prepare special inputs for "texture" focusing
  std::vector<size_t> bankIDs;
  std::vector<std::string> effectiveFilenames;
  std::vector<std::string> specs;
  if (!specNos.empty()) {
    // Cropped focusing
    // just to iterate once, but there's no real bank here
    bankIDs.push_back(0);
    specs.push_back(specNos); // one spectrum Nos list given by the user
    effectiveFilenames.push_back(outputFocusCroppedFilename(runNo));
  } else {
    if (dgFile.empty()) {
      // Basic/normal focusing
      for (size_t bidx = 0; bidx < banks.size(); bidx++) {
        if (banks[bidx]) {
          bankIDs.push_back(bidx + 1);
          specs.emplace_back("");
          effectiveFilenames = outputFocusFilenames(runNo, banks);
        }
      }
    } else {
      // texture focusing
      try {
        loadDetectorGroupingCSV(dgFile, bankIDs, specs);
      } catch (std::runtime_error &re) {
        g_log.error() << "Error loading detector grouping file: " + dgFile +
                             ". Detailed error: " + re.what() << std::endl;
        bankIDs.clear();
        specs.clear();
      }
      effectiveFilenames = outputFocusTextureFilenames(runNo, bankIDs);
    }
  }

  // focus all requested banks
  for (size_t idx = 0; idx < bankIDs.size(); idx++) {

    Poco::Path fpath(dir);
    const std::string fullFilename =
        fpath.append(effectiveFilenames[idx]).toString();
    g_log.notice() << "Generating new focused file (bank " +
                          boost::lexical_cast<std::string>(bankIDs[idx]) +
                          ") for run " + runNo +
                          " into: " << effectiveFilenames[idx] << std::endl;
    try {
      m_focusFinishedOK = false;
      doFocusing(cs, fullFilename, runNo, bankIDs[idx], specs[idx], dgFile);
      m_focusFinishedOK = true;
    } catch (std::runtime_error &rexc) {
      g_log.error() << "The focusing calculations failed. One of the algorithms"
                       "did not execute correctly. See log messages for "
                       "further details. Error: " +
                           std::string(rexc.what()) << std::endl;
    } catch (std::invalid_argument &ia) {
      g_log.error() << "The focusing failed. Some input properties "
                       "were not valid. "
                       "See log messages for details. Error: " << ia.what()
                    << std::endl;
    }
  }

  // restore initial data search paths
  conf.setDataSearchDirs(tmpDirs);
}

void EnggDiffractionPresenter::loadDetectorGroupingCSV(
    const std::string &dgFile, std::vector<size_t> &bankIDs,
    std::vector<std::string> &specs) {
  const char commentChar = '#';
  const std::string delim = ",";

  std::ifstream file(dgFile.c_str());
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open file.");
  }

  bankIDs.clear();
  specs.clear();
  std::string line;
  for (size_t li = 1; getline(file, line); li++) {
    if (line.empty() || commentChar == line[0])
      continue;

    auto delimPos = line.find_first_of(delim);
    if (std::string::npos == delimPos) {
      throw std::runtime_error(
          "In file '" + dgFile + "', wrong format in line: " +
          boost::lexical_cast<std::string>(li) +
          " which does not contain any delimiters (comma, etc.)");
    }

    try {
      const std::string bstr = line.substr(0, delimPos);
      const std::string spec = line.substr(delimPos + 1, std::string::npos);

      if (bstr.empty()) {
        throw std::runtime_error(
            "In file '" + dgFile + "', wrong format in line: " +
            boost::lexical_cast<std::string>(li) + ", the bank ID is empty!");
      }
      if (spec.empty()) {
        throw std::runtime_error("In file '" + dgFile +
                                 "', wrong format in line: " +
                                 boost::lexical_cast<std::string>(li) +
                                 ", the list of spectrum Nos is empty!");
      }

      size_t bankID = boost::lexical_cast<size_t>(bstr);
      bankIDs.push_back(bankID);
      specs.push_back(spec);
    } catch (std::runtime_error &re) {
      throw std::runtime_error(
          "In file '" + dgFile +
          "', issue found when trying to interpret line: " +
          boost::lexical_cast<std::string>(li) + ". Error description: " +
          re.what());
    }
  }
}

/**
* Method (Qt slot) to call when the focusing work has finished,
* possibly from a separate thread but sometimes not (as in this
* presenter class' test).
*/
void EnggDiffractionPresenter::focusingFinished() {
  if (!m_view)
    return;

  if (!m_focusFinishedOK) {
    g_log.warning() << "The focusing did not finish correctly. Check previous "
                       "log messages for details" << std::endl;
    m_view->showStatus("Focusing didn't finish succesfully. Ready");
  } else {
    g_log.notice() << "Focusing finished - focused run(s) are ready."
                   << std::endl;
    m_view->showStatus("Focusing finished succesfully. Ready");
  }
  if (m_workerThread) {
    delete m_workerThread;
    m_workerThread = NULL;
  }

  m_view->enableCalibrateAndFocusActions(true);

  // display warning and information to the users regarding Stop Focus
  if (g_abortThread) {
    // will get the last number in the list
    std::string last_RunNo = isValidRunNumber(m_view->focusingRunNo());
    double lastRun = boost::lexical_cast<double>(last_RunNo);
    double lastValid = boost::lexical_cast<double>(g_lastValidRun);

    if (lastRun != lastValid) {
      g_log.warning()
          << "Focussing process has been stopped, last successful "
             "run number: " << g_lastValidRun
          << " , total number of focus run that could not be processed: "
          << (lastRun - lastValid) << std::endl;
      m_view->showStatus("Focusing stopped. Ready");
    }
  }
}

/**
* Focuses a run, produces a focused workspace, and saves it into a
* file.
*
* @param cs user settings for calibration (this does not calibrate but
* uses calibration input files such as vanadium runs
*
* @param fullFilename full path for the output (focused) filename
*
* @param runNo input run to focus
*
* @param bank instrument bank number to focus
*
* @param specNos string specifying a list of spectra (for "cropped"
* focusing or "texture" focusing), only considered if not empty
*
* @param dgFile detector grouping file name. If not empty implies
* texture focusing
*/
void EnggDiffractionPresenter::doFocusing(const EnggDiffCalibSettings &cs,
                                          const std::string &fullFilename,
                                          const std::string &runNo, size_t bank,
                                          const std::string &specNos,
                                          const std::string &dgFile) {
  ITableWorkspace_sptr vanIntegWS;
  MatrixWorkspace_sptr vanCurvesWS;
  MatrixWorkspace_sptr inWS;

  const std::string vanNo = m_view->currentVanadiumNo();
  loadOrCalcVanadiumWorkspaces(vanNo, cs.m_inputDirCalib, vanIntegWS,
                               vanCurvesWS, cs.m_forceRecalcOverwrite, "");

  const std::string inWSName = "engggui_focusing_input_ws";
  const std::string instStr = m_view->currentInstrument();
  std::vector<std::string> multi_RunNo = sumOfFilesLoadVec();
  std::string loadInput = "";

  for (size_t i = 0; i < multi_RunNo.size(); i++) {
    // if last run number in list
    if (i + 1 == multi_RunNo.size())
      loadInput += instStr + multi_RunNo[i];
    else
      loadInput += instStr + multi_RunNo[i] + '+';
  }

  // if its not empty the global variable is set for sumOfFiles
  if (!g_sumOfFilesFocus.empty()) {

    try {
      auto load =
          Mantid::API::AlgorithmManager::Instance().createUnmanaged("Load");
      load->initialize();
      load->setPropertyValue("Filename", loadInput);

      load->setPropertyValue("OutputWorkspace", inWSName);
      load->execute();

      AnalysisDataServiceImpl &ADS =
          Mantid::API::AnalysisDataService::Instance();
      inWS = ADS.retrieveWS<MatrixWorkspace>(inWSName);
    } catch (std::runtime_error &re) {
      g_log.error()
          << "Error while loading files provided. "
             "Could not run the algorithm Load succesfully for the focus "
             "(run number provided. Error description:"
             "Please check also the previous log messages for details." +
                 static_cast<std::string>(re.what());
      throw;
    }

    if (multi_RunNo.size() == 1) {
      g_log.notice() << "Only single file has been listed, the Sum Of Files"
                        "cannot not be processed" << std::endl;
    } else {
      g_log.notice()
          << "Load alogirthm has successfully merged the files provided"
          << std::endl;
    }

  } else {
    try {
      auto load =
          Mantid::API::AlgorithmManager::Instance().createUnmanaged("Load");
      load->initialize();
      load->setPropertyValue("Filename", instStr + runNo);
      load->setPropertyValue("OutputWorkspace", inWSName);
      load->execute();

      AnalysisDataServiceImpl &ADS =
          Mantid::API::AnalysisDataService::Instance();
      inWS = ADS.retrieveWS<MatrixWorkspace>(inWSName);
    } catch (std::runtime_error &re) {
      g_log.error() << "Error while loading sample data for focusing. "
                       "Could not run the algorithm Load succesfully for "
                       "the focusing "
                       "sample (run number: " +
                           runNo + "). Error description: " + re.what() +
                           " Please check also the previous log messages "
                           "for details.";
      throw;
    }
  }

  std::string outWSName;
  std::string specNumsOpenGenie;
  if (!dgFile.empty()) {
    // doing focus "texture"
    outWSName = "engggui_focusing_output_ws_texture_bank_" +
                boost::lexical_cast<std::string>(bank);
    specNumsOpenGenie = specNos;
  } else if (specNos.empty()) {
    // doing focus "normal" / by banks
    outWSName = "engggui_focusing_output_ws_bank_" +
                boost::lexical_cast<std::string>(bank);

    // specnum for opengenie according to bank number
    if (boost::lexical_cast<std::string>(bank) == "1") {
      specNumsOpenGenie = "1 - 1200";
    } else if (boost::lexical_cast<std::string>(bank) == "2") {
      specNumsOpenGenie = "1201 - 1400";
    }

  } else {
    // doing focus "cropped"
    outWSName = "engggui_focusing_output_ws_cropped";
    specNumsOpenGenie = specNos;
  }
  try {
    auto alg =
        Mantid::API::AlgorithmManager::Instance().createUnmanaged("EnggFocus");
    alg->initialize();
    alg->setProperty("InputWorkspace", inWSName);
    alg->setProperty("OutputWorkspace", outWSName);
    alg->setProperty("VanIntegrationWorkspace", vanIntegWS);
    alg->setProperty("VanCurvesWorkspace", vanCurvesWS);
    // cropped / normal focusing
    if (specNos.empty()) {
      alg->setPropertyValue("Bank", boost::lexical_cast<std::string>(bank));
    } else {
      alg->setPropertyValue("SpectrumNumbers", specNos);
    }
    // TODO: use detector positions (from calibrate full) when available
    // alg->setProperty(DetectorPositions, TableWorkspace)
    alg->execute();
    g_plottingCounter++;
    plotFocusedWorkspace(outWSName);

  } catch (std::runtime_error &re) {
    g_log.error() << "Error in calibration. ",
        "Could not run the algorithm EnggCalibrate succesfully for bank " +
            boost::lexical_cast<std::string>(bank) + ". Error description: " +
            re.what() + " Please check also the log messages for details.";
    throw;
  }
  g_log.notice() << "Produced focused workspace: " << outWSName << std::endl;

  try {
    g_log.debug() << "Going to save focused output into nexus file: "
                  << fullFilename << std::endl;
    auto alg =
        Mantid::API::AlgorithmManager::Instance().createUnmanaged("SaveNexus");
    alg->initialize();
    alg->setPropertyValue("InputWorkspace", outWSName);
    alg->setPropertyValue("Filename", fullFilename);
    alg->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Error in calibration. ",
        "Could not run the algorithm EnggCalibrate succesfully for bank " +
            boost::lexical_cast<std::string>(bank) + ". Error description: " +
            re.what() + " Please check also the log messages for details.";
    throw;
  }
  g_log.notice() << "Saved focused workspace as file: " << fullFilename
                 << std::endl;

  copyFocusedToUserAndAll(fullFilename);

  bool saveOutputFiles = m_view->saveFocusedOutputFiles();

  if (saveOutputFiles) {
    try {
      saveFocusedXYE(outWSName, boost::lexical_cast<std::string>(bank), runNo);
      saveGSS(outWSName, boost::lexical_cast<std::string>(bank), runNo);
      saveOpenGenie(outWSName, specNumsOpenGenie,
                    boost::lexical_cast<std::string>(bank), runNo);
    } catch (std::runtime_error &re) {
      g_log.error() << "Error saving focused data. ",
          "There was an error while saving focused data. "
          "Error Description: " +
              std::string(re.what()) +
              "Please check log messages for more details.";
      throw;
    }
  }
}

/**
* Produce the two workspaces that are required to apply Vanadium
* corrections. Try to load them if precalculated results are
* available from files, otherwise load the source Vanadium run
* workspace and do the calculations.
*
* @param vanNo Vanadium run number
*
* @param inputDirCalib The 'calibration files' input directory given
* in settings
*
* @param vanIntegWS workspace where to create/load the Vanadium
* spectra integration
*
* @param vanCurvesWS workspace where to create/load the Vanadium
* aggregated per-bank curve
*
* @param forceRecalc whether to calculate Vanadium corrections even
* if the files of pre-calculated results are found
*
*
* @param specNos string carrying cropped calib info: spectra to use
* when cropped calibrating.
*/
void EnggDiffractionPresenter::loadOrCalcVanadiumWorkspaces(
    const std::string &vanNo, const std::string &inputDirCalib,
    ITableWorkspace_sptr &vanIntegWS, MatrixWorkspace_sptr &vanCurvesWS,
    bool forceRecalc, const std::string specNos) {
  bool foundPrecalc = false;

  std::string preIntegFilename, preCurvesFilename;
  findPrecalcVanadiumCorrFilenames(vanNo, inputDirCalib, preIntegFilename,
                                   preCurvesFilename, foundPrecalc);

  // if pre caluclated not found ..
  if (forceRecalc || !foundPrecalc) {
    g_log.notice() << "Calculating Vanadium corrections. This may take a "
                      "few seconds..." << std::endl;
    try {
      calcVanadiumWorkspaces(vanNo, vanIntegWS, vanCurvesWS);
    } catch (std::invalid_argument &ia) {
      g_log.error() << "Failed to calculate Vanadium corrections. "
                       "There was an error in the execution of the algorithms "
                       "required to calculate Vanadium corrections. Some "
                       "properties passed to the algorithms were invalid. "
                       "This is possibly because some of the settings are not "
                       "consistent. Please check the log messages for "
                       "details. Details: " +
                           std::string(ia.what()) << std::endl;
      throw;
    } catch (std::runtime_error &re) {
      g_log.error() << "Failed to calculate Vanadium corrections. "
                       "There was an error while executing one of the "
                       "algorithms used to perform Vanadium corrections. "
                       "There was no obvious error in the input properties "
                       "but the algorithm failed. Please check the log "
                       "messages for details." +
                           std::string(re.what()) << std::endl;
      throw;
    }
  } else {
    g_log.notice() << "Found precalculated Vanadium correction features for "
                      "Vanadium run " << vanNo
                   << ". Re-using these files: " << preIntegFilename << ", and "
                   << preCurvesFilename << std::endl;
    try {
      loadVanadiumPrecalcWorkspaces(preIntegFilename, preCurvesFilename,
                                    vanIntegWS, vanCurvesWS, vanNo, specNos);
    } catch (std::invalid_argument &ia) {
      g_log.error() << "Error while loading precalculated Vanadium corrections",
          "The files with precalculated Vanadium corection features (spectra "
          "integration and per-bank curves) were found (with names '" +
              preIntegFilename + "' and '" + preCurvesFilename +
              "', respectively, but there was a problem with the inputs to "
              "the "
              "load algorithms to load them: " +
              std::string(ia.what());
      throw;
    } catch (std::runtime_error &re) {
      g_log.error() << "Error while loading precalculated Vanadium corrections",
          "The files with precalculated Vanadium corection features (spectra "
          "integration and per-bank curves) were found (with names '" +
              preIntegFilename + "' and '" + preCurvesFilename +
              "', respectively, but there was a problem while loading them. "
              "Please check the log messages for details. You might want to "
              "delete those files or force recalculations (in settings). "
              "Error "
              "details: " +
              std::string(re.what());
      throw;
    }
  }
}

/**
* builds the expected names of the precalculated Vanadium correction
* files and tells if both files are found, similar to:
* ENGINX_precalculated_vanadium_run000236516_integration.nxs
* ENGINX_precalculated_vanadium_run00236516_bank_curves.nxs
*
* @param vanNo Vanadium run number
* @param inputDirCalib calibration directory in settings
* @param preIntegFilename if not found on disk, the string is set as empty
* @param preCurvesFilename if not found on disk, the string is set as empty
* @param found true if both files are found and (re-)usable
*/
void EnggDiffractionPresenter::findPrecalcVanadiumCorrFilenames(
    const std::string &vanNo, const std::string &inputDirCalib,
    std::string &preIntegFilename, std::string &preCurvesFilename,
    bool &found) {
  found = false;

  const std::string runNo = std::string(2, '0').append(vanNo);

  preIntegFilename =
      g_enginxStr + "_precalculated_vanadium_run" + runNo + "_integration.nxs";

  preCurvesFilename =
      g_enginxStr + "_precalculated_vanadium_run" + runNo + "_bank_curves.nxs";

  Poco::Path pathInteg(inputDirCalib);
  pathInteg.append(preIntegFilename);

  Poco::Path pathCurves(inputDirCalib);
  pathCurves.append(preCurvesFilename);

  if (Poco::File(pathInteg).exists() && Poco::File(pathCurves).exists()) {
    preIntegFilename = pathInteg.toString();
    preCurvesFilename = pathCurves.toString();
    found = true;
  }
}

/**
* Load precalculated results from Vanadium corrections previously
* calculated.
*
* @param preIntegFilename filename (can be full path) where the
* vanadium spectra integration table should be loaded from
*
* @param preCurvesFilename filename (can be full path) where the
* vanadium per-bank curves should be loaded from
*
* @param vanIntegWS output (matrix) workspace loaded from the
* precalculated Vanadium correction file, with the integration
* resutls
*
* @param vanCurvesWS output (matrix) workspace loaded from the
* precalculated Vanadium correction file, with the per-bank curves
*
* @param vanNo the Vanadium run number used for the openGenieAscii
* output label
*
* @param specNos string carrying cropped calib info: spectra to use
* when cropped calibrating.
*/
void EnggDiffractionPresenter::loadVanadiumPrecalcWorkspaces(
    const std::string &preIntegFilename, const std::string &preCurvesFilename,
    ITableWorkspace_sptr &vanIntegWS, MatrixWorkspace_sptr &vanCurvesWS,
    const std::string &vanNo, const std::string specNos) {
  AnalysisDataServiceImpl &ADS = Mantid::API::AnalysisDataService::Instance();

  auto alg =
      Mantid::API::AlgorithmManager::Instance().createUnmanaged("LoadNexus");
  alg->initialize();
  alg->setPropertyValue("Filename", preIntegFilename);
  std::string integWSName = g_vanIntegrationWSName;
  alg->setPropertyValue("OutputWorkspace", integWSName);
  alg->execute();
  // alg->getProperty("OutputWorkspace");
  vanIntegWS = ADS.retrieveWS<ITableWorkspace>(integWSName);

  auto algCurves =
      Mantid::API::AlgorithmManager::Instance().createUnmanaged("LoadNexus");
  algCurves->initialize();
  algCurves->setPropertyValue("Filename", preCurvesFilename);
  const std::string curvesWSName = g_vanCurvesWSName;
  algCurves->setPropertyValue("OutputWorkspace", curvesWSName);
  algCurves->execute();
  // algCurves->getProperty("OutputWorkspace");
  vanCurvesWS = ADS.retrieveWS<MatrixWorkspace>(curvesWSName);

  const std::string specNosBank1 = "1-2000";
  const std::string specNosBank2 = "1201-2400";
  const std::string northBank = "North";
  const std::string southBank = "South";

  if (specNos != "") {
    if (specNos == northBank) {
      // when north bank is selected while cropped calib
      saveOpenGenie(curvesWSName, specNosBank1, northBank, vanNo);
    } else if (specNos == southBank) {
      // when south bank is selected while cropped calib
      saveOpenGenie(curvesWSName, specNosBank2, southBank, vanNo);
    } else {

      // when SpectrumNos are provided
      std::string CustomisedBankName = m_view->currentCalibCustomisedBankName();

      // assign default value if empty string passed
      if (CustomisedBankName.empty())
        CustomisedBankName = "cropped";

      saveOpenGenie(curvesWSName, specNos, CustomisedBankName, vanNo);
    }
  } else {
    // when full calibration is carried; saves both banks
    saveOpenGenie(curvesWSName, specNosBank1, northBank, vanNo);
    saveOpenGenie(curvesWSName, specNosBank2, southBank, vanNo);
  }
}

/**
* Calculate vanadium corrections (in principle only for when
* pre-calculated results are not available). This is expensive.
*
* @param vanNo Vanadium run number
*
* @param vanIntegWS where to keep the Vanadium run spectra
* integration values
*
* @param vanCurvesWS workspace where to keep the per-bank vanadium
* curves
*/
void EnggDiffractionPresenter::calcVanadiumWorkspaces(
    const std::string &vanNo, ITableWorkspace_sptr &vanIntegWS,
    MatrixWorkspace_sptr &vanCurvesWS) {

  auto load = Mantid::API::AlgorithmManager::Instance().createUnmanaged("Load");
  load->initialize();
  load->setPropertyValue("Filename",
                         vanNo); // TODO more specific build Vanadium filename
  std::string vanWSName = "engggui_vanadium_ws";
  load->setPropertyValue("OutputWorkspace", vanWSName);
  load->execute();
  AnalysisDataServiceImpl &ADS = Mantid::API::AnalysisDataService::Instance();
  MatrixWorkspace_sptr vanWS = ADS.retrieveWS<MatrixWorkspace>(vanWSName);
  // TODO?: maybe use setChild() and then
  // load->getProperty("OutputWorkspace");

  auto alg = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
      "EnggVanadiumCorrections");
  alg->initialize();
  alg->setProperty("VanadiumWorkspace", vanWS);
  std::string integName = g_vanIntegrationWSName;
  alg->setPropertyValue("OutIntegrationWorkspace", integName);
  const std::string curvesName = g_vanCurvesWSName;
  alg->setPropertyValue("OutCurvesWorkspace", curvesName);
  alg->execute();

  ADS.remove(vanWSName);

  vanIntegWS = ADS.retrieveWS<ITableWorkspace>(integName);
  vanCurvesWS = ADS.retrieveWS<MatrixWorkspace>(curvesName);
}

/**
* Loads a workspace to pre-process (rebin, etc.). The workspace
* loaded can be a MatrixWorkspace or a group of MatrixWorkspace (for
* multiperiod data).
*
* @param runNo run number to search for the file with 'Load'.
*/
Workspace_sptr
EnggDiffractionPresenter::loadToPreproc(const std::string runNo) {
  const std::string instStr = m_view->currentInstrument();
  Workspace_sptr inWS;

  // this is required when file is selected via browse button
  const auto MultiRunNoDir = m_view->currentPreprocRunNo();
  const auto runNoDir = MultiRunNoDir[0];

  try {
    auto load =
        Mantid::API::AlgorithmManager::Instance().createUnmanaged("Load");
    load->initialize();
    if (Poco::File(runNoDir).exists()) {
      load->setPropertyValue("Filename", runNoDir);
    } else {
      load->setPropertyValue("Filename", instStr + runNo);
    }
    const std::string inWSName = "engggui_preproc_input_ws";
    load->setPropertyValue("OutputWorkspace", inWSName);

    load->execute();

    auto &ADS = Mantid::API::AnalysisDataService::Instance();
    inWS = ADS.retrieveWS<Workspace>(inWSName);
  } catch (std::runtime_error &re) {
    g_log.error()
        << "Error while loading run data to pre-process. "
           "Could not run the algorithm Load succesfully for the run "
           "number: " +
               runNo + "). Error description: " + re.what() +
               " Please check also the previous log messages for details.";
    throw;
  }

  return inWS;
}

void EnggDiffractionPresenter::doRebinningTime(const std::string &runNo,
                                               double bin,
                                               const std::string &outWSName) {

  // Runs something like:
  // Rebin(InputWorkspace='ws_runNo', outputWorkspace=outWSName,Params=bin)

  m_rebinningFinishedOK = false;
  const Workspace_sptr inWS = loadToPreproc(runNo);
  if (!inWS)
    g_log.error() << "Error: could not load the input workspace for rebinning."
                  << std::endl;

  const std::string rebinName = "Rebin";
  try {
    auto alg =
        Mantid::API::AlgorithmManager::Instance().createUnmanaged(rebinName);
    alg->initialize();
    alg->setPropertyValue("InputWorkspace", inWS->name());
    alg->setPropertyValue("OutputWorkspace", outWSName);
    alg->setProperty("Params", boost::lexical_cast<std::string>(bin));

    alg->execute();
  } catch (std::invalid_argument &ia) {
    g_log.error() << "Error when rebinning with a regular bin width in time. "
                     "There was an error in the inputs to the algorithm " +
                         rebinName + ". Error description: " + ia.what() +
                         "." << std::endl;
    return;
  } catch (std::runtime_error &re) {
    g_log.error() << "Error when rebinning with a regular bin width in time. "
                     "Coult not run the algorithm " +
                         rebinName + " successfully. Error description: " +
                         re.what() + "." << std::endl;
    return;
  }

  // succesful completion
  m_rebinningFinishedOK = true;
}

void EnggDiffractionPresenter::inputChecksBeforeRebin(
    const std::string &runNo) {
  if (runNo.empty()) {
    throw std::invalid_argument("The run to pre-process" + g_runNumberErrorStr);
  }
}

void EnggDiffractionPresenter::inputChecksBeforeRebinTime(
    const std::string &runNo, double bin) {
  inputChecksBeforeRebin(runNo);

  if (bin <= 0) {
    throw std::invalid_argument("The bin width must be strictly positive");
  }
}

/**
* Starts the Rebin algorithm(s) without blocking the GUI. This is
* based on Qt connect / signals-slots so that it goes in sync with
* the Qt event loop. For that reason this class needs to be a
* Q_OBJECT.
*
* @param runNo run number(s)
* @param bin bin width parameter for Rebin
* @param outWSName name for the output workspace produced here
*/
void EnggDiffractionPresenter::startAsyncRebinningTimeWorker(
    const std::string &runNo, double bin, const std::string &outWSName) {

  delete m_workerThread;
  m_workerThread = new QThread(this);
  EnggDiffWorker *worker = new EnggDiffWorker(this, runNo, bin, outWSName);
  worker->moveToThread(m_workerThread);

  connect(m_workerThread, SIGNAL(started()), worker, SLOT(rebinTime()));
  connect(worker, SIGNAL(finished()), this, SLOT(rebinningFinished()));
  // early delete of thread and worker
  connect(m_workerThread, SIGNAL(finished()), m_workerThread,
          SLOT(deleteLater()), Qt::DirectConnection);
  connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
  m_workerThread->start();
}

void EnggDiffractionPresenter::inputChecksBeforeRebinPulses(
    const std::string &runNo, size_t nperiods, double timeStep) {
  inputChecksBeforeRebin(runNo);

  if (0 == nperiods) {
    throw std::invalid_argument("The number of periods has been set to 0 so "
                                "none of the periods will be processed");
  }

  if (timeStep <= 0) {
    throw std::invalid_argument(
        "The bin or step for the time axis must be strictly positive");
  }
}

void EnggDiffractionPresenter::doRebinningPulses(const std::string &runNo,
                                                 size_t nperiods,
                                                 double timeStep,
                                                 const std::string &outWSName) {
  // TOOD: not clear what will be the role of this parameter for now
  UNUSED_ARG(nperiods);

  // Runs something like:
  // RebinByPulseTimes(InputWorkspace='ws_runNo', outputWorkspace=outWSName,
  //                   Params=timeStepstep)

  m_rebinningFinishedOK = false;
  const Workspace_sptr inWS = loadToPreproc(runNo);
  if (!inWS)
    g_log.error() << "Error: could not load the input workspace for rebinning."
                  << std::endl;

  const std::string rebinName = "RebinByPulseTimes";
  try {
    auto alg =
        Mantid::API::AlgorithmManager::Instance().createUnmanaged(rebinName);
    alg->initialize();
    alg->setPropertyValue("InputWorkspace", inWS->name());
    alg->setPropertyValue("OutputWorkspace", outWSName);
    alg->setProperty("Params", boost::lexical_cast<std::string>(timeStep));

    alg->execute();
  } catch (std::invalid_argument &ia) {
    g_log.error() << "Error when rebinning by pulse times. "
                     "There was an error in the inputs to the algorithm " +
                         rebinName + ". Error description: " + ia.what() +
                         "." << std::endl;
    return;
  } catch (std::runtime_error &re) {
    g_log.error() << "Error when rebinning by pulse times. "
                     "Coult not run the algorithm " +
                         rebinName + " successfully. Error description: " +
                         re.what() + "." << std::endl;
    return;
  }

  // successful execution
  m_rebinningFinishedOK = true;
}

/**
* Starts the Rebin (by pulses) algorithm(s) without blocking the
* GUI. This is based on Qt connect / signals-slots so that it goes in
* sync with the Qt event loop. For that reason this class needs to be
* a Q_OBJECT.
*
* @param runNo run number(s)
* @param nperiods max number of periods to process
* @param timeStep bin width parameter for the x (time) axis
* @param outWSName name for the output workspace produced here
*/
void EnggDiffractionPresenter::startAsyncRebinningPulsesWorker(
    const std::string &runNo, size_t nperiods, double timeStep,
    const std::string &outWSName) {

  delete m_workerThread;
  m_workerThread = new QThread(this);
  EnggDiffWorker *worker =
      new EnggDiffWorker(this, runNo, nperiods, timeStep, outWSName);
  worker->moveToThread(m_workerThread);

  connect(m_workerThread, SIGNAL(started()), worker, SLOT(rebinPulses()));
  connect(worker, SIGNAL(finished()), this, SLOT(rebinningFinished()));
  // early delete of thread and worker
  connect(m_workerThread, SIGNAL(finished()), m_workerThread,
          SLOT(deleteLater()), Qt::DirectConnection);
  connect(worker, SIGNAL(finished()), worker, SLOT(deleteLater()));
  m_workerThread->start();
}

/**
* Method (Qt slot) to call when the rebin work has finished,
* possibly from a separate thread but sometimes not (as in this
* presenter class' test).
*/
void EnggDiffractionPresenter::rebinningFinished() {
  if (!m_view)
    return;

  if (!m_rebinningFinishedOK) {
    g_log.warning() << "The pre-processing (re-binning) did not finish "
                       "correctly. Check previous log messages for details"
                    << std::endl;
    m_view->showStatus("Rebinning didn't finish succesfully. Ready");
  } else {
    g_log.notice() << "Pre-processing (re-binning) finished - the output "
                      "workspace is ready." << std::endl;
    m_view->showStatus("Rebinning finished succesfully. Ready");
  }
  if (m_workerThread) {
    delete m_workerThread;
    m_workerThread = NULL;
  }

  m_view->enableCalibrateAndFocusActions(true);
}

/**
* Checks the plot type selected and applies the appropriate
* python function to apply during first bank and second bank
*
* @param outWSName title of the focused workspace
*/
void EnggDiffractionPresenter::plotFocusedWorkspace(std::string outWSName) {
  const bool plotFocusedWS = m_view->focusedOutWorkspace();
  enum PlotMode { REPLACING = 0, WATERFALL = 1, MULTIPLE = 2 };

  int plotType = m_view->currentPlotType();

  if (plotFocusedWS) {
    if (plotType == PlotMode::REPLACING) {
      if (g_plottingCounter == 1)
        m_view->plotFocusedSpectrum(outWSName);
      else
        m_view->plotReplacingWindow(outWSName, "0", "0");

    } else if (plotType == PlotMode::WATERFALL) {
      if (g_plottingCounter == 1)
        m_view->plotFocusedSpectrum(outWSName);
      else
        m_view->plotWaterfallSpectrum(outWSName);

    } else if (plotType == PlotMode::MULTIPLE) {
      m_view->plotFocusedSpectrum(outWSName);
    }
  }
}

/**
* Check if the plot calibration check-box is ticked
* python script is passed on to mantid python window
* which plots the workspaces with customisation
*
* @param difc vector of double passed on to py script
* @param tzero vector of double to plot graph
* @param specNos string carrying cropped calib info
*/
void EnggDiffractionPresenter::plotCalibWorkspace(std::vector<double> difc,
                                                  std::vector<double> tzero,
                                                  std::string specNos) {
  const bool plotCalibWS = m_view->plotCalibWorkspace();
  if (plotCalibWS) {
    std::string pyCode = vanadiumCurvesPlotFactory();
    m_view->plotCalibOutput(pyCode);

    // Get the Customised Bank Name text-ield string from qt
    std::string CustomisedBankName = m_view->currentCalibCustomisedBankName();
    if (CustomisedBankName.empty())
      CustomisedBankName = "cropped";
    const std::string pythonCode =
        DifcZeroWorkspaceFactory(difc, tzero, specNos, CustomisedBankName) +
        plotDifcZeroWorkspace(CustomisedBankName);
    m_view->plotCalibOutput(pythonCode);
  }
}

/**
* Convert the generated output files and saves them in
* FocusedXYE format
*
* @param inputWorkspace title of the focused workspace
* @param bank the number of the bank as a string
* @param runNo the run number as a string
*/
void EnggDiffractionPresenter::saveFocusedXYE(const std::string inputWorkspace,
                                              std::string bank,
                                              std::string runNo) {

  // Generates the file name in the appropriate format
  std::string fullFilename =
      outFileNameFactory(inputWorkspace, runNo, bank, ".dat");

  const std::string focusingComp = "Focus";
  // Creates appropriate directory
  auto saveDir = outFilesUserDir(focusingComp);

  // append the full file name in the end
  saveDir.append(fullFilename);

  try {
    g_log.debug() << "Going to save focused output into OpenGenie file: "
                  << fullFilename << std::endl;
    auto alg = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
        "SaveFocusedXYE");
    alg->initialize();
    alg->setProperty("InputWorkspace", inputWorkspace);
    const std::string filename(saveDir.toString());
    alg->setPropertyValue("Filename", filename);
    alg->setProperty("SplitFiles", false);
    alg->setPropertyValue("StartAtBankNumber", bank);
    alg->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Error in saving FocusedXYE format file. ",
        "Could not run the algorithm SaveFocusXYE succesfully for "
        "workspace " +
            inputWorkspace + ". Error description: " + re.what() +
            " Please check also the log messages for details.";
    throw;
  }
  g_log.notice() << "Saved focused workspace as file: " << saveDir.toString()
                 << std::endl;
  copyToGeneral(saveDir, focusingComp);
}

/**
* Convert the generated output files and saves them in
* GSS format
*
* @param inputWorkspace title of the focused workspace
* @param bank the number of the bank as a string
* @param runNo the run number as a string
*/
void EnggDiffractionPresenter::saveGSS(const std::string inputWorkspace,
                                       std::string bank, std::string runNo) {

  // Generates the file name in the appropriate format
  std::string fullFilename =
      outFileNameFactory(inputWorkspace, runNo, bank, ".gss");

  const std::string focusingComp = "Focus";
  // Creates appropriate directory
  auto saveDir = outFilesUserDir(focusingComp);

  // append the full file name in the end
  saveDir.append(fullFilename);

  try {
    g_log.debug() << "Going to save focused output into OpenGenie file: "
                  << fullFilename << std::endl;
    auto alg =
        Mantid::API::AlgorithmManager::Instance().createUnmanaged("SaveGSS");
    alg->initialize();
    alg->setProperty("InputWorkspace", inputWorkspace);
    std::string filename(saveDir.toString());
    alg->setPropertyValue("Filename", filename);
    alg->setProperty("SplitFiles", false);
    alg->setPropertyValue("Bank", bank);
    alg->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Error in saving GSS format file. ",
        "Could not run the algorithm saveGSS succesfully for "
        "workspace " +
            inputWorkspace + ". Error description: " + re.what() +
            " Please check also the log messages for details.";
    throw;
  }
  g_log.notice() << "Saved focused workspace as file: " << saveDir.toString()
                 << std::endl;
  copyToGeneral(saveDir, focusingComp);
}

/**
* Convert the generated output files and saves them in
* OpenGenie format
*
* @param inputWorkspace title of the focused workspace
* @param specNums number of spectrum to display
* @param bank the number of the bank as a string
* @param runNo the run number as a string
*/
void EnggDiffractionPresenter::saveOpenGenie(const std::string inputWorkspace,
                                             std::string specNums,
                                             std::string bank,
                                             std::string runNo) {

  // Generates the file name in the appropriate format
  std::string fullFilename =
      outFileNameFactory(inputWorkspace, runNo, bank, ".his");

  std::string comp;
  Poco::Path saveDir;
  if (inputWorkspace.std::string::find("curves") != std::string::npos ||
      inputWorkspace.std::string::find("intgration") != std::string::npos) {
    // Creates appropriate directory
    comp = "Calibration";
    saveDir = outFilesUserDir(comp);
  } else {
    // Creates appropriate directory
    comp = "Focus";
    saveDir = outFilesUserDir(comp);
  }

  // append the full file name in the end
  saveDir.append(fullFilename);

  try {
    g_log.debug() << "Going to save focused output into OpenGenie file: "
                  << fullFilename << std::endl;
    auto alg = Mantid::API::AlgorithmManager::Instance().createUnmanaged(
        "SaveOpenGenieAscii");
    alg->initialize();
    alg->setProperty("InputWorkspace", inputWorkspace);
    std::string filename(saveDir.toString());
    alg->setPropertyValue("Filename", filename);
    alg->setPropertyValue("SpecNumberField", specNums);
    alg->execute();
  } catch (std::runtime_error &re) {
    g_log.error() << "Error in saving OpenGenie format file. ",
        "Could not run the algorithm SaveOpenGenieAscii succesfully for "
        "workspace " +
            inputWorkspace + ". Error description: " + re.what() +
            " Please check also the log messages for details.";
    throw;
  }
  g_log.notice() << "Saves OpenGenieAscii (.his) file written as: "
                 << saveDir.toString() << std::endl;
  copyToGeneral(saveDir, comp);
}

/**
* Generates the required file name of the output files
*
* @param inputWorkspace title of the focused workspace
* @param runNo the run number as a string
* @param bank the number of the bank as a string
* @param format the format of the file to be saved as
*/
std::string EnggDiffractionPresenter::outFileNameFactory(
    std::string inputWorkspace, std::string runNo, std::string bank,
    std::string format) {
  std::string fullFilename;

  // calibration output files
  if (inputWorkspace.std::string::find("curves") != std::string::npos) {
    fullFilename = "ob+ENGINX_" + runNo + "_" + bank + "_bank" + format;

    // focus output files
  } else if (inputWorkspace.std::string::find("texture") != std::string::npos) {
    fullFilename = "ENGINX_" + runNo + "_texture_" + bank + format;
  } else if (inputWorkspace.std::string::find("cropped") != std::string::npos) {
    fullFilename = "ENGINX_" + runNo + "_cropped_" +
                   boost::lexical_cast<std::string>(g_croppedCounter) + format;
    g_croppedCounter++;
  } else {
    fullFilename = "ENGINX_" + runNo + "_bank_" + bank + format;
  }
  return fullFilename;
}

std::string EnggDiffractionPresenter::vanadiumCurvesPlotFactory() {
  std::string pyCode =

      "van_curve_twin_ws = \"__engggui_vanadium_curves_twin_ws\"\n"

      "if(mtd.doesExist(van_curve_twin_ws)):\n"
      " DeleteWorkspace(van_curve_twin_ws)\n"

      "CloneWorkspace(InputWorkspace = \"engggui_vanadium_curves_ws\", "
      "OutputWorkspace = van_curve_twin_ws)\n"

      "van_curves_ws = workspace(van_curve_twin_ws)\n"
      "for i in range(1, 3):\n"
      " if (i == 1):\n"
      "  curve_plot_bank_1 = plotSpectrum(van_curves_ws, [0, 1, "
      "2]).activeLayer()\n"
      "  curve_plot_bank_1.setTitle(\"Engg GUI Vanadium Curves Bank 1\")\n"

      " if (i == 2):\n"
      "  curve_plot_bank_2 = plotSpectrum(van_curves_ws, [3, 4, "
      "5]).activeLayer()\n"
      "  curve_plot_bank_2.setTitle(\"Engg GUI Vanadium Curves Bank 2\")\n";

  return pyCode;
}

/**
* Generates the workspace with difc/zero according to the selected bank
*
* @param difc vector containing constants difc value of each bank
* @param tzero vector containing constants tzero value of each bank
* @param specNo used to set range for Calibration Cropped
* @param customisedBankName used to set the file and workspace name
*
* @return string with a python script
*/
std::string EnggDiffractionPresenter::DifcZeroWorkspaceFactory(
    const std::vector<double> &difc, const std::vector<double> &tzero,
    const std::string &specNo, const std::string &customisedBankName) const {

  size_t bank1 = size_t(0);
  size_t bank2 = size_t(1);
  std::string pyRange;
  std::string plotSpecNum = "False";

  // sets the range to plot appropriate graph for the particular bank
  if (specNo == "North") {
    // only enable script to plot bank 1
    pyRange = "1, 2";
  } else if (specNo == "South") {
    // only enables python script to plot bank 2
    // as bank 2 data will be located in difc[0] & tzero[0] - refactor
    pyRange = "2, 3";
    bank2 = size_t(0);
  } else if (specNo != "") {
    pyRange = "1, 2";
    plotSpecNum = "True";
  } else {
    // enables python script to plot bank 1 & 2
    pyRange = "1, 3";
  }

  std::string pyCode =
      "plotSpecNum = " + plotSpecNum + "\n"
                                       "for i in range(" +
      pyRange +
      "):\n"

      " if (plotSpecNum == False):\n"
      "  bank_ws = workspace(\"engggui_calibration_bank_\" + str(i))\n"
      " else:\n"
      "  bank_ws = workspace(\"engggui_calibration_bank_" +
      customisedBankName + "\")\n"

                           " xVal = []\n"
                           " yVal = []\n"
                           " y2Val = []\n"

                           " if (i == 1):\n"
                           "  difc=" +
      boost::lexical_cast<std::string>(difc[bank1]) + "\n" + "  tzero=" +
      boost::lexical_cast<std::string>(tzero[bank1]) + "\n" + " else:\n"

                                                              "  difc=" +
      boost::lexical_cast<std::string>(difc[bank2]) + "\n" + "  tzero=" +
      boost::lexical_cast<std::string>(tzero[bank2]) + "\n" +

      " for irow in range(0, bank_ws.rowCount()):\n"
      "  xVal.append(bank_ws.cell(irow, 0))\n"
      "  yVal.append(bank_ws.cell(irow, 5))\n"

      "  y2Val.append(xVal[irow] * difc + tzero)\n"

      " ws1 = CreateWorkspace(DataX=xVal, DataY=yVal, UnitX=\"Expected "
      "Peaks "
      " Centre(dSpacing, A)\", YUnitLabel = \"Fitted Peaks Centre(TOF, "
      "us)\")\n"
      " ws2 = CreateWorkspace(DataX=xVal, DataY=y2Val)\n";
  return pyCode;
}

/**
* Plot the workspace with difc/zero acordding to selected bank
*
* @param customisedBankName used to set the file and workspace name
*
* @return string with a python script which will merge with
*

*/
std::string EnggDiffractionPresenter::plotDifcZeroWorkspace(
    const std::string &customisedBankName) const {
  std::string pyCode =
      // plotSpecNum is true when SpectrumNos being used
      " if (plotSpecNum == False):\n"
      "  output_ws = \"engggui_difc_zero_peaks_bank_\" + str(i)\n"
      " else:\n"
      "  output_ws = \"engggui_difc_zero_peaks_" +
      customisedBankName +
      "\"\n"

      // delete workspace if exists within ADS already
      " if(mtd.doesExist(output_ws)):\n"
      "  DeleteWorkspace(output_ws)\n"

      // append workspace with peaks data for Peaks Fitted
      // and Difc/TZero Straight line
      " AppendSpectra(ws1, ws2, OutputWorkspace=output_ws)\n"
      " DeleteWorkspace(ws1)\n"
      " DeleteWorkspace(ws2)\n"

      " if (plotSpecNum == False):\n"
      "  DifcZero = \"engggui_difc_zero_peaks_bank_\" + str(i)\n"
      " else:\n"
      "  DifcZero = \"engggui_difc_zero_peaks_" +
      customisedBankName +
      "\"\n"

      " DifcZeroWs = workspace(DifcZero)\n"
      " DifcZeroPlot = plotSpectrum(DifcZeroWs, [0, 1]).activeLayer()\n"

      " if (plotSpecNum == False):\n"
      "  DifcZeroPlot.setTitle(\"Engg Gui Difc Zero Peaks Bank \" + "
      "str(i))\n"
      " else:\n"
      "  DifcZeroPlot.setTitle(\"Engg Gui Difc Zero Peaks " +
      customisedBankName +
      "\")\n"

      // set the legend title
      " DifcZeroPlot.setCurveTitle(0, \"Peaks Fitted\")\n"
      " DifcZeroPlot.setCurveTitle(1, \"DifC/TZero Fitted Straight Line\")\n"
      " DifcZeroPlot.setAxisTitle(Layer.Bottom, \"Expected Peaks "
      "Centre(dSpacing, "
      " A)\")\n"
      " DifcZeroPlot.setCurveLineStyle(0, QtCore.Qt.DotLine)\n";

  return pyCode;
}

/**
* Generates appropriate names for table workspaces
*
* @param specNos SpecNos or bank name to be passed
* @param bank_i current loop of the bank during calibration
*/
std::string EnggDiffractionPresenter::outFitParamsTblNameGenerator(
    const std::string specNos, const size_t bank_i) const {
  std::string outFitParamsTblName;
  bool specNumUsed = specNos != "";

  if (specNumUsed) {
    if (specNos == "North")
      outFitParamsTblName = "engggui_calibration_bank_1";
    else if (specNos == "South")
      outFitParamsTblName = "engggui_calibration_bank_2";
    else {
      // Get the Customised Bank Name text-ield string from qt
      std::string CustomisedBankName = m_view->currentCalibCustomisedBankName();

      if (CustomisedBankName.empty())
        outFitParamsTblName = "engggui_calibration_bank_cropped";
      else
        outFitParamsTblName = "engggui_calibration_bank_" + CustomisedBankName;
    }
  } else {
    outFitParamsTblName = "engggui_calibration_bank_" +
                          boost::lexical_cast<std::string>(bank_i + 1);
  }
  return outFitParamsTblName;
}

/**
* Produces a path to the output directory where files are going to be
* written for a specific user + RB number / experiment ID. It creates
* the output directory if not found, and checks if it is ok and readable.
*
* @param addToDir adds a component to a specific directory for
* focusing, calibration or other files, for example "Calibration" or
* "Focus"
*/
Poco::Path
EnggDiffractionPresenter::outFilesUserDir(const std::string &addToDir) {
  std::string rbn = m_view->getRBNumber();
  Poco::Path dir = outFilesRootDir();

  try {
    dir.append("User");
    dir.append(rbn);
    dir.append(addToDir);

    Poco::File dirFile(dir);
    if (!dirFile.exists()) {
      dirFile.createDirectories();
    }
  } catch (Poco::FileAccessDeniedException &e) {
    g_log.error() << "Error caused by file access/permission, path to user "
                     "directory: " << dir.toString()
                  << ". Error details: " << e.what() << std::endl;
  } catch (std::runtime_error &re) {
    g_log.error() << "Error while finding/creating a user path: "
                  << dir.toString() << ". Error details: " << re.what()
                  << std::endl;
  }
  return dir;
}

/**
* Produces a path to the output directory where files are going to be
* written for a specific user + RB number / experiment ID. It creates
* the output directory if not found. See outFilesUserDir() for the
* sibling method that produces user/rb number-specific directories.
*
* @param addComponent path component to add to the root of general
* files, for example "Calibration" or "Focus"
*/
Poco::Path
EnggDiffractionPresenter::outFilesGeneralDir(const std::string &addComponent) {
  Poco::Path dir = outFilesRootDir();

  try {

    dir.append(addComponent);
  } catch (Poco::FileAccessDeniedException &e) {
    g_log.error() << "Error caused by file access/permission, path to "
                     "general directory: " << dir.toString()
                  << ". Error details: " << e.what() << std::endl;
  } catch (std::runtime_error &re) {
    g_log.error() << "Error while finding/creating a general path: "
                  << dir.toString() << ". Error details: " << re.what()
                  << std::endl;
  }
  return dir;
}

/**
 * Produces the root path where output files are going to be written.
 */
Poco::Path EnggDiffractionPresenter::outFilesRootDir() {
  const std::string rootDir = "EnginX_Mantid";
  Poco::Path dir;

  try {
// takes to the root of directory according to the platform
#ifdef __unix__
    dir = Poco::Path().home();
#else
    const std::string ROOT_DRIVE = "C:/";
    dir.assign(ROOT_DRIVE);
#endif
    dir.append(rootDir);

    Poco::File dirFile(dir);
    if (!dirFile.exists()) {
      dirFile.createDirectories();
      g_log.notice() << "Creating output directory root for the first time: "
                     << dir.toString() << std::endl;
    }

  } catch (Poco::FileAccessDeniedException &e) {
    g_log.error() << "Error, access/permission denied for root directory: "
                  << dir.toString()
                  << ". This is a severe error. The interface will not behave "
                     "correctly when generating files. Error details: "
                  << e.what() << std::endl;
  } catch (std::runtime_error &re) {
    g_log.error() << "Error while finding/creating the root directory: "
                  << dir.toString()
                  << ". This is a severe error. Details: " << re.what()
                  << std::endl;
  }

  return dir;
}

/**
 * Copy files to the general directories. Normally files are produced
 * in the user/RB number specific directories and then can be copied
 * to the general/all directories using this method.
 *
 * @param source path to the file to copy
 *
 * @param pathComp path component to use for the copy file in the
 * general directories, for example "Calibration" or "Focus"
 */
void EnggDiffractionPresenter::copyToGeneral(const Poco::Path &source,
                                             const std::string &pathComp) {
  Poco::File file(source);
  if (!file.exists() || !file.canRead()) {
    g_log.warning() << "Cannot copy the file " << source.toString()
                    << " to the general/all users directories because it "
                       "cannot be read." << std::endl;
    return;
  }

  auto destDir = outFilesGeneralDir(pathComp);
  try {
    Poco::File destDirFile(destDir);
    if (!destDirFile.exists()) {
      destDirFile.createDirectories();
    }
  } catch (std::runtime_error &rexc) {
    g_log.error() << "Could not create output directory for the general/all "
                     "files. Cannot copy the user files there:  "
                  << destDir.toString() << ". Error details: " << rexc.what()
                  << std::endl;

    return;
  }

  try {
    file.copyTo(destDir.toString());
  } catch (std::runtime_error &rexc) {
    g_log.error() << " Could not copy the file '" << file.path() << "' to "
                  << destDir.toString() << ". Error details: " << rexc.what()
                  << std::endl;
  }

  g_log.information() << "Copied file '" << source.toString()
                      << "'to general/all directory: " << destDir.toString()
                      << std::endl;
}

/**
 * Copy files to the user/RB number directories.
 *
 * @param source path to the file to copy
 *
 * @param pathComp path component to use for the copy file in the
 * general directories, for example "Calibration" or "Focus"
 */
void EnggDiffractionPresenter::copyToUser(const Poco::Path &source,
                                          const std::string &pathComp) {
  Poco::File file(source);
  if (!file.exists() || !file.canRead()) {
    g_log.warning() << "Cannot copy the file " << source.toString()
                    << " to the user directories because it cannot be read."
                    << std::endl;
    return;
  }

  auto destDir = outFilesUserDir(pathComp);
  try {
    Poco::File destDirFile(destDir);
    if (!destDirFile.exists()) {
      destDirFile.createDirectories();
    }
  } catch (std::runtime_error &rexc) {
    g_log.error() << "Could not create output directory for the user "
                     "files. Cannot copy the user files there:  "
                  << destDir.toString() << ". Error details: " << rexc.what()
                  << std::endl;

    return;
  }

  try {
    file.copyTo(destDir.toString());
  } catch (std::runtime_error &rexc) {
    g_log.error() << " Could not copy the file '" << file.path() << "' to "
                  << destDir.toString() << ". Error details: " << rexc.what()
                  << std::endl;
  }

  g_log.information() << "Copied file '" << source.toString()
                      << "'to user directory: " << destDir.toString()
                      << std::endl;
}

/**
 * Copies a file from a third location to the standard user/RB number
 * and the general/all directories. This just uses copyToUser() and
 * copyToGeneral().
 *
 * @param fullFilename full path to the origin file
 */
void EnggDiffractionPresenter::copyFocusedToUserAndAll(
    const std::string &fullFilename) {
  // The files are saved by SaveNexus in the Settings/Focusing output folder.
  // Then they need to go to the user and 'all' directories.
  // The "Settings/Focusing output folder" may go away in the future
  Poco::Path nxsPath(fullFilename);
  const std::string focusingComp = "Focus";
  auto saveDir = outFilesUserDir(focusingComp);
  Poco::Path outFullPath(saveDir);
  outFullPath.append(nxsPath.getFileName());
  copyToUser(nxsPath, focusingComp);
  copyToGeneral(nxsPath, focusingComp);
}

/**
 * To write the calibration/instrument parameter for GSAS.
 *
 * @param outFilename name of the output .par/.prm/.iparm file for GSAS
 * @param difc list of GSAS DIFC values to include in the file
 * @param tzero list of GSAS TZERO values to include in the file
 * @param bankNames list of bank names corresponding the the difc/tzero
 *
 * @param ceriaNo ceria/calibration run number, to be replaced in the
 * template file
 *
 * @param vanNo vanadium run number, to be replaced in the template file
 *
 * @param templateFile a template file where to replace the difc/zero
 * values. An empty default implies using an "all-banks" template.
 */
void EnggDiffractionPresenter::writeOutCalibFile(
    const std::string &outFilename, const std::vector<double> &difc,
    const std::vector<double> &tzero, const std::vector<std::string> &bankNames,
    const std::string &ceriaNo, const std::string &vanNo,
    const std::string &templateFile) {
  // TODO: this is horrible and should be changed to avoid running
  // Python code. Update this as soon as we have a more stable way of
  // generating IPARM/PRM files.

  // Writes a file doing this:
  // write_ENGINX_GSAS_iparam_file(output_file, difc, zero, ceria_run=241391,
  // vanadium_run=236516, template_file=None):

  // this replace is to prevent issues with network drives on windows:
  const std::string safeOutFname =
      boost::replace_all_copy(outFilename, "\\", "/");
  std::string pyCode = "import EnggUtils\n";
  pyCode += "import os\n";
  // normalize apparently not needed after the replace, but to be double-safe:
  pyCode += "GSAS_iparm_fname = os.path.normpath('" + safeOutFname + "')\n";
  pyCode += "bank_names = []\n";
  pyCode += "ceria_number = " + ceriaNo + "\n";
  pyCode += "van_number = " + vanNo + "\n";
  pyCode += "Difcs = []\n";
  pyCode += "Zeros = []\n";
  std::string templateFileVal = "None";
  if (!templateFile.empty()) {
    templateFileVal = "'" + templateFile + "'";
  }
  pyCode += "template_file = " + templateFileVal + "\n";
  for (size_t i = 0; i < difc.size(); ++i) {
    pyCode += "bank_names.append('" + bankNames[i] + "')\n";
    pyCode +=
        "Difcs.append(" + boost::lexical_cast<std::string>(difc[i]) + ")\n";
    pyCode +=
        "Zeros.append(" + boost::lexical_cast<std::string>(tzero[i]) + ")\n";
  }
  pyCode +=
      "EnggUtils.write_ENGINX_GSAS_iparam_file(output_file=GSAS_iparm_fname, "
      "bank_names=bank_names, difc=Difcs, tzero=Zeros, ceria_run=ceria_number, "
      "vanadium_run=van_number, template_file=template_file) \n";

  const auto status = m_view->enggRunPythonCode(pyCode);
  g_log.information() << "Saved output calibration file via Python. Status: "
                      << status << std::endl;
}

/**
 * Note down a directory that needs to be added to the data search
 * path when looking for run files. This simply uses a vector and adds
 * all the paths, as the ConfigService will take care of duplicates,
 * invalid directories, etc.
 *
 * @param filename (full) path to a file
 */
void EnggDiffractionPresenter::recordPathBrowsedTo(
    const std::string &filename) {

  Poco::File file(filename);
  if (!file.exists() || !file.isFile())
    return;

  Poco::Path path(filename);
  Poco::File directory(path.parent());
  if (!directory.exists() || !directory.isDirectory())
    return;

  m_browsedToPaths.push_back(directory.path());
}

} // namespace CustomInterfaces
} // namespace MantidQt

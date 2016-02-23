//------------------------------------------------------------------------------
// Includes
//------------------------------------------------------------------------------
#include "MantidDataHandling/LoadNexusMonitors2.h"

#include "MantidDataHandling/LoadEventNexus.h"
#include "MantidDataHandling/ISISRunLogs.h"
#include "MantidAPI/Axis.h"
#include "MantidAPI/FileProperty.h"
#include "MantidAPI/WorkspaceGroup.h"
#include "MantidAPI/WorkspaceFactory.h"
#include "MantidKernel/ConfigService.h"
#include "MantidKernel/DateAndTime.h"
#include "MantidKernel/UnitFactory.h"

#include <boost/lexical_cast.hpp>
#include <Poco/File.h>
#include <Poco/Path.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

using Mantid::DataObjects::EventWorkspace;
using Mantid::DataObjects::EventWorkspace_sptr;
using Mantid::API::WorkspaceGroup;
using Mantid::API::WorkspaceGroup_sptr;

namespace Mantid {
namespace DataHandling {

// Register the algorithm into the AlgorithmFactory
DECLARE_ALGORITHM(LoadNexusMonitors2)

namespace {
void loadSampleDataISIScompatibilityInfo(
    ::NeXus::File &file, Mantid::API::MatrixWorkspace_sptr const WS) {
  try {
    file.openGroup("isis_vms_compat", "IXvms");
  } catch (::NeXus::Exception &) {
    // No problem, it just means that this entry does not exist
    return;
  }

  // read the data
  try {
    std::vector<int32_t> spb;
    std::vector<float> rspb;
    file.readData("SPB", spb);
    file.readData("RSPB", rspb);

    WS->mutableSample().setGeometryFlag(
        spb[2]); // the flag is in the third value
    WS->mutableSample().setThickness(rspb[3]);
    WS->mutableSample().setHeight(rspb[4]);
    WS->mutableSample().setWidth(rspb[5]);
  } catch (::NeXus::Exception &ex) {
    // it means that the data was not as expected, report the problem
    std::stringstream s;
    s << "Wrong definition found in isis_vms_compat :> " << ex.what();
    file.closeGroup();
    throw std::runtime_error(s.str());
  }

  file.closeGroup();
}
} // namespace

//------------------------------------------------------------------------------
LoadNexusMonitors2::LoadNexusMonitors2() : Algorithm(), m_monitor_count(0) {}

//------------------------------------------------------------------------------
LoadNexusMonitors2::~LoadNexusMonitors2() {}

//------------------------------------------------------------------------------
/// Initialization method.
void LoadNexusMonitors2::init() {
  declareProperty(
      new API::FileProperty("Filename", "", API::FileProperty::Load, ".nxs"),
      "The name (including its full or relative path) of the NeXus file to "
      "attempt to load. The file extension must either be .nxs or .NXS");

  declareProperty(
      new API::WorkspaceProperty<API::Workspace>("OutputWorkspace", "",
                                                 Kernel::Direction::Output),
      "The name of the output workspace in which to load the NeXus monitors.");

  declareProperty(new Kernel::PropertyWithValue<bool>("MonitorsAsEvents", true,
                                                      Kernel::Direction::Input),
                  "If enabled (by default), load the monitors as events (into "
                  "an EventWorkspace), as long as there is event data. If "
                  "disabled, load monitors as spectra (into a Workspace2D, "
                  "regardless of whether event data is found.");
}

//------------------------------------------------------------------------------
/**
 * Executes the algorithm. Reading in the file and creating and populating
 * the output workspace
 */
void LoadNexusMonitors2::exec() {
  // Retrieve the filename from the properties
  m_filename = this->getPropertyValue("Filename");

  API::Progress prog1(this, 0.0, 0.2, 2);

  if (!canOpenAsNeXus(m_filename)) {
    throw std::runtime_error(
        "Failed to recognize this file as a NeXus file, cannot continue.");
  }

  // top level file information
  ::NeXus::File file(m_filename);

  // Start with the base entry
  typedef std::map<std::string, std::string> string_map_t;
  string_map_t::const_iterator it;
  string_map_t entries = file.getEntries();
  for (it = entries.begin(); it != entries.end(); ++it) {
    if (((it->first == "entry") || (it->first == "raw_data_1")) &&
        (it->second == "NXentry")) {
      file.openGroup(it->first, it->second);
      m_top_entry_name = it->first;
      break;
    }
  }
  prog1.report();

  // Now we want to go through and find the monitors
  entries = file.getEntries();
  std::vector<std::string> monitorNames;
  size_t numHistMon = 0;
  size_t numEventMon = 0;
  size_t numPeriods = 0;
  // we want to sort monitors by monitor_number if they are present
  std::map<int, std::string> monitorNumber2Name;
  prog1.report();

  API::Progress prog2(this, 0.2, 0.6, entries.size());

  it = entries.begin();
  for (; it != entries.end(); ++it) {
    std::string entry_name(it->first);
    std::string entry_class(it->second);
    if ((entry_class == "NXmonitor")) {
      monitorNames.push_back(entry_name);

      // check for event/histogram monitor
      // -> This will prefer event monitors over histogram
      //    if they are found in the same group.
      file.openGroup(entry_name, "NXmonitor");
      int numEventThings =
          0; // number of things that are eventish - should be 3
      string_map_t inner_entries = file.getEntries(); // get list of entries
      for (auto &entry : inner_entries) {
        if (entry.first == "event_index") {
          numEventThings += 1;
          continue;
        } else if (entry.first == "event_time_offset") {
          numEventThings += 1;
          continue;
        } else if (entry.first == "event_time_zero") {
          numEventThings += 1;
          continue;
        }
      }

      if (numEventThings == 3) {
        numEventMon += 1;
      } else {
        numHistMon += 1;
        if (inner_entries.find("monitor_number") != inner_entries.end()) {
          specid_t monitorNo;
          file.openData("monitor_number");
          file.getData(&monitorNo);
          file.closeData();
          monitorNumber2Name[monitorNo] = entry_name;
        }
        if ((numPeriods == 0) &&
            (inner_entries.find("period_index") != inner_entries.end())) {
          MantidVec period_data;
          file.openData("period_index");
          file.getDataCoerce(period_data);
          file.closeData();
          numPeriods = period_data.size();
        }
      }
      file.closeGroup(); // close NXmonitor
    }
    prog2.report();
  }
  m_monitor_count = monitorNames.size();

  // Nothing to do
  if (0 == m_monitor_count) {
    // previous version just used to return, but that
    // threw an error when the OutputWorkspace property was not set.
    // and the error message was confusing.
    // This has changed to throw a specific error.
    throw std::invalid_argument(m_filename + " does not contain any monitors");
  }

  // With this property you can make the exception that even if there's event
  // data, monitors will be loaded
  // as histograms (if set to false)
  bool monitorsAsEvents = getProperty("MonitorsAsEvents");
  // Beware, even if monitorsAsEvents==False (user requests to load monitors as
  // histograms)
  // check if there's histogram data. If not, ignore setting, step back and load
  // monitors as
  // events which is the only possibility left.
  if (!monitorsAsEvents)
    if (!allMonitorsHaveHistoData(file, monitorNames)) {
      g_log.information() << "Cannot load monitors as histogram data. Loading "
                             "as events even if the opposite was requested by "
                             "disabling the property MonitorsAsEvents"
                          << std::endl;
      monitorsAsEvents = true;
    }

  // only used if using event monitors
  EventWorkspace_sptr eventWS;
  // Create the output workspace
  bool useEventMon;
  if (numHistMon == m_monitor_count || !monitorsAsEvents) {
    useEventMon = false;
    if (m_monitor_count == 0)
      throw std::runtime_error(
          "Not loading event data. Trying to load histogram data but failed to "
          "find monitors with histogram data or could not interpret the data. "
          "This file may be corrupted or it may not be supported");

    m_workspace = API::WorkspaceFactory::Instance().create(
        "Workspace2D", m_monitor_count, 1, 1);
    // if there is a distinct monitor number for each monitor sort them by that
    // number
    if (monitorNumber2Name.size() == monitorNames.size()) {
      monitorNames.clear();
      for (auto &numberName : monitorNumber2Name) {
        monitorNames.push_back(numberName.second);
      }
    }
  } else if (numEventMon == m_monitor_count) {
    eventWS = EventWorkspace_sptr(new EventWorkspace());
    useEventMon = true;
    eventWS->initialize(m_monitor_count, 1, 1);

    // Set the units
    eventWS->getAxis(0)->unit() =
        Mantid::Kernel::UnitFactory::Instance().create("TOF");
    eventWS->setYUnit("Counts");
    m_workspace = eventWS;
  } else {
    g_log.error() << "Found " << numEventMon << " event monitors and "
                  << numHistMon << " histogram monitors (" << m_monitor_count
                  << " total)\n";
    throw std::runtime_error(
        "All monitors must be either event or histogram based");
  }

  // a temporary place to put the spectra/detector numbers
  boost::scoped_array<specid_t> spectra_numbers(new specid_t[m_monitor_count]);
  boost::scoped_array<detid_t> detector_numbers(new detid_t[m_monitor_count]);

  API::Progress prog3(this, 0.6, 1.0, m_monitor_count);

  for (std::size_t i = 0; i < m_monitor_count; ++i) {
    g_log.information() << "Loading " << monitorNames[i] << std::endl;
    // Do not rely on the order in path list
    Poco::Path monPath(monitorNames[i]);
    std::string monitorName = monPath.getBaseName();

    // check for monitor name - in our case will be of the form either monitor1
    // or monitor_1
    std::string::size_type loc = monitorName.rfind('_');
    if (loc == std::string::npos) {
      loc = monitorName.rfind('r');
    }

    detid_t monIndex = -1 * boost::lexical_cast<int>(
                                monitorName.substr(loc + 1)); // SNS default
    file.openGroup(monitorNames[i], "NXmonitor");

    // Check if the spectra index is there
    specid_t spectrumNo(static_cast<specid_t>(i + 1));
    try {
      file.openData("spectrum_index");
      file.getData(&spectrumNo);
      file.closeData();
    } catch (::NeXus::Exception &) {
      // Use the default as matching the workspace index
    }

    g_log.debug() << "monIndex = " << monIndex << std::endl;
    g_log.debug() << "spectrumNo = " << spectrumNo << std::endl;

    spectra_numbers[i] = spectrumNo;
    detector_numbers[i] = monIndex;

    if (useEventMon) {
      // setup local variables
      std::vector<uint64_t> event_index;
      MantidVec time_of_flight;
      std::string tof_units;
      MantidVec seconds;

      // read in the data
      file.openData("event_index");
      file.getData(event_index);
      file.closeData();
      file.openData("event_time_offset");
      file.getDataCoerce(time_of_flight);
      file.getAttr("units", tof_units);
      file.closeData();
      file.openData("event_time_zero");
      file.getDataCoerce(seconds);
      Mantid::Kernel::DateAndTime pulsetime_offset;
      {
        std::string startTime;
        file.getAttr("offset", startTime);
        pulsetime_offset = Mantid::Kernel::DateAndTime(startTime);
      }
      file.closeData();

      // load up the event list
      DataObjects::EventList &event_list = eventWS->getEventList(i);

      Mantid::Kernel::DateAndTime pulsetime(0);
      Mantid::Kernel::DateAndTime lastpulsetime(0);
      std::size_t numEvents = time_of_flight.size();
      bool pulsetimesincreasing = true;
      size_t pulse_index(0);
      size_t numPulses = seconds.size();
      for (std::size_t j = 0; j < numEvents; ++j) {
        while (!((j >= event_index[pulse_index]) &&
                 (j < event_index[pulse_index + 1]))) {
          pulse_index += 1;
          if (pulse_index > (numPulses + 1))
            break;
        }
        if (pulse_index >= (numPulses))
          pulse_index = numPulses - 1; // fix it
        pulsetime = pulsetime_offset + seconds[pulse_index];
        if (pulsetime < lastpulsetime)
          pulsetimesincreasing = false;
        lastpulsetime = pulsetime;
        event_list.addEventQuickly(
            DataObjects::TofEvent(time_of_flight[j], pulsetime));
      }
      if (pulsetimesincreasing)
        event_list.setSortOrder(DataObjects::PULSETIME_SORT);
    } else // is a histogram monitor
    {
      // Now, actually retrieve the necessary data
      file.openData("data");
      MantidVec data;
      file.getDataCoerce(data);
      file.closeData();
      MantidVec error(data.size()); // create vector of correct size

      // Transform errors via square root
      std::transform(data.begin(), data.end(), error.begin(),
                     (double (*)(double))sqrt);

      // Get the TOF axis
      file.openData("time_of_flight");
      MantidVec tof;
      file.getDataCoerce(tof);
      file.closeData();

      m_workspace->dataX(i) = tof;
      m_workspace->dataY(i) = data;
      m_workspace->dataE(i) = error;
    }

    file.closeGroup(); // NXmonitor

    // Default values, might change later.
    m_workspace->getSpectrum(i)->setSpectrumNo(spectrumNo);
    m_workspace->getSpectrum(i)->setDetectorID(monIndex);

    prog3.report();
  }

  if (useEventMon) // set the x-range to be the range for min/max events
  {
    double xmin, xmax;
    eventWS->getEventXMinMax(xmin, xmax);

    Kernel::cow_ptr<MantidVec> axis;
    MantidVec &xRef = axis.access();
    xRef.resize(2, 0.0);
    if (eventWS->getNumberEvents() > 0) {
      xRef[0] = xmin - 1; // Just to make sure the bins hold it all
      xRef[1] = xmax + 1;
    }
    eventWS->setAllX(axis); // Set the binning axis using this.
  }

  // Fix the detector numbers if the defaults above are not correct
  fixUDets(detector_numbers, file, spectra_numbers, m_monitor_count);

  // Check for and ISIS compat block to get the detector IDs for the loaded
  // spectrum numbers
  // @todo: Find out if there is a better (i.e. more generic) way to do this
  try {
    g_log.debug() << "Load Sample data isis" << std::endl;
    loadSampleDataISIScompatibilityInfo(file, m_workspace);
  } catch (::NeXus::Exception &) {
  }

  // Need to get the instrument name from the file
  std::string instrumentName;
  file.openGroup("instrument", "NXinstrument");
  try {
    file.openData("name");
    instrumentName = file.getStrData();
    // Now let's close the file as we don't need it anymore to load the
    // instrument.
    file.closeData();
    file.closeGroup(); // Close the NXentry
    file.close();

  } catch (std::runtime_error &) // no correct instrument definition (old ISIS
                                 // file, fall back to isis_vms_compat)
  {
    file.closeGroup(); // Close the instrument NXentry
    instrumentName = LoadEventNexus::readInstrumentFromISIS_VMSCompat(file);
    file.close();
  }

  g_log.debug() << "Instrument name read from NeXus file is " << instrumentName
                << std::endl;

  m_workspace->getAxis(0)->unit() =
      Kernel::UnitFactory::Instance().create("TOF");
  m_workspace->setYUnit("Counts");

  // Load the logs
  this->runLoadLogs(m_filename, m_workspace);

  // Old SNS files don't have this
  try {
    // The run_start will be loaded from the pulse times.
    Kernel::DateAndTime run_start(0, 0);
    run_start = m_workspace->getFirstPulseTime();
    m_workspace->mutableRun().addProperty("run_start",
                                          run_start.toISO8601String(), true);
  } catch (...) {
    // Old files have the start_time defined, so all SHOULD be good.
    // The start_time, however, has been known to be wrong in old files.
  }
  // Load the instrument
  LoadEventNexus::loadInstrument(m_filename, m_workspace, m_top_entry_name,
                                 this);

  // Load the meta data, but don't stop on errors
  g_log.debug() << "Loading metadata" << std::endl;
  try {
    LoadEventNexus::loadEntryMetadata<API::MatrixWorkspace_sptr>(
        m_filename, m_workspace, m_top_entry_name);
  } catch (std::exception &e) {
    g_log.warning() << "Error while loading meta data: " << e.what()
                    << std::endl;
  }

  // Fix the detector IDs/spectrum numbers
  for (size_t i = 0; i < m_workspace->getNumberHistograms(); i++) {
    m_workspace->getSpectrum(i)->setSpectrumNo(spectra_numbers[i]);
    m_workspace->getSpectrum(i)->setDetectorID(detector_numbers[i]);
  }
  // add filename
  m_workspace->mutableRun().addProperty("Filename", m_filename);

  // if multiperiod histogram data
  if ((numPeriods > 1) && (!useEventMon)) {
    splitMutiPeriodHistrogramData(numPeriods);
  } else {
    this->setProperty("OutputWorkspace", m_workspace);
  }
}

//------------------------------------------------------------------------------
/**
 * Can we get a histogram (non event data) for every monitor?
 *
 * @param file :: NeXus file object (open)
 * @param monitorNames :: names of monitors of interest
 * @return If there seems to be histograms for all monitors (they have "data")
 **/
bool LoadNexusMonitors2::allMonitorsHaveHistoData(
    ::NeXus::File &file, const std::vector<std::string> &monitorNames) {
  bool res = true;

  try {
    for (std::size_t i = 0; i < m_monitor_count; ++i) {
      file.openGroup(monitorNames[i], "NXmonitor");
      file.openData("data");
      file.closeData();
      file.closeGroup();
    }
  } catch (::NeXus::Exception &) {
    file.closeGroup();
    res = false;
  }
  return res;
}

//------------------------------------------------------------------------------
/**
 * Fix the detector numbers if the defaults are not correct. Currently checks
 * the isis_vms_compat block and reads them from there if possible.
 *
 * @param det_ids :: An array of prefilled detector IDs
 * @param file :: A reference to the NeXus file opened at the root entry
 * @param spec_ids :: An array of spectrum numbers that the monitors have
 * @param nmonitors :: The size of the det_ids and spec_ids arrays
 */
void LoadNexusMonitors2::fixUDets(boost::scoped_array<detid_t> &det_ids,
                                  ::NeXus::File &file,
                                  const boost::scoped_array<specid_t> &spec_ids,
                                  const size_t nmonitors) const {
  try {
    file.openGroup("isis_vms_compat", "IXvms");
  } catch (::NeXus::Exception &) {
    return;
  }
  // UDET
  file.openData("UDET");
  std::vector<int32_t> udet;
  file.getData(udet);
  file.closeData();
  // SPEC
  file.openData("SPEC");
  std::vector<int32_t> spec;
  file.getData(spec);
  file.closeData();

  // This is a little complicated: Each value in the spec_id array is a value
  // found in the
  // SPEC block of isis_vms_compat. The index that this value is found at then
  // corresponds
  // to the index within the UDET block that holds the detector ID
  std::vector<int32_t>::const_iterator beg = spec.begin();
  for (size_t mon_index = 0; mon_index < nmonitors; ++mon_index) {
    std::vector<int32_t>::const_iterator itr =
        std::find(spec.begin(), spec.end(), spec_ids[mon_index]);
    if (itr == spec.end()) {
      det_ids[mon_index] = -1;
      continue;
    }
    std::vector<int32_t>::difference_type udet_index = std::distance(beg, itr);
    det_ids[mon_index] = udet[udet_index];
  }
  file.closeGroup();
}

void LoadNexusMonitors2::runLoadLogs(const std::string filename,
                                     API::MatrixWorkspace_sptr localWorkspace) {
  // do the actual work
  API::IAlgorithm_sptr loadLogs = createChildAlgorithm("LoadNexusLogs");

  // Now execute the Child Algorithm. Catch and log any error, but don't stop.
  try {
    g_log.information() << "Loading logs from NeXus file..." << std::endl;
    loadLogs->setPropertyValue("Filename", filename);
    loadLogs->setProperty<API::MatrixWorkspace_sptr>("Workspace",
                                                     localWorkspace);
    loadLogs->execute();
  } catch (...) {
    g_log.error() << "Error while loading Logs from Nexus. Some sample logs "
                     "may be missing." << std::endl;
  }
}

//------------------------------------------------------------------------------
/**
 * Helper method to make sure that a file is / can be openend as a NeXus file
 *
 * @param fname :: name of the file
 * @return True if opening the file as NeXus and retrieving entries succeeds
 **/
bool LoadNexusMonitors2::canOpenAsNeXus(const std::string &fname) {
  bool res = true;
  ::NeXus::File *f = nullptr;
  try {
    f = new ::NeXus::File(fname);
    if (f)
      f->getEntries();
  } catch (::NeXus::Exception &e) {
    g_log.error() << "Failed to open as a NeXus file: '" << fname
                  << "', error description: " << e.what() << std::endl;
    res = false;
  }
  if (f)
    delete f;
  return res;
}

//------------------------------------------------------------------------------
/**
 * Splits multiperiod histogram data into seperate workspaces and puts them in
 * a group
 *
 * @param numPeriods :: number of periods
 **/
void LoadNexusMonitors2::splitMutiPeriodHistrogramData(
    const size_t numPeriods) {
  // protection - we should not have entered the routine if these are not true
  // More than 1 period
  if (numPeriods < 2) {
    g_log.warning()
        << "Attempted to split multiperiod histogram workspace with "
        << numPeriods << "periods, aborted." << std::endl;
    return;
  }

  // Y array should be divisible by the number of periods
  if (m_workspace->blocksize() % numPeriods != 0) {
    g_log.warning()
        << "Attempted to split multiperiod histogram workspace with "
        << m_workspace->blocksize() << "data entries, into " << numPeriods
        << "periods."
           " Aborted." << std::endl;
    return;
  }

  WorkspaceGroup_sptr wsGroup(new WorkspaceGroup);
  size_t yLength = m_workspace->blocksize() / numPeriods;
  size_t xLength = yLength + 1;
  size_t numSpectra = m_workspace->getNumberHistograms();
  ISISRunLogs monLogCreator(m_workspace->run(), static_cast<int>(numPeriods));
  for (size_t i = 0; i < numPeriods; i++) {
    // create the period workspace
    API::MatrixWorkspace_sptr wsPeriod =
        API::WorkspaceFactory::Instance().create(m_workspace, numSpectra,
                                                 xLength, yLength);

    // assign x values - restart at start for all periods
    for (size_t specIndex = 0; specIndex < numSpectra; specIndex++) {
      MantidVec &outputVec = wsPeriod->dataX(specIndex);
      const MantidVec &inputVec = m_workspace->readX(specIndex);
      for (size_t index = 0; index < xLength; index++) {
        outputVec[index] = inputVec[index];
      }
    }

    // assign y values - use the values offset by the period number
    for (size_t specIndex = 0; specIndex < numSpectra; specIndex++) {
      MantidVec &outputVec = wsPeriod->dataY(specIndex);
      const MantidVec &inputVec = m_workspace->readY(specIndex);
      for (size_t index = 0; index < yLength; index++) {
        outputVec[index] = inputVec[(yLength * i) + index];
      }
    }

    // assign E values
    for (size_t specIndex = 0; specIndex < numSpectra; specIndex++) {
      MantidVec &outputVec = wsPeriod->dataE(specIndex);
      const MantidVec &inputVec = m_workspace->readE(specIndex);
      for (size_t index = 0; index < yLength; index++) {
        outputVec[index] = inputVec[(yLength * i) + index];
      }
    }

    // add period logs
    monLogCreator.addPeriodLogs(static_cast<int>(i + 1),
                                wsPeriod->mutableRun());

    // add to workspace group
    wsGroup->addWorkspace(wsPeriod);
  }

  // set the output workspace
  this->setProperty("OutputWorkspace", wsGroup);
}

} // end DataHandling
} // end Mantid

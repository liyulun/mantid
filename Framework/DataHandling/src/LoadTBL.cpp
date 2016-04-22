//----------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------
#include "MantidDataHandling/LoadTBL.h"
#include "MantidAPI/FileProperty.h"
#include "MantidAPI/RegisterFileLoader.h"
#include "MantidAPI/WorkspaceFactory.h"
#include "MantidKernel/Strings.h"
#include "MantidAPI/TableRow.h"
#include <fstream>

#include <boost/tokenizer.hpp>
#include <MantidKernel/StringTokenizer.h>
// String utilities
#include <boost/algorithm/string.hpp>

namespace Mantid {
namespace DataHandling {
DECLARE_FILELOADER_ALGORITHM(LoadTBL)

using namespace Kernel;
using namespace API;

/// Empty constructor
LoadTBL::LoadTBL() : m_expectedCommas(16) {}

/**
* Return the confidence with with this algorithm can load the file
* @param descriptor A descriptor for the file
* @returns An integer specifying the confidence level. 0 indicates it will not
* be used
*/
int LoadTBL::confidence(Kernel::FileDescriptor &descriptor) const {
  const std::string &filePath = descriptor.filename();
  const size_t filenameLength = filePath.size();

  // Avoid some known file types that have different loaders
  int confidence(0);
  if (filePath.compare(filenameLength - 12, 12, "_runinfo.xml") == 0 ||
      filePath.compare(filenameLength - 6, 6, ".peaks") == 0 ||
      filePath.compare(filenameLength - 10, 10, ".integrate") == 0) {
    confidence = 0;
  } else if (descriptor.isAscii()) {
    std::istream &stream = descriptor.data();
    std::string firstLine;
    Kernel::Strings::extractToEOL(stream, firstLine);
    std::vector<std::string> columns;
    try {
      if (getCells(firstLine, columns, 16) == 17) // right ammount of columns
      {
        if (filePath.compare(filenameLength - 4, 4, ".tbl") == 0) {
          confidence = 40;
        } else {
          confidence = 20;
        }
      } else // incorrect amount of columns
      {
        confidence = 0;
      }
    } catch (std::length_error) {
      confidence = 0;
    }
  }
  return confidence;
}

/**
* counte the commas in the line
* @param line the line to count from
* @returns a size_t of how many commas were in line
*/
size_t LoadTBL::countCommas(std::string line) const {
  size_t found = 0;
  size_t pos = line.find(',', 0);
  if (pos != std::string::npos) {
    ++found;
  }
  while (pos != std::string::npos) {
    pos = line.find(',', pos + 1);
    if (pos != std::string::npos) {
      ++found;
    }
  }
  return found;
}

/**
* find pairs of qutoes and store them in a vector
* @param line the line to count from
* @param quoteBounds a vector<vector<size_t>> which will contain the locations
* of pairs of quotes
* @returns a size_t of how many pairs of quotes were in line
*/
size_t
LoadTBL::findQuotePairs(std::string line,
                        std::vector<std::vector<size_t>> &quoteBounds) const {
  size_t quoteOne = 0;
  size_t quoteTwo = 0;
  while (quoteOne != std::string::npos && quoteTwo != std::string::npos) {
    if (quoteTwo == 0) {
      quoteOne = line.find('"');
    } else {
      quoteOne = line.find('"', quoteTwo + 1);
    }
    if (quoteOne != std::string::npos) {
      quoteTwo = line.find('"', quoteOne + 1);
      if (quoteTwo != std::string::npos) {
        std::vector<size_t> quotepair;
        quotepair.push_back(quoteOne);
        quotepair.push_back(quoteTwo);
        quoteBounds.push_back(quotepair);
      }
    }
  }
  return quoteBounds.size();
}

/**
* parse the CSV format if it's not a simple case of splitting 16 commas
* @param line the line to parse
* @param cols The vector to parse into
* @param quoteBounds a vector<vector<size_t>> containing the locations of pairs
* of quotes
* @throws std::length_error if anything other than 17 columns (or 16
* cell-delimiting commas) is found
*/
void LoadTBL::csvParse(std::string line, std::vector<std::string> &cols,
                       std::vector<std::vector<size_t>> &quoteBounds,
                       size_t expectedCommas) const {
  size_t pairID = 0;
  size_t valsFound = 0;
  size_t lastComma = 0;
  size_t pos = 0;
  bool firstCheck = true;
  bool firstCell = true;
  cols.clear();
  while (pos != std::string::npos) {
    if (firstCheck) {
      pos = line.find(',');
      firstCheck = false;
      // lastpos = pos;
    } else {
      pos = line.find(',', pos + 1);
      // lastpos = pos;
    }
    if (pos != std::string::npos) {
      if (pairID < quoteBounds.size() && pos > quoteBounds.at(pairID).at(0)) {
        if (pos > quoteBounds.at(pairID).at(1)) {
          // use the quote indexes to get the substring
          cols.push_back(line.substr(quoteBounds.at(pairID).at(0) + 1,
                                     quoteBounds.at(pairID).at(1) -
                                         (quoteBounds.at(pairID).at(0) + 1)));
          ++pairID;
          ++valsFound;
        }
      } else {
        if (firstCell) {
          cols.push_back(line.substr(0, pos));
          firstCell = false;
        } else {
          cols.push_back(line.substr(lastComma + 1, pos - (lastComma + 1)));
        }
        ++valsFound;
      }
      lastComma = pos;
    } else {
      if (lastComma + 1 < line.length()) {
        cols.push_back(line.substr(lastComma + 1));
      } else {
        cols.emplace_back("");
      }
    }
  }
  if (cols.size() != expectedCommas + 1) {
    std::string message = "A line must contain " +
                          boost::lexical_cast<std::string>(expectedCommas) +
                          " cell-delimiting commas. Found " +
                          boost::lexical_cast<std::string>(cols.size() - 1) +
                          ".";
    throw std::length_error(message);
  }
}

/**
* Return the confidence with with this algorithm can load the file
* @param line the line to parse
* @param cols The vector to parse into
* @returns An integer specifying how many columns were parsed into. This should
* always be 17.
* @throws std::length_error if anything other than 17 columns (or 16
* cell-delimiting commas) is found
*/
size_t LoadTBL::getCells(std::string line, std::vector<std::string> &cols,
                         size_t expectedCommas) const {
  // first check the number of commas in the line.
  size_t found = countCommas(line);
  if (found == expectedCommas) {
    // If there are 16 that simplifies things and i can get boost to do the hard
    // work
    boost::split(cols, line, boost::is_any_of(","), boost::token_compress_off);
  } else if (found < expectedCommas) {
    // less than 16 means the line isn't properly formatted. So Throw
    std::string message = "A line must contain " +
                          boost::lexical_cast<std::string>(expectedCommas) +
                          " cell-delimiting commas. Found " +
                          boost::lexical_cast<std::string>(found) + ".";
    throw std::length_error(message);
  } else {
    // More than 16 will need further checks as more is only ok when pairs of
    // quotes surround a comma, meaning it isn't a delimiter
    std::vector<std::vector<size_t>> quoteBounds;
    findQuotePairs(line, quoteBounds);
    // if we didn't find any quotes, then there are too many commas and we
    // definitely have too many delimiters
    if (quoteBounds.empty()) {
      std::string message = "A line must contain " +
                            boost::lexical_cast<std::string>(expectedCommas) +
                            " cell-delimiting commas. Found " +
                            boost::lexical_cast<std::string>(found) + ".";
      throw std::length_error(message);
    }
    // now go through and split it up manually. Throw if we find ourselves in a
    // positon where we'd add a 18th value to the vector
    csvParse(line, cols, quoteBounds, expectedCommas);
  }
  return cols.size();
}
bool LoadTBL::getColumnHeadings(std::string line,
                                std::vector<std::string> &cols) {
  boost::split(cols, line, boost::is_any_of(","), boost::token_compress_off);
  std::string firstEntry = cols[0];
  if (std::all_of(firstEntry.begin(), firstEntry.end(), ::isalpha))
    // TBL file contains column headings
    return false;
  else {
    cols.clear();
    return true;
  }
}
//--------------------------------------------------------------------------
// Private methods
//--------------------------------------------------------------------------
/// Initialisation method.
void LoadTBL::init() {
  declareProperty(Kernel::make_unique<FileProperty>("Filename", "",
                                                    FileProperty::Load, ".tbl"),
                  "The name of the table file to read, including its full or "
                  "relative path. The file extension must be .tbl");
  declareProperty(make_unique<WorkspaceProperty<ITableWorkspace>>(
                      "OutputWorkspace", "", Direction::Output),
                  "The name of the workspace that will be created.");
}

/**
*   Executes the algorithm.
*/
void LoadTBL::exec() {
  std::string filename = getProperty("Filename");
  std::ifstream file(filename.c_str());
  if (!file) {
    throw Exception::FileError("Unable to open file: ", filename);
  }
  std::string line = "";

  ITableWorkspace_sptr ws = WorkspaceFactory::Instance().createTable();

  std::vector<std::string> columns;

  Kernel::Strings::extractToEOL(file, line);
  // We want to check if the first line contains an empty string or series of
  // ",,,,,"
  // to see if we are loading a TBL file that actually contains data or not.
  boost::split(columns, line, boost::is_any_of(","), boost::token_compress_off);
  for (auto entry = columns.begin(); entry != columns.end();) {
    if (std::string(*entry).compare("") == 0) {
      // erase the empty values
      entry = columns.erase(entry);
    } else {
      // keep any non-empty values
      ++entry;
    }
  }
  if (columns.empty()) {
    // we have an empty string or series of ",,,,,"
    throw std::runtime_error("The file you are trying to load is Empty. \n "
                             "Please load a non-empty TBL file");
  } else {
    // set columns back to empty ready to populated with columnHeadings.
    columns.clear();
  }
  // this will tell us if we need to just fill in the cell values
  // or whether we will have to create the column headings as well.
  bool isOld = getColumnHeadings(line, columns);

  if (isOld) {
    /**THIS IS ESSENTIALLY THE OLD LoadReflTBL CODE**/
    // create the column headings
    auto colRuns = ws->addColumn("str", "Run(s)");
    auto colTheta = ws->addColumn("str", "ThetaIn");
    auto colTrans = ws->addColumn("str", "TransRun(s)");
    auto colQmin = ws->addColumn("str", "Qmin");
    auto colQmax = ws->addColumn("str", "Qmax");
    auto colDqq = ws->addColumn("str", "dq/q");
    auto colScale = ws->addColumn("double", "Scale");
    auto colStitch = ws->addColumn("int", "StitchGroup");
    auto colOptions = ws->addColumn("str", "Options");

    colRuns->setPlotType(0);
    colTheta->setPlotType(0);
    colTrans->setPlotType(0);
    colQmin->setPlotType(0);
    colQmax->setPlotType(0);
    colDqq->setPlotType(0);
    colScale->setPlotType(0);
    colStitch->setPlotType(0);
    colOptions->setPlotType(0);
    // we are using the old ReflTBL format
    // where all of the entries are on one line
    // so we must reset the stream to reread the first line.
    std::ifstream file(filename.c_str());
    if (!file) {
      throw Exception::FileError("Unable to open file: ", filename);
    }
    std::string line = "";
    int stitchID = 1;
    while (Kernel::Strings::extractToEOL(file, line)) {
      if (line == "" || line == ",,,,,,,,,,,,,,,,") {
        continue;
      }
      getCells(line, columns, 16);
      const std::string scaleStr = columns.at(16);
      double scale = 1.0;
      if (!scaleStr.empty())
        Mantid::Kernel::Strings::convert<double>(columns.at(16), scale);

      // check if the first run in the row has any data associated with it
      // 0 = runs, 1 = theta, 2 = trans, 3 = qmin, 4 = qmax
      if (columns[0] != "" || columns[1] != "" || columns[2] != "" ||
          columns[3] != "" || columns[4] != "") {
        TableRow row = ws->appendRow();
        for (int i = 0; i < 5; ++i) {
          row << columns.at(i);
        }
        row << columns.at(15);
        row << scale;
        row << stitchID;
      }

      // check if the second run in the row has any data associated with it
      // 5 = runs, 6 = theta, 7 = trans, 8 = qmin, 9 = qmax
      if (columns[5] != "" || columns[6] != "" || columns[7] != "" ||
          columns[8] != "" || columns[9] != "") {
        TableRow row = ws->appendRow();
        for (int i = 5; i < 10; ++i) {
          row << columns.at(i);
        }
        row << columns.at(15);
        row << scale;
        row << stitchID;
      }

      // check if the third run in the row has any data associated with it
      // 10 = runs, 11 = theta, 12 = trans, 13 = qmin, 14 = qmax
      if (columns[10] != "" || columns[11] != "" || columns[12] != "" ||
          columns[13] != "" || columns[14] != "") {
        TableRow row = ws->appendRow();
        for (int i = 10; i < 17; ++i) {
          if (i == 16)
            row << scale;
          else
            row << columns.at(i);
        }
        row << stitchID;
      }
      ++stitchID;
      setProperty("OutputWorkspace", ws);
    }

  } else {
    // we have a TBL format that contains column headings
    // on the first row. These are now entries in the columns vector
    if (!columns.empty()) {
      // now we need to add the custom column headings from
      // the columns vector to the TableWorkspace
      for (auto heading = columns.begin(); heading != columns.end();) {
        if (std::string(*heading).compare("") == 0) {
          // there is no need to have empty column headings.
          heading = columns.erase(heading);
        } else {
          Mantid::API::Column_sptr col;
          // The Group column will always be second-to-last
          // in the TBL file. This is the only column that
          // should be of type "int".
          if (*heading == columns.at(columns.size() - 2))
            col = ws->addColumn("int", *heading);
          else
            // All other entries in the TableWorkspace will
            // have a type of "str"
            col = ws->addColumn("str", *heading);
          col->setPlotType(0);
          heading++;
        }
      }
    }

    while (Kernel::Strings::extractToEOL(file, line)) {
      if (line == "" || line == ",,,,,,,,,,,,,,,,") {
        // skip over any empty lines
        continue;
      }
      getCells(line, columns, columns.size() - 1);
      // populate the columns with their values for this row.
      TableRow row = ws->appendRow();
      for (int i = 0; i < columns.size(); ++i) {
        if (i == columns.size() - 2)
          // taking into consideration Group column
          // of type "int"
          row << boost::lexical_cast<int>(columns.at(i));
        else
          row << columns.at(i);
      }
    }
    setProperty("OutputWorkspace", ws);
  }
}

} // namespace DataHandling
} // namespace Mantid
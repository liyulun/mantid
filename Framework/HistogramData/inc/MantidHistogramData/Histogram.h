#ifndef MANTID_HISTOGRAMDATA_HISTOGRAM_H_
#define MANTID_HISTOGRAMDATA_HISTOGRAM_H_

#include "MantidHistogramData/DllConfig.h"
#include "MantidKernel/cow_ptr.h"
#include "MantidHistogramData/BinEdges.h"
#include "MantidHistogramData/Points.h"
#include "MantidHistogramData/PointStandardDeviations.h"
#include "MantidHistogramData/PointVariances.h"

#include <vector>

namespace Mantid {
namespace HistogramData {

/** Histogram : TODO: DESCRIPTION

  Copyright &copy; 2016 ISIS Rutherford Appleton Laboratory, NScD Oak Ridge
  National Laboratory & European Spallation Source

  This file is part of Mantid.

  Mantid is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  Mantid is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  File change history is stored at: <https://github.com/mantidproject/mantid>
  Code Documentation is available at: <http://doxygen.mantidproject.org>
*/
class MANTID_HISTOGRAMDATA_DLL Histogram {
private:
  Kernel::cow_ptr<HistogramX> m_x;
  // Mutable until Dx legacy interface is removed.
  mutable Kernel::cow_ptr<HistogramDx> m_dx{nullptr};

public:
  enum class XMode { BinEdges, Points };
  explicit Histogram(XMode mode)
      : m_x(Kernel::make_cow<HistogramX>(0)), m_xMode(mode) {}
  explicit Histogram(const Points &points)
      : m_x(points.cowData()), m_xMode(XMode::Points) {}
  explicit Histogram(const BinEdges &edges)
      : m_x(edges.cowData()), m_xMode(XMode::BinEdges) {
    if (m_x->size() == 1)
      throw std::logic_error("Histogram: BinEdges size cannot be 1");
  }

  XMode xMode() const noexcept { return m_xMode; }

  BinEdges binEdges() const;
  Points points() const;
  PointVariances pointVariances() const;
  PointStandardDeviations pointStandardDeviations() const;
  template <typename... T> void setBinEdges(T &&... data);
  template <typename... T> void setPoints(T &&... data);
  template <typename... T> void setPointVariances(T &&... data);
  template <typename... T> void setPointStandardDeviations(T &&... data);

  const HistogramX &x() const { return *m_x; }
  const HistogramDx &dx() const { return *m_dx; }
  HistogramX &mutableX() { return m_x.access(); }
  HistogramDx &mutableDx() { return m_dx.access(); }

  Kernel::cow_ptr<HistogramX> sharedX() const { return m_x; }
  Kernel::cow_ptr<HistogramDx> sharedDx() const { return m_dx; }
  void setSharedX(const Kernel::cow_ptr<HistogramX> &X);
  void setSharedDx(const Kernel::cow_ptr<HistogramDx> &Dx);

  // Temporary legacy interface to X
  void setX(const Kernel::cow_ptr<HistogramX> &X) { m_x = X; }
  MantidVec &dataX() { return m_x.access().mutableRawData(); }
  const MantidVec &dataX() const { return m_x->rawData(); }
  const MantidVec &readX() const { return m_x->rawData(); }
  Kernel::cow_ptr<HistogramX> ptrX() const { return m_x; }

  // Temporary legacy interface to Dx. Note that accessors mimic the current
  // behavior which always has Dx allocated.
  MantidVec &dataDx() {
    if(!m_dx)
      m_dx = Kernel::make_cow<HistogramDx>(m_x->size(), 0.0);
    return m_dx.access().mutableRawData();
  }
  const MantidVec &dataDx() const {
    if(!m_dx)
      m_dx = Kernel::make_cow<HistogramDx>(m_x->size(), 0.0);
    return m_dx->rawData();
  }
  const MantidVec &readDx() const {
    if(!m_dx)
      m_dx = Kernel::make_cow<HistogramDx>(m_x->size(), 0.0);
    return m_dx->rawData();
  }

private:
  void checkSize(const Points &points) const;
  void checkSize(const PointVariances &points) const;
  void checkSize(const PointStandardDeviations &points) const;
  void checkSize(const BinEdges &binEdges) const;
  template <class... T> bool selfAssignmentX(const T &...) { return false; }
  template <class... T> bool selfAssignmentDx(const T &...) { return false; }

  XMode m_xMode;
};

/** Sets the Histogram's bin edges.

 Any arguments that can be used for constructing a BinEdges object are allowed,
 however, a size check ensures that the Histogram stays valid, i.e., that x and
 y lengths are consistent. */
template <typename... T> void Histogram::setBinEdges(T &&... data) {
  BinEdges edges(std::forward<T>(data)...);
  // If there is no data changing the size is ok.
  // if(m_y)
  checkSize(edges);
  if (selfAssignmentX(data...))
    return;
  m_xMode = XMode::BinEdges;
  m_x = edges.cowData();
}

/** Sets the Histogram's points.

 Any arguments that can be used for constructing a Points object are allowed,
 however, a size check ensures that the Histogram stays valid, i.e., that x and
 y lengths are consistent. */
template <typename... T> void Histogram::setPoints(T &&... data) {
  Points points(std::forward<T>(data)...);
  // If there is no data changing the size is ok.
  // if(m_y)
  checkSize(points);
  if (selfAssignmentX(data...))
    return;
  m_xMode = XMode::Points;
  m_x = points.cowData();
}

/// Sets the Histogram's point variances.
template <typename... T> void Histogram::setPointVariances(T &&... data) {
  PointVariances points(std::forward<T>(data)...);
  if(points)
    checkSize(points);
  // No sensible self assignment is possible, we do not store variances, so if
  // anyone tries to set our current data as variances it must be an error.
  if (selfAssignmentDx(data...))
    throw std::logic_error("Histogram::setPointVariances: Attempt to "
                           "self-assign standard deviations as variance.");
  // Convert variances to standard deviations before storing it.
  m_dx = PointStandardDeviations(std::move(points)).cowData();
}

/// Sets the Histogram's point standard deviations.
template <typename... T> void Histogram::setPointStandardDeviations(T &&... data) {
  PointStandardDeviations points(std::forward<T>(data)...);
  if(points)
    checkSize(points);
  if (selfAssignmentDx(data...))
    return;
  m_dx = points.cowData();
}

template <> inline bool Histogram::selfAssignmentX(const HistogramX &data) {
  return &data == m_x.get();
}

template <>
inline bool Histogram::selfAssignmentX(const std::vector<double> &data) {
  return &data == &(m_x->rawData());
}

template <> inline bool Histogram::selfAssignmentDx(const HistogramDx &data) {
  return &data == m_dx.get();
}

template <>
inline bool Histogram::selfAssignmentDx(const std::vector<double> &data) {
  return &data == &(m_dx->rawData());
}

MANTID_HISTOGRAMDATA_DLL Histogram::XMode getHistogramXMode(size_t xLength,
                                                            size_t yLength);

} // namespace HistogramData
} // namespace Mantid

#endif /* MANTID_HISTOGRAMDATA_HISTOGRAM_H_ */

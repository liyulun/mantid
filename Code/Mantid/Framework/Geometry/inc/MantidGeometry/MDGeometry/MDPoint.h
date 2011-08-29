#ifndef MDPoint_H_
#define MDPoint_H_

/** Abstract type to represent a multidimensional pixel/point. 
*   This type may not be a suitable pure virtual type in future iterations. Future implementations should be non-abstract.

    @author Owen Arnold, RAL ISIS
    @date 15/11/2010

    Copyright &copy; 2007-10 ISIS Rutherford Appleton Laboratory & NScD Oak Ridge National Laboratory

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

    File change history is stored at: <https://svn.mantidproject.org/mantid/trunk/Code/Mantid>.
    Code Documentation is available at: <http://doxygen.mantidproject.org>
*/

//----------------------------------------------------------------------
// Includes
//----------------------------------------------------------------------
#include "MantidGeometry/DllConfig.h"
#include "MantidGeometry/IDetector.h"
#include "MantidGeometry/Instrument.h"
#include "MantidGeometry/MDGeometry/Coordinate.h"
#include "MantidGeometry/DllConfig.h"
#include <vector>

namespace Mantid
{
  namespace Geometry
  {
    //TODO: relocate once name is stable.
    class MDPoint;
    class MANTID_GEOMETRY_DLL SignalAggregate
    {
    public:
      virtual std::vector<Coordinate> getVertexes() const = 0;
      virtual signal_t getSignal() const = 0;
      virtual signal_t getError() const = 0;
      virtual std::vector<boost::shared_ptr<MDPoint> > getContributingPoints() const = 0;
      virtual ~SignalAggregate(){};
    };

    class MANTID_GEOMETRY_DLL MDPoint : public SignalAggregate
    {
    private:
      signal_t m_signal;
      signal_t m_error;
      Instrument_const_sptr m_instrument;
      IDetector_const_sptr m_detector;
      std::vector<Coordinate> m_vertexes;
      int m_runId;
    public:
      MDPoint(){};
      MDPoint(signal_t signal, signal_t error, const std::vector<Coordinate>& vertexes, IDetector_const_sptr detector, Instrument_const_sptr instrument,
          const int runId = 0);
      std::vector<Coordinate> getVertexes() const;
      signal_t getSignal() const;
      signal_t getError() const;
      IDetector_const_sptr getDetector() const;
      Instrument_const_sptr getInstrument() const;
      int getRunId() const;
      virtual ~MDPoint();
      std::vector<boost::shared_ptr<MDPoint> > getContributingPoints() const;
      //virtual Mantid::API::Run getRun();
    protected:
    };
  }
}

#endif 

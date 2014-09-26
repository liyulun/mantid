#ifndef MANTID_SINQ_POLDICALCULATESPECTRUM2D_H_
#define MANTID_SINQ_POLDICALCULATESPECTRUM2D_H_

#include "MantidKernel/System.h"
#include "MantidSINQ/DllConfig.h"
#include "MantidAPI/Algorithm.h"
#include "MantidAPI/MultiDomainFunction.h"
#include "MantidDataObjects/TableWorkspace.h"
#include "MantidSINQ/PoldiUtilities/PoldiPeakCollection.h"
#include "MantidSINQ/PoldiUtilities/PoldiTimeTransformer.h"
#include "MantidSINQ/PoldiUtilities/Poldi2DFunction.h"


namespace Mantid
{
namespace Poldi
{

/** PoldiCalculateSpectrum2D

    An Algorithm to calculate a POLDI 2D-spectrum from a given table containing POLDI
    peak data. A MatrixWorkspace containing a proper POLDI instrument definition is required
    to determine output workspace dimensions etc.

      @author Michael Wedel, Paul Scherrer Institut - SINQ
      @date 16/05/2014

      Copyright © 2014 PSI-MSS

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

class MANTID_SINQ_DLL PoldiCalculateSpectrum2D  : public API::Algorithm
{
public:
    PoldiCalculateSpectrum2D();
    virtual ~PoldiCalculateSpectrum2D();
    
    virtual const std::string name() const;
    virtual int version() const;
    virtual const std::string category() const;

    virtual const std::string summary() const;

protected:
    PoldiPeakCollection_sptr getPeakCollection(const DataObjects::TableWorkspace_sptr &peakTable) const;
    PoldiPeakCollection_sptr getIntegratedPeakCollection(const PoldiPeakCollection_sptr &rawPeakCollection) const;
    PoldiPeakCollection_sptr getNormalizedPeakCollection(const PoldiPeakCollection_sptr &peakCollection) const;

    boost::shared_ptr<Poldi2DFunction> getFunctionFromPeakCollection(const PoldiPeakCollection_sptr &peakCollection) const;
    void addBackgroundTerms(boost::shared_ptr<Poldi2DFunction> poldi2DFunction) const;

    API::MatrixWorkspace_sptr calculateSpectrum(const PoldiPeakCollection_sptr &peakCollection, const API::MatrixWorkspace_sptr &matrixWorkspace);

    void setTimeTransformerFromInstrument(const PoldiInstrumentAdapter_sptr &poldiInstrument);
    void setTimeTransformer(const PoldiTimeTransformer_sptr &poldiTimeTransformer);

    void setDeltaTFromWorkspace(const API::MatrixWorkspace_sptr &matrixWorkspace);
    void setDeltaT(double newDeltaT);
    bool isValidDeltaT(double deltaT) const;

    void throwOnInsufficientState();


    PoldiTimeTransformer_sptr m_timeTransformer;
    double m_deltaT;

private:
    void init();
    void exec();

};


} // namespace Poldi
} // namespace Mantid

#endif  /* MANTID_SINQ_POLDICALCULATESPECTRUM2D_H_ */
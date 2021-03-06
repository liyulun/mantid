.. algorithm::

.. summary::

.. alias::

.. properties::

Description
-----------

This utility algorithm attempts to search for the elastic peak position (EPP) in each spectrum of the given workspace. The algorithm estimates the starting parameters and performs Gaussian fit using the :ref:`algm-Fit` algorithm.

.. note::
    This algorithm uses very simple approach to search for an elastic peak: it suggests that the elastic peak has maximal intensity. This approach may fail in the case if the dataset contains Bragg peaks with higher intensities.

As a result, `TableWorkspace <http://www.mantidproject.org/TableWorkspace>`_ with the following columns is produced: *WorkspaceIndex*, *PeakCentre*, *PeakCentreError*, *Sigma*, *SigmaError*, *Height*, *HeightError*, *chiSq* and *FitStatus*. Table rows correspond to the workspace indices.


Restrictions on the input workspaces
####################################

-  The unit of the X-axis must be **Time-of-flight**.


Usage
-----
**Example: Find EPP in the given workspace.**

.. testcode:: ExFindEPP

    # create sample workspace
    ws = CreateSampleWorkspace(Function="User Defined", UserDefinedFunction="name=LinearBackground, \
                A0=0.3;name=Gaussian, PeakCentre=6000, Height=5, Sigma=75", NumBanks=2, BankPixelWidth=1,
                XMin=4005.75, XMax=7995.75, BinWidth=10.5, BankDistanceFromSample=4.0)

    # search for elastic peak positions
    table = FindEPP(ws)

    # print some results
    print "The fit status is", table.row(0)['FitStatus']
    print "The peak centre is at", round(table.row(0)['PeakCentre'], 2), "microseconds"
    print "The peak height is", round(table.row(0)['Height'],2)

Output:

.. testoutput:: ExFindEPP

    The fit status is success
    The peak centre is at 6005.25 microseconds
    The peak height is 4.84


.. categories::

.. sourcelink::

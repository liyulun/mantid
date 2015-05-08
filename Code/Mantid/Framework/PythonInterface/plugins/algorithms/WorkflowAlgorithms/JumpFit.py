#pylint: disable=no-init
from mantid.kernel import *
from mantid.api import *
from mantid.simpleapi import (SaveNexusProcessed, ExtractSingleSpectrum, Fit,
                              CopyLogs, AddSampleLogMultiple, DeleteWorkspace)
from mantid import logger, mtd
import os


class JumpFit(PythonAlgorithm):

    _in_ws = None
    _out_name = None
    _jump_function = None
    _width = None
    _q_min = None
    _q_max = None
    _plot = None
    _save = None


    def category(self):
        return 'Workflow\\Inelastic;PythonAlgorithms;Inelastic'


    def PyInit(self):
        self.declareProperty(WorkspaceProperty('InputWorkspace', '',
                                               direction=Direction.Input),
                             doc='Input workspace in HWHM')

        valid_functions = ['ChudleyElliot', 'HallRoss', 'FickDiffusion', 'TeixeiraWater']
        self.declareProperty(name='Function', defaultValue=valid_functions[0],
                             validator=StringListValidator(valid_functions),
                             doc='The fit function to use')

        self.declareProperty(name='Width', defaultValue=0,
                             validator=IntMandatoryValidator(),
                             doc='Spectrum in the workspace to use for fiting')

        self.declareProperty(name='QMin', defaultValue=0.0,
                             validator=FloatMandatoryValidator(),
                             doc='Lower bound of Q range to use for fitting')
        self.declareProperty(name='QMax', defaultValue=0.0,
                             validator=FloatMandatoryValidator(),
                             doc='Upper bound of Q range to use for fitting')

        self.declareProperty(name='Output', defaultValue='',
                             direction=Direction.InOut,
                             doc='Output name')

        self.declareProperty(name='Plot', defaultValue=False,
                             doc='Plot result workspace')
        self.declareProperty(name='Save', defaultValue=False,
                             doc='Save result workspace to nexus file in the default save directory')


    def PyExec(self):
        self._setup()

        # Select the width we wish to fit
        spectrum_ws = "__" + self._in_ws
        ExtractSingleSpectrum(InputWorkspace=self._in_ws, OutputWorkspace=spectrum_ws,
                              WorkspaceIndex=self._width)

        logger.information('Cropping from Q= ' + str(self._q_min) + ' to ' + str(self._q_max))
        in_run = mtd[self._in_ws].getRun()
        try:
            log = in_run.getLogData('fit_program')
            if log:
                val = log.value
                logger.notice('Fit program was : ' + val)
        except RuntimeError:
            # If we couldn't find the fit program, just pass
            pass

        logger.information('Parameters in ' + self._in_ws)

        x_data = mtd[self._in_ws].readX(0)
        m_max = x_data[-1]

        # Select fit function to use
        if self._jump_function == 'ChudleyElliot':
            # Chudley-Elliott: HWHM=(1-sin*(Q*L)/(Q*L))/Tau
            # for Q->0 W=Q^2*L^2/(6*Tau)

            t_val = 1.0 / m_max
            l_val = 1.5
            function = 'name=ChudleyElliot, Tau=' + str(t_val) + ', L=' + str(l_val)

        elif self._jump_function == 'HallRoss':
            # Hall-Ross: HWHM=(1-exp(-L*Q^2))/Tau
            # for Q->0 W=A*Q^2*r

            t_val = 1.0 / m_max
            l_val = 1.5
            function = 'name=HallRoss, Tau=' + str(t_val) + ', L=' + str(l_val)

        elif self._jump_function == 'FickDiffusion':
            # Fick: HWHM=D*Q^2

            y_data = mtd[self._in_ws].readY(0)
            diff = (y_data[2] - y_data[0]) / ((x_data[2] - x_data[0]) * (x_data[2] - x_data[0]))
            function = 'name=FickDiffusion, D=' + str(diff)

        elif self._jump_function == 'TeixeiraWater':
            # Teixeira: HWHM=Q^2*L/((1+Q^2*L)*tau)
            # for Q->0 W=

            t_val = 1.0 / m_max
            l_val = 1.5
            function = 'name=TeixeiraWater, Tau=' + str(t_val) + ', L=' + str(l_val)

        # Run fit function
        if self._out_name is "":
            # Find the last underscore
            ws_suffix_index = self._in_ws.rfind('_')

            if ws_suffix_index == -1:
                # Use the entire string if underscore not found
                ws_suffix_index = len(self._in_ws)

            self._out_name = self._in_ws[:ws_suffix_index] + '_' + self._jump_function + '_fit'

        Fit(Function=function,
            InputWorkspace=spectrum_ws,
            CreateOutput=True,
            Output=self._out_name,
            StartX=self._q_min,
            EndX=self._q_max)
        fit_workspace = self._out_name + '_Workspace'

        # Populate sample logs
        CopyLogs(InputWorkspace=self._in_ws,
                 OutputWorkspace=fit_workspace)

        sample_logs = [
                ('jumpfit_function', self._jump_function),
                ('jumpfit_q_min', self._q_min),
                ('jumpfit_q_max', self._q_max)
            ]

        AddSampleLogMultiple(Workspace=fit_workspace,
                             LogNames=[item[0] for item in sample_logs],
                             LogValues=[item[1] for item in sample_logs])

        self._process_output(fit_workspace)
        self.setProperty('Output', self._out_name)

        DeleteWorkspace(Workspace=spectrum_ws)


    def _setup(self):
        self._in_ws = self.getPropertyValue('InputWorkspace')
        self._out_name = self.getPropertyValue('Output')

        self._jump_function = self.getProperty('Function').value
        self._width = self.getProperty('Width').value
        self._q_min = self.getProperty('QMin').value
        self._q_max = self.getProperty('QMax').value

        self._plot = self.getProperty('Plot').value
        self._save = self.getProperty('Save').value


    def _process_output(self, workspace):
        if self._save:
            fit_path = os.path.join(config['defaultsave.directory'], workspace + '.nxs')
            SaveNexusProcessed(InputWorkspace=workspace, Filename=fit_path)

            logger.information('Fit file is ' + fit_path)

        if self._plot:
            from IndirectImport import import_mantidplot
            mtd_plot = import_mantidplot()
            mtd_plot.plotSpectrum(workspace, [0, 1, 2], True)


# Register algorithm with Mantid
AlgorithmFactory.subscribe(JumpFit)

# pylint: disable=no-init,invalid-name
import mantid.simpleapi as api
from mantid.api import *
from mantid.kernel import *
from reduction_workflow.instruments.sans import hfir_instrument
from reduction_workflow.instruments.sans import sns_instrument
import sys


class SANSMask(PythonAlgorithm):
    """
        Normalise detector counts by the sample thickness
    """

    def category(self):
        return "Workflow\\SANS"

    def name(self):
        return "SANSMask"

    def summary(self):
        return "Apply mask to SANS detector"

    def PyInit(self):
        facilities = ["SNS", "HFIR"]
        self.declareProperty("Facility", "SNS",
                             StringListValidator(facilities),
                             "Facility to which the SANS instrument belongs")

        self.declareProperty(
            MatrixWorkspaceProperty(
                "Workspace",
                "",
                direction=Direction.InOut),
            "Workspace to apply the mask to")

        self.declareProperty(IntArrayProperty("MaskedDetectorList", values=[],
                                              direction=Direction.Input),
                             "List of detector IDs to be masked")

        self.declareProperty(
            IntArrayProperty(
                "MaskedEdges",
                values=[
                    0,
                    0,
                    0,
                    0],
                direction=Direction.Input),
            "Number of pixels to mask on the edges: X-low, X-high, Y-low, Y-high")

        self.declareProperty(
            "MaskedComponent",
            "",
            doc="Component Name to mask Edges. Consult the IDF!")

        self.declareProperty(
            "MaskedFullComponent",
            "",
            doc="Component Name to mask ALL of it. Consult the IDF!")

        sides = ["None", "Front", "Back"]
        self.declareProperty("MaskedSide", "None",
                             StringListValidator(sides),
                             "Side of the detector to which to apply the mask")

        self.declareProperty("OutputMessage", "",
                             direction=Direction.Output, doc="Output message")

    def PyExec(self):
        workspace = self.getProperty("Workspace").value
        facility = self.getProperty("Facility").value

        # Apply saved mask as needed
        self._apply_saved_mask(workspace, facility)

        # Mask a detector side
        self._mask_detector_side(workspace, facility)

        # Mask edges
        edges = self.getProperty("MaskedEdges").value
        component_name = self.getProperty("MaskedComponent").value
        if len(edges) == 4:
            if facility.upper() == "HFIR":
                masked_ids = hfir_instrument.get_masked_ids(
                    edges[0], edges[1], edges[2], edges[3], workspace, component_name)
                self._mask_ids(masked_ids, workspace)
            else:
                masked_pixels = sns_instrument.get_masked_pixels(
                    edges[0], edges[1], edges[2], edges[3], workspace)
                self._mask_pixels(masked_pixels, workspace, facility)

        # Mask a list of detectors
        masked_dets = self.getProperty("MaskedDetectorList").value
        if len(masked_dets) > 0:
            api.MaskDetectors(Workspace=workspace, DetectorList=masked_dets)

        # Mask component: Note that edges cannot be masked
        component_name = self.getProperty("MaskedFullComponent").value
        if component_name:
            Logger("SANSMask").debug(
                "Masking FULL component named %s." %
                component_name)
            self._mask_component(workspace, component_name)

        self.setProperty("OutputMessage", "Mask applied")

    def _mask_pixels(self, pixel_list, workspace, facility):
        if len(pixel_list) > 0:
            # Transform the list of pixels into a list of Mantid detector IDs
            if facility.upper() == "HFIR":
                masked_detectors = hfir_instrument.get_detector_from_pixel(
                    pixel_list)
            else:
                masked_detectors = sns_instrument.get_detector_from_pixel(
                    pixel_list, workspace)
            # Mask the pixels by passing the list of IDs
            api.MaskDetectors(
                Workspace=workspace,
                DetectorList=masked_detectors)

    def _mask_ids(self, id_list, workspace):
        api.MaskDetectors(Workspace=workspace, DetectorList=id_list)

    def _apply_saved_mask(self, workspace, facility):
        # Check whether the workspace has mask information
        if workspace.getRun().hasProperty("rectangular_masks"):
            mask_str = workspace.getRun().getProperty("rectangular_masks").value
            rectangular_masks = []
            toks = mask_str.split(',')
            for item in toks:
                if len(item) > 0:
                    c = item.strip().split(' ')
                    if len(c) == 4:
                        rectangular_masks.append(
                            [int(c[0]), int(c[2]), int(c[1]), int(c[3])])
            masked_pixels = []
            for rec in rectangular_masks:
                try:
                    for ix in range(rec[0], rec[1] + 1):
                        for iy in range(rec[2], rec[3] + 1):
                            masked_pixels.append([ix, iy])
                except:
                    Logger("SANSMask").error(
                        "Badly defined mask from configuration file: %s" %
                        str(rec))
                    Logger("SANSMask").error(str(sys.exc_info()[1]))
            self._mask_pixels(masked_pixels, workspace, facility)

    def _mask_detector_side(self, workspace, facility):
        """
            Mask the back side or front side as needed
        """
        side = self.getProperty("MaskedSide").value
        if side == "Front":
            side_to_mask = 0
        elif side == "Back":
            side_to_mask = 1
        else:
            return

        if not workspace.getInstrument().hasParameter("number-of-x-pixels") \
                and not workspace.getInstrument().hasParameter("number-of-y-pixels"):
            Logger("SANSMask").error(
                "Could not find number of pixels: skipping side masking")
            return

        nx = int(workspace.getInstrument().getNumberParameter(
            "number-of-x-pixels")[0])
        ny = int(workspace.getInstrument().getNumberParameter(
            "number-of-y-pixels")[0])
        id_side = []

        for iy in range(ny):
            for ix in range(side_to_mask, nx + side_to_mask, 2):
                # For some odd reason the HFIR format has the x,y coordinates
                # inverted
                if facility.upper() == "HFIR":
                    id_side.append([iy, ix])
                else:
                    id_side.append([ix, iy])

        self._mask_pixels(id_side, workspace, facility)

    def _mask_component(self, workspace, component_name):
        '''
        Masks component by name (e.g. "detector1" or "wing_detector".
        '''
        instrument = workspace.getInstrument()
        try:
            component = instrument.getComponentByName(component_name)
        except:
            Logger("SANSMask").error(
                "Component not valid! %s" %
                component_name)
            return

        masked_detectors = []
        if component.type() == 'RectangularDetector':
            id_min = component.minDetectorID()
            id_max = component.maxDetectorID()
            masked_detectors = range(id_min, id_max + 1)
        elif component.type() == 'CompAssembly' or component.type() == 'ObjCompAssembly' or component.type() == 'DetectorComponent':
            ids_gen = self.__get_ids_for_assembly(component)
            masked_detectors = list(ids_gen)
        else:
            Logger("SANSMask").error(
                "Mask not applied. Component not valid: %s of type %s." %
                (component.getName(), component.type()))

        api.MaskDetectors(Workspace=workspace, DetectorList=masked_detectors)

    def __get_ids_for_assembly(self, component):
        '''
        Recursive function that get a generator for all IDs for a component.
        Component must be one of these:
        'CompAssembly'
        'ObjCompAssembly'
        'DetectorComponent'
        '''
        if component.type() == 'DetectorComponent':
            yield component.getID()
        else:
            for i in range(component.nelements()):
                for j in self.__get_ids_for_assembly(component[i]):
                    yield j

AlgorithmFactory.subscribe(SANSMask())

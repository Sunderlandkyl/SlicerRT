/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Kyle Sunderland and Csaba Pinter,
  PerkLab, Queen's University and was supported through the Applied Cancer
  Research Unit program of Cancer Care Ontario with funds provided by the
  Ontario Ministry of Health and Long-Term Care

==============================================================================*/

#ifndef __vtkSlicerDoseVolumeHistogramComparisonLogic_h
#define __vtkSlicerDoseVolumeHistogramComparisonLogic_h

#include <vtkSlicerDoseVolumeHistogramModuleLogicExport.h>

// VTK includes
#include "vtkObject.h"

// MRML includes
#include <vtkMRMLScalarVolumeNode.h>
#include <vtkMRMLTableNode.h>

class VTK_SLICER_DOSEVOLUMEHISTOGRAM_LOGIC_EXPORT  vtkSlicerDoseVolumeHistogramComparisonLogic : public vtkObject
{

public:
  static vtkSlicerDoseVolumeHistogramComparisonLogic *New();
  vtkTypeMacro(vtkSlicerDoseVolumeHistogramComparisonLogic, vtkObject);

public:
  // Returns the percent of agreeing bins for two DVH arrays.
  // Maximum dose is calculated from the dose volume node if valid, otherwise doseMax is used.
  static double CompareDvhTables( vtkMRMLTableNode* dvh1TableNode, vtkMRMLTableNode* dvh2TableNode, vtkMRMLScalarVolumeNode* doseVolumeNode, 
                                  double volumeDifferenceCriterion, double doseToAgreementCriterion, double doseMax=0.0 );

protected:
  // Formula is (based on the article Ebert2010):
  //   gamma(i) = min{ Gamma[(di, vi), (dr, vr)] } for all {r=1..P}, where
  //   compareIndexth Dvh point has dose di and volume vi
  //   P is the number of bins in the reference Dvh, each rth bin having absolute dose dr and volume vr
  //   Gamma[(di, vi), (dr, vr)] = [ ( (100*(vr-vi)) / (volumeDifferenceCriterion * totalVolume) )^2 + ( (100*(dr-di)) / (doseToAgreementCriterion * maxDose) )^2 ] ^ 1/2
  //   volumeDifferenceCriterion is the volume-difference criterion (% of the total structure volume, totalVolume)
  //   doseToAgreementCriterion is the dose-to-agreement criterion (% of the maximum dose, maxDose)
  // A return value of < 1 indicates agreement for the Dvh bin
  static double GetAgreementForDvhPlotPoint( vtkTable* referenceDvhPlot, vtkTable* compareDvhPlot,
                                             unsigned int compareIndex, double totalVolumeCCs, double doseMax,
                                             double volumeDifferenceCriterion, double doseToAgreementCriterion );

protected:
  vtkSlicerDoseVolumeHistogramComparisonLogic();
  ~vtkSlicerDoseVolumeHistogramComparisonLogic() override;
};

#endif
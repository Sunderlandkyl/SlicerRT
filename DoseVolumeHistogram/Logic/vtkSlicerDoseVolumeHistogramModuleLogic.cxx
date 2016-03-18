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

  This file was originally developed by Csaba Pinter, PerkLab, Queen's University
  and was supported through the Applied Cancer Research Unit program of Cancer Care
  Ontario with funds provided by the Ontario Ministry of Health and Long-Term Care

==============================================================================*/

// DoseVolumeHistogram includes
#include "vtkMRMLDoseVolumeHistogramNode.h"
#include "vtkSlicerDoseVolumeHistogramModuleLogic.h"

// SlicerRT includes
#include "SlicerRtCommon.h"

// Segmentations includes
#include "vtkMRMLSegmentationNode.h"
#include "vtkSegmentation.h"
#include "vtkSegment.h"
#include "vtkMRMLSegmentationDisplayNode.h"
#include "vtkClosedSurfaceToBinaryLabelmapConversionRule.h"
#include "vtkCalculateOversamplingFactor.h"
#include "vtkOrientedImageDataResample.h"
#include "vtkSlicerSegmentationsModuleLogic.h"

// Subject Hierarchy includes
#include "vtkMRMLSubjectHierarchyNode.h"
#include "vtkMRMLSubjectHierarchyConstants.h"

// MRML includes
#include <vtkMRMLChartNode.h>
#include <vtkMRMLChartViewNode.h>
#include <vtkMRMLDoubleArrayNode.h>
#include <vtkMRMLTableNode.h>
#include <vtkMRMLLayoutNode.h>
#include <vtkMRMLScalarVolumeNode.h>
#include <vtkMRMLScene.h>
#include <vtkEventBroker.h>

// VTK includes
#include <vtkImageAccumulate.h>
#include <vtkImageStencilData.h>
#include <vtkImageToImageStencil.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPiecewiseFunction.h>
#include <vtkDoubleArray.h>
#include <vtkStringArray.h>
#include <vtkBitArray.h>
#include <vtkImageConstantPad.h>
#include <vtkMath.h>
#include <vtkTable.h>
#include <vtkTimerLog.h>
#include <vtkCallbackCommand.h>
#include <vtkDelimitedTextWriter.h>

// VTKSYS includes
#include <vtksys/SystemTools.hxx>

// STD includes
#include <set>

//----------------------------------------------------------------------------
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_DVH_IDENTIFIER_ATTRIBUTE_NAME = vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX + "DVH"; // Identifier
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_CREATED_DVH_NODE_REFERENCE_ROLE = vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX + "createdDvhArray" + SlicerRtCommon::SLICERRT_REFERENCE_ROLE_ATTRIBUTE_NAME_POSTFIX; // Reference

const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_DOSE_VOLUME_OVERSAMPLING_FACTOR_ATTRIBUTE_NAME = vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX + "DoseVolumeOversamplingFactor";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_SEGMENT_ID_ATTRIBUTE_NAME = vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX + "SegmentID";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_STRUCTURE_PLOT_NAME_ATTRIBUTE_NAME = vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX + "StructurePlotName";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_TABLE_ROW_ATTRIBUTE_NAME = vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX + "TableRow";

const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_METRIC_STRUCTURE = "Structure";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_METRIC_TOTAL_VOLUME_CC = "Volume (cc)";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_METRIC_MEAN_PREFIX = "Mean ";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_METRIC_MIN_PREFIX = "Min ";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_METRIC_MAX_PREFIX = "Max ";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_METRIC_DOSE_POSTFIX = "dose";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_METRIC_INTENSITY_POSTFIX = "intensity";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_ARRAY_NODE_NAME_POSTFIX = "_DvhArray";

const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_CSV_HEADER_VOLUME_FIELD_MIDDLE = " Value (% of ";
const std::string vtkSlicerDoseVolumeHistogramModuleLogic::DVH_CSV_HEADER_VOLUME_FIELD_END = " cc)";

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerDoseVolumeHistogramModuleLogic);

//----------------------------------------------------------------------------
vtkSlicerDoseVolumeHistogramModuleLogic::vtkSlicerDoseVolumeHistogramModuleLogic()
{
  this->DoseVolumeHistogramNode = NULL;
  this->StartValue = 0.1;
  this->StepSize = 0.2;
  this->NumberOfSamplesForNonDoseVolumes = 100;
  this->DefaultDoseVolumeOversamplingFactor = 2.0;

  this->LogSpeedMeasurements = false;

  this->VisibilityChangedCallbackCommand = vtkCallbackCommand::New();
  this->VisibilityChangedCallbackCommand->SetClientData( reinterpret_cast<void *>(this) );
  this->VisibilityChangedCallbackCommand->SetCallback( vtkSlicerDoseVolumeHistogramModuleLogic::OnVisibilityChanged );
}

//----------------------------------------------------------------------------
vtkSlicerDoseVolumeHistogramModuleLogic::~vtkSlicerDoseVolumeHistogramModuleLogic()
{
  vtkSetAndObserveMRMLNodeMacro(this->DoseVolumeHistogramNode, NULL);

  if (this->VisibilityChangedCallbackCommand)
  {
    this->VisibilityChangedCallbackCommand->SetClientData(NULL);
    this->VisibilityChangedCallbackCommand->Delete();
    this->VisibilityChangedCallbackCommand = NULL;
  }
}

//----------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::SetAndObserveDoseVolumeHistogramNode(vtkMRMLDoseVolumeHistogramNode *node)
{
  vtkSetAndObserveMRMLNodeMacro(this->DoseVolumeHistogramNode, node);
}

//---------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::SetMRMLSceneInternal(vtkMRMLScene * newScene)
{
  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::EndImportEvent);
  events->InsertNextValue(vtkMRMLScene::EndCloseEvent);
  events->InsertNextValue(vtkMRMLScene::EndBatchProcessEvent);
  this->SetAndObserveMRMLSceneEvents(newScene, events.GetPointer());
}

//-----------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::RegisterNodes()
{
  vtkMRMLScene* scene = this->GetMRMLScene(); 
  if (!scene)
  {
    vtkErrorMacro("RegisterNodes: Invalid MRML scene!");
    return;
  }
  scene->RegisterNodeClass(vtkSmartPointer<vtkMRMLDoseVolumeHistogramNode>::New());
}

//---------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::OnMRMLSceneEndImport()
{
  // If we have a parameter node select it
  vtkMRMLDoseVolumeHistogramNode *paramNode = NULL;
  vtkMRMLNode *node = this->GetMRMLScene()->GetNthNodeByClass(0, "vtkMRMLDoseVolumeHistogramNode");
  if (node)
  {
    paramNode = vtkMRMLDoseVolumeHistogramNode::SafeDownCast(node);
    vtkSetAndObserveMRMLNodeMacro(this->DoseVolumeHistogramNode, paramNode);
  }
}

//---------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::OnMRMLSceneEndClose()
{
  if (!this->GetMRMLScene())
  {
    vtkErrorMacro("OnMRMLSceneEndClose: Invalid MRML scene!");
    return;
  }

  this->SetAndObserveDoseVolumeHistogramNode(NULL);
}

//---------------------------------------------------------------------------
std::string vtkSlicerDoseVolumeHistogramModuleLogic::ComputeDvh()
{
  if (!this->GetMRMLScene() || !this->DoseVolumeHistogramNode)
  {
    std::string errorMessage("Invalid MRML scene or parameter set node");
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }

  this->DoseVolumeHistogramNode->ClearAutomaticOversamplingFactors();
  vtkMRMLSegmentationNode* segmentationNode = this->DoseVolumeHistogramNode->GetSegmentationNode();
  vtkMRMLScalarVolumeNode* doseVolumeNode = this->DoseVolumeHistogramNode->GetDoseVolumeNode();
  if ( !segmentationNode || !doseVolumeNode )
  {
    std::string errorMessage("Both segmentation node and dose volume node need to be set");
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }

  // Fire only one modified event when the computation is done
  this->SetDisableModifiedEvent(1);
  int disabledNodeModify = this->DoseVolumeHistogramNode->StartModify();

  // Get maximum dose from dose volume for number of DVH bins
  vtkNew<vtkImageAccumulate> doseStat;
  doseStat->SetInputData(doseVolumeNode->GetImageData());
  doseStat->Update();
  double maxDose = doseStat->GetMax()[0];

  // Get selected segmentation
  vtkSegmentation* selectedSegmentation = segmentationNode->GetSegmentation();

  // If segment IDs list is empty then include all segments
  std::vector<std::string> segmentIDs;
  this->DoseVolumeHistogramNode->GetSelectedSegmentIDs(segmentIDs);
  if (segmentIDs.empty())
  {
    vtkSegmentation::SegmentMap segmentMap = selectedSegmentation->GetSegments();
    for (vtkSegmentation::SegmentMap::iterator segmentIt = segmentMap.begin(); segmentIt != segmentMap.end(); ++segmentIt)
    {
      segmentIDs.push_back(segmentIt->first);
    }
  }

  // Temporarily duplicate selected segments to contain binary labelmap of a different geometry (tied to dose volume)
  vtkSmartPointer<vtkSegmentation> segmentationCopy = vtkSmartPointer<vtkSegmentation>::New();
  segmentationCopy->SetMasterRepresentationName(selectedSegmentation->GetMasterRepresentationName());
  segmentationCopy->CopyConversionParameters(selectedSegmentation);
  for (std::vector<std::string>::iterator segmentIt = segmentIDs.begin(); segmentIt != segmentIDs.end(); ++segmentIt)
  {
    segmentationCopy->CopySegmentFromSegmentation(selectedSegmentation, (*segmentIt));
  }

  // Use dose volume geometry as reference, with oversampling of fixed 2 or automatic (as selected)
  vtkSmartPointer<vtkMatrix4x4> doseIjkToRasMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
  doseVolumeNode->GetIJKToRASMatrix(doseIjkToRasMatrix);
  std::string doseGeometryString = vtkSegmentationConverter::SerializeImageGeometry(doseIjkToRasMatrix, doseVolumeNode->GetImageData());
  segmentationCopy->SetConversionParameter( vtkSegmentationConverter::GetReferenceImageGeometryParameterName(),
    doseGeometryString );
  std::stringstream fixedOversamplingValuStream;
  fixedOversamplingValuStream << this->DefaultDoseVolumeOversamplingFactor;
  segmentationCopy->SetConversionParameter( vtkClosedSurfaceToBinaryLabelmapConversionRule::GetOversamplingFactorParameterName(),
    this->DoseVolumeHistogramNode->GetAutomaticOversampling() ? "A" : fixedOversamplingValuStream.str().c_str() );
  
  // Reconvert segments to specified geometry if possible
  bool resamplingRequired = false;
  if ( !segmentationCopy->CreateRepresentation(
    vtkSegmentationConverter::GetSegmentationBinaryLabelmapRepresentationName(), true) )
  {
    // If conversion failed and there is no binary labelmap in the segmentation, then cannot calculate DVH
    if (!segmentationCopy->ContainsRepresentation(vtkSegmentationConverter::GetSegmentationBinaryLabelmapRepresentationName()))
    {
      std::string errorMessage("Unable to acquire binary labelmap from segmentation");
      vtkErrorMacro("ComputeDvh: " << errorMessage);
      return errorMessage;
    }

    // If conversion failed, then resample binary labelmaps in the segments
    resamplingRequired = true;
  }

  // Calculate and store oversampling factors if automatically calculated for reporting purposes
  if (this->DoseVolumeHistogramNode->GetAutomaticOversampling())
  {
    // Get spacing for dose volume
    double doseSpacing[3] = {0.0,0.0,0.0};
    doseVolumeNode->GetSpacing(doseSpacing);

    // Calculate oversampling factors for all segments (need to calculate as it is not stored per segment)
    vtkSegmentation::SegmentMap segmentMap = segmentationCopy->GetSegments();
    for (vtkSegmentation::SegmentMap::iterator segmentIt = segmentMap.begin(); segmentIt != segmentMap.end(); ++segmentIt)
    {
      vtkSegment* currentSegment = segmentIt->second;
      vtkOrientedImageData* currentBinaryLabelmap = vtkOrientedImageData::SafeDownCast(
        currentSegment->GetRepresentation(vtkSegmentationConverter::GetSegmentationBinaryLabelmapRepresentationName()) );
      if (!currentBinaryLabelmap)
      {
        std::string errorMessage("Binary representation missing after converting with automatic oversampling factor!");
        vtkErrorMacro("ComputeDvh: " << errorMessage);
        return errorMessage;
      }
      double currentSpacing[3] = {0.0,0.0,0.0};
      currentBinaryLabelmap->GetSpacing(currentSpacing);

      double voxelSizeRatio = ((doseSpacing[0]*doseSpacing[1]*doseSpacing[2]) / (currentSpacing[0]*currentSpacing[1]*currentSpacing[2]));
      // Round oversampling to two decimals
      // Note: We need to round to some degree, because e.g. pow(64,1/3) is not exactly 4. It may be debated whether to round to integer or to a certain number of decimals
      double oversamplingFactor = vtkMath::Round( pow( voxelSizeRatio, 1.0/3.0 ) * 100.0 ) / 100.0;
      this->DoseVolumeHistogramNode->AddAutomaticOversamplingFactor(segmentIt->first, oversamplingFactor);
    }
  }

  // Create oriented image data from dose volume
  vtkSmartPointer<vtkOrientedImageData> doseImageData = vtkSmartPointer<vtkOrientedImageData>::Take(
    vtkSlicerSegmentationsModuleLogic::CreateOrientedImageDataFromVolumeNode(doseVolumeNode) );
  if (!doseImageData.GetPointer())
  {
    std::string errorMessage("Failed to get image data from dose volume");
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }
  // Apply parent transform on dose volume if necessary
  if (doseVolumeNode->GetParentTransformNode())
  {
    if (!vtkSlicerSegmentationsModuleLogic::ApplyParentTransformToOrientedImageData(doseVolumeNode, doseImageData))
    {
      std::string errorMessage("Failed to apply parent transformation to dose!");
      vtkErrorMacro("ComputeDvh: " << errorMessage);
      return errorMessage;
    }
  }

  // Use the same resampled dose volume if oversampling is fixed
  vtkSmartPointer<vtkOrientedImageData> fixedOversampledDoseVolume;
  if (!this->DoseVolumeHistogramNode->GetAutomaticOversampling())
  {
    // Get geometry of oversampled dose volume
    fixedOversampledDoseVolume = vtkSmartPointer<vtkOrientedImageData>::New();
    fixedOversampledDoseVolume->ShallowCopy(doseImageData);
    vtkCalculateOversamplingFactor::ApplyOversamplingOnImageGeometry(fixedOversampledDoseVolume, this->DefaultDoseVolumeOversamplingFactor);

    // Resample dose volume using linear interpolation
    if ( !vtkOrientedImageDataResample::ResampleOrientedImageToReferenceOrientedImage(
      doseImageData, fixedOversampledDoseVolume, fixedOversampledDoseVolume, true ) )
    {
      std::string errorMessage("Failed to resample dose volume");
      vtkErrorMacro("ComputeDvh: " << errorMessage);
      return errorMessage;
    }
  }

  // Compute DVH for each selected segment
  vtkSegmentation::SegmentMap segmentMap = segmentationCopy->GetSegments();
  int counter = 1; // Start at one so that progress can reach 100%
  int numberOfSelectedSegments = segmentationCopy->GetNumberOfSegments();
  for (vtkSegmentation::SegmentMap::iterator segmentIt = segmentMap.begin(); segmentIt != segmentMap.end(); ++segmentIt, ++counter)
  {
    // Get segment binary labelmap
    vtkOrientedImageData* segmentBinaryLabelmap = vtkOrientedImageData::SafeDownCast( segmentIt->second->GetRepresentation(
      vtkSegmentationConverter::GetSegmentationBinaryLabelmapRepresentationName() ) );
    if (!segmentBinaryLabelmap)
    {
      std::string errorMessage("Failed to get binary labelmap for segments");
      vtkErrorMacro("ComputeDvh: " << errorMessage);
      return errorMessage;
    }

    // Apply parent transformation nodes if necessary
    if (segmentationNode->GetParentTransformNode())
    {
      if (!vtkSlicerSegmentationsModuleLogic::ApplyParentTransformToOrientedImageData(segmentationNode, segmentBinaryLabelmap))
      {
        std::string errorMessage("Failed to apply parent transformation to segment!");
        vtkErrorMacro("ComputeDvh: " << errorMessage);
        return errorMessage;
      }
      resamplingRequired = true;
    }
    // Resample binary labelmap if necessary (if it was master, and could not be re-converted using the oversampled geometry, or if there was a parent transform)
    if (resamplingRequired)
    {
      // Resample dose volume using linear interpolation
      if ( !vtkOrientedImageDataResample::ResampleOrientedImageToReferenceOrientedImage(
        segmentBinaryLabelmap, fixedOversampledDoseVolume, segmentBinaryLabelmap ) )
      {
        std::string errorMessage("Failed to resample segment binary labelmap");
        vtkErrorMacro("ComputeDvh: " << errorMessage);
        return errorMessage;
      }
    }

    // Get oversampled dose volume
    vtkSmartPointer<vtkOrientedImageData> oversampledDoseVolume;
    // Use the same resampled dose volume if oversampling is fixed
    if (!this->DoseVolumeHistogramNode->GetAutomaticOversampling())
    {
      oversampledDoseVolume = fixedOversampledDoseVolume;
    }
    // Resample dose volume to match automatically oversampled segment labelmap geometry
    else
    {
      oversampledDoseVolume = vtkSmartPointer<vtkOrientedImageData>::New();
      if ( !vtkOrientedImageDataResample::ResampleOrientedImageToReferenceOrientedImage(
        doseImageData, segmentBinaryLabelmap, oversampledDoseVolume, true ) )
      {
        std::string errorMessage("Failed to resample dose volume");
        vtkErrorMacro("ComputeDvh: " << errorMessage);
        return errorMessage;
      }
    }

    // Make sure the segment labelmap is the same dimension as the dose volume
    vtkSmartPointer<vtkImageConstantPad> padder = vtkSmartPointer<vtkImageConstantPad>::New();
    padder->SetInputData(segmentBinaryLabelmap);
    int extent[6] = {0,-1,0,-1,0,-1};
    oversampledDoseVolume->GetExtent(extent);
    padder->SetOutputWholeExtent(extent);
    padder->Update();
    segmentBinaryLabelmap->vtkImageData::DeepCopy(padder->GetOutput());

    // Calculate DVH for current segment
    std::string errorMessage = this->ComputeDvh(segmentBinaryLabelmap, oversampledDoseVolume, segmentIt->first, maxDose);
    if (!errorMessage.empty())
    {
      vtkErrorMacro("ComputeDvh: " << errorMessage);
      return errorMessage;
    }

    // Update progress bar
    double progress = (double)counter / (double)numberOfSelectedSegments;
    this->InvokeEvent(SlicerRtCommon::ProgressUpdated, (void*)&progress);
  } // For each segment

  // Fire only one modified event when the computation is done
  this->SetDisableModifiedEvent(0);
  this->Modified();
  this->DoseVolumeHistogramNode->EndModify(disabledNodeModify);

  return "";
}

//---------------------------------------------------------------------------
std::string vtkSlicerDoseVolumeHistogramModuleLogic::ComputeDvh(vtkOrientedImageData* segmentLabelmap, vtkOrientedImageData* oversampledDoseVolume, std::string segmentID, double maxDoseGy)
{
  if (!this->GetMRMLScene() || !this->DoseVolumeHistogramNode)
  {
    std::string errorMessage("Invalid MRML scene or parameter set node");
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }
  if (!segmentLabelmap)
  {
    std::string errorMessage("Invalid segment labelmap");
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }
  if (!oversampledDoseVolume)
  {
    std::string errorMessage("Invalid oversampled dose volume");
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }
  vtkMRMLSegmentationNode* segmentationNode = this->DoseVolumeHistogramNode->GetSegmentationNode();
  vtkMRMLScalarVolumeNode* doseVolumeNode = this->DoseVolumeHistogramNode->GetDoseVolumeNode();
  if ( !segmentationNode || !doseVolumeNode )
  {
    std::string errorMessage("Both segmentation node and dose volume node need to be set");
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }
  std::string segmentName = this->DoseVolumeHistogramNode->GetSegmentationNode()->GetSegmentation()->GetSegment(segmentID)->GetName();

  vtkSmartPointer<vtkTimerLog> timer = vtkSmartPointer<vtkTimerLog>::New();
  double checkpointStart = timer->GetUniversalTime();
  UNUSED_VARIABLE(checkpointStart); // Although it is used later, a warning is logged so needs to be suppressed

  // Create stencil for structure
  vtkNew<vtkImageToImageStencil> stencil;
  stencil->SetInputData(segmentLabelmap);
  stencil->ThresholdByUpper(0.5); // Thresholds only the labelmap, so the point is to keep the ones bigger than 0
  stencil->Update();

  vtkSmartPointer<vtkImageStencilData> structureStencil = vtkSmartPointer<vtkImageStencilData>::New();
  structureStencil->DeepCopy(stencil->GetOutput());

  int stencilExtent[6] = {0,-1,0,-1,0,-1};
  structureStencil->GetExtent(stencilExtent);
  if (stencilExtent[1]-stencilExtent[0] <= 0 || stencilExtent[3]-stencilExtent[2] <= 0 || stencilExtent[5]-stencilExtent[4] <= 0)
  {
    std::string errorMessage("Invalid stenciled dose volume");
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }

  // Compute statistics
  vtkNew<vtkImageAccumulate> structureStat;
  structureStat->SetInputData(oversampledDoseVolume);
  structureStat->SetStencilData(structureStencil);
  structureStat->Update();

  // Report error if there are no voxels in the stenciled dose volume (no non-zero voxels in the resampled labelmap)
  if (structureStat->GetVoxelCount() < 1)
  {
    std::string errorMessage("Dose volume and the structure do not overlap"); // User-friendly error to help troubleshooting
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }

  // Get metrics table for the parameter node; Create one if missing
  vtkMRMLTableNode* metricsTableNode = this->DoseVolumeHistogramNode->GetMetricsTableNode();
  vtkTable* metricsTable = metricsTableNode->GetTable();
  // Setup table if empty
  if (metricsTable->GetNumberOfColumns() == 0)
  {
    this->InitializeMetricsTable();
  }

  // Get DVH array node for the inputs (dose volume, segmentation, segment).
  // If found, then it gets overwritten by the new computation, otherwise
  std::string structureDvhNodeRef = this->DoseVolumeHistogramNode->AssembleDvhNodeReference(segmentID);
  vtkMRMLDoubleArrayNode* arrayNode = vtkMRMLDoubleArrayNode::SafeDownCast(
    metricsTableNode->GetNodeReference(structureDvhNodeRef.c_str()) );
  int tableRow = -1;
  if (!arrayNode)
  {
    arrayNode = vtkMRMLDoubleArrayNode::New();
    std::string dvhArrayNodeName = segmentID + DVH_ARRAY_NODE_NAME_POSTFIX;
    dvhArrayNodeName = this->GetMRMLScene()->GenerateUniqueName(dvhArrayNodeName);
    arrayNode->SetName(dvhArrayNodeName.c_str());
    arrayNode->SetAttribute(DVH_DVH_IDENTIFIER_ATTRIBUTE_NAME.c_str(), "1");
    this->GetMRMLScene()->AddNode(arrayNode);
    tableRow = metricsTable->GetNumberOfRows();
    std::stringstream ss;
    ss << tableRow;
    arrayNode->SetAttribute(DVH_TABLE_ROW_ATTRIBUTE_NAME.c_str(), ss.str().c_str());
    arrayNode->Delete(); // Release ownership to scene only
    metricsTable->InsertNextBlankRow();

    // Set node references
    metricsTableNode->SetNodeReferenceID(structureDvhNodeRef.c_str(), arrayNode->GetID());
    arrayNode->SetNodeReferenceID(vtkMRMLDoseVolumeHistogramNode::DOSE_VOLUME_REFERENCE_ROLE, doseVolumeNode->GetID());
    arrayNode->SetNodeReferenceID(vtkMRMLDoseVolumeHistogramNode::SEGMENTATION_REFERENCE_ROLE, segmentationNode->GetID());
    arrayNode->SetNodeReferenceID(vtkMRMLDoseVolumeHistogramNode::DVH_METRICS_TABLE_REFERENCE_ROLE, metricsTableNode->GetID());
  }
  else if (arrayNode->GetAttribute(DVH_TABLE_ROW_ATTRIBUTE_NAME.c_str()))
  {
    std::stringstream ss;
    ss << arrayNode->GetAttribute(DVH_TABLE_ROW_ATTRIBUTE_NAME.c_str());
    ss >> tableRow;
  }
  else
  {
    std::string errorMessage("Failed to find metrics table row for structure " + segmentName);
    vtkErrorMacro("ComputeDvh: " << errorMessage);
    return errorMessage;
  }

  // Set array node attributes:
  // Structure name and segment color for visualization in the chart view
  arrayNode->SetAttribute(DVH_SEGMENT_ID_ATTRIBUTE_NAME.c_str(), segmentID.c_str());
  // Oversampling factor
  std::ostringstream oversamplingAttrValueStream;
  oversamplingAttrValueStream << (this->DoseVolumeHistogramNode->GetAutomaticOversampling() ? (-1.0) : this->DefaultDoseVolumeOversamplingFactor);
  arrayNode->SetAttribute(DVH_DOSE_VOLUME_OVERSAMPLING_FACTOR_ATTRIBUTE_NAME.c_str(), oversamplingAttrValueStream.str().c_str());

  // Get spacing and voxel volume
  double* segmentLabelmapSpacing = segmentLabelmap->GetSpacing();
  double cubicMMPerVoxel = segmentLabelmapSpacing[0] * segmentLabelmapSpacing[1] * segmentLabelmapSpacing[2];
  double ccPerCubicMM = 0.001;

  // Set default column values

  // Structure name
  metricsTable->SetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnStructure, vtkVariant(segmentName));
  // Volume name
  metricsTable->SetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnDoseVolume, vtkVariant(doseVolumeNode->GetName()));
  // Volume (cc) - save as attribute too (the DVH contains percentages that often need to be converted to volume)
  double volumeCc = structureStat->GetVoxelCount() * cubicMMPerVoxel * ccPerCubicMM;
  metricsTable->SetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnVolumeCc, vtkVariant(volumeCc));
  std::ostringstream attributeNameStream;
  std::ostringstream attributeValueStream;
  attributeNameStream << vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX << vtkSlicerDoseVolumeHistogramModuleLogic::DVH_METRIC_TOTAL_VOLUME_CC;
  attributeValueStream << volumeCc;
  arrayNode->SetAttribute(attributeNameStream.str().c_str(), attributeValueStream.str().c_str());
  // Mean dose
  metricsTable->SetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnMeanDose, vtkVariant(structureStat->GetMean()[0]));
  // Min dose
  metricsTable->SetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnMinDose, vtkVariant(structureStat->GetMin()[0]));
  // Max dose
  metricsTable->SetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnMaxDose, vtkVariant(structureStat->GetMax()[0]));

  // Create DVH plot values
  int numSamples = 0;
  double startValue;
  double stepSize;
  double rangeMin = structureStat->GetMin()[0];
  double rangeMax = structureStat->GetMax()[0];
  bool isDoseVolume = this->DoseVolumeContainsDose();
  if (isDoseVolume)
  {
    if (rangeMin<0)
    {
      std::string errorMessage("The dose volume contains negative dose values");
      vtkErrorMacro("ComputeDvh: " << errorMessage);
      return errorMessage;
    }

    startValue = this->StartValue;
    stepSize = this->StepSize;
    numSamples = (int)ceil( (maxDoseGy-startValue)/stepSize ) + 1;
  }
  else
  {
    startValue = rangeMin;
    numSamples = this->NumberOfSamplesForNonDoseVolumes;
    stepSize = (rangeMax - rangeMin) / (double)(numSamples-1);
  }

  // Get the number of voxels with smaller dose than at the start value
  structureStat->SetComponentExtent(0,1,0,0,0,0);
  structureStat->SetComponentOrigin(0,0,0);
  structureStat->SetComponentSpacing(startValue,1,1);
  structureStat->Update();
  unsigned long voxelBelowDose = structureStat->GetOutput()->GetScalarComponentAsDouble(0,0,0,0);

  // We put a fixed point at (0.0, 100%), but only if there are only positive values in the histogram
  // Negative values can occur when the user requests histogram for an image, such as s CT volume (in this case Intensity Volume Histogram is computed),
  // or the startValue became negative for the dose volume because the range minimum was smaller than the original start value.
  bool insertPointAtOrigin=true;
  if (startValue<0)
  {
    insertPointAtOrigin=false;
  }

  structureStat->SetComponentExtent(0,numSamples-1,0,0,0,0);
  structureStat->SetComponentOrigin(startValue,0,0);
  structureStat->SetComponentSpacing(stepSize,1,1);
  structureStat->Update();

  vtkDoubleArray* doubleArray = arrayNode->GetArray();
  doubleArray->SetNumberOfTuples(numSamples + (insertPointAtOrigin?1:0));

  int outputArrayIndex=0;

  if (insertPointAtOrigin)
  {
    // Add first fixed point at (0.0, 100%)
    doubleArray->SetComponent(outputArrayIndex, 0, 0.0);
    doubleArray->SetComponent(outputArrayIndex, 1, 100.0);
    doubleArray->SetComponent(outputArrayIndex, 2, 0);
    ++outputArrayIndex;
  }

  vtkImageData* statArray = structureStat->GetOutput();
  unsigned long totalVoxels = structureStat->GetVoxelCount();
  for (int sampleIndex=0; sampleIndex<numSamples; ++sampleIndex)
  {
    unsigned long voxelsInBin = statArray->GetScalarComponentAsDouble(sampleIndex,0,0,0);
    doubleArray->SetComponent( outputArrayIndex, 0, startValue + sampleIndex * stepSize );
    doubleArray->SetComponent( outputArrayIndex, 1, (1.0-(double)voxelBelowDose/(double)totalVoxels)*100.0 );
    doubleArray->SetComponent( outputArrayIndex, 2, 0 );
    ++outputArrayIndex;
    voxelBelowDose += voxelsInBin;
  }

  // Set the start of the first bin to 0 if the volume contains dose and the start value was negative
  if (isDoseVolume && !insertPointAtOrigin)
  {
    doubleArray->SetComponent(0,0,0);
  }

  // Add DVH to subject hierarchy
  vtkMRMLSubjectHierarchyNode::CreateSubjectHierarchyNode(
    this->GetMRMLScene(), vtkMRMLSubjectHierarchyNode::GetAssociatedSubjectHierarchyNode(doseVolumeNode),
    vtkMRMLSubjectHierarchyConstants::GetDICOMLevelSubseries(), arrayNode->GetName(), arrayNode);

  // Add connection attribute to input segmentation node
  vtkMRMLSubjectHierarchyNode* segmentSubjectHierarchyNode = segmentationNode->GetSegmentSubjectHierarchyNode(segmentID);
  if (segmentSubjectHierarchyNode)
  {
    segmentSubjectHierarchyNode->AddNodeReferenceID(DVH_CREATED_DVH_NODE_REFERENCE_ROLE.c_str(), arrayNode->GetID());
  }

  // Log measured time
  double checkpointEnd = timer->GetUniversalTime();
  UNUSED_VARIABLE(checkpointEnd); // Although it is used just below, a warning is logged so needs to be suppressed
  if (this->LogSpeedMeasurements)
  {
    vtkDebugMacro("ComputeDvh: DVH computation time for structure '" << segmentID << "': " << checkpointEnd-checkpointStart << " s");
  }

  return "";
}

//---------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::AddDvhToChart(const char* dvhArrayNodeId)
{
  if (!this->GetMRMLScene()|| !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("AddDvhToChart: Invalid MRML scene or parameter set node!");
    return;
  }

  // Get chart node
  vtkMRMLChartNode* chartNode = this->DoseVolumeHistogramNode->GetChartNode();
  if (!chartNode)
  {
    vtkErrorMacro("AddDvhToChart: No chart node!");
    return;
  }
  // Get DVH array node
  vtkMRMLDoubleArrayNode* dvhArrayNode = vtkMRMLDoubleArrayNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(dvhArrayNodeId));
  if (!dvhArrayNode)
  {
    vtkErrorMacro("AddDvhToChart: Unable to get double array node!");
    return;
  }
  const char* segmentId = dvhArrayNode->GetAttribute(DVH_SEGMENT_ID_ATTRIBUTE_NAME.c_str());

  // Get selected chart and dose volume nodes
  vtkMRMLScalarVolumeNode* doseVolumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(dvhArrayNode->GetNodeReference(vtkMRMLDoseVolumeHistogramNode::DOSE_VOLUME_REFERENCE_ROLE));
  vtkMRMLSegmentationNode* segmentationNode = vtkMRMLSegmentationNode::SafeDownCast(dvhArrayNode->GetNodeReference(vtkMRMLDoseVolumeHistogramNode::SEGMENTATION_REFERENCE_ROLE));
  if (!doseVolumeNode || !segmentationNode)
  {
    vtkErrorMacro("AddDvhToChart: Unable to find all referenced nodes!");
    return;
  }
  vtkSegment* segment = segmentationNode->GetSegmentation()->GetSegment(segmentId);
  if (!segment)
  {
    vtkErrorMacro("AddDvhToChart: Unable to get segment!");
    return;
  }
  std::string segmentName(segment->GetName());
  // Get chart view node
  vtkMRMLChartViewNode* chartViewNode = this->GetChartViewNode();
  if (chartViewNode == NULL)
  {
    vtkErrorMacro("AddDvhToChart: Unable to get chart view node!");
    return;
  }

  // Get segment color from display node
  double segmentColor[3] = {0.0,0.0,0.0};
  vtkMRMLSegmentationDisplayNode* displayNode = vtkMRMLSegmentationDisplayNode::SafeDownCast(segmentationNode->GetDisplayNode());
  vtkMRMLSegmentationDisplayNode::SegmentDisplayProperties properties;
  if (displayNode && displayNode->GetSegmentDisplayProperties(segmentId, properties))
  {
    segmentColor[0] = properties.Color[0];
    segmentColor[1] = properties.Color[1];
    segmentColor[2] = properties.Color[2];
  }
  else
  {
    // If no display node is found, use the default color from the segment
    segment->GetDefaultColor(segmentColor);
  }

  // Set chart general properties
  std::string doseAxisName;
  std::string chartTitle;
  const char* doseIdentifier = doseVolumeNode->GetAttribute(SlicerRtCommon::DICOMRTIMPORT_DOSE_VOLUME_IDENTIFIER_ATTRIBUTE_NAME.c_str());
  if (doseIdentifier)
  {
    vtkMRMLSubjectHierarchyNode* doseSubjectHierarchyNode = vtkMRMLSubjectHierarchyNode::GetAssociatedSubjectHierarchyNode(doseVolumeNode);
    if (doseSubjectHierarchyNode)
    {
      const char* doseUnitName = doseSubjectHierarchyNode->GetAttributeFromAncestor(
        SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_NAME_ATTRIBUTE_NAME.c_str(), vtkMRMLSubjectHierarchyConstants::GetDICOMLevelStudy());
      doseAxisName=std::string("Dose [")+doseUnitName+"]";
    }
    else
    {
      vtkErrorMacro("AddDvhToChart: Invalid subject hierarchy node for dose volume!");
      doseAxisName=std::string("Dose");
    }
    chartTitle="Dose Volume Histogram";
  }
  else
  {
    doseAxisName="Intensity";
    chartTitle="Intensity Volume Histogram";
  }

  chartNode->SetProperty("default", "title", chartTitle.c_str());
  chartNode->SetProperty("default", "xAxisLabel", doseAxisName.c_str());
  chartNode->SetProperty("default", "yAxisLabel", "Fractional volume [%]");
  chartNode->SetProperty("default", "type", "Line");
  chartNode->SetProperty("default", "xAxisPad", "0");
  chartNode->SetProperty("default", "yAxisPad", "0");

  // Get number of arrays showing plot for the same structure (for plot name and line style)
  vtkStringArray* arrayIds = chartNode->GetArrays();
  int numberOfStructuresWithSameName = 0;
  for (int arrayIndex = 0; arrayIndex < arrayIds->GetNumberOfValues(); ++arrayIndex)
  {
    vtkMRMLDoubleArrayNode* currentArrayNode = vtkMRMLDoubleArrayNode::SafeDownCast(
      this->GetMRMLScene()->GetNodeByID(arrayIds->GetValue(arrayIndex).c_str()) );

    std::string currentSegmentName("");
    const char* currentSegmentId = currentArrayNode->GetAttribute(DVH_SEGMENT_ID_ATTRIBUTE_NAME.c_str());
    vtkMRMLSegmentationNode* currentSegmentationNode = vtkMRMLSegmentationNode::SafeDownCast(currentArrayNode->GetNodeReference(vtkMRMLDoseVolumeHistogramNode::SEGMENTATION_REFERENCE_ROLE));
    if (currentSegmentationNode && currentSegmentId)
    {
      vtkSegment* currentSegment = currentSegmentationNode->GetSegmentation()->GetSegment(currentSegmentId);
      if (currentSegment)
      {
        currentSegmentName = std::string(currentSegment->GetName());
      }
    }
    if (currentSegmentName.empty())
    {
      vtkErrorMacro("AddDvhToChart: Failed to get segment name for DVH array " << currentArrayNode->GetName());
      continue;
    }
    if (!currentSegmentName.compare(segmentName))
    {
      ++numberOfStructuresWithSameName;
    }
  }

  // Assemble plot name and determine style
  std::stringstream structurePlotNameStream;
  std::string lineStyle("");
  structurePlotNameStream << segmentName << " (" << arrayIds->GetNumberOfValues() + 1 << ")";
  if (numberOfStructuresWithSameName % 4 == 1)
  {
    lineStyle = "dashed";
    structurePlotNameStream << " [- -]";
  }
  else if (numberOfStructuresWithSameName % 4 == 2)
  {
    lineStyle = "dotted";
    structurePlotNameStream << " [...]";
  }
  else if (numberOfStructuresWithSameName % 4 == 3)
  {
    lineStyle = "dashed-dotted";
    structurePlotNameStream << " [-.-]";
  }
  else
  {
    lineStyle = "solid";
  }

  std::string structurePlotName = structurePlotNameStream.str();
  dvhArrayNode->SetAttribute(DVH_STRUCTURE_PLOT_NAME_ATTRIBUTE_NAME.c_str(), structurePlotName.c_str());

  // Add chart to chart view
  chartViewNode->SetChartNodeID(chartNode->GetID());

  // Add array to chart
  chartNode->AddArray(structurePlotName.c_str(), dvhArrayNodeId);

  // Set plot color and line style
  std::ostringstream colorAttrValueStream;
  colorAttrValueStream.setf(ios::hex, ios::basefield);
  colorAttrValueStream << "#" << std::setw(2) << std::setfill('0') << (int)(segmentColor[0]*255.0+0.5)
    << std::setw(2) << std::setfill('0') << (int)(segmentColor[1]*255.0+0.5)
    << std::setw(2) << std::setfill('0') << (int)(segmentColor[2]*255.0+0.5);
  chartNode->SetProperty(structurePlotName.c_str(), "color", colorAttrValueStream.str().c_str());
  chartNode->SetProperty(structurePlotName.c_str(), "linePattern", lineStyle.c_str());
}

//---------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::RemoveDvhFromChart(const char* dvhArrayNodeId)
{
  vtkMRMLScene* scene = this->GetMRMLScene();
  if (!scene || !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("RemoveDvhFromChart: Invalid MRML scene or parameter set node!");
    return;
  }

  vtkMRMLChartNode* chartNode = this->DoseVolumeHistogramNode->GetChartNode();
  if (!chartNode)
  {
    vtkErrorMacro("RemoveDvhFromChart: Invalid chart node!");
    return;
  }

  vtkMRMLChartViewNode* chartViewNode = GetChartViewNode();
  if (chartViewNode == NULL)
  {
    vtkErrorMacro("RemoveDvhFromChart: Unable to get chart view node!");
    return;
  }

  vtkStringArray* arrayIds = chartNode->GetArrays();
  for (int arrayIndex = 0; arrayIndex < arrayIds->GetNumberOfValues(); ++arrayIndex)
  {
    if (!STRCASECMP(arrayIds->GetValue(arrayIndex).c_str(), dvhArrayNodeId))
    {
      chartNode->RemoveArray(chartNode->GetArrayNames()->GetValue(arrayIndex).c_str());
      return;
    }
  }
}

//---------------------------------------------------------------------------
bool vtkSlicerDoseVolumeHistogramModuleLogic::IsDvhAddedToChart(const char* dvhArrayNodeId)
{
  vtkMRMLScene* scene = this->GetMRMLScene();
  if (!scene || !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("IsDvhAddedToChart: Invalid MRML scene or parameter set node!");
    return false;
  }

  vtkMRMLChartNode* chartNode = this->DoseVolumeHistogramNode->GetChartNode();
  if (!chartNode)
  {
    vtkErrorMacro("IsDvhAddedToChart: Invalid chart node!");
    return false;
  }

  vtkStringArray* arrayIds = chartNode->GetArrays();
  for (int arrayIndex = 0; arrayIndex < arrayIds->GetNumberOfValues(); ++arrayIndex)
  {
    if (!STRCASECMP(arrayIds->GetValue(arrayIndex).c_str(), dvhArrayNodeId))
    {
      return true;
    }
  }

  return false;
}

//---------------------------------------------------------------------------
vtkMRMLChartViewNode* vtkSlicerDoseVolumeHistogramModuleLogic::GetChartViewNode()
{
  vtkSmartPointer<vtkCollection> layoutNodes =
    vtkSmartPointer<vtkCollection>::Take( this->GetMRMLScene()->GetNodesByClass("vtkMRMLLayoutNode") );
  layoutNodes->InitTraversal();
  vtkObject* layoutNodeVtkObject = layoutNodes->GetNextItemAsObject();
  vtkMRMLLayoutNode* layoutNode = vtkMRMLLayoutNode::SafeDownCast(layoutNodeVtkObject);
  if (!layoutNode)
  {
    vtkErrorMacro("GetChartViewNode: Unable to get layout node!");
    return NULL;
  }
  layoutNode->SetViewArrangement( vtkMRMLLayoutNode::SlicerLayoutConventionalQuantitativeView );
  
  vtkSmartPointer<vtkCollection> chartViewNodes =
    vtkSmartPointer<vtkCollection>::Take( this->GetMRMLScene()->GetNodesByClass("vtkMRMLChartViewNode") );
  chartViewNodes->InitTraversal();
  vtkMRMLChartViewNode* chartViewNode = vtkMRMLChartViewNode::SafeDownCast( chartViewNodes->GetNextItemAsObject() );
  if (!chartViewNode)
  {
    vtkErrorMacro("GetChartViewNode: Unable to get chart view node!");
    return NULL;
  }

  return chartViewNode;
}

//---------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::GetNumbersFromMetricString(std::string metricStr, std::vector<double> &metricNumbers)
{
  metricNumbers.clear();
  
  size_t commaPosition = metricStr.find(",");
  while (commaPosition != std::string::npos)
  {
    std::stringstream ss;
    ss << metricStr.substr(0, commaPosition);
    double num = -1;
    ss >> num;
    if (ss.fail())
    {
      vtkWarningMacro("GetNumbersFromMetricString: Invalid metric value in string: '" << metricStr.substr(0, commaPosition) << "'");
    }
    else
    {
      metricNumbers.push_back(num);
    }
    metricStr = metricStr.substr(commaPosition+1);
    commaPosition = metricStr.find(",");
  }
  if (!metricStr.empty())
  {
    std::stringstream ss;
    ss << metricStr;
    double num = -1;
    ss >> num;
    if (ss.fail())
    {
      vtkWarningMacro("GetNumbersFromMetricString: Invalid metric value in string: '" << metricStr << "'");
    }
    else
    {
      metricNumbers.push_back(num);
    }
  }
}

//---------------------------------------------------------------------------
bool vtkSlicerDoseVolumeHistogramModuleLogic::IsVMetricName(std::string name)
{
  if (name.empty())
  {
    return false;
  }
  // First character needs to be a 'V'
  if (name.substr(0,1).compare("V"))
  {
    return false;
  }
  // If second character is a number, then we consider it a V metric
  std::stringstream secondCharStream;
  secondCharStream << name.substr(1,1);
  int secondCharNumber = -1;
  secondCharStream >> secondCharNumber;
  return !secondCharStream.fail();
}

//---------------------------------------------------------------------------
bool vtkSlicerDoseVolumeHistogramModuleLogic::ComputeVMetrics()
{
  if (!this->GetMRMLScene() || !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("ComputeVMetrics: Invalid MRML scene or parameter set node!");
    return false;
  }
  vtkMRMLTableNode* metricsTableNode = this->DoseVolumeHistogramNode->GetMetricsTableNode();
  if (!metricsTableNode)
  {
    vtkErrorMacro("ComputeVMetrics: Unable to access DVH metrics table!");
    return false;
  }

  // Remove all V metrics from the table
  vtkTable* metricsTable = metricsTableNode->GetTable();
  int numberOfColumnsBeforeRemoval = -1;
  do 
  {
    numberOfColumnsBeforeRemoval = metricsTable->GetNumberOfColumns();
    for (int col=0; col<metricsTable->GetNumberOfColumns(); ++col)
    {
      std::string columnName(metricsTable->GetColumnName(col));
      if (this->IsVMetricName(columnName))
      {
        metricsTable->RemoveColumn(col);
        break;
      }
    }
  }
  while (numberOfColumnsBeforeRemoval != metricsTable->GetNumberOfColumns());

  // If no V metrics need to be shown then exit
  if (!this->DoseVolumeHistogramNode->GetShowVMetricsCc() && !this->DoseVolumeHistogramNode->GetShowVMetricsPercent())
  {
    return true;
  }

  // Get V metric dose values from input string
  std::string doseValuesStr(this->DoseVolumeHistogramNode->GetVDoseValues());
  std::vector<double> doseValues;
  this->GetNumbersFromMetricString(doseValuesStr, doseValues);

  // Create table columns for requested V metrics
  int numberOfColumnsBefore = metricsTable->GetNumberOfColumns();
  for (std::vector<double>::iterator doseValueIt=doseValues.begin(); doseValueIt!=doseValues.end(); ++doseValueIt)
  {
    if (this->DoseVolumeHistogramNode->GetShowVMetricsCc())
    {
      std::stringstream newColumnName;
      newColumnName << "V" << (*doseValueIt) << " (cc)";
      vtkAbstractArray* newColumn = metricsTableNode->AddColumn();
      newColumn->SetName(newColumnName.str().c_str());
      metricsTable->AddColumn(newColumn);
    }
    if (this->DoseVolumeHistogramNode->GetShowVMetricsPercent())
    {
      std::stringstream newColumnName;
      newColumnName << "V" << (*doseValueIt) << " (%)";
      vtkAbstractArray* newColumn = metricsTableNode->AddColumn();
      newColumn->SetName(newColumnName.str().c_str());
      metricsTable->AddColumn(newColumn);
    }
  }

  // Traverse all DVH nodes referenced from metrics table and calculate V metrics
  std::vector<std::string> roles;
  metricsTableNode->GetNodeReferenceRoles(roles);
  for (std::vector<std::string>::iterator roleIt=roles.begin(); roleIt!=roles.end(); ++roleIt)
  {
    if ( roleIt->substr(0, vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX.size()).compare(
      vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX ) )
    {
      // Not a DVH reference
      continue;
    }

    // Get DVH node
    vtkMRMLDoubleArrayNode* dvhArrayNode = vtkMRMLDoubleArrayNode::SafeDownCast(
      metricsTableNode->GetNodeReference(roleIt->c_str()) );
    if (!dvhArrayNode)
    {
      vtkErrorMacro("ComputeVMetrics: Metrics table node reference '" << (*roleIt) << "' does not contain DVH node!");
      continue;
    }

    // Get corresponding table row
    int tableRow = -1;
    std::stringstream ss;
    ss << dvhArrayNode->GetAttribute(DVH_TABLE_ROW_ATTRIBUTE_NAME.c_str());
    ss >> tableRow;
    if (ss.fail())
    {
      vtkErrorMacro("ComputeVMetrics: Failed to get metrics table row from DVH node " << dvhArrayNode->GetName());
      continue;
    }

    // Get structure volume
    double structureVolume = metricsTable->GetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnVolumeCc).ToDouble();
    if (structureVolume == 0)
    {
      vtkErrorMacro("ComputeVMetrics: Failed to get structure volume for structure " << metricsTable->GetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnStructure).ToString());
      continue;
    }

    // Compute volume for all V's
    vtkDoubleArray* doubleArray = dvhArrayNode->GetArray();
    vtkNew<vtkPiecewiseFunction> interpolator;
    interpolator->ClampingOn();

    // Starting from second point, because BuildFunctionFromTable needs uniform distance between X coordinates
    //  and the first point may be 0 (with different distance to neighbors than the second)
    double* dvhArrayDouble = new double[doubleArray->GetNumberOfTuples()-1];
    for (int i=1; i<doubleArray->GetNumberOfTuples(); ++i)
    {
      dvhArrayDouble[i-1] = doubleArray->GetComponent(i, 1);
    }
    interpolator->BuildFunctionFromTable(doubleArray->GetComponent(1, 0), doubleArray->GetComponent(doubleArray->GetNumberOfTuples()-1, 0), doubleArray->GetNumberOfTuples()-1, dvhArrayDouble);

    // Add first point
    interpolator->AddPoint(doubleArray->GetComponent(0, 0), doubleArray->GetComponent(0, 1));

    // Calculate metrics and set table entries
    int tableColumn = numberOfColumnsBefore;
    for (std::vector<double>::iterator it = doseValues.begin(); it != doseValues.end(); ++it)
    {
      double volumePercentEstimated = interpolator->GetValue(*it);
      if (this->DoseVolumeHistogramNode->GetShowVMetricsCc())
      {
        metricsTable->SetValue( tableRow, tableColumn++, vtkVariant(volumePercentEstimated*structureVolume/100.0) );
      }
      if (this->DoseVolumeHistogramNode->GetShowVMetricsPercent())
      {
        metricsTable->SetValue( tableRow, tableColumn++, vtkVariant(volumePercentEstimated) );
      }
    }
  } // For all DVHs

  metricsTableNode->Modified();
  return true;
}

//---------------------------------------------------------------------------
bool vtkSlicerDoseVolumeHistogramModuleLogic::IsDMetricName(std::string name)
{
  if (name.empty())
  {
    return false;
  }
  // First character needs to be a 'D'
  if (name.substr(0,1).compare("D"))
  {
    return false;
  }
  // If second character is a number, then we consider it a D metric
  std::stringstream secondCharStream;
  secondCharStream << name.substr(1,1);
  int secondCharNumber = -1;
  secondCharStream >> secondCharNumber;
  return !secondCharStream.fail();
}

//---------------------------------------------------------------------------
bool vtkSlicerDoseVolumeHistogramModuleLogic::ComputeDMetrics()
{
  if (!this->GetMRMLScene() || !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("ComputeDMetrics: Invalid MRML scene or parameter set node!");
    return false;
  }
  vtkMRMLTableNode* metricsTableNode = this->DoseVolumeHistogramNode->GetMetricsTableNode();
  if (!metricsTableNode)
  {
    vtkErrorMacro("ComputeDMetrics: Unable to access DVH metrics table!");
    return false;
  }
  vtkMRMLScalarVolumeNode* doseVolumeNode = this->DoseVolumeHistogramNode->GetDoseVolumeNode();
  if (!doseVolumeNode)
  {
    vtkErrorMacro("ComputeDMetrics: Unable to find dose volume node!");
    return false;
  }
  // Get dose unit name
  std::string doseUnitPostfix = "";
  vtkMRMLSubjectHierarchyNode* doseVolumeSubjectHierarchyNode = vtkMRMLSubjectHierarchyNode::GetAssociatedSubjectHierarchyNode(doseVolumeNode);
  if (doseVolumeSubjectHierarchyNode)
  {
    doseUnitPostfix = " (" +
      std::string( doseVolumeSubjectHierarchyNode->GetAttributeFromAncestor(
        SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_NAME_ATTRIBUTE_NAME.c_str(), vtkMRMLSubjectHierarchyConstants::GetDICOMLevelStudy()) )
      + ")";
  }

  // Remove all D metrics from the table
  vtkTable* metricsTable = metricsTableNode->GetTable();
  int numberOfColumnsBeforeRemoval = -1;
  do 
  {
    numberOfColumnsBeforeRemoval = metricsTable->GetNumberOfColumns();
    for (int col=0; col<metricsTable->GetNumberOfColumns(); ++col)
    {
      std::string columnName(metricsTable->GetColumnName(col));
      if (this->IsDMetricName(columnName))
      {
        metricsTable->RemoveColumn(col);
        break;
      }
    }
  }
  while (numberOfColumnsBeforeRemoval != metricsTable->GetNumberOfColumns());

  // If no D metrics need to be shown then exit
  if (!this->DoseVolumeHistogramNode->GetShowDMetrics())
  {
    return true;
  }

  // Get D metric dose values from input string
  std::vector<double> volumeValuesCc;
  std::vector<double> volumeValuesPercent;
  if (this->DoseVolumeHistogramNode->GetDVolumeValuesCc())
  {
    std::string volumeValuesCcStr(this->DoseVolumeHistogramNode->GetDVolumeValuesCc());
    this->GetNumbersFromMetricString(volumeValuesCcStr, volumeValuesCc);
  }
  if (this->DoseVolumeHistogramNode->GetDVolumeValuesPercent())
  {
    std::string volumeValuesPercentStr(this->DoseVolumeHistogramNode->GetDVolumeValuesPercent());
    this->GetNumbersFromMetricString(volumeValuesPercentStr, volumeValuesPercent);
  }

  // Create table columns for requested D metrics
  int numberOfColumnsBefore = metricsTable->GetNumberOfColumns();
  for (std::vector<double>::iterator ccIt=volumeValuesCc.begin(); ccIt!=volumeValuesCc.end(); ++ccIt)
  {
    std::stringstream newColumnName;
    newColumnName << "D" << (*ccIt) << "cc" << doseUnitPostfix;
    vtkAbstractArray* newColumn = metricsTableNode->AddColumn();
    newColumn->SetName(newColumnName.str().c_str());
    metricsTable->AddColumn(newColumn);
  }
  for (std::vector<double>::iterator percentIt=volumeValuesPercent.begin(); percentIt!=volumeValuesPercent.end(); ++percentIt)
  {
    std::stringstream newColumnName;
    newColumnName << "D" << (*percentIt) << "%" << doseUnitPostfix;
    vtkAbstractArray* newColumn = metricsTableNode->AddColumn();
    newColumn->SetName(newColumnName.str().c_str());
    metricsTable->AddColumn(newColumn);
  }

  // Traverse all DVH nodes referenced from metrics table and calculate V metrics
  std::vector<std::string> roles;
  metricsTableNode->GetNodeReferenceRoles(roles);
  for (std::vector<std::string>::iterator roleIt=roles.begin(); roleIt!=roles.end(); ++roleIt)
  {
    if ( roleIt->substr(0, vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX.size()).compare(
      vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX ) )
    {
      // Not a DVH reference
      continue;
    }

    // Get DVH node
    vtkMRMLDoubleArrayNode* dvhArrayNode = vtkMRMLDoubleArrayNode::SafeDownCast(
      metricsTableNode->GetNodeReference(roleIt->c_str()) );
    if (!dvhArrayNode)
    {
      vtkErrorMacro("ComputeDMetrics: Metrics table node reference '" << (*roleIt) << "' does not contain DVH node!");
      continue;
    }

    // Get corresponding table row
    int tableRow = -1;
    std::stringstream ss;
    ss << dvhArrayNode->GetAttribute(DVH_TABLE_ROW_ATTRIBUTE_NAME.c_str());
    ss >> tableRow;
    if (ss.fail())
    {
      vtkErrorMacro("ComputeDMetrics: Failed to get metrics table row from DVH node " << dvhArrayNode->GetName());
      continue;
    }

    // Get structure volume
    double structureVolume = metricsTable->GetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnVolumeCc).ToDouble();
    if (structureVolume == 0)
    {
      vtkErrorMacro("ComputeDMetrics: Failed to get structure volume for structure " << metricsTable->GetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnStructure).ToString());
      continue;
    }

    // Calculate metrics and set table entries
    int tableColumn = numberOfColumnsBefore;
    for (std::vector<double>::iterator ccIt=volumeValuesCc.begin(); ccIt!=volumeValuesCc.end(); ++ccIt)
    {
      double d = ComputeDMetric(dvhArrayNode, (*ccIt), structureVolume, false);
      metricsTable->SetValue( tableRow, tableColumn++, vtkVariant(d) );
    }
    for (std::vector<double>::iterator percentIt=volumeValuesPercent.begin(); percentIt!=volumeValuesPercent.end(); ++percentIt)
    {
      double d = ComputeDMetric(dvhArrayNode, (*percentIt), structureVolume, true);
      metricsTable->SetValue( tableRow, tableColumn++, vtkVariant(d) );
    }
  } // For all DVHs

  metricsTableNode->Modified();
  return true;
}

//---------------------------------------------------------------------------
double vtkSlicerDoseVolumeHistogramModuleLogic::ComputeDMetric(vtkMRMLDoubleArrayNode* dvhArrayNode, double volume, double structureVolume, bool isPercent)
{
  if (!dvhArrayNode)
  {
    vtkErrorMacro("ComputeDMetric: Invalid DVH array node!");
    return 0.0;
  }
  if (isPercent && structureVolume == 0.0)
  {
    vtkErrorMacro("ComputeDMetric: Invalid structure volume!");
    return 0.0;
  }

  vtkDoubleArray* doubleArray = dvhArrayNode->GetArray();
  double volumeSize = 0.0;
  double doseForVolume = 0.0;

  if (isPercent)
  {
    volumeSize = volume * structureVolume / 100.0;
  }
  else
  {
    volumeSize = volume;
  }

  // Check if the given volume is above the highest (first) in the array then assign no dose
  if (volumeSize >= doubleArray->GetComponent(0, 1) / 100.0 * structureVolume)
  {
    doseForVolume = 0.0;
  }
  // If volume is below the lowest (last) in the array then assign maximum dose
  else if (volumeSize < doubleArray->GetComponent(doubleArray->GetNumberOfTuples()-1, 1) / 100.0 * structureVolume)
  {
    doseForVolume = doubleArray->GetComponent(doubleArray->GetNumberOfTuples()-1, 0);
  }
  else
  {
    for (int i=0; i<doubleArray->GetNumberOfTuples()-1; ++i)
    {
      double volumePrevious = doubleArray->GetComponent(i, 1) / 100.0 * structureVolume;
      double volumeNext = doubleArray->GetComponent(i+1, 1) / 100.0 * structureVolume;
      if (volumePrevious > volumeSize && volumeSize >= volumeNext)
      {
        // Compute the dose using linear interpolation
        double dosePrevious = doubleArray->GetComponent(i, 0);
        double doseNext = doubleArray->GetComponent(i+1, 0);
        double doseEstimated = dosePrevious + (doseNext-dosePrevious)*(volumeSize-volumePrevious)/(volumeNext-volumePrevious);
        doseForVolume = doseEstimated;

        break;
      }
    }
  }

  return doseForVolume;
}

//---------------------------------------------------------------------------
bool vtkSlicerDoseVolumeHistogramModuleLogic::DoseVolumeContainsDose()
{
  if (!this->GetMRMLScene() || !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("DoseVolumeContainsDose: Invalid MRML scene or parameter set node!");
    return false;
  }

  vtkMRMLScalarVolumeNode* doseVolumeNode = this->DoseVolumeHistogramNode->GetDoseVolumeNode();
  return SlicerRtCommon::IsDoseVolumeNode(doseVolumeNode);
}

//---------------------------------------------------------------------------
bool vtkSlicerDoseVolumeHistogramModuleLogic::ExportDvhToCsv(const char* fileName, bool comma/*=true*/)
{
  if (!this->GetMRMLScene() || !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("ExportDvhToCsv: Invalid MRML scene or parameter set node!");
    return false;
  }
  vtkMRMLScalarVolumeNode* doseVolumeNode = this->DoseVolumeHistogramNode->GetDoseVolumeNode();
  if (!doseVolumeNode)
  {
    vtkErrorMacro("ExportDvhToCsv: Unable to find dose volume node!");
    return false;
  }
  vtkMRMLTableNode* metricsTableNode = this->DoseVolumeHistogramNode->GetMetricsTableNode();
  if (!metricsTableNode)
  {
    vtkErrorMacro("ExportDvhToCsv: Unable to access DVH metrics table node");
		return false;
  }
  vtkTable* metricsTable = metricsTableNode->GetTable();

  // Get dose unit name
  const char* doseUnitName = NULL;
  vtkMRMLSubjectHierarchyNode* doseVolumeSubjectHierarchyNode = vtkMRMLSubjectHierarchyNode::GetAssociatedSubjectHierarchyNode(doseVolumeNode);
  if (doseVolumeSubjectHierarchyNode)
  {
    doseUnitName = doseVolumeSubjectHierarchyNode->GetAttributeFromAncestor(
      SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_NAME_ATTRIBUTE_NAME.c_str(), vtkMRMLSubjectHierarchyConstants::GetDICOMLevelStudy());
  }

  // Get all DVH array nodes from the parameter set node
  std::vector<vtkMRMLDoubleArrayNode*> dvhArrayNodes;
  this->DoseVolumeHistogramNode->GetDvhArrayNodes(dvhArrayNodes);

  // Open output file
  std::ofstream outfile;
  outfile.open(fileName, std::ios_base::out | std::ios_base::trunc);
	if (!outfile)
	{
    vtkErrorMacro("ExportDvhToCsv: Output file '" << fileName << "' cannot be opened!");
		return false;
	}

  // Determine the maximum number of values
  int maxNumberOfValues = -1;
  for (std::vector<vtkMRMLDoubleArrayNode*>::iterator dvhIt=dvhArrayNodes.begin(); dvhIt!=dvhArrayNodes.end(); ++dvhIt)
  {
    vtkMRMLDoubleArrayNode* dvhArrayNode = (*dvhIt);
    if (dvhArrayNode->GetArray()->GetNumberOfTuples() > maxNumberOfValues)
    {
      maxNumberOfValues = dvhArrayNode->GetArray()->GetNumberOfTuples();
    }
  }

  // Write header
  for (std::vector<vtkMRMLDoubleArrayNode*>::iterator dvhIt=dvhArrayNodes.begin(); dvhIt!=dvhArrayNodes.end(); ++dvhIt)
  {
    vtkMRMLDoubleArrayNode* dvhArrayNode = (*dvhIt);
    int tableRow = -1;
    std::stringstream ss;
    ss << dvhArrayNode->GetAttribute(DVH_TABLE_ROW_ATTRIBUTE_NAME.c_str());
    ss >> tableRow;

    double volume = metricsTable->GetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnVolumeCc).ToDouble();
    std::string structureName = metricsTable->GetValue(tableRow, vtkMRMLDoseVolumeHistogramNode::MetricColumnStructure).ToString();

    outfile << structureName << " Dose (" << doseUnitName << ")" << (comma ? "," : "\t");
    outfile << structureName << DVH_CSV_HEADER_VOLUME_FIELD_MIDDLE
      << std::fixed << std::setprecision(3) << volume << DVH_CSV_HEADER_VOLUME_FIELD_END << (comma ? "," : "\t");
  }
	outfile << std::endl;

  // Write values
	for (int row=0; row<maxNumberOfValues; ++row)
  {
    for (std::vector<vtkMRMLDoubleArrayNode*>::iterator dvhIt=dvhArrayNodes.begin(); dvhIt!=dvhArrayNodes.end(); ++dvhIt)
    {
      vtkMRMLDoubleArrayNode* dvhArrayNode = (*dvhIt);

      if (row < dvhArrayNode->GetArray()->GetNumberOfTuples())
      {
    	  std::ostringstream doseStringStream;
			  doseStringStream << std::fixed << std::setprecision(6) <<
          dvhArrayNode->GetArray()->GetComponent(row, 0);
        std::string dose = doseStringStream.str();
        if (!comma)
        {
          size_t periodPosition = dose.find(".");
          if (periodPosition != std::string::npos)
          {
            dose.replace(periodPosition, 1, ",");
          }
        }
        outfile << dose;
      }
      outfile << (comma ? "," : "\t");

      if (row < dvhArrayNode->GetArray()->GetNumberOfTuples())
      {
    	  std::ostringstream valueStringStream;
			  valueStringStream << std::fixed << std::setprecision(6) <<
          dvhArrayNode->GetArray()->GetComponent(row, 1);
        std::string value = valueStringStream.str();
        if (!comma)
        {
          size_t periodPosition = value.find(".");
          if (periodPosition != std::string::npos)
          {
            value.replace(periodPosition, 1, ",");
          }
        }
        outfile << value;
      }
      outfile << (comma ? "," : "\t");
    }
		outfile << std::endl;
  }

	outfile.close();

  return true;
}

//-----------------------------------------------------------------------------
bool vtkSlicerDoseVolumeHistogramModuleLogic::ExportDvhMetricsToCsv(const char* fileName, bool comma/*=true*/)
{
  if (!this->GetMRMLScene() || !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("ExportDvhMetricsToCsv: Invalid MRML scene or parameter set node!");
    return false;
  }
  vtkMRMLTableNode* metricsTableNode = this->DoseVolumeHistogramNode->GetMetricsTableNode();

  // Make a copy of the metrics table without the visualization and dose volume name columns
  vtkSmartPointer<vtkTable> metricsTableCopy = vtkSmartPointer<vtkTable>::New();
  metricsTableCopy->DeepCopy(metricsTableNode->GetTable());
  metricsTableCopy->RemoveColumn(vtkMRMLDoseVolumeHistogramNode::MetricColumnDoseVolume);
  metricsTableCopy->RemoveColumn(vtkMRMLDoseVolumeHistogramNode::MetricColumnVisible);

  vtkNew<vtkDelimitedTextWriter> writer;
  writer->SetFileName(fileName);
  writer->SetInputData(metricsTableCopy);
  writer->SetUseStringDelimiter(false);

	if (!comma)
	{
    writer->SetFieldDelimiter("\t");
  }
  else
  {
    writer->SetFieldDelimiter(",");
  }

  try
  {
    writer->Write();
  }
  catch (...)
  {
    vtkErrorMacro("ExportDvhMetricsToCsv: Failed to write DVH metrics table to file " << fileName);
    return false;
  }

  return true;
}

//-----------------------------------------------------------------------------
vtkCollection* vtkSlicerDoseVolumeHistogramModuleLogic::ReadCsvToDoubleArrayNode(std::string csvFilename)
{
  std::string csvSeparatorCharacter = ",";
  
  std::vector< vtkSmartPointer< vtkDoubleArray > > currentDvh;
  
  // Vectors containing the names and total volumes of structures
  std::vector<std::string> structureNames;
  std::vector<double> structureVolumeCCs;

  // Load current DVH from CSV
  std::ifstream dvhStream;
  dvhStream.open(csvFilename.c_str(), std::ifstream::in);

  bool firstLine = true;
  int fieldCount = 0;
  int lineNumber = 0;
  char line[16384];

  while (dvhStream.getline(line, 16384, '\n'))
  {
    std::string lineStr(line);
    size_t commaPosition = lineStr.find(csvSeparatorCharacter);

    // Determine number of fields (twice the number of structures)
    if (firstLine)
    {
      while (commaPosition != std::string::npos)
      {
        if (fieldCount%2==1)
        {
          // Get the structure's name
          std::string field = lineStr.substr(0, commaPosition);
          size_t middlePosition = field.find(DVH_CSV_HEADER_VOLUME_FIELD_MIDDLE);
          structureNames.push_back(field.substr(0, middlePosition - DVH_ARRAY_NODE_NAME_POSTFIX.size()));

          // Get the structure's total volume and add it to the vector
          double volumeCCs = 0;
          {
            std::string structureVolumeString = field.substr( middlePosition + DVH_CSV_HEADER_VOLUME_FIELD_MIDDLE.size(),
              field.size() - middlePosition - DVH_CSV_HEADER_VOLUME_FIELD_MIDDLE.size() - DVH_CSV_HEADER_VOLUME_FIELD_END.size() );
            std::stringstream ss;
            ss << structureVolumeString;
            double doubleValue;
            ss >> doubleValue;
            volumeCCs = doubleValue;
          }
          structureVolumeCCs.push_back(volumeCCs);

          if (volumeCCs == 0)
          {
            std::cerr << "Invalid structure volume in CSV header field " << field << std::endl;
          }
        }

        // Move to the next structure's location in the string
        fieldCount++;
        lineStr = lineStr.substr(commaPosition+1);
        commaPosition = lineStr.find(csvSeparatorCharacter);
      }
      
      // Handle last field (if there was no comma at the end)
      if (!lineStr.empty() )
      {
        // Get the structure's name
        size_t middlePosition = lineStr.find(DVH_CSV_HEADER_VOLUME_FIELD_MIDDLE);
        if (middlePosition != std::string::npos)
        {
          structureNames.push_back(lineStr.substr(0, middlePosition - DVH_ARRAY_NODE_NAME_POSTFIX.size()));

          // Get the structure's total volume and add it to the vector
          double volumeCCs = 0;
          {
            std::string structureVolumeString = lineStr.substr( middlePosition + DVH_CSV_HEADER_VOLUME_FIELD_MIDDLE.size(),
              lineStr.size() - middlePosition - DVH_CSV_HEADER_VOLUME_FIELD_MIDDLE.size() - DVH_CSV_HEADER_VOLUME_FIELD_END.size() );
            std::stringstream ss;
            ss << structureVolumeString;
            double doubleValue;
            ss >> doubleValue;
            volumeCCs = doubleValue;
          }
          structureVolumeCCs.push_back(volumeCCs);

          if (volumeCCs == 0)
          {
            std::cerr << "Invalid structure volume in CSV header field " << lineStr << std::endl;
          }

          fieldCount++;
        }
      }
        
      // Add a vtkDoubleArray for each structure into the vector
      for (int structureIndex=0; structureIndex < fieldCount/2; ++structureIndex)
      {
        vtkSmartPointer<vtkDoubleArray> tempArray = vtkSmartPointer<vtkDoubleArray>::New();
        tempArray->SetNumberOfComponents(3);
        currentDvh.push_back(tempArray);
      }
      firstLine = false;
      continue;
    }

    // Read all tuples from the current line
    int structureNumber = 0;
    while (commaPosition != std::string::npos)
    {
      // Tuple to be inserted into the vtkDoubleArray object
      double *tupleToInsert = new double[3];
      for(int j=0; j<3; ++j)
        tupleToInsert[j] = 0;
      
      // Get the current bin's dose from the string
      double doseGy = 0;
      {
        double doubleValue;
        std::stringstream ss;
        ss << lineStr.substr(0, commaPosition);
        ss >> doubleValue;
        doseGy = doubleValue;
      }
      tupleToInsert[0] = doseGy;

      // Get the current bin's volume from the string
      double volumePercent = 0;
      {
        double doubleValue;
        lineStr = lineStr.substr(commaPosition+1);
        commaPosition = lineStr.find(csvSeparatorCharacter);
        std::stringstream ss;
        ss << lineStr.substr(0, commaPosition);
        ss >> doubleValue;
        volumePercent = doubleValue;
      }
      tupleToInsert[1] = volumePercent;
      
      if ((doseGy != 0.0 || volumePercent != 0.0) && (commaPosition > 0))
      {
        // Add the current bin into the vtkDoubleArray for the current structure
        currentDvh.at(structureNumber)->InsertTuple(lineNumber, tupleToInsert);        
      }
      
      // Destroy the tuple
      delete tupleToInsert;
      tupleToInsert = NULL;
      
      // Move to the next structure's bin in the string
      lineStr = lineStr.substr(commaPosition+1);
      commaPosition = lineStr.find(csvSeparatorCharacter);
      structureNumber++;
    }
    lineNumber++;
  }
 
  dvhStream.close();
  
  vtkCollection* doubleArrayNodes = vtkCollection::New();
  for (unsigned int structureIndex=0; structureIndex < currentDvh.size(); structureIndex++)
  {
    // Create the vtkMRMLDoubleArrayNodes which will be passed to the logic function.
    vtkNew<vtkMRMLDoubleArrayNode> currentNode;
    currentNode->SetArray(currentDvh.at(structureIndex));
    
    // Set the total volume attribute in the vtkMRMLDoubleArrayNode attributes
    std::ostringstream attributeNameStream;
    attributeNameStream << vtkMRMLDoseVolumeHistogramNode::DVH_ATTRIBUTE_PREFIX << vtkSlicerDoseVolumeHistogramModuleLogic::DVH_METRIC_TOTAL_VOLUME_CC;
    std::ostringstream attributeValueStream;
    attributeValueStream << structureVolumeCCs[structureIndex];
    currentNode->SetAttribute(attributeNameStream.str().c_str(), attributeValueStream.str().c_str());
    
    // Set the structure's name attribute and variables
    currentNode->SetAttribute(DVH_SEGMENT_ID_ATTRIBUTE_NAME.c_str(), structureNames.at(structureIndex).c_str());
    std::string nameAttribute = structureNames.at(structureIndex) + DVH_ARRAY_NODE_NAME_POSTFIX;
    currentNode->SetName(nameAttribute.c_str());

    // add the new node to the vector
    doubleArrayNodes->AddItem(currentNode.GetPointer());
  }

  return doubleArrayNodes;
}

//---------------------------------------------------------------------------
std::string vtkSlicerDoseVolumeHistogramModuleLogic::AssembleDoseMetricName(std::string doseMetricAttributeNamePrefix)
{
  if (!this->GetMRMLScene() || !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("AssembleDoseMetricName: Invalid MRML scene or parameter set node!");
    return "";
  }
  vtkMRMLScalarVolumeNode* doseVolumeNode = this->DoseVolumeHistogramNode->GetDoseVolumeNode();
  if (!doseVolumeNode)
  {
    vtkErrorMacro("AssembleDoseMetricName: Unable to find dose volume node!");
    return "";
  }

  // Get dose unit name
  const char* doseUnitName = NULL;
  vtkMRMLSubjectHierarchyNode* doseVolumeSubjectHierarchyNode = vtkMRMLSubjectHierarchyNode::GetAssociatedSubjectHierarchyNode(doseVolumeNode);
  if (doseVolumeSubjectHierarchyNode)
  {
    doseUnitName = doseVolumeSubjectHierarchyNode->GetAttributeFromAncestor(
      SlicerRtCommon::DICOMRTIMPORT_DOSE_UNIT_NAME_ATTRIBUTE_NAME.c_str(), vtkMRMLSubjectHierarchyConstants::GetDICOMLevelStudy());
  }

  // Assemble metric name
  std::string valueType = ( doseUnitName
    ? (DVH_METRIC_DOSE_POSTFIX + " (" + doseUnitName + ")")
    : (DVH_METRIC_INTENSITY_POSTFIX) );
  std::ostringstream metricNameStream;
  metricNameStream << doseMetricAttributeNamePrefix << valueType;

  return metricNameStream.str();
}

//-----------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::InitializeMetricsTable()
{
  if (!this->GetMRMLScene() || !this->DoseVolumeHistogramNode)
  {
    vtkErrorMacro("InitializeMetricsTable: Invalid MRML scene or parameter set node!");
    return;
  }
  vtkMRMLTableNode* metricsTableNode = this->DoseVolumeHistogramNode->GetMetricsTableNode();
  if (!metricsTableNode)
  {
    vtkErrorMacro("InitializeMetricsTable: Unable to find metrics table!");
    return;
  }

  // Empty the table first
  metricsTableNode->RemoveAllColumns();
  vtkEventBroker::GetInstance()->RemoveObservations(this);

  // Assemble metric names
  std::string meanDoseMetricName = this->AssembleDoseMetricName(DVH_METRIC_MEAN_PREFIX);
  std::string minDoseMetricName = this->AssembleDoseMetricName(DVH_METRIC_MIN_PREFIX);
  std::string maxDoseMetricName = this->AssembleDoseMetricName(DVH_METRIC_MAX_PREFIX);
  if (meanDoseMetricName.empty() || minDoseMetricName.empty() || maxDoseMetricName.empty())
  {
    vtkErrorMacro("InitializeMetricsTable: Failed to assemble metric names!");
    return;
  }

  // Add default columns
  vtkSmartPointer<vtkBitArray> visColumn = vtkSmartPointer<vtkBitArray>::New();
  visColumn->SetName("Show");
  metricsTableNode->AddColumn(visColumn);
  vtkEventBroker::GetInstance()->AddObservation(
    visColumn, vtkCommand::ModifiedEvent, this, this->VisibilityChangedCallbackCommand );

  vtkAbstractArray* structureNameColumn = metricsTableNode->AddColumn();
  structureNameColumn->SetName(DVH_METRIC_STRUCTURE.c_str());

  vtkAbstractArray* volumeNameColumn = metricsTableNode->AddColumn();
  volumeNameColumn->SetName("Volume name");

  vtkAbstractArray* volumeCcColumn = metricsTableNode->AddColumn();
  volumeCcColumn->SetName(DVH_METRIC_TOTAL_VOLUME_CC.c_str());

  vtkAbstractArray* meanDoseColumn = metricsTableNode->AddColumn();
  meanDoseColumn->SetName(meanDoseMetricName.c_str());

  vtkAbstractArray* minDoseColumn = metricsTableNode->AddColumn();
  minDoseColumn->SetName(minDoseMetricName.c_str());

  vtkAbstractArray* maxDoseColumn = metricsTableNode->AddColumn();
  maxDoseColumn->SetName(maxDoseMetricName.c_str());
}

//---------------------------------------------------------------------------
void vtkSlicerDoseVolumeHistogramModuleLogic::OnVisibilityChanged(vtkObject* caller,
                                                                  unsigned long vtkNotUsed(eid),
                                                                  void* clientData,
                                                                  void* vtkNotUsed(callData))
{
  vtkSlicerDoseVolumeHistogramModuleLogic* self = reinterpret_cast<vtkSlicerDoseVolumeHistogramModuleLogic*>(clientData);
  vtkBitArray* visArray = reinterpret_cast<vtkBitArray*>(caller);
  if (!self || !visArray)
  {
    return;
  }
  vtkMRMLDoseVolumeHistogramNode* paramNode = self->GetDoseVolumeHistogramNode();
  if (!self->GetMRMLScene() || !paramNode)
  {
    vtkErrorWithObjectMacro(self,"OnVisibilityChanged: Invalid MRML scene or parameter set node!");
    return;
  }

  // Go through DVHs and change chart visibility for DVH for which the visibility value was modified
  std::set<int> rows;
  std::vector<vtkMRMLDoubleArrayNode*> dvhArrayNodes;
  paramNode->GetDvhArrayNodes(dvhArrayNodes);
  for (std::vector<vtkMRMLDoubleArrayNode*>::iterator dvhIt=dvhArrayNodes.begin(); dvhIt!=dvhArrayNodes.end(); ++dvhIt)
  {
    vtkMRMLDoubleArrayNode* dvhArrayNode = (*dvhIt);

    // Get corresponding table row
    int tableRow = -1;
    std::stringstream ss;
    ss << dvhArrayNode->GetAttribute(DVH_TABLE_ROW_ATTRIBUTE_NAME.c_str());
    ss >> tableRow;
    if (ss.fail())
    {
      vtkErrorWithObjectMacro(self,"OnVisibilityChanged: Failed to get metrics table row from DVH node " << dvhArrayNode->GetName());
      continue;
    }
    rows.insert(tableRow); // Safety check

    // Change chart visibility if necessary
    bool visibleInChart = self->IsDvhAddedToChart(dvhArrayNode->GetID());
    bool visibilityInTable = (bool)visArray->GetValue(tableRow);
    if (visibleInChart != visibilityInTable)
    {
      if (visibilityInTable)
      {
        self->AddDvhToChart(dvhArrayNode->GetID());
      }
      else
      {
        self->RemoveDvhFromChart(dvhArrayNode->GetID());
      }
    }
  }

  if (rows.size() != paramNode->GetMetricsTableNode()->GetNumberOfRows())
  {
    vtkErrorWithObjectMacro(self,"OnVisibilityChanged: Mismatch between referenced DVH arrays and metrics table!");
  }
}

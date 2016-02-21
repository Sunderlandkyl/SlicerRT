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

#ifndef __qSlicerSegmentEditorLabelEffect_h
#define __qSlicerSegmentEditorLabelEffect_h

// Segmentations Editor Effects includes
#include "qSlicerSegmentationsEditorEffectsExport.h"

#include "qSlicerSegmentEditorAbstractEffect.h"

class qSlicerSegmentEditorLabelEffectPrivate;

class vtkMatrix4x4;
class vtkOrientedImageData;
class vtkPolyData;
class vtkMRMLVolumeNode;
class vtkMRMLSegmentationNode;

/// \ingroup SlicerRt_QtModules_Segmentations
/// \brief Base class for all "label" effects.
/// 
/// This base class provides GUI and MRML for the options PaintOver and Threshold.
class Q_SLICER_SEGMENTATIONS_EFFECTS_EXPORT qSlicerSegmentEditorLabelEffect :
  public qSlicerSegmentEditorAbstractEffect
{
public:
  Q_OBJECT

public:
  typedef qSlicerSegmentEditorAbstractEffect Superclass;
  qSlicerSegmentEditorLabelEffect(QObject* parent = NULL);
  virtual ~qSlicerSegmentEditorLabelEffect(); 

  Q_INVOKABLE static QString paintOverParameterName() { return QString("PaintOver"); };
  Q_INVOKABLE static QString paintThresholdParameterName() { return QString("PaintThreshold"); };
  Q_INVOKABLE static QString paintThresholdMinParameterName() { return QString("PaintThresholdMin"); };
  Q_INVOKABLE static QString paintThresholdMaxParameterName() { return QString("PaintThresholdMax"); };
  Q_INVOKABLE static QString thresholdAvailableParameterName() { return QString("ThresholdAvailable"); };
  Q_INVOKABLE static QString paintOverAvailableParameterName() { return QString("PaintOverAvailable"); };

public:
  /// Clone editor effect
  /// (redefinition of pure virtual function to allow python wrapper to identify this as abstract class)
  virtual qSlicerSegmentEditorAbstractEffect* clone() = 0;

  /// Perform actions needed before the edited labelmap is applied back to the segment
  Q_INVOKABLE virtual void apply();

  /// Create options frame widgets, make connections, and add them to the main options frame using \sa addOptionsWidget
  virtual void setupOptionsFrame();

  /// Set default parameters in the parameter MRML node
  virtual void setMRMLDefaults();

  /// Perform actions needed on edited labelmap change
  virtual void editedLabelmapChanged();

  /// Perform actions needed on master volume change
  virtual void masterVolumeNodeChanged();

public slots:
  /// Update user interface from parameter set node
  virtual void updateGUIFromMRML();

  /// Update parameter set node from user interface
  virtual void updateMRMLFromGUI();

// Utility functions
public:
  /// Apply mask image on an input image
  /// \param input Input image to apply the mask on
  /// \param mask Mask to apply
  /// \param notMask If on, the mask is passed through a boolean not before it is used to mask the image.
  ///   The effect is to pass the pixels where the input mask is zero, and replace the pixels where the
  ///   input value is non zero
  Q_INVOKABLE static void applyImageMask(vtkOrientedImageData* input, vtkOrientedImageData* mask, bool notMask = false);

  /// Rasterize a poly data onto the input image into the slice view
  Q_INVOKABLE static void appendPolyMask(vtkOrientedImageData* input, vtkPolyData* polyData, qMRMLSliceWidget* sliceWidget);

  /// Create a slice view screen space (2D) mask image for the given polydata
  Q_INVOKABLE static void createMaskImageFromPolyData(vtkPolyData* polyData, vtkOrientedImageData* outputMask, qMRMLSliceWidget* sliceWidget);

  /// Append image onto image. Resamples appended image and saves result in input image
  Q_INVOKABLE static void appendImage(vtkOrientedImageData* inputImage, vtkOrientedImageData* appendedImage);

  /// Return matrix for volume node that takes into account the IJKToRAS
  /// and any linear transforms that have been applied
  Q_INVOKABLE static void imageToWorldMatrix(vtkMRMLVolumeNode* node, vtkMatrix4x4* ijkToRas);

  /// Return matrix for oriented image data that takes into account the image to world
  /// and any linear transforms that have been applied on the given segmentation
  Q_INVOKABLE static void imageToWorldMatrix(vtkOrientedImageData* image, vtkMRMLSegmentationNode* node, vtkMatrix4x4* ijkToRas);

protected:
  QScopedPointer<qSlicerSegmentEditorLabelEffectPrivate> d_ptr;

private:
  Q_DECLARE_PRIVATE(qSlicerSegmentEditorLabelEffect);
  Q_DISABLE_COPY(qSlicerSegmentEditorLabelEffect);
};

#endif

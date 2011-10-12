#include "DimensionSliceWidget.h"
#include <iosfwd>
#include <iostream>
#include <iomanip>
#include <qlayout.h>

DimensionSliceWidget::DimensionSliceWidget(QWidget *parent)
    : QWidget(parent)
{
  ui.setupUi(this);

  m_insideSetShownDim = false;
  m_insideSpinBoxChanged = false;

  QObject::connect(ui.horizontalSlider, SIGNAL( valueChanged(int)),
                   this, SLOT(sliderMoved()));
  QObject::connect(ui.doubleSpinBox, SIGNAL( valueChanged(double)),
                   this, SLOT(spinBoxChanged()));
  QObject::connect(ui.btnX, SIGNAL(toggled(bool)),
                   this, SLOT(btnXYChanged()));
  QObject::connect(ui.btnY, SIGNAL(toggled(bool)),
                   this, SLOT(btnXYChanged()));
}

DimensionSliceWidget::~DimensionSliceWidget()
{

}

//-------------------------------------------------------------------------------------------------
/** Slot called when the slider moves */
void DimensionSliceWidget::sliderMoved()
{
  // Don't update when called from the spin box
  if (m_insideSpinBoxChanged) return;

  // Find the slice point
  size_t index = size_t(ui.horizontalSlider->value());
  m_slicePoint =  m_dim->getX(index);

  // This will, in turn, emit the changedSlicePoint() signal
  ui.doubleSpinBox->setValue(m_slicePoint);
}

//-------------------------------------------------------------------------------------------------
/** Slot called when the slider moves */
void DimensionSliceWidget::spinBoxChanged()
{
  m_insideSpinBoxChanged = true;
  // This is the slice point
  m_slicePoint = ui.doubleSpinBox->value();

  // Set the slider to the matching point
  int index = int((m_slicePoint - m_dim->getMinimum()) / m_dim->getBinWidth());
  ui.horizontalSlider->setValue(index);

  // Emit that the user changed the slicing point
  emit changedSlicePoint(m_dimIndex, m_slicePoint);

  m_insideSpinBoxChanged = false;
}

//-------------------------------------------------------------------------------------------------
/** Called when the X button is clicked */
void DimensionSliceWidget::btnXYChanged()
{
  if (m_insideSetShownDim)
    return;
  int oldDim = m_shownDim;
  if (ui.btnX->isChecked() && ui.btnY->isChecked() )
  {
    // Toggle when both are checked
    if (m_shownDim == 0)
      this->setShownDim(1);
    else
      this->setShownDim(0);
  }
  else if (ui.btnX->isChecked())
    this->setShownDim(0);
  else if (ui.btnY->isChecked())
    this->setShownDim(1);
  else
    this->setShownDim(-1);
  // Emit that the user changed the shown dimension
  emit changedShownDim(m_dimIndex, m_shownDim, oldDim);
}



//-------------------------------------------------------------------------------------------------
/** Set the shown dimension
 *
 * @param dim :: -1 = None, 0 = X, 1 = Y. 2+ reserved for higher dimensions
 */
void DimensionSliceWidget::setShownDim(int dim)
{
  m_insideSetShownDim = true;
  m_shownDim = dim;
  ui.btnX->setChecked( m_shownDim == 0 );
  ui.btnY->setChecked( m_shownDim == 1 );
  bool slicing = m_shownDim == -1;
  ui.horizontalSlider->setVisible( slicing );
  ui.doubleSpinBox->setVisible( slicing );
  ui.lblUnits->setVisible( slicing );

  // Make the spacer expand to keep the buttons in the same spot
  if (slicing)
  {
    // Remove the 3rd item = the spacer
    QLayoutIterator it = ui.horizontalLayout->iterator();
    ++it; ++it; ++it;
    ui.horizontalLayout->removeItem(it.current());
  }
  else
    // Put the spacer back
    ui.horizontalLayout->insertSpacerItem(3, ui.horizontalSpacer );

  this->update();
  m_insideSetShownDim = false;
}

//-------------------------------------------------------------------------------------------------
/** Set the dimension to display */
void DimensionSliceWidget::setDimension(int index, Mantid::Geometry::IMDDimension_const_sptr dim)
{
  m_dim = dim;
  m_dimIndex = index;
  double min = m_dim->getMinimum();
  double max = m_dim->getMaximum(); //- m_dim->getBinWidth()/2.0;
  ui.lblName->setText(QString::fromStdString(m_dim->getName()) );
  ui.lblUnits->setText(QString::fromStdString(m_dim->getUnits()) );
  ui.horizontalSlider->setMinimum(0);
  ui.horizontalSlider->setMaximum( int(m_dim->getNBins()) );
  ui.doubleSpinBox->setMinimum(min);
  ui.doubleSpinBox->setMaximum(max);
  ui.doubleSpinBox->setSingleStep(m_dim->getBinWidth());
  m_slicePoint = m_dim->getMinimum();
}


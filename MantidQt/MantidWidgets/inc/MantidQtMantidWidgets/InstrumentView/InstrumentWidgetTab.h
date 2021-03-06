#ifndef INSTRUMENTWIDGETTAB_H
#define INSTRUMENTWIDGETTAB_H

#include <MantidQtMantidWidgets/WidgetDllOption.h>
#include "InstrumentWidgetTypes.h"

#include <QFrame>
#include <boost/shared_ptr.hpp>

//--------------------------------------------------
//  Forward declarations
//--------------------------------------------------

class QSettings;
class QMenu;

namespace MantidQt {
namespace MantidWidgets {
class InstrumentWidget;
class ProjectionSurface;

class EXPORT_OPT_MANTIDQT_MANTIDWIDGETS InstrumentWidgetTab
    : public QFrame,
      public InstrumentWidgetTypes {
  Q_OBJECT
public:
  explicit InstrumentWidgetTab(InstrumentWidget *parent);
  /// Called by InstrumentWidget after the projection surface crated
  /// Use it for surface-specific initialization
  virtual void initSurface() {}
  /// Save tab's persistent settings to the provided QSettings instance
  virtual void saveSettings(QSettings &) const {}
  /// Load (read and apply) tab's persistent settings from the provided
  /// QSettings instance
  virtual void loadSettings(const QSettings &) {}
  /// Add tab-specific items to the context menu
  /// Return true if at least 1 item was added or false otherwise.
  virtual bool addToDisplayContextMenu(QMenu &) const { return false; }
  /// Get the projection surface
  boost::shared_ptr<ProjectionSurface> getSurface() const;

protected:
  /// The parent InstrumentWidget
  InstrumentWidget *m_instrWidget;
};
} // MantidWidgets
} // MantidQt

#endif // INSTRUMENTWIDGETTAB_H

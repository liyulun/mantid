#include "Shape2DCollection.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QStringList>

#include <iostream>
#include <stdexcept>
#include <cmath>

Shape2DCollection::Shape2DCollection():
Shape2D(),
m_creating(false),
m_editing(false),
m_moving(false),
m_x(0),
m_y(0),
m_currentShape(NULL),
m_leftButtonPressed(false),
m_overridingCursor(false)
{
}

Shape2DCollection::~Shape2DCollection()
{
  foreach(Shape2D* shape,m_shapes)
  {
    delete shape;
  }
}

void Shape2DCollection::draw(QPainter& painter) const
{
  if (m_shapes.isEmpty()) return;
  painter.save();
  painter.setTransform(m_transform);
  foreach(const Shape2D* shape,m_shapes)
  {
    shape->draw(painter);
  }
  painter.restore();
}

void Shape2DCollection::addShape(Shape2D* shape,bool slct)
{
  m_shapes.push_back(shape);
  m_boundingRect |= shape->getBoundingRect();
  if (slct)
  {
    select(shape);
  }
  emit shapeCreated();
}

void Shape2DCollection::setWindow(const QRectF& rect,const QRect& viewport) const
{
  m_transform.reset();
  m_viewport = viewport;
  if ( m_windowRect.isNull() )
  {
    m_windowRect = rect;
    m_h = viewport.height();
    m_wx = viewport.width() / rect.width();
    m_wy = m_h / rect.height();
  }
  else
  {
    double wx = viewport.width() / rect.width();
    double wy = viewport.height() / rect.height();
    double rx = m_windowRect.left() - rect.left();
    double ry = m_windowRect.top() - rect.top();
    qreal sx = wx / m_wx;
    qreal sy = wy / m_wy;
    qreal dx = rx * wx;
    qreal dy = viewport.height() - sy * m_h - ry * wy;

    m_transform.translate(dx,dy);
    m_transform.scale(sx,sy);
  }
}

void Shape2DCollection::refit()
{
}

void Shape2DCollection::resetBoundingRect() 
{
  m_boundingRect = QRectF();
  foreach(const Shape2D* shape,m_shapes)
  {
    m_boundingRect |= shape->getBoundingRect();
  }
}

void Shape2DCollection::mousePressEvent(QMouseEvent* e)
{
  if (e->button() == Qt::LeftButton)
  {
    m_leftButtonPressed = true;
    if (m_creating && !m_shapeType.isEmpty())
    {
      deselectAll();
      addShape(m_shapeType,e->x(),e->y());
      if (!m_currentShape) return;
      m_currentShape->edit(true);
      m_currentCP = 2;
      m_editing = true;
    }
    else if (selectControlPointAt(e->x(),e->y()))
    {
      m_editing = true;
    }
    else if (selectAtXY(e->x(),e->y()))
    {
      m_x = e->x();
      m_y = e->y();
      m_moving = true;
    }
    else
    {
      deselectAll();
    }
  }
}

void Shape2DCollection::mouseMoveEvent(QMouseEvent* e)
{
  if (m_editing)
  {
    if (m_leftButtonPressed && m_currentShape && m_currentShape->isEditing() && m_currentCP < m_currentShape->getNControlPoints())
    {
      QPointF p = m_transform.inverted().map(QPointF(e->x(),e->y()));
      m_currentShape->setControlPoint(m_currentCP,p);
      emit shapeChanged();
    }
  }
  else if (m_moving && m_leftButtonPressed && m_currentShape)
  {
    QPointF p1 = m_transform.inverted().map(QPointF( e->x(),e->y() ));
    QPointF p2 = m_transform.inverted().map(QPointF( m_x, m_y ));
    m_currentShape->moveBy(p1 - p2);
    emit shapeChanged();
    m_x = e->x();
    m_y = e->y();
  }
  else if (selectControlPointAt(e->x(),e->y()) || isOverCurrentAt(e->x(),e->y()))
  {
    m_overridingCursor = true;
    QApplication::setOverrideCursor(Qt::SizeAllCursor);
  }
  else if (m_overridingCursor)
  {
    QApplication::restoreOverrideCursor();
  }
}

void Shape2DCollection::mouseReleaseEvent(QMouseEvent* e)
{
  if (e->button() == Qt::LeftButton)
  {
    m_leftButtonPressed = false;
  }
  m_creating = false;
  m_editing = false;
  m_moving = false;
}

void Shape2DCollection::wheelEvent(QWheelEvent*)
{
}

void Shape2DCollection::keyPressEvent(QKeyEvent* e)
{
  switch(e->key())
  {
  case Qt::Key_Delete:
  case Qt::Key_Backspace: removeCurrentShape(); break;
  }
}

void Shape2DCollection::addShape(const QString& type,int x,int y)
{
  m_currentShape = createShape(type,x,y);
  if ( ! m_currentShape )
  {
    emit shapeSelected();
    return;
  }
  m_currentShape->setColor(m_borderColor);
  m_currentShape->setFillColor(m_fillColor);
  m_creating = true;
  addShape( m_currentShape);
  emit shapeSelected();
}

Shape2D* Shape2DCollection::createShape(const QString& type,int x,int y) const
{
  QPointF p = m_transform.inverted().map(QPointF(x,y));

  if (type.toLower() == "ellipse")
  {
     return new Shape2DEllipse(p,1.0);
  }
  else if (type.toLower() == "rectangle")
  {
     return new Shape2DRectangle(p,QSizeF(1,1));
  }

  QStringList complexType = type.split(' ',QString::SkipEmptyParts);

  if (complexType.size() < 2) return NULL;

  QString mainType = complexType[0];

  if (mainType.toLower() == "ring")
  {
    Shape2D* child = createShape(complexType[1],x,y);
    return new Shape2DRing(child);
  }

  throw std::invalid_argument("Shape " + type.toStdString() + " cannot be created");

}

void Shape2DCollection::startCreatingShape2D(const QString& type,const QColor& borderColor,const QColor& fillColor)
{
  m_creating = true;
  m_shapeType = type;
  m_borderColor = borderColor;
  m_fillColor = fillColor;
}

void Shape2DCollection::deselectAll()
{
  foreach(Shape2D* shape,m_shapes)
  {
    shape->edit(false);
  }
  m_currentShape = NULL;
  emit shapesDeselected();
}

/**
 * Select a shape which contains a point (x,y) of the screen.
 */
bool Shape2DCollection::selectAtXY(int x,int y)
{
  QPointF p = m_transform.inverted().map(QPointF(x,y));
  foreach(Shape2D* shape,m_shapes)
  {
    bool picked = shape->selectAt(p);
    if (picked) 
    {
      select(shape);
      return true;
    }
  }
  return false;
}

/**
 * Select a shape with index i.
 */
void Shape2DCollection::select(size_t i)
{
  if (i < size())
  {
    select(m_shapes[i]);
  }
}

/**
 * Make a shape current.
 * @param shape :: Pointer to a shape which is to become current. The shape must be in the collection.
 */
void Shape2DCollection::select(Shape2D* shape)
{
  if (m_currentShape)
  {
    m_currentShape->edit(false);
  }
  m_currentShape = shape;
  m_currentShape->edit(true);
  emit shapeSelected();
}

/**
 * Checks if the screen point (x,y) is inside the current shape.
 */
bool Shape2DCollection::isOverCurrentAt(int x,int y)
{
  if (!m_currentShape) return false;
  QPointF p = m_transform.inverted().map(QPointF(x,y));
  return m_currentShape->selectAt(p);
}

bool Shape2DCollection::selectControlPointAt(int x,int y)
{
  //QPointF p = m_transform.inverted().map(QPointF(x,y));
  QPointF p = QPointF(x,y);
  if (!m_currentShape) return false;
  for(size_t i = 0; i < m_currentShape->getNControlPoints(); ++i)
  {
    //QPointF cp = m_currentShape->getControlPoint(i) - p;
    QPointF cp = m_transform.map(m_currentShape->getControlPoint(i)) - p;
    if (fabs(cp.x()) + fabs(cp.y()) <= sizeCP + 2)
    //if (cp.manhattanLength() <= sizeCP + 2)
    {
      m_currentCP = i;
      return true;
    }
  }
  return false;
}

void Shape2DCollection::removeCurrentShape()
{
  if (m_currentShape)
  {
    m_shapes.removeOne(m_currentShape);
    m_currentShape = NULL;
    emit shapesDeselected();
  }
}

void Shape2DCollection::clear()
{
  m_shapes.clear();
  m_currentShape = NULL;
  emit shapesDeselected();
}

QStringList Shape2DCollection::getCurrentDoubleNames()const
{
  if (m_currentShape)
  {
    return m_currentShape->getDoubleNames();
  }
  return QStringList();
}

double Shape2DCollection::getCurrentDouble(const QString& prop) const
{
  if (m_currentShape)
  {
    return m_currentShape->getDouble(prop);
  }
  return 0.0;
}

void Shape2DCollection::setCurrentDouble(const QString& prop, double value)
{
  if (m_currentShape)
  {
    return m_currentShape->setDouble(prop,value);
  }
}

QStringList Shape2DCollection::getCurrentPointNames()const
{
  if (m_currentShape)
  {
    return m_currentShape->getPointNames();
  }
  return QStringList();
}

QPointF Shape2DCollection::getCurrentPoint(const QString& prop) const
{
  if (m_currentShape)
  {
    //return m_transform.map(m_currentShape->getPoint(prop));
    return m_currentShape->getPoint(prop);
  }
  return QPointF();
}

void Shape2DCollection::setCurrentPoint(const QString& prop, const QPointF& value)
{
  if (m_currentShape)
  {
    //return m_currentShape->setPoint(prop,m_transform.inverted().map(value));
    return m_currentShape->setPoint(prop,value);
  }
}

QRectF Shape2DCollection::getCurrentBoundingRect()const
{
  if (m_currentShape)
  {
    //return m_transform.mapRect(m_currentShape->getBoundingRect());
    return m_currentShape->getBoundingRect();
  }
  return QRectF();
}

void Shape2DCollection::setCurrentBoundingRect(const QRectF& rect)
{
  if (m_currentShape)
  {
    //m_currentShape->setBoundingRect(m_transform.inverted().mapRect(rect));
    m_currentShape->setBoundingRect(rect);
  }
}

bool Shape2DCollection::isMasked(double x,double y)const
{
  QPointF p;
  p.rx() = (x - m_windowRect.left()) * m_wx;
  p.ry() = m_h - (y - m_windowRect.top()) * m_wy;
  foreach(Shape2D* shape,m_shapes)
  {
    if (shape->isMasked(p))
    {
      return true;
    }
  }
  return false;
}

void Shape2DCollection::getMaskedPixels(QList<QPoint>& pixels)const
{
  pixels.clear();
  QTransform inv = m_transform.inverted();
  for(int i = m_viewport.left(); i <= m_viewport.right(); ++i)
  {
    for(int j = m_viewport.top(); j <= m_viewport.bottom(); ++j)
    {
      QPoint p = QPoint(i,j);
      QPointF p0 = inv.map(p);
      foreach(Shape2D* shape,m_shapes)
      {
        if (shape->isMasked(p0))
        {
          pixels.append(p);
        }
      }
    }
  }
}

/**
 * Set the bounding rect of the current shape in real coordinates.
 */
void Shape2DCollection::setCurrentBoundingRectReal(const QRectF& rect)
{
  if (!m_currentShape) return;
  // convert rect from real to original screen coordinates
  double x = (rect.x() - m_windowRect.left()) * m_wx;
  double y = m_h - (rect.bottom() - m_windowRect.y()) * m_wy;
  double width = rect.width() * m_wx;
  double height = rect.height() * m_wy;
  
  m_currentShape->setBoundingRect(QRectF(x,y,width,height));
}

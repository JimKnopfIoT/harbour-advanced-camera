/*
  harbour-advanced-camera-ext — HistogramItem
  Copyright (C) 2026  harbour-advanced-camera-ext contributors — GPLv2 or later.

  Live RGB + luma histogram overlay, fed by small grabToImage() results of
  the viewfinder (see CameraUI.qml).
*/
#ifndef HISTOGRAMITEM_H
#define HISTOGRAMITEM_H

#include <QQuickPaintedItem>
#include <QVector>

class HistogramItem : public QQuickPaintedItem
{
    Q_OBJECT
    // Fraction of pixels in the top / bottom luma bin
    Q_PROPERTY(qreal clippedHighlights READ clippedHighlights NOTIFY histogramChanged)
    Q_PROPERTY(qreal clippedShadows READ clippedShadows NOTIFY histogramChanged)
    Q_PROPERTY(bool hasData READ hasData NOTIFY histogramChanged)

public:
    explicit HistogramItem(QQuickItem *parent = nullptr);

    // Result object of Item.grabToImage()
    Q_INVOKABLE void updateFromGrab(QObject *grabResult);
    Q_INVOKABLE void clear();

    qreal clippedHighlights() const { return m_clipHi; }
    qreal clippedShadows() const { return m_clipLo; }
    bool hasData() const { return m_hasData; }

    void paint(QPainter *painter) override;

signals:
    void histogramChanged();

private:
    static const int kBins = 64;
    QVector<int> m_r, m_g, m_b, m_y;
    int m_max = 1;
    qreal m_clipHi = 0;
    qreal m_clipLo = 0;
    bool m_hasData = false;
};

#endif // HISTOGRAMITEM_H

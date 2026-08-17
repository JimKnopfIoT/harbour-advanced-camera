/*
  harbour-advanced-camera-ext — HistogramItem
  Copyright (C) 2026  harbour-advanced-camera-ext contributors — GPLv2 or later.
*/
#include "histogramitem.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QVariant>
#include <cmath>

HistogramItem::HistogramItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_r(kBins, 0), m_g(kBins, 0), m_b(kBins, 0), m_y(kBins, 0)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAntialiasing(true);
}

void HistogramItem::clear()
{
    m_r.fill(0); m_g.fill(0); m_b.fill(0); m_y.fill(0);
    m_max = 1;
    m_clipHi = m_clipLo = 0;
    m_hasData = false;
    emit histogramChanged();
    update();
}

void HistogramItem::updateFromGrab(QObject *grabResult)
{
    if (!grabResult)
        return;
    QImage img = grabResult->property("image").value<QImage>();
    if (img.isNull())
        return;
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32
        && img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_RGB32);

    m_r.fill(0); m_g.fill(0); m_b.fill(0); m_y.fill(0);
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            const int r = qRed(p), g = qGreen(p), b = qBlue(p);
            const int l = (r * 77 + g * 150 + b * 29) >> 8;
            m_r[r * kBins >> 8]++;
            m_g[g * kBins >> 8]++;
            m_b[b * kBins >> 8]++;
            m_y[l * kBins >> 8]++;
        }
    }
    const int total = qMax(1, w * h);
    m_clipHi = qreal(m_y[kBins - 1]) / total;
    m_clipLo = qreal(m_y[0]) / total;

    m_max = 1;
    for (int i = 0; i < kBins; ++i)
        m_max = qMax(m_max, qMax(qMax(m_r[i], m_g[i]), qMax(m_b[i], m_y[i])));
    m_hasData = true;
    emit histogramChanged();
    update();
}

void HistogramItem::paint(QPainter *p)
{
    const qreal W = width(), H = height();
    p->setRenderHint(QPainter::Antialiasing, true);

    // Backdrop
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, 140));
    p->drawRoundedRect(QRectF(0, 0, W, H), 4, 4);

    if (!m_hasData)
        return;

    // Mild gamma keeps the dark tail visible without a log scale.
    auto binH = [this, H](int count) {
        return H * 0.92 * std::pow(qreal(count) / m_max, 0.6);
    };
    auto path = [&](const QVector<int> &v) {
        QPainterPath pp;
        const qreal step = W / kBins;
        pp.moveTo(0, H);
        for (int i = 0; i < kBins; ++i)
            pp.lineTo(i * step + step / 2, H - binH(v[i]));
        pp.lineTo(W, H);
        pp.closeSubpath();
        return pp;
    };

    p->setCompositionMode(QPainter::CompositionMode_Plus);
    p->setBrush(QColor(220, 40, 40, 150)); p->drawPath(path(m_r));
    p->setBrush(QColor(40, 200, 40, 150)); p->drawPath(path(m_g));
    p->setBrush(QColor(60, 90, 255, 150)); p->drawPath(path(m_b));
    p->setCompositionMode(QPainter::CompositionMode_SourceOver);
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(QColor(255, 255, 255, 220), 1.5));
    p->drawPath(path(m_y));

    // Clipping bars at >1 %
    if (m_clipHi > 0.01) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 60, 60, 230));
        p->drawRect(QRectF(W - 4, 0, 4, H));
    }
    if (m_clipLo > 0.01) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(80, 140, 255, 230));
        p->drawRect(QRectF(0, 0, 4, H));
    }
    p->setPen(QPen(QColor(255, 255, 255, 90), 1));
    p->drawLine(QPointF(W / 2, H - 6), QPointF(W / 2, H));
}

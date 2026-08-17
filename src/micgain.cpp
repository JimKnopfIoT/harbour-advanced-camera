/*
  harbour-advanced-camera-ext — MicGain
  Copyright (C) 2026  harbour-advanced-camera-ext contributors — GPLv2 or later.
*/
#include "micgain.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#include <glib.h>
#include <unistd.h>

#include <cstring>

namespace {

// Marks the persist() stream so it is not mistaken for a recording.
const char *kPhantomProp = "harbour-advanced-camera-ext.phantom";

pa_cvolume volumeFor(int percent, uint8_t channels)
{
    pa_cvolume v;
    pa_cvolume_init(&v);
    pa_cvolume_set(&v, channels ? channels : 1,
                   (pa_volume_t)(PA_VOLUME_NORM * (double)percent / 100.0));
    return v;
}

// Same client name as gst pulsesrc in this process (gst_pulse_client_name),
// so all our streams share one stream-restore id.
const char *clientName()
{
    return g_get_application_name(); // null: libpulse falls back like gst does
}

} // namespace

MicGain::MicGain(QObject *parent)
    : QObject(parent)
    , m_pid(QByteArray::number((qlonglong)getpid()))
{
    m_sinceStart.start();
    m_persistTimer.setSingleShot(true);
    m_persistTimer.setInterval(700);
    connect(&m_persistTimer, &QTimer::timeout, this, &MicGain::persist);
    ensureContext();
}

MicGain::~MicGain()
{
    if (!m_ml)
        return;
    pa_threaded_mainloop_lock(m_ml);
    dropPhantom();
    if (m_ctx) {
        pa_context_set_state_callback(m_ctx, nullptr, nullptr);
        pa_context_set_subscribe_callback(m_ctx, nullptr, nullptr);
        pa_context_disconnect(m_ctx);
        pa_context_unref(m_ctx);
        m_ctx = nullptr;
    }
    pa_threaded_mainloop_unlock(m_ml);
    pa_threaded_mainloop_stop(m_ml);
    pa_threaded_mainloop_free(m_ml);
    m_ml = nullptr;
}

// ---------------------------------------------------------------- GUI thread

void MicGain::ensureContext()
{
    if (m_ctx) {
        pa_threaded_mainloop_lock(m_ml);
        pa_context_state_t st = pa_context_get_state(m_ctx);
        pa_threaded_mainloop_unlock(m_ml);
        if (PA_CONTEXT_IS_GOOD(st))
            return;
        // Dead context (PulseAudio restarted): rebuild.
        pa_threaded_mainloop_lock(m_ml);
        dropPhantom();
        pa_context_set_state_callback(m_ctx, nullptr, nullptr);
        pa_context_set_subscribe_callback(m_ctx, nullptr, nullptr);
        pa_context_disconnect(m_ctx);
        pa_context_unref(m_ctx);
        m_ctx = nullptr;
        m_paTracked.clear();
        pa_threaded_mainloop_unlock(m_ml);
        pa_threaded_mainloop_stop(m_ml);
        pa_threaded_mainloop_free(m_ml);
        m_ml = nullptr;
    }
    m_ready = false;
    if (!m_tracked.isEmpty()) {
        m_tracked.clear();
        emit recordingChanged();
    }

    m_ml = pa_threaded_mainloop_new();
    m_ctx = pa_context_new(pa_threaded_mainloop_get_api(m_ml), clientName());
    pa_context_set_state_callback(m_ctx, contextStateCb, this);
    pa_context_set_subscribe_callback(m_ctx, subscribeCb, this);

    pa_threaded_mainloop_start(m_ml);
    pa_threaded_mainloop_lock(m_ml);
    if (pa_context_connect(m_ctx, nullptr, PA_CONTEXT_NOFAIL, nullptr) < 0) {
        m_error = QStringLiteral("PulseAudio: %1")
                      .arg(QString::fromUtf8(pa_strerror(pa_context_errno(m_ctx))));
        emit errorChanged();
    }
    pa_threaded_mainloop_unlock(m_ml);
}

void MicGain::setGain(int percent)
{
    if (percent < 10)
        percent = 10;
    if (percent > 1000)
        percent = 1000;
    if (percent == m_gain)
        return;
    m_gain = percent;
    emit gainChanged();

    ensureContext();
    pa_threaded_mainloop_lock(m_ml);
    if (m_ready) {
        for (uint32_t idx : m_paTracked)
            applyGainTo(idx);
    }
    pa_threaded_mainloop_unlock(m_ml);

    // Debounce slider drags; skip the startup restore from settings (the
    // live path covers that, no need to touch the mic).
    if (m_sinceStart.elapsed() > 4000)
        m_persistTimer.start();
}

void MicGain::persist()
{
    ensureContext();
    pa_threaded_mainloop_lock(m_ml);
    if (!m_paTracked.isEmpty()) { // live stream carries it already
        pa_threaded_mainloop_unlock(m_ml);
        return;
    }
    if (m_ready)
        startPhantom();
    else
        m_persistPending = true;
    pa_threaded_mainloop_unlock(m_ml);
}

void MicGain::trackedChanged(uint idx, bool present)
{
    const bool was = recording();
    if (present)
        m_tracked.insert(idx);
    else
        m_tracked.remove(idx);
    if (was != recording())
        emit recordingChanged();
    if (!recording() && m_liveGain != 0) {
        m_liveGain = 0;
        emit liveGainChanged();
    }
}

void MicGain::scheduleReconnect()
{
    QTimer::singleShot(3000, this, [this]() { ensureContext(); });
}

void MicGain::liveGainRead(int percent)
{
    if (percent == m_liveGain)
        return;
    m_liveGain = percent;
    emit liveGainChanged();
}

void MicGain::reportError(const QString &msg)
{
    qWarning() << "MicGain:" << msg;
    if (msg == m_error)
        return;
    m_error = msg;
    emit errorChanged();
}

void MicGain::reportInfo(const QString &msg)
{
    qInfo() << "MicGain:" << msg;
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorChanged();
    }
}

// ------------------------------------------------ PulseAudio thread (locked)

void MicGain::contextStateCb(pa_context *c, void *ud)
{
    MicGain *self = static_cast<MicGain *>(ud);
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY:
        self->onContextReady();
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        self->m_ready = false;
        self->m_paTracked.clear();
        QMetaObject::invokeMethod(self, "reportError", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("PulseAudio: %1")
                                                     .arg(QString::fromUtf8(pa_strerror(
                                                         pa_context_errno(c))))));
        QMetaObject::invokeMethod(self, "scheduleReconnect", Qt::QueuedConnection);
        break;
    default:
        break;
    }
}

void MicGain::onContextReady()
{
    m_ready = true;
    QMetaObject::invokeMethod(this, "reportInfo", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("connected as \"%1\"")
                                                 .arg(QString::fromUtf8(clientName()))));
    pa_operation *o = pa_context_subscribe(m_ctx, PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT,
                                           nullptr, nullptr);
    if (o)
        pa_operation_unref(o);
    // Streams that already exist (context rebuilt mid-recording)
    o = pa_context_get_source_output_info_list(m_ctx, sourceOutputInfoCb, this);
    if (o)
        pa_operation_unref(o);
    if (m_persistPending) {
        m_persistPending = false;
        startPhantom();
    }
}

void MicGain::subscribeCb(pa_context *c, pa_subscription_event_type_t t,
                          uint32_t idx, void *ud)
{
    MicGain *self = static_cast<MicGain *>(ud);
    if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) != PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT)
        return;
    const pa_subscription_event_type_t type = pa_subscription_event_type_t(
        t & PA_SUBSCRIPTION_EVENT_TYPE_MASK);
    if (type == PA_SUBSCRIPTION_EVENT_REMOVE) {
        if (self->m_paTracked.remove(idx)) {
            QMetaObject::invokeMethod(self, "trackedChanged", Qt::QueuedConnection,
                                      Q_ARG(uint, idx), Q_ARG(bool, false));
        }
        return;
    }
    // NEW or CHANGE (the latter also for our own volume set: read-back)
    pa_operation *o = pa_context_get_source_output_info(c, idx, sourceOutputInfoCb, self);
    if (o)
        pa_operation_unref(o);
}

void MicGain::sourceOutputInfoCb(pa_context *, const pa_source_output_info *i,
                                 int eol, void *ud)
{
    if (eol || !i)
        return;
    static_cast<MicGain *>(ud)->handleSourceOutput(i);
}

void MicGain::handleSourceOutput(const pa_source_output_info *i)
{
    const char *pid = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_PROCESS_ID);
    if (!pid || m_pid != pid || pa_proplist_contains(i->proplist, kPhantomProp))
        return;

    const int percent = i->volume.channels
                            ? (int)((i->volume.values[0] * 100.0 / PA_VOLUME_NORM) + 0.5)
                            : 0;

    if (!m_paTracked.contains(i->index)) {
        m_paTracked.insert(i->index);
        const char *name = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
        QMetaObject::invokeMethod(this, "reportInfo", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("record stream #%1 of \"%2\" at %3 %, applying %4 %")
                                                     .arg(i->index)
                                                     .arg(QString::fromUtf8(name ? name : "?"))
                                                     .arg(percent)
                                                     .arg(m_gain)));
        QMetaObject::invokeMethod(this, "trackedChanged", Qt::QueuedConnection,
                                  Q_ARG(uint, i->index), Q_ARG(bool, true));
        if (percent != m_gain)
            applyGainTo(i->index);
        else
            QMetaObject::invokeMethod(this, "liveGainRead", Qt::QueuedConnection,
                                      Q_ARG(int, percent));
    } else {
        QMetaObject::invokeMethod(this, "liveGainRead", Qt::QueuedConnection,
                                  Q_ARG(int, percent));
    }
}

void MicGain::applyGainTo(uint32_t idx)
{
    pa_cvolume v = volumeFor(m_gain, 1);
    pa_operation *o = pa_context_set_source_output_volume(m_ctx, idx, &v, nullptr, nullptr);
    if (o)
        pa_operation_unref(o);
}

void MicGain::startPhantom()
{
    if (m_phantom)
        return; // in flight; applies m_gain when READY

    pa_proplist *pl = pa_proplist_new();
    pa_proplist_sets(pl, kPhantomProp, "1");
    pa_sample_spec spec;
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = 48000;
    spec.channels = 1;
    m_phantom = pa_stream_new_with_proplist(m_ctx, "Record Stream", &spec, nullptr, pl);
    pa_proplist_free(pl);
    if (!m_phantom) {
        QMetaObject::invokeMethod(this, "reportError", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("PulseAudio: cannot create stream")));
        return;
    }
    pa_stream_set_state_callback(m_phantom, phantomStateCb, this);

    pa_buffer_attr attr;
    memset(&attr, 0xff, sizeof(attr));
    attr.fragsize = 960;
    if (pa_stream_connect_record(m_phantom, nullptr, &attr, PA_STREAM_NOFLAGS) < 0) {
        QMetaObject::invokeMethod(this, "reportError", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("PulseAudio: %1")
                                                     .arg(QString::fromUtf8(pa_strerror(
                                                         pa_context_errno(m_ctx))))));
        dropPhantom();
    }
}

void MicGain::phantomStateCb(pa_stream *s, void *ud)
{
    MicGain *self = static_cast<MicGain *>(ud);
    switch (pa_stream_get_state(s)) {
    case PA_STREAM_READY: {
        pa_cvolume v = volumeFor(self->m_gain, 1);
        pa_operation *o = pa_context_set_source_output_volume(
            self->m_ctx, pa_stream_get_index(s), &v, phantomVolumeSetCb, self);
        if (o)
            pa_operation_unref(o);
        break;
    }
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        QMetaObject::invokeMethod(self, "reportError", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("PulseAudio: %1")
                                                     .arg(QString::fromUtf8(pa_strerror(
                                                         pa_context_errno(self->m_ctx))))));
        self->dropPhantom();
        break;
    default:
        break;
    }
}

void MicGain::phantomVolumeSetCb(pa_context *, int success, void *ud)
{
    MicGain *self = static_cast<MicGain *>(ud);
    if (success)
        QMetaObject::invokeMethod(self, "reportInfo", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("stored %1 % for future recordings")
                                                     .arg(self->m_gain)));
    else
        QMetaObject::invokeMethod(self, "reportError", Qt::QueuedConnection,
                                  Q_ARG(QString, QStringLiteral("PulseAudio: storing the gain failed")));
    self->dropPhantom();
}

void MicGain::dropPhantom()
{
    if (!m_phantom)
        return;
    pa_stream_set_state_callback(m_phantom, nullptr, nullptr);
    pa_stream_disconnect(m_phantom);
    pa_stream_unref(m_phantom);
    m_phantom = nullptr;
}

/*
  harbour-advanced-camera-ext — MicGain
  Copyright (C) 2026  harbour-advanced-camera-ext contributors — GPLv2 or later.

  Microphone gain for video recordings: the PulseAudio volume of this
  process's record stream. Some ports (Xperia 10 III) record 20–25 dB too
  quiet because of the Android HAL's camcorder input gain; the stream volume
  compensates. Applied the moment the stream appears, live while recording,
  and persisted by module-stream-restore-nemo under the app's name.
*/
#ifndef MICGAIN_H
#define MICGAIN_H

#include <QObject>
#include <QElapsedTimer>
#include <QSet>
#include <QString>
#include <QTimer>

#include <pulse/pulseaudio.h>

class MicGain : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int gain READ gain WRITE setGain NOTIFY gainChanged) // percent, 100 = neutral
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged) // own record stream exists
    Q_PROPERTY(int liveGain READ liveGain NOTIFY liveGainChanged) // read back from it, 0 if none
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit MicGain(QObject *parent = nullptr);
    ~MicGain() override;

    int gain() const { return m_gain; }
    void setGain(int percent);
    bool recording() const { return !m_tracked.isEmpty(); }
    int liveGain() const { return m_liveGain; }
    QString error() const { return m_error; }

    // Store the gain for future recordings while none runs: opens a record
    // stream for a fraction of a second so stream-restore picks the value up.
    // Called automatically (debounced) after gain changes.
    Q_INVOKABLE void persist();

signals:
    void gainChanged();
    void recordingChanged();
    void liveGainChanged();
    void errorChanged();

private:
    // PulseAudio thread (mainloop locked)
    static void contextStateCb(pa_context *c, void *ud);
    static void subscribeCb(pa_context *c, pa_subscription_event_type_t t,
                            uint32_t idx, void *ud);
    static void sourceOutputInfoCb(pa_context *c, const pa_source_output_info *i,
                                   int eol, void *ud);
    static void phantomStateCb(pa_stream *s, void *ud);
    static void phantomVolumeSetCb(pa_context *c, int success, void *ud);

    void onContextReady();
    void handleSourceOutput(const pa_source_output_info *i);
    void applyGainTo(uint32_t idx);
    void startPhantom();
    void dropPhantom();

    // GUI thread
    void ensureContext();
    Q_INVOKABLE void trackedChanged(uint idx, bool present);
    Q_INVOKABLE void scheduleReconnect();
    Q_INVOKABLE void liveGainRead(int percent);
    Q_INVOKABLE void reportError(const QString &msg);
    Q_INVOKABLE void reportInfo(const QString &msg);

    pa_threaded_mainloop *m_ml = nullptr;
    pa_context *m_ctx = nullptr;
    pa_stream *m_phantom = nullptr;
    bool m_ready = false;
    bool m_persistPending = false;
    QByteArray m_pid;

    int m_gain = 100;
    QElapsedTimer m_sinceStart;
    QTimer m_persistTimer;
    int m_liveGain = 0;
    QSet<quint32> m_tracked;   // GUI thread
    QSet<quint32> m_paTracked; // PA thread
    QString m_error;
};

#endif // MICGAIN_H

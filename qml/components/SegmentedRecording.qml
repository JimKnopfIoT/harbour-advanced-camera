import QtQuick 2.0
import QtMultimedia 5.6

/*
  Pause / resume for a CameraRecorder that cannot pause (camerabin rejects
  QMediaRecorder::pause()). Every resume records a new segment file next to
  the final one (VID_x.mp4, VID_x.part2.mp4, ...); stop() joins them
  losslessly with the C++ VideoJoiner and swaps the result into place.
  If nothing was paused the single segment simply is the final file.

  Usage: set recorder / joiner / fs, call start(path) / pause() / resume() /
  stop(); finished(path) fires for every file that should show up in the
  gallery (the joined file, or the untouched segments if joining failed).
*/
Item {
    id: root
    visible: false

    property var recorder     // CameraRecorder (camera.videoRecorder)
    property var joiner       // VideoJoiner context property
    property var fs           // FSOperations context property

    readonly property bool recording: recorder.recorderStatus === CameraRecorder.RecordingStatus
    readonly property bool paused: _paused
    readonly property bool pausing: _pauseRequested   // stop of a segment in flight
    readonly property bool joining: joiner.busy
    // Recorded time in ms across all segments.
    readonly property int elapsed: _elapsedBefore + (recording ? recorder.duration : 0)

    signal finished(string path)

    property var _segments: []
    property string _finalPath: ""
    property bool _paused: false
    property bool _pauseRequested: false
    property bool _finalizing: false
    property int _elapsedBefore: 0

    function start(path) {
        if (recording || _paused || joining)
            return
        _segments = []
        _elapsedBefore = 0
        _finalPath = path
        _startSegment()
    }

    function pause() {
        if (!recording || _pauseRequested)
            return
        _elapsedBefore += recorder.duration
        _pauseRequested = true
        recorder.stop()               // -> _segmentFinished()
    }

    function resume() {
        if (!_paused)
            return
        _paused = false
        _startSegment()
    }

    // Ends the recording: from a running segment (its completion triggers
    // the join) or from the paused state (segment already complete).
    function stop() {
        if (recording) {
            _pauseRequested = false
            recorder.stop()           // -> _segmentFinished() -> _finish()
        } else if (_paused) {
            _paused = false
            _finish()
        }
    }

    function _startSegment() {
        var n = _segments.length
        var path = n === 0 ? _finalPath
                           : _finalPath.replace(/\.mp4$/, "") + ".part" + (n + 1) + ".mp4"
        var segs = _segments.slice()
        segs.push(path)
        _segments = segs
        recorder.outputLocation = path
        recorder.record()
    }

    function _segmentFinished() {
        console.log("segment finished:", _segments[_segments.length - 1])
        if (_pauseRequested) {
            _pauseRequested = false
            _paused = true
            return
        }
        _finish()
    }

    function _finish() {
        _paused = false
        _pauseRequested = false
        if (_segments.length === 0)
            return
        if (_segments.length === 1) {
            finished(_finalPath)
            _segments = []
            return
        }
        joiner.join(_segments, _finalPath.replace(/\.mp4$/, "") + ".join.mp4")
    }

    Connections {
        target: recorder
        // A segment file is complete once the recorder leaves
        // FinalizingStatus (moov written).
        onRecorderStatusChanged: {
            if (recorder.recorderStatus === CameraRecorder.FinalizingStatus) {
                _finalizing = true
            } else if (_finalizing) {
                _finalizing = false
                _segmentFinished()
            }
        }
    }

    Connections {
        target: joiner
        onFinished: {
            if (ok) {
                for (var i = 0; i < _segments.length; ++i)
                    fs.deleteFile(_segments[i])
                joiner.renameFile(output, _finalPath)
                root.finished(_finalPath)
            } else {
                // Keep every segment: nothing is lost, the parts are playable.
                console.warn("join failed:", error)
                fs.deleteFile(output)
                for (var j = 0; j < _segments.length; ++j)
                    root.finished(_segments[j])
            }
            _segments = []
        }
    }
}

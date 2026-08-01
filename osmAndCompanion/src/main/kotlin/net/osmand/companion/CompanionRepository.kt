package net.osmand.companion

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class RecordingState {
    STOPPED,
    RUNNING,
    PAUSED_AUTO,
    PAUSED_MANUAL
}

object CompanionRepository {
    private val _osmandConnected = MutableStateFlow(false)
    val osmandConnected: StateFlow<Boolean> = _osmandConnected.asStateFlow()

    private val _pebbleConnected = MutableStateFlow(false)
    val pebbleConnected: StateFlow<Boolean> = _pebbleConnected.asStateFlow()

    private val _heartRate = MutableStateFlow(0)
    val heartRate: StateFlow<Int> = _heartRate.asStateFlow()

    private val _recordingState = MutableStateFlow(RecordingState.STOPPED)
    val recordingState: StateFlow<RecordingState> = _recordingState.asStateFlow()

    private val _speed = MutableStateFlow(0f)
    val speed: StateFlow<Float> = _speed.asStateFlow()

    fun setOsmAndConnected(
        connected: Boolean
    ) {
        _osmandConnected.value = connected
    }
    fun setPebbleConnected(
        connected: Boolean
    ) {
        _pebbleConnected.value = connected
    }
    fun setHeartRate(
        hr: Int
    ) {
        _heartRate.value = hr
    }
    fun setRecordingState(
        state: RecordingState
    ) {
        _recordingState.value = state
    }
    fun setSpeed(
        speed: Float
    ) {
        _speed.value = speed
    }
}

package net.osmand.companion

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

object CompanionRepository {
    private val _osmandConnected = MutableStateFlow(false)
    val osmandConnected: StateFlow<Boolean> = _osmandConnected.asStateFlow()

    private val _pebbleConnected = MutableStateFlow(false)
    val pebbleConnected: StateFlow<Boolean> = _pebbleConnected.asStateFlow()

    private val _heartRate = MutableStateFlow(0)
    val heartRate: StateFlow<Int> = _heartRate.asStateFlow()

    private val _isRecording = MutableStateFlow(false)
    val isRecording: StateFlow<Boolean> = _isRecording.asStateFlow()

    fun setOsmAndConnected(connected: Boolean) { _osmandConnected.value = connected }
    fun setPebbleConnected(connected: Boolean) { _pebbleConnected.value = connected }
    fun setHeartRate(hr: Int) { _heartRate.value = hr }
    fun setRecording(recording: Boolean) { _isRecording.value = recording }
}

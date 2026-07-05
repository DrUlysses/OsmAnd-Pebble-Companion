package net.osmand.companion

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach

class MainViewModel : ViewModel() {
    private val _uiState = MutableStateFlow(MainUiState())
    val uiState: StateFlow<MainUiState> = _uiState.asStateFlow()

    init {
        CompanionRepository.osmandConnected.onEach {
            _uiState.value = _uiState.value.copy(osmandConnected = it)
        }.launchIn(viewModelScope)

        CompanionRepository.pebbleConnected.onEach {
            _uiState.value = _uiState.value.copy(pebbleConnected = it)
        }.launchIn(viewModelScope)

        CompanionRepository.heartRate.onEach {
            _uiState.value = _uiState.value.copy(heartRate = it)
        }.launchIn(viewModelScope)
        
        CompanionRepository.isRecording.onEach {
            _uiState.value = _uiState.value.copy(isRecording = it)
        }.launchIn(viewModelScope)
    }

    data class MainUiState(
        val isServiceRunning: Boolean = false,
        val osmandConnected: Boolean = false,
        val pebbleConnected: Boolean = false,
        val heartRate: Int = 0,
        val isRecording: Boolean = false
    )

    fun setServiceRunning(running: Boolean) {
        _uiState.value = _uiState.value.copy(isServiceRunning = running)
    }
}

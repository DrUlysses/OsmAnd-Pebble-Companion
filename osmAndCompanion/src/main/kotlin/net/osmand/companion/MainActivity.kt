package net.osmand.companion

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

class MainActivity : ComponentActivity() {
    private val viewModel: MainViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        checkPermissions()
        setContent {
            MaterialTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    MainScreen(
                        viewModel = viewModel,
                        onStartService = ::startCompanionService,
                        onStopService = ::stopCompanionService
                    )
                }
            }
        }
    }

    private fun checkPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val permissions = arrayOf(
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.BLUETOOTH_SCAN
            )
            val neededPermissions = permissions.filter {
                checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED
            }
            if (neededPermissions.isNotEmpty()) {
                requestPermissions(neededPermissions.toTypedArray(), 1)
            }
        }
    }

    private fun startCompanionService() {
        val intent = Intent(this, CompanionService::class.java)
        startForegroundService(intent)
        viewModel.setServiceRunning(true)
    }

    private fun stopCompanionService() {
        val intent = Intent(this, CompanionService::class.java)
        stopService(intent)
        viewModel.setServiceRunning(false)
    }
}

@Composable
fun MainScreen(
    viewModel: MainViewModel,
    onStartService: () -> Unit,
    onStopService: () -> Unit
) {
    val uiState by viewModel.uiState.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(
            text = "OsmAnd Pebble Companion",
            style = MaterialTheme.typography.headlineMedium
        )
        Spacer(modifier = Modifier.height(32.dp))
        
        StatusItem("Service", if (uiState.isServiceRunning) "Running" else "Stopped")
        StatusItem("OsmAnd", if (uiState.osmandConnected) "Connected" else "Disconnected")
        StatusItem("Pebble", if (uiState.pebbleConnected) "Connected" else "Disconnected")
        StatusItem("Heart Rate", "${uiState.heartRate} bpm")
        StatusItem("Recording", if (uiState.isRecording) "On" else "Off")
        
        Spacer(modifier = Modifier.height(32.dp))
        
        if (!uiState.isServiceRunning) {
            Button(onClick = onStartService) {
                Text("Start Service")
            }
        } else {
            Button(
                onClick = onStopService,
                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
            ) {
                Text("Stop Service")
            }
        }
    }
}

@Composable
fun StatusItem(label: String, status: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyLarge)
        Text(text = status, style = MaterialTheme.typography.bodyLarge)
    }
}

package net.osmand.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.util.Log
import android.view.KeyEvent
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.getpebble.android.kit.PebbleKit
import com.getpebble.android.kit.util.PebbleDictionary
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import net.osmand.aidlapi.IOsmAndAidlCallback
import net.osmand.aidlapi.IOsmAndAidlInterface
import net.osmand.aidlapi.customization.PreferenceParams
import net.osmand.aidlapi.gpx.AGpxBitmap
import net.osmand.aidlapi.gpx.StartGpxRecordingParams
import net.osmand.aidlapi.logcat.OnLogcatMessageParams
import net.osmand.aidlapi.navigation.ADirectionInfo
import net.osmand.aidlapi.navigation.ANavigationUpdateParams
import net.osmand.aidlapi.navigation.OnVoiceNavigationParams
import net.osmand.aidlapi.plugins.PluginParams
import net.osmand.aidlapi.search.SearchResult
import kotlin.time.Duration.Companion.milliseconds

class CompanionService : Service(), OsmAndHelper.OsmAndConnectionListener, PebbleConnector.PebbleMessageListener, LocationListener {
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private lateinit var osmandHelper: OsmAndHelper
    private lateinit var pebbleConnector: PebbleConnector
    private var recordingState = RecordingState.STOPPED
    private var locationManager: LocationManager? = null

    private var lastInstruction: String = "Disconnected"
    private var lastDistance: String = "---"
    private var lastSpeed: Float = 0f
    private var lastTurnType: Int = 0

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "CompanionService Created")

        osmandHelper = OsmAndHelper(this, this)
        pebbleConnector = PebbleConnector(this, this)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(
                NOTIFICATION_ID,
                createNotification(),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
            )
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                createNotification(),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
            )
        } else {
            startForeground(
                NOTIFICATION_ID,
                createNotification()
            )
        }

        locationManager = getSystemService(LOCATION_SERVICE) as LocationManager
        if (ContextCompat.checkSelfPermission(this, android.Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED) {
            locationManager?.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, this)
        }

        osmandHelper.bind()
        pebbleConnector.connect()
    }

    override fun onDestroy() {
        Log.d(TAG, "CompanionService Destroying")
        serviceScope.cancel()
        locationManager?.removeUpdates(this)
        osmandHelper.unbind()
        pebbleConnector.disconnect()
        super.onDestroy()
        Log.d(TAG, "CompanionService Destroyed")
    }

    override fun onLocationChanged(location: Location) {
        lastSpeed = location.speed
        CompanionRepository.setSpeed(lastSpeed)
        handleAutoPause(lastSpeed)
        sendStateToPebble()
    }

    override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}
    override fun onProviderEnabled(provider: String) {}
    override fun onProviderDisabled(provider: String) {}

    private fun handleAutoPause(speed: Float) {
        if (recordingState == RecordingState.RUNNING && speed <= 0.1f) {
            Log.i(TAG, "Auto-pausing recording (speed=0)")
            serviceScope.launch {
                setOsmAndRecordingPreference(false)
                recordingState = RecordingState.PAUSED_AUTO
                CompanionRepository.setRecordingState(recordingState)
                sendStateToPebble()
            }
        } else if (recordingState == RecordingState.PAUSED_AUTO && speed > 0.5f) {
            Log.i(TAG, "Auto-resuming recording (speed > 0)")
            serviceScope.launch {
                setOsmAndRecordingPreference(true)
                recordingState = RecordingState.RUNNING
                CompanionRepository.setRecordingState(recordingState)
                sendStateToPebble()
            }
        }
    }

    private suspend fun setOsmAndRecordingPreference(enabled: Boolean) {
        val aidl = osmandHelper.getInterface() ?: return
        try {
            val params = PreferenceParams("save_global_track_to_gpx")
            params.value = enabled.toString()
            aidl.setPreference(params)
        } catch (e: Exception) {
            Log.e(TAG, "Error setting recording preference", e)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onConnected(osmandAidlInterface: IOsmAndAidlInterface) {
        Log.i(TAG, "Connected to OsmAnd")
        CompanionRepository.setOsmAndConnected(true)
        
        serviceScope.launch {
            try {
                syncNavInfo(osmandAidlInterface)
            } catch (e: Exception) {
                Log.e(TAG, "Error getting initial app info", e)
                lastInstruction = "Connected to OsmAnd"
                lastDistance = "---"
            }
            
            PebbleKit.startAppOnPebble(applicationContext, pebbleConnector.getAppUuid())
            syncRecordingState(osmandAidlInterface)
            sendStateToPebble()
        }

        try {
            osmandAidlInterface.registerForNavigationUpdates(
                ANavigationUpdateParams(),
                aidlCallback
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error registering for nav updates", e)
        }
    }

    override fun onDisconnected() {
        Log.i(TAG, "Disconnected from OsmAnd")
        CompanionRepository.setOsmAndConnected(false)
    }

    private val aidlCallback = object : IOsmAndAidlCallback.Stub() {
        override fun onSearchComplete(resultSet: MutableList<SearchResult>?) {
            Log.d(TAG, "onSearchComplete: ${resultSet?.size ?: 0} results")
        }
        override fun onUpdate() {
            Log.d(TAG, "onUpdate")
        }
        override fun onAppInitialized() {
            Log.d(TAG, "onAppInitialized")
        }
        override fun onGpxBitmapCreated(bitmap: AGpxBitmap?) {
            Log.d(TAG, "onGpxBitmapCreated")
        }
        override fun updateNavigationInfo(directionInfo: ADirectionInfo?) {
            Log.d(TAG, "updateNavigationInfo: $directionInfo")
            directionInfo?.let { handleNavigationUpdate(it) }
        }

        override fun onContextMenuButtonClicked(buttonId: Int, pointId: String?, layerId: String?) {
            Log.d(TAG, "onContextMenuButtonClicked: buttonId=$buttonId")
        }
        override fun onVoiceRouterNotify(params: OnVoiceNavigationParams?) {
            Log.d(TAG, "onVoiceRouterNotify: ${params?.commands}")
        }
        override fun onLogcatMessage(params: OnLogcatMessageParams?) {}
        override fun onKeyEvent(event: KeyEvent?) {
            Log.d(TAG, "onKeyEvent: $event")
        }
    }

    private fun handleNavigationUpdate(info: ADirectionInfo) {
        val distance = info.distanceTo
        val turnType = info.turnType

        lastTurnType = turnType
        lastInstruction = mapTurnType(turnType)
        lastDistance = formatDistance(distance)

        Log.i(TAG, "Nav Update: $lastInstruction in $lastDistance (type=$lastTurnType)")
        sendStateToPebble()
    }

    private fun mapTurnType(type: Int): String {
        return when (type) {
            1 -> "↑" // Straight
            2 -> "←" // Left
            3 -> "↖" // Slight Left
            4 -> "↙" // Sharp Left
            5 -> "→" // Right
            6 -> "↗" // Slight Right
            7 -> "↘" // Sharp Right
            8 -> "↖" // Keep Left
            9 -> "↗" // Keep Right
            10 -> "↺" // U-Turn
            11 -> "↺" // U-Turn Right
            12 -> "✕" // Off Route
            13 -> "⟳" // Roundabout
            14 -> "⟲" // Roundabout Left
            else -> "?"
        }
    }

    private fun mapTurnTypeXml(xml: String): String {
        return when {
            xml == "C" -> "↑"
            xml == "TL" -> "←"
            xml == "TSLL" -> "↖"
            xml == "TSHL" -> "↙"
            xml == "TR" -> "→"
            xml == "TSLR" -> "↗"
            xml == "TSHR" -> "↘"
            xml == "KL" -> "↖"
            xml == "KR" -> "↗"
            xml == "TU" -> "↺"
            xml == "TRU" -> "↺"
            xml == "OFFR" -> "✕"
            xml.startsWith("RNDB") -> "⟳"
            xml.startsWith("RNLB") -> "⟲"
            else -> "?"
        }
    }

    private fun formatDistance(meters: Int): String {
        return if (meters >= 1000) {
            String.format("%.1f km", meters / 1000.0)
        } else {
            "$meters m"
        }
    }

    override fun onMessageReceived(data: PebbleDictionary) {
        serviceScope.launch {
            Log.i(TAG, "Message received from Pebble: $data")
            CompanionRepository.setPebbleConnected(true)

            if (data.getInteger(KEY_RECORDING_COMMAND) != null) {
                toggleGpxRecording()
            }
            
            if (data.getInteger(KEY_REFRESH_COMMAND) != null) {
                osmandHelper.getInterface()?.let { syncNavInfo(it) }
            }

            val heartRate = data.getInteger(KEY_HEALTH_HEART_RATE)
            if (heartRate != null) {
                Log.d(TAG, "Heart rate from Pebble: $heartRate")
                CompanionRepository.setHeartRate(heartRate.toInt())
            }
            
            // Always send full state back to keep Pebble in sync
            sendStateToPebble()
        }
    }

    private suspend fun syncNavInfo(aidl: IOsmAndAidlInterface) {
        try {
            val appInfo = aidl.getAppInfo()
            if (appInfo != null && appInfo.turnInfo != null) {
                val turnInfo = appInfo.turnInfo
                val turnTypeXml = turnInfo.getString("next_turn_type")
                if (turnTypeXml != null) {
                    lastTurnType = getTurnTypeFromXml(turnTypeXml)
                    lastInstruction = mapTurnTypeXml(turnTypeXml)
                    lastDistance = formatDistance(turnInfo.getInt("next_turn_distance"))
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error syncing nav info", e)
        }
    }

    private fun getTurnTypeFromXml(xml: String): Int {
        return when {
            xml == "C" -> 1
            xml == "TL" -> 2
            xml == "TSLL" -> 3
            xml == "TSHL" -> 4
            xml == "TR" -> 5
            xml == "TSLR" -> 6
            xml == "TSHR" -> 7
            xml == "KL" -> 8
            xml == "KR" -> 9
            xml == "TU" -> 10
            xml == "TRU" -> 11
            xml == "OFFR" -> 12
            xml.startsWith("RNDB") -> 13
            xml.startsWith("RNLB") -> 14
            else -> 0
        }
    }

    private suspend fun toggleGpxRecording() {
        val aidl = osmandHelper.getInterface() ?: return
        try {
            if (aidl.getAppInfo() == null) {
                Log.e(TAG, "Not authorized to control OsmAnd. Please enable this Plugin in OsmAnd settings.")
                return
            }
            if (recordingState == RecordingState.STOPPED) {
                val started = aidl.startGpxRecording(StartGpxRecordingParams())
                if (!started) {
                    Log.w(TAG, "Failed to start GPX recording, attempting to enable plugin...")
                    val pluginEnabled = aidl.changePluginState(PluginParams("osmand.monitoring", 1))
                    Log.i(TAG, "changePluginState(osmand.monitoring, 1) result: $pluginEnabled")
                    
                    // Wait for plugin to initialize
                    delay(500.milliseconds)
                    
                    if (aidl.startGpxRecording(StartGpxRecordingParams())) {
                        recordingState = RecordingState.RUNNING
                        Log.i(TAG, "Started GPX recording after enabling plugin")
                    } else {
                        Log.e(TAG, "Still failed to start GPX recording. Monitoring plugin might be inactive or locked.")
                    }
                } else {
                    recordingState = RecordingState.RUNNING
                    Log.i(TAG, "Started GPX recording")
                }
            } else if (recordingState == RecordingState.RUNNING || recordingState == RecordingState.PAUSED_AUTO) {
                // Manually pause if running
                setOsmAndRecordingPreference(false)
                recordingState = RecordingState.PAUSED_MANUAL
                Log.i(TAG, "Manually paused GPX recording")
            } else if (recordingState == RecordingState.PAUSED_MANUAL) {
                // Resume from manual pause
                setOsmAndRecordingPreference(true)
                recordingState = RecordingState.RUNNING
                Log.i(TAG, "Resumed GPX recording from manual pause")
            }
            CompanionRepository.setRecordingState(recordingState)
            sendStateToPebble()
        } catch (e: Exception) {
            Log.e(TAG, "Error toggling GPX recording", e)
        }
    }

    private fun syncRecordingState(aidl: IOsmAndAidlInterface) {
        serviceScope.launch {
            try {
                val appInfo = aidl.getAppInfo()
                if (appInfo == null) {
                    Log.w(TAG, "Cannot sync state: Not authorized")
                    return@launch
                }
                val params = PreferenceParams("save_global_track_to_gpx")
                if (aidl.getPreference(params)) {
                    val isRecording = params.value?.toBoolean() ?: false
                    recordingState = if (isRecording) RecordingState.RUNNING else RecordingState.STOPPED
                    CompanionRepository.setRecordingState(recordingState)
                    Log.i(TAG, "Synced recording state from OsmAnd: $recordingState")
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error syncing recording state", e)
            }
        }
    }

    private fun sendStateToPebble() {
        val dict = PebbleDictionary()
        dict.addString(KEY_NAV_INSTRUCTION, lastInstruction)
        dict.addString(KEY_NAV_DISTANCE, lastDistance)
        dict.addInt32(KEY_SPEED, (lastSpeed * 3.6f).toInt())
        dict.addInt32(KEY_NAV_TYPE, lastTurnType)
        val pebbleRecState = when (recordingState) {
            RecordingState.RUNNING -> 1
            RecordingState.PAUSED_AUTO, RecordingState.PAUSED_MANUAL -> 2
            RecordingState.STOPPED -> 0
        }
        dict.addInt32(KEY_RECORDING_STATE, pebbleRecState)
        pebbleConnector.sendData(dict)
    }

    private fun createNotification(): Notification {
        val channelId = "osm_pebble_companion"
        val channel = NotificationChannel(
            channelId,
            "OsmAnd Pebble Companion Service",
            NotificationManager.IMPORTANCE_LOW
        )
        val manager = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
        manager.createNotificationChannel(channel)

        return NotificationCompat.Builder(this, channelId)
            .setContentTitle("OsmAnd Pebble Companion")
            .setContentText("Relaying data between OsmAnd and Pebble")
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
    }

    companion object {
        private const val TAG = "CompanionService"
        private const val NOTIFICATION_ID = 1001

        private const val KEY_NAV_INSTRUCTION = 0
        private const val KEY_NAV_DISTANCE = 1
        private const val KEY_RECORDING_COMMAND = 2
        private const val KEY_HEALTH_HEART_RATE = 3
        private const val KEY_SPEED = 4
        private const val KEY_RECORDING_STATE = 5
        private const val KEY_REFRESH_COMMAND = 6
        private const val KEY_NAV_TYPE = 7
    }
}

package net.osmand.companion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.util.Log
import android.view.KeyEvent
import androidx.core.app.NotificationCompat
import com.getpebble.android.kit.PebbleKit
import com.getpebble.android.kit.util.PebbleDictionary
import kotlinx.coroutines.*
import net.osmand.aidlapi.IOsmAndAidlCallback
import net.osmand.aidlapi.IOsmAndAidlInterface
import net.osmand.aidlapi.customization.PreferenceParams
import net.osmand.aidlapi.info.AppInfoParams
import net.osmand.aidlapi.gpx.AGpxBitmap
import net.osmand.aidlapi.gpx.StartGpxRecordingParams
import net.osmand.aidlapi.gpx.StopGpxRecordingParams
import net.osmand.aidlapi.plugins.PluginParams
import net.osmand.aidlapi.logcat.OnLogcatMessageParams
import net.osmand.aidlapi.navigation.ADirectionInfo
import net.osmand.aidlapi.navigation.ANavigationUpdateParams
import net.osmand.aidlapi.navigation.OnVoiceNavigationParams
import net.osmand.aidlapi.search.SearchResult
import kotlin.time.Duration.Companion.milliseconds

class CompanionService : Service(), OsmAndHelper.OsmAndConnectionListener, PebbleConnector.PebbleMessageListener {
    private val serviceScope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private lateinit var osmandHelper: OsmAndHelper
    private lateinit var pebbleConnector: PebbleConnector
    private var isRecording = false

    private var lastInstruction: String = "Waiting for Nav..."
    private var lastDistance: String = "---"

    override fun onCreate() {
        super.onCreate()
        Log.d(TAG, "CompanionService Created")

        osmandHelper = OsmAndHelper(this, this)
        pebbleConnector = PebbleConnector(this, this)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                createNotification(),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            )
        } else {
            startForeground(
                NOTIFICATION_ID,
                createNotification()
            )
        }

        osmandHelper.bind()
        pebbleConnector.connect()
    }

    override fun onDestroy() {
        Log.d(TAG, "CompanionService Destroying")
        serviceScope.cancel()
        osmandHelper.unbind()
        pebbleConnector.disconnect()
        super.onDestroy()
        Log.d(TAG, "CompanionService Destroyed")
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onConnected(osmandAidlInterface: IOsmAndAidlInterface) {
        Log.i(TAG, "Connected to OsmAnd")
        CompanionRepository.setOsmAndConnected(true)
        
        lastInstruction = "Connected to OsmAnd"
        lastDistance = "---"
        
        PebbleKit.startAppOnPebble(applicationContext, pebbleConnector.getAppUuid())
        syncRecordingState(osmandAidlInterface)
        sendStateToPebble()

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

        lastInstruction = mapTurnType(turnType)
        lastDistance = formatDistance(distance)

        Log.i(TAG, "Nav Update: $lastInstruction in $lastDistance")
        sendStateToPebble()
    }

    private fun mapTurnType(type: Int): String {
        return when (type) {
            1 -> "Straight"
            2 -> "Left"
            3 -> "Slight Left"
            4 -> "Sharp Left"
            5 -> "Right"
            6 -> "Slight Right"
            7 -> "Sharp Right"
            8 -> "Keep Left"
            9 -> "Keep Right"
            10 -> "U-Turn"
            11 -> "U-Turn Right"
            12 -> "Off Route"
            13 -> "Roundabout"
            14 -> "Roundabout Left"
            else -> "Unknown"
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

            val heartRate = data.getInteger(KEY_HEALTH_HEART_RATE)
            if (heartRate != null) {
                Log.d(TAG, "Heart rate from Pebble: $heartRate")
                CompanionRepository.setHeartRate(heartRate.toInt())
            }
            
            // Always send full state back to keep Pebble in sync
            sendStateToPebble()
        }
    }

    private suspend fun toggleGpxRecording() {
        val aidl = osmandHelper.getInterface() ?: return
        try {
            if (aidl.appInfo == null) {
                Log.e(TAG, "Not authorized to control OsmAnd. Please enable this Plugin in OsmAnd settings.")
                return
            }
            if (!isRecording) {
                val started = aidl.startGpxRecording(StartGpxRecordingParams())
                if (!started) {
                    Log.w(TAG, "Failed to start GPX recording, attempting to enable plugin...")
                    val pluginEnabled = aidl.changePluginState(PluginParams("osmand.monitoring", 1))
                    Log.i(TAG, "changePluginState(osmand.monitoring, 1) result: $pluginEnabled")
                    
                    // ponytail: wait for plugin to initialize
                    delay(500.milliseconds)
                    
                    if (aidl.startGpxRecording(StartGpxRecordingParams())) {
                        isRecording = true
                        Log.i(TAG, "Started GPX recording after enabling plugin")
                    } else {
                        Log.e(TAG, "Still failed to start GPX recording. Monitoring plugin might be inactive or locked.")
                    }
                } else {
                    isRecording = true
                    Log.i(TAG, "Started GPX recording")
                }
            } else {
                if (aidl.stopGpxRecording(StopGpxRecordingParams())) {
                    isRecording = false
                    Log.i(TAG, "Stopped GPX recording")
                }
            }
            CompanionRepository.setRecording(isRecording)
            sendStateToPebble()
        } catch (e: Exception) {
            Log.e(TAG, "Error toggling GPX recording", e)
        }
    }

    private fun syncRecordingState(aidl: IOsmAndAidlInterface) {
        serviceScope.launch {
            try {
                if (aidl.appInfo == null) {
                    Log.w(TAG, "Cannot sync state: Not authorized")
                    return@launch
                }
                val params = PreferenceParams("save_global_track_to_gpx")
                if (aidl.getPreference(params)) {
                    isRecording = params.value?.toBoolean() ?: false
                    CompanionRepository.setRecording(isRecording)
                    Log.i(TAG, "Synced recording state from OsmAnd: $isRecording")
                } else {
                    Log.w(TAG, "Failed to sync recording state: preference 'save_global_track_to_gpx' not available via AIDL (likely global)")
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
        dict.addInt32(KEY_RECORDING_COMMAND, if (isRecording) 1 else 0)
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
    }
}

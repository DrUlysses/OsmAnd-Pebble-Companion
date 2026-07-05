package net.osmand.companion

import android.content.Context
import android.content.IntentFilter
import android.util.Log
import androidx.core.content.ContextCompat
import com.getpebble.android.kit.Constants
import com.getpebble.android.kit.PebbleKit
import com.getpebble.android.kit.util.PebbleDictionary
import java.util.UUID

class PebbleConnector(
    context: Context,
    private val listener: PebbleMessageListener
) {
    private val appContext = context.applicationContext
    interface PebbleMessageListener {
        fun onMessageReceived(data: PebbleDictionary)
    }

    private val appUuid = UUID.fromString("70c3d279-9746-4eb7-a608-1a06fb61d2e3")
    private var dataReceiver: PebbleKit.PebbleDataReceiver? = null

    fun connect() {
        Log.d(TAG, "Connecting to Pebble...")
        dataReceiver = object : PebbleKit.PebbleDataReceiver(appUuid) {
            override fun receiveData(
                context: Context,
                transactionId: Int,
                data: PebbleDictionary
            ) {
                PebbleKit.sendAckToPebble(context, transactionId)
                listener.onMessageReceived(data)
            }
        }
        val filter = IntentFilter(Constants.INTENT_APP_RECEIVE)
        ContextCompat.registerReceiver(
            appContext,
            dataReceiver!!,
            filter,
            ContextCompat.RECEIVER_EXPORTED
        )
    }

    fun disconnect() {
        dataReceiver?.let {
            try {
                appContext.unregisterReceiver(it)
            } catch (e: Exception) {
                Log.e(TAG, "Error unregistering receiver", e)
            }
            dataReceiver = null
        }
    }

    fun sendData(data: PebbleDictionary) {
        val connected = PebbleKit.isWatchConnected(appContext)
        Log.d(TAG, "sendData: connected=$connected, data=$data")
        if (connected) {
            PebbleKit.sendDataToPebble(
                appContext,
                appUuid,
                data
            )
        }
    }

    fun getAppUuid(): UUID = appUuid

    companion object {
        private const val TAG = "PebbleConnector"
    }
}

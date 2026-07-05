package net.osmand.companion

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import android.util.Log
import net.osmand.aidlapi.IOsmAndAidlInterface

class OsmAndHelper(
    context: Context,
    private val listener: OsmAndConnectionListener
) {
    private val appContext = context.applicationContext

    interface OsmAndConnectionListener {
        fun onConnected(osmandAidlInterface: IOsmAndAidlInterface)
        fun onDisconnected()
    }

    private var osmandAidlInterface: IOsmAndAidlInterface? = null
    private var isBindCalled = false

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(
            name: ComponentName?,
            service: IBinder?
        ) {
            Log.d(TAG, "OsmAnd Service Connected: $name")
            osmandAidlInterface = IOsmAndAidlInterface.Stub.asInterface(service)
            osmandAidlInterface?.let { listener.onConnected(it) }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            Log.d(TAG, "OsmAnd Service Disconnected: $name")
            osmandAidlInterface = null
            listener.onDisconnected()
        }

        override fun onBindingDied(name: ComponentName?) {
            Log.d(TAG, "OsmAnd Binding Died: $name")
            osmandAidlInterface = null
        }
    }

    fun bind() {
        if (isBindCalled) return
        
        val intent = Intent("net.osmand.aidl.OsmandAidlServiceV2")
        val packages = listOf("net.osmand.plus", "net.osmand")
        
        for (pkg in packages) {
            intent.setPackage(pkg)
            try {
                Log.d(TAG, "Attempting to bind to $pkg")
                if (
                    appContext.bindService(
                        intent,
                        connection,
                        Context.BIND_AUTO_CREATE
                    )
                ) {
                    isBindCalled = true
                    Log.i(TAG, "Binding attempt started for $pkg")
                    return
                }
            } catch (e: SecurityException) {
                Log.e(TAG, "SecurityException while binding to $pkg", e)
            }
        }
        Log.e(TAG, "Failed to bind to any OsmAnd Service")
    }

    fun unbind() {
        Log.d(TAG, "Unbind requested, isBindCalled = $isBindCalled")
        if (isBindCalled) {
            try {
                appContext.unbindService(connection)
                Log.i(TAG, "Successfully called unbindService")
            } catch (e: Exception) {
                Log.e(TAG, "Error during unbindService", e)
            } finally {
                isBindCalled = false
                osmandAidlInterface = null
            }
        }
    }

    fun getInterface(): IOsmAndAidlInterface? = osmandAidlInterface

    companion object {
        private const val TAG = "OsmAndHelper"
    }
}

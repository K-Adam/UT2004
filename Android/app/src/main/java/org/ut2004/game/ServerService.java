package org.ut2004.game;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

import java.io.File;

public class ServerService extends Service {
    static final String EXTRA_CMD = "cmd";
    private static final String ACTION_STOP = "org.ut2004.game.STOP_SERVER";
    private static final String CHANNEL = "server";

    private Thread server;
    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;
    private WifiManager.MulticastLock lanLock;

    private static native int runServer(String dir, String cache, String cmd);
    private static native void stopServer();

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (ACTION_STOP.equals(intent.getAction())) {
            if (server != null)
                stopServer();
            else
                stopSelf();
            return START_NOT_STICKY;
        }
        if (server != null)
            return START_NOT_STICKY;
        String cmd = intent.getStringExtra(EXTRA_CMD);
        Notification note = notification(cmd.split("[ ?]", 2)[0]);
        // Older versions don't know the type and refuse it.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
            startForeground(1, note, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        else
            startForeground(1, note);

        // The screen going off would otherwise stop the server's ticks and put Wi-Fi into power save, which delays every packet.
        wakeLock = ((PowerManager) getSystemService(Context.POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "UT2004:server");
        wakeLock.acquire();
        WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifi != null) {
            wifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "UT2004 server");
            wifiLock.acquire();
            // LAN server lists ask by broadcast.
            lanLock = wifi.createMulticastLock("UT2004 server LAN");
            lanLock.acquire();
        }

        // The same folders SDL gives the game.
        File root = getExternalFilesDir(null);
        if (root == null)
            root = getFilesDir();
        String dir = new File(root, "UT2004/System").getAbsolutePath(), cache = getCacheDir().getAbsolutePath();
        server = new Thread(() -> {
            System.loadLibrary("main");
            // The map URL has to come first.
            runServer(dir, cache, cmd + " -nohomedir");
            // The engine cannot start again in this process.
            System.exit(0);
        }, "UT2004 server");
        server.start();
        return START_NOT_STICKY;
    }

    private Notification notification(String map) {
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(CHANNEL, "Dedicated server", NotificationManager.IMPORTANCE_LOW));
        PendingIntent stop = PendingIntent.getService(this, 0, new Intent(this, ServerService.class).setAction(ACTION_STOP),
                                                      PendingIntent.FLAG_IMMUTABLE);
        PendingIntent open = PendingIntent.getActivity(this, 0, new Intent(this, UT2004Activity.class), PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.mipmap.ic_launcher)
            .setContentTitle("UT2004 server")
            .setContentText("Hosting " + map)
            .setContentIntent(open)
            .setOngoing(true)
            .addAction(new Notification.Action.Builder((Icon) null, "Stop", stop).build())
            .build();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}

package org.libsdl.app;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.Uri;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Message;
import android.provider.Settings;
import android.view.Window;
import android.view.WindowManager;

public class PlatformUtils {
    public static boolean isBatterySupported() {
        Context context = SDLActivity.getContext();
        Intent batteryIntent = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        return batteryIntent != null;
    }

    public static int getBatteryLevel() {
        Context context = SDLActivity.getContext();

        Intent batteryIntent = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (batteryIntent == null) {
            return 0;
        }
        int level = batteryIntent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
        int scale = batteryIntent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);

        if (level >= 0 && scale > 0) {
            return (level * 100) / scale;
        }

        return 0;
    }

    public static boolean isBatteryCharging() {
        Context context = SDLActivity.getContext();

        IntentFilter filter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
        Intent batteryStatus = context.registerReceiver(null, filter);

        int status = batteryStatus.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
        return status == BatteryManager.BATTERY_STATUS_CHARGING ||
                status == BatteryManager.BATTERY_STATUS_FULL;
    }

    public static boolean isEthernetConnected() {
        Context context = SDLActivity.getContext();

        ConnectivityManager connectivityManager = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        Network[] networks = connectivityManager.getAllNetworks();
        for (Network network : networks) {
            NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(network);
            if (capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
                return true;
            }
        }
        return false;
    }

    public static boolean isWifiSupported() {
        Context context = SDLActivity.getContext();

        WifiManager wifiManager = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        return wifiManager != null && wifiManager.isWifiEnabled();
    }

    public static boolean isWifiConnected() {
        Context context = SDLActivity.getContext();

        ConnectivityManager connectivityManager = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
        NetworkInfo wifiInfo = connectivityManager.getNetworkInfo(ConnectivityManager.TYPE_WIFI);
        return wifiInfo != null && wifiInfo.isConnected();
    }

    public static int getWifiSignalStrength() {
        Context context = SDLActivity.getContext();

        WifiManager wifiManager = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        WifiInfo wifiInfo = wifiManager.getConnectionInfo();
        return wifiInfo.getRssi();
    }

    /**
     * Hand a downloaded update APK to the system package installer.
     * Works on Android TV as well as mobile: TVs usually ship no browser,
     * so opening the release page there does nothing, but the installer
     * (and its unknown-sources consent flow) exists everywhere. The
     * content:// URI is served by ApkProvider, which only ever exposes
     * this one file.
     */
    public static void installApk(String path) {
        Context context = SDLActivity.getContext();
        try {
            // API 26+ needs BOTH the REQUEST_INSTALL_PACKAGES manifest
            // permission AND a per-app "install unknown apps" grant. Older
            // Android auto-prompted for the grant when the install intent
            // launched; newer builds (Android TV especially) do NOT — the
            // installer just shows "Staging app... (Unknown)" and silently
            // aborts, so the user is never asked and every update fails the
            // same way. Check the grant ourselves and send the user to the
            // settings screen to enable it; they grant it and press Update
            // again, and future updates skip this step.
            if (android.os.Build.VERSION.SDK_INT >= 26 &&
                    !context.getPackageManager().canRequestPackageInstalls()) {
                Intent grant = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + context.getPackageName()))
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                // Some Android TV builds have no per-app source screen: fall
                // back to the global list, then to security settings.
                if (grant.resolveActivity(context.getPackageManager()) == null) {
                    grant = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                }
                if (grant.resolveActivity(context.getPackageManager()) == null) {
                    grant = new Intent(Settings.ACTION_SECURITY_SETTINGS)
                            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                }
                android.util.Log.i("VitaABS",
                        "installApk: no install-unknown-apps grant; opening settings");
                context.startActivity(grant);
                return;   // user enables it, then retries Update
            }

            Intent intent = new Intent(Intent.ACTION_VIEW);
            Uri uri;
            if (android.os.Build.VERSION.SDK_INT >= 24) {
                uri = Uri.parse("content://" + context.getPackageName() + ".apkprovider/update.apk");
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } else {
                uri = Uri.fromFile(new java.io.File(path));
            }
            intent.setDataAndType(uri, "application/vnd.android.package-archive");
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            context.startActivity(intent);

            // Deliberately NO self-kill here. The installer streams the APK
            // from ApkProvider, a ContentProvider hosted in THIS process, so
            // killing ourselves mid-"Staging app..." tears the provider down
            // and aborts the install (a fixed timer also fires before the
            // install commits, so it never stopped a "stale" app anyway).
            // Android force-stops the package when an update commits, which
            // is exactly the behaviour the kill was trying to imitate; on a
            // cancelled install we want to keep running.
        } catch (Exception e) {
            android.util.Log.e("VitaABS", "installApk failed", e);
        }
    }

    public static void openBrowser(String url) {
        Context context = SDLActivity.getContext();

        Uri webpage = Uri.parse(url);
        Intent intent = new Intent(Intent.ACTION_VIEW, webpage);
        if (intent.resolveActivity(context.getPackageManager()) != null) {
            context.startActivity(intent);
        }
    }

    public static float getSystemScreenBrightness(Context context) {
        ContentResolver contentResolver = context.getContentResolver();
        return Settings.System.getInt(contentResolver,
                Settings.System.SCREEN_BRIGHTNESS, 125) * 1.0f / 255.0f;
    }

    public static BorealisHandler borealisHandler = null;

    public static void setAppScreenBrightness(Activity activity, float value) {
        Message message = Message.obtain();
        message.obj = activity;
        message.arg1 = (int)(value * 255);
        message.what = 0;
        if(borealisHandler != null) borealisHandler.sendMessage(message);
    }

    public static float getAppScreenBrightness(Activity activity) {
        Window window = activity.getWindow();
        WindowManager.LayoutParams lp = window.getAttributes();
        if (lp.screenBrightness < 0) return getSystemScreenBrightness(activity);
        return lp.screenBrightness;
    }

    public static String getAndroidId() {
        Context context = SDLActivity.getContext();
        return Settings.Secure.getString(context.getContentResolver(), Settings.Secure.ANDROID_ID);
    }
}
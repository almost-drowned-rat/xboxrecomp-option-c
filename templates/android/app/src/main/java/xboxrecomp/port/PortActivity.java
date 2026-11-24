// SPDX-License-Identifier: MIT
// PortActivity - the Android activity of an option-c port (SDL3's SDLActivity loads libmain.so and calls SDL_main).
// 2026-11-11.. for Panzer Dragoon Orta (OrtaActivity); option-c template since 2026-11-19: the port's
// names come from resources app/build.gradle sets (R.string.port_title, saves_prefix, touch_name, R.bool.touch_aim).
package xboxrecomp.port;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.libsdl.app.SDLActivity;

public class PortActivity extends SDLActivity {
    private static final String TAG = "XRPORT";

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "python3.15", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // python = CPython's stdlib + site-packages (the lifter, capstone); toolchain = the on-device compiler's
        // sysroot + clang resource dir (clang / lld themselves are lib*_exe.so in nativeLibraryDir); probe = agent
        // the toolchain probe (dev, stage 1)
        // tools = the launcher's read-only folder (release/toolchain/lift/build.txt, the port's sources, the runtime
        // SDK, the compiler's sysroot + resource dir: tools/toolchain)
        // turnip = the Mesa Turnip driver for Adreno GPUs without Vulkan 1.3 (vkshim/vulkan_port.c)
        // (a Turnip driver is never shipped: the player picks one when the device needs it - importDriver)
        startRunLog();
        // an update (tools/ from an older APK already there) rebuilds the game; a fresh install hints after the image pick
        boolean update = new File(getFilesDir(), "tools/.apk_version").exists(), newTools = false;
        for (String a : new String[] { "python", "tools", "probe" }) newTools |= extractOnce(a) && a.equals("tools");
        newTools &= update;
        migrateUserFiles();
        migrateDriver();
        applyPendingSaves();
        super.onCreate(savedInstanceState);
        // forced landscape (maintainer 2026-11-11), both ways round; loader.c sets SDL's orientation hint to match
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        // the first start builds the game for many minutes: the screen stays on (also while playing)
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        // draw under the display cutout too (punch-hole phones; Android 9+), then hide the system bars
        android.view.WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.layoutInDisplayCutoutMode = android.os.Build.VERSION.SDK_INT >= 30
            ? android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
            : android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        getWindow().setAttributes(lp);
        immersive();
        // BACK: with targetSdk 36 the back gesture / key reaches an app only through OnBackInvokedCallback
        // (predictive back; onBackPressed and KEYCODE_BACK are no longer delivered) - handed on to SDL as AC_BACK
        // BACK opens the Android options (maintainer 2026-11-11: hide / move / hide single touch buttons); in the
        // touch layout editor it ends editing
        if (android.os.Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, () -> {
                    Log.i(TAG, "[BACK] back gesture / key");
                    onBackMenu();
                });
        }
        // the touch controls over SDL's surface (TouchOverlay.java)
        mOverlay = new TouchOverlay(this);
        mLayout.addView(mOverlay, new android.widget.RelativeLayout.LayoutParams(
            android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        // the BACK menu: a panel sliding in from the left (SideMenu.java), above the touch controls
        mMenu = new SideMenu(this);
        mMenu.setOnClosed(this::immersive);
        mLayout.addView(mMenu, new android.widget.RelativeLayout.LayoutParams(
            android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT));
        // a new APK (or the first install): the game is built again on this device before it starts
        // (an update: the setup screen shows the rebuild itself - PortSetup)
    }

    /** The first start after an install or update translates and compiles the game on the device - several minutes
     *  in which the launcher's progress bar is all the player sees (2026-11-19, maintainer's list item 4). */
    static void buildHint(Context ctx) {
        new android.os.Handler(android.os.Looper.getMainLooper()).post(() -> android.widget.Toast.makeText(ctx,
            "The game is now prepared on this device - this takes about 7-10 minutes the first time. Please keep the "
            + "app open.", android.widget.Toast.LENGTH_LONG).show());
    }

    // ---- Run logs (maintainer 2026-11-19: BACK > Export log; the log of a run that crashed must not be overwritten
    // by the next start). Every start runs `logcat --pid` of this process into <files>/data/logs/run_<stamp>.txt
    // (written as it goes, so a crash keeps everything up to it); the newest LOG_KEEP runs stay. Export log writes all
    // of them, newest first, with the device / driver lines on top, to a file the player picks.
    private static final int LOG_KEEP = 5;
    private static Process sLogcat;
    private static File sRunLog;            // this start's log
    private File mLogSel;                   // the run Export log writes; null = all of them

    private File[] runLogs() {
        File[] r = logDir(this).listFiles((dir, n) -> n.startsWith("run_") && n.endsWith(".txt"));
        if (r == null) return new File[0];
        java.util.Arrays.sort(r, (a, b) -> b.getName().compareTo(a.getName()));   // newest first
        return r;
    }

    /** Export log (maintainer 2026-11-19): one of the kept runs, newest first, or all of them. */
    private SideMenu.Page logPage() {
        return new SideMenu.Page() {
            public String title() { return "Export log"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                File[] runs = runLogs();
                if (runs.length == 0) { l.add(new SideMenu.Item("There is no log yet.")); return l; }
                for (final File r : runs) {
                    String n = r.getName();         // run_yyyyMMdd-HHmmss.txt
                    String when = n.length() >= 23 ? n.substring(4, 8) + "-" + n.substring(8, 10) + "-" + n.substring(10, 12)
                        + " " + n.substring(13, 15) + ":" + n.substring(15, 17) + ":" + n.substring(17, 19) : n;
                    l.add(new SideMenu.Item("Run " + when + (r.equals(sRunLog) ? "  (this run)" : "") + "  - "
                                            + Math.max(1, r.length() >> 10) + " KB") {
                        @Override void run() { mLogSel = r; mMenu.close(); pickLogExport(); }
                    });
                }
                if (runs.length > 1)
                    l.add(new SideMenu.Item("All " + runs.length + " runs") {
                        @Override void run() { mLogSel = null; mMenu.close(); pickLogExport(); }
                    });
                l.add(new SideMenu.Item("(the newest " + LOG_KEEP + " starts are kept)"));
                return l;
            }
        };
    }

    static File logDir(Context ctx) { return new File(ctx.getFilesDir(), "data/logs"); }

    private void startRunLog() {
        try {
            File d = logDir(this);
            d.mkdirs();
            File[] old = d.listFiles((dir, n) -> n.startsWith("run_") && n.endsWith(".txt"));
            if (old != null && old.length >= LOG_KEEP) {
                java.util.Arrays.sort(old, (a, b) -> a.getName().compareTo(b.getName()));
                for (int i = 0; i <= old.length - LOG_KEEP; i++) old[i].delete();
            }
            File f = new File(d, "run_" + stamp() + ".txt");
            sRunLog = f;
            sLogcat = new ProcessBuilder("logcat", "-v", "time", "--pid=" + android.os.Process.myPid())
                .redirectErrorStream(true).redirectOutput(f).start();
        } catch (Exception e) {
            Log.e(TAG, "[LOG] run log not started: " + e);
        }
    }

    static String readText(File f) {
        try { return new String(java.nio.file.Files.readAllBytes(f.toPath())).trim(); } catch (IOException e) { return ""; }
    }

    private void pickLogExport() {
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("text/plain");
        i.putExtra(Intent.EXTRA_TITLE, getString(R.string.saves_prefix).toLowerCase(java.util.Locale.ROOT) + "_log_"
                   + stamp() + ".txt");
        startActivityForResult(i, PICK_LOG_EXPORT);
    }

    /** Export: "" on success, else a sentence for the player. */
    private String exportLog(android.net.Uri u) {
        File[] runs = mLogSel != null ? new File[] { mLogSel } : runLogs();
        if (runs.length == 0 || !runs[0].exists()) return "There is no log yet.";
        String ver = "?";
        try { ver = getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception ignored) { }
        String drv = driverName(this);
        try (OutputStream o = getContentResolver().openOutputStream(u, "wt")) {
            if (o == null) return "The file could not be written.";
            StringBuilder h = new StringBuilder();
            h.append(getString(R.string.port_title)).append(" for Android ").append(ver).append('\n')
             .append("Device: ").append(android.os.Build.MANUFACTURER).append(' ').append(android.os.Build.MODEL)
             .append(android.os.Build.VERSION.SDK_INT >= 31 ? " (" + android.os.Build.SOC_MODEL + ")" : "").append(", Android ").append(android.os.Build.VERSION.RELEASE)
             .append(" (API ").append(android.os.Build.VERSION.SDK_INT).append(")\n")
             .append("GPU (system driver): ").append(readText(new File(getFilesDir(), "system_gpu.txt")).replace('\n', ' ')).append('\n')
             .append("Driver in use: ").append(drv != null ? drv : "system").append('\n')
             .append("RAM: ").append(totalRamMb()).append(" MB; free storage: ").append(getFilesDir().getUsableSpace() >> 20).append(" MB\n");
            o.write(h.toString().getBytes());
            byte[] buf = new byte[65536];
            for (File r : runs) {
                o.write(("\n===== " + r.getName() + " =====\n").getBytes());
                try (InputStream in = new java.io.FileInputStream(r)) {
                    int n;
                    while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
                }
            }
            Log.i(TAG, "[LOG] exported " + runs.length + " runs -> " + u);
            return "";
        } catch (Exception e) {
            Log.e(TAG, "[LOG] export failed: " + e);
            return "The log could not be exported: " + e.getMessage();
        }
    }

    private long totalRamMb() {
        android.app.ActivityManager.MemoryInfo mi = new android.app.ActivityManager.MemoryInfo();
        ((android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE)).getMemoryInfo(mi);
        return mi.totalMem >> 20;
    }

    private TouchOverlay mOverlay;
    private SideMenu mMenu;

    /** Called through JNI by loader.c right before the game's entry (port_android.h PORT_GAME_MAIN; any thread). */
    public static void onGameStarting() {
        final PortActivity a = (PortActivity) mSingleton;
        if (a != null) a.runOnUiThread(() -> { if (a.mOverlay != null) a.mOverlay.setGameRunning(); });
    }
    private static native void nativeTouchPadEnable(boolean on);
    private static native String nativePadPorts();
    private static native void nativeSwapPorts(int a, int b);

    /** touchpad.c's pad ranking (any thread): the gamepads Android reports as built in (not USB / Bluetooth / a
     *  dock's), one name per line - the toolkit puts external pads on the first ports (agent-l/pads). */
    public static String builtInPadNames() {
        StringBuilder b = new StringBuilder();
        for (int id : android.view.InputDevice.getDeviceIds()) {
            android.view.InputDevice d = android.view.InputDevice.getDevice(id);
            if (d == null || d.isVirtual() || d.isExternal() || !fromGamepad(d.getSources())) continue;
            b.append(d.getName()).append('\n');
        }
        return b.toString();
    }

    private static boolean fromGamepad(int source) {
        return (source & android.view.InputDevice.SOURCE_GAMEPAD) == android.view.InputDevice.SOURCE_GAMEPAD
            || (source & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK;
    }

    private void gamepadUsed() {
        if (mOverlay == null || mOverlay.getVisibility() != android.view.View.VISIBLE
            || mOverlay.getMode() != TouchOverlay.MODE_AUTO || mOverlay.isEditing()) return;
        mOverlay.onGamepadInput();
        try { nativeTouchPadEnable(false); } catch (UnsatisfiedLinkError ignored) { }
    }

    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent e) {
        // the open menu takes every key (pad / keyboard), the game sees none; BACK itself comes through
        // OnBackInvokedCallback (onBackMenu) on Android 13+, as a key only on older ones
        if (PortSetup.visible()) {           // the setup screen takes every key (pad / keyboard)
            if (e.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK && android.os.Build.VERSION.SDK_INT >= 33)
                return super.dispatchKeyEvent(e);
            if (PortSetup.key(e)) return true;
            return super.dispatchKeyEvent(e);
        }
        if (mMenu != null && mMenu.isOpen()) {
            if (e.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK && android.os.Build.VERSION.SDK_INT >= 33)
                return super.dispatchKeyEvent(e);
            return mMenu.onKey(e);
        }
        if (fromGamepad(e.getSource()) && e.getKeyCode() != android.view.KeyEvent.KEYCODE_BACK) gamepadUsed();
        return super.dispatchKeyEvent(e);
    }

    @Override
    public boolean dispatchGenericMotionEvent(android.view.MotionEvent e) {
        if (mMenu != null && mMenu.isOpen() && fromGamepad(e.getSource())) return mMenu.onMotion(e);
        if (fromGamepad(e.getSource())) gamepadUsed();
        return super.dispatchGenericMotionEvent(e);
    }

    @Override
    public boolean dispatchTouchEvent(android.view.MotionEvent e) {
        // a touch brings the hidden (AUTO) controls back - the hidden view itself gets no touches
        if (mOverlay != null && (mMenu == null || !mMenu.isOpen()) && e.getActionMasked() == android.view.MotionEvent.ACTION_DOWN
            && mOverlay.getMode() == TouchOverlay.MODE_AUTO && mOverlay.getVisibility() != android.view.View.VISIBLE) {
            try { nativeTouchPadEnable(true); } catch (UnsatisfiedLinkError ignored) { }
            mOverlay.onTouchEvent(android.view.MotionEvent.obtain(e.getDownTime(), e.getEventTime(),
                android.view.MotionEvent.ACTION_CANCEL, e.getX(), e.getY(), 0));
        }
        return super.dispatchTouchEvent(e);
    }

    private static final int PICK_DRIVER = 4711, PICK_BOOT = 4712, PICK_SAVE_EXPORT = 4713, PICK_SAVE_IMPORT = 4714,
                             PICK_LOG_EXPORT = 4715;

    /** The system file picker for a Turnip package, opened in Downloads (where a downloaded .adpkg.zip lands). */
    private void pickDriver() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
                    android.net.Uri.parse("content://com.android.providers.downloads.documents/root/downloads"));
        startActivityForResult(i, PICK_DRIVER);
    }

    /** True full screen (maintainer 2026-11-11, Odin 2 Portal: the 3-button navigation bar stayed, the app got
     *  1920x969 of 1920x1080). Android 11+ (API 30) no longer honours the old setSystemUiVisibility flags SDL uses:
     *  the WindowInsetsController hides status + navigation bar (a swipe from the edge shows them for a moment), the
     *  window draws under them and the display cutout. Called at start, on every focus gain (after the menu, the file
     *  picker, coming back from the background) and when the BACK menu opens or closes. */
    void immersive() {
        android.view.Window w = getWindow();
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
            android.view.WindowInsetsController c = w.getInsetsController();
            if (c != null) {
                c.hide(android.view.WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            w.getDecorView().setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) immersive();
    }

    /** Where saves/ and media/ live (maintainer 2026-11-11): the app's folder in shared storage,
     *  /storage/emulated/0/Android/data/<package>/files - reachable with adb push and file managers, no permission
     *  needed (loader.c passes it to the game: --save-dir, port_android.h PORT_MEDIA_ENV). Internal storage when none
     *  is mounted. */
    static File userDir(Context ctx) {
        File ext = ctx.getExternalFilesDir(null);
        return ext != null && android.os.Environment.MEDIA_MOUNTED.equals(android.os.Environment.getExternalStorageState(ext))
            ? ext : ctx.getFilesDir();
    }

    static File bootVideo(Context ctx) { return new File(userDir(ctx), "media/boot.mpg"); }

    private void pickBootVideo() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
                    android.net.Uri.parse("content://com.android.providers.downloads.documents/root/downloads"));
        startActivityForResult(i, PICK_BOOT);
    }

    /** The picked file to media/boot.mpg (an MPEG-1 system stream starts with the pack header 00 00 01 BA); "" on
     *  success, else a sentence for the player. */
    private String importBootVideo(android.net.Uri u) {
        File out = bootVideo(this), tmp = new File(out.getPath() + ".new");
        out.getParentFile().mkdirs();
        long n = 0;
        try (InputStream in = getContentResolver().openInputStream(u); OutputStream o = new FileOutputStream(tmp)) {
            if (in == null) return "The file could not be read.";
            byte[] buf = new byte[1 << 20];
            int r;
            boolean first = true;
            while ((r = in.read(buf)) > 0) {
                if (first && (r < 4 || buf[0] != 0 || buf[1] != 0 || buf[2] != 1 || (buf[3] & 0xFF) != 0xBA)) {
                    tmp.delete();
                    return "This is not an MPEG-1 video (boot.mpg). Convert your video to MPEG-1 + MP2 first (see the README).";
                }
                first = false;
                o.write(buf, 0, r);
                n += r;
            }
        } catch (Exception e) {
            tmp.delete();
            return "The file could not be copied: " + e.getMessage();
        }
        if (out.exists()) out.delete();
        if (!tmp.renameTo(out)) { tmp.delete(); return "The file could not be stored."; }
        shareTree(out);
        Log.i(TAG, "[MEDIA] boot.mpg imported: " + n + " bytes");
        return "";
    }

    /** Once: saves/ and media/ from the internal storage (v1.1.0 and older) to userDir - copied, then the old copy
     *  removed; never over files that are already there. */
    private void migrateUserFiles() {
        File to = userDir(this);
        if (to.equals(getFilesDir())) return;
        // made here, before the game, so a fresh install has them group-writable too (adb push of boot.mpg or a save
        // backup into an empty folder - a fresh Odin 2 Portal install got drwxr-s--- from the game's own mkdir);
        // shareTree on every start also fixes what the game created since (UDATA subfolders, new saves)
        for (String name : new String[] { "saves", "media" }) {
            File d = new File(to, name);
            d.mkdirs();
            shareTree(d);
        }
        for (String name : new String[] { "saves", "media" }) {
            File src = new File(getFilesDir(), name), dst = new File(to, name);
            if (!src.isDirectory() || (dst.exists() && dst.list() != null && dst.list().length > 0)) continue;
            try {
                int n = copyTree(src, dst);
                deleteTree(src);
                shareTree(dst);
                Log.i(TAG, "[MIGRATE] " + name + ": " + n + " files -> " + dst);
            } catch (IOException e) {
                Log.e(TAG, "[MIGRATE] " + name + " failed (left where it was): " + e);
            }
        }
    }

    /** saves/ and media/ readable + writable for the shared folder's group (adb pull / push, file managers): the app
     *  writes files -rw------- by default. Dirs 2770, files 0660. */
    private static void shareTree(File f) {
        try {
            android.system.Os.chmod(f.getPath(), f.isDirectory() ? 02770 : 0660);
        } catch (Exception ignored) { }
        File[] kids = f.isDirectory() ? f.listFiles() : null;
        if (kids != null) for (File k : kids) shareTree(k);
    }

    private static int copyTree(File from, File to) throws IOException {
        if (from.isDirectory()) {
            to.mkdirs();
            int n = 0;
            File[] kids = from.listFiles();
            if (kids != null) for (File k : kids) n += copyTree(k, new File(to, k.getName()));
            return n;
        }
        java.nio.file.Files.copy(from.toPath(), to.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        return 1;
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (PortSetup.onPicked(request, result, data)) { immersive(); return; }
        if (request == PICK_BOOT) {
            immersive();
            if (result == RESULT_OK && data != null && data.getData() != null) {
                String err = importBootVideo(data.getData());
                new android.app.AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("Boot animation")
                    .setMessage(err.isEmpty() ? "boot.mpg is in place. It plays at the next start when OPTIONS > RECOMP "
                                                + "OPTIONS > BOOT ANIMATION is on." : err)
                    .setPositiveButton("OK", null).show();
            }
            return;
        }
        if (request == PICK_SAVE_EXPORT || request == PICK_SAVE_IMPORT) {
            immersive();
            if (result != RESULT_OK || data == null || data.getData() == null) return;
            if (request == PICK_SAVE_EXPORT) {
                String err = exportSaves(data.getData());
                message("Saves", err.isEmpty() ? "Your saves are exported. Keep the zip file somewhere safe (it is not "
                        + "deleted when the app is uninstalled)." : err);
                return;
            }
            String err = stageSaves(data.getData());
            if (!err.isEmpty()) { message("Saves", err); return; }
            new android.app.AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Saves")
                .setMessage("The saves are ready. The game restarts now to use them; your current saves are kept as a "
                            + "backup in saves_backup/.")
                .setCancelable(false)
                .setPositiveButton("Restart now", (dlg, w) -> { restartApp(); System.exit(0); })
                .setNegativeButton("Cancel", (dlg, w) -> deleteTree(new File(userDir(this), SAVES_STAGE)))
                .show();
            return;
        }
        if (request == PICK_LOG_EXPORT) {
            immersive();
            if (result != RESULT_OK || data == null || data.getData() == null) return;
            String err = exportLog(data.getData());
            message("Export log", err.isEmpty() ? "The log is exported. Please attach this file to your report." : err);
            return;
        }
        if (request == PICK_DRIVER) {
            immersive();
            if (result == RESULT_OK && data != null && data.getData() != null) {
                final String[] slot = { null };
                String err = importDriver(this, data.getData(), slot);
                if (!err.isEmpty()) { message("Graphics driver", err); return; }
                new android.app.AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("Graphics driver")
                    .setMessage(keptDriverName(new File(driversDir(this), slot[0])) + " is added. Use it now? The game "
                                + "restarts to switch the driver.")
                    .setPositiveButton("Use now", (dlg, w) -> switchToKept(slot[0]))
                    .setNegativeButton("Later", null)
                    .show();
            }
            return;
        }
        super.onActivityResult(request, result, data);
    }

    /** BACK: the Android options as a panel from the left (SideMenu; maintainer 2026-11-11: Dolphin-like, the game
     *  keeps running behind it - freezing is a later toolkit item, and no START is sent). BACK in the open menu goes
     *  back one level / closes it; in the touch layout editor it ends editing. */
    private void onBackMenu() {
        if (PortSetup.visible()) { PortSetup.back(); return; }     // the setup screen: its own BACK (ask / quit)
        if (mOverlay == null || !mOverlay.isGameRunning()) return;     // the installer: BACK does nothing
        if (mMenu.isOpen()) { mMenu.back(); return; }
        if (mOverlay.isEditing()) { mOverlay.setEditing(false); return; }
        mOverlay.release();
        mMenu.open(rootPage());
        immersive();
    }

    private static String pct(float f) { return Math.round(f * 100) + " %"; }

    /** The BACK menu in the maintainer's fixed order for all PS2 / Xbox Android ports (2026-11-21): Back to the game,
     *  Controls >, Settings > (Saves, Export log, Boot animation, Graphics driver, About and licences). */
    private SideMenu.Page rootPage() {
        return new SideMenu.Page() {
            public String title() { return getString(R.string.port_title); }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                l.add(new SideMenu.Item("Back to the game") { @Override void run() { mMenu.close(); } });
                l.add(new SideMenu.Item("Controls") { @Override void run() { mMenu.push(controlsPage()); } });
                l.add(new SideMenu.Item("Settings") { @Override void run() { mMenu.push(settingsPage()); } });
                return l;
            }
        };
    }

    private SideMenu.Page settingsPage() {
        return new SideMenu.Page() {
            public String title() { return "Settings"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                l.add(new SideMenu.Item("Saves") { @Override void run() { mMenu.push(savesPage()); } });
                l.add(new SideMenu.Item("Export log") { @Override void run() { mMenu.push(logPage()); } });
                l.add(new SideMenu.Item("Boot animation: " + (bootVideo(PortActivity.this).exists() ? "boot.mpg" : "none")) {
                    @Override void run() { mMenu.push(bootPage()); }
                });
                String drv = driverName(PortActivity.this);
                l.add(new SideMenu.Item("Graphics driver: " + (drv != null ? drv : "system")) {
                    @Override void run() { mMenu.push(driverPage()); }
                });
                l.add(new SideMenu.Item("About and licences") { @Override void run() { mMenu.push(aboutPage()); } });
                return l;
            }
        };
    }

    /** Controls: the touch buttons (on / off / automatic, move, size, hide single ones, opacity, stick, reset) and the
     *  connected controllers (touch buttons off for the Odin 2 Portal; stick sensitivity = a faster cursor). */
    private SideMenu.Page controlsPage() {
        final String[] modes = { "Automatic", "Always", "Off" };
        return new SideMenu.Page() {
            public String title() { return "Controls"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                l.add(new SideMenu.Item("Touch buttons") {     // Automatic = hidden while a gamepad is used
                    @Override String value() { return modes[mOverlay.getMode()]; }
                    @Override void step(int d) {
                        int m = (mOverlay.getMode() + d + 3) % 3;
                        mOverlay.setMode(m);
                        try { nativeTouchPadEnable(m != TouchOverlay.MODE_OFF); } catch (UnsatisfiedLinkError ignored) { }
                    }
                });
                if (getResources().getBoolean(R.bool.touch_aim))   // 2026-11-13: touch aiming (TouchOverlay),
                    l.add(new SideMenu.Item("Touch layout") {      // only in a port with PORT_TOUCH_AIM (-PtouchAim)
                        @Override String value() { return mOverlay.getAimLayout() ? "Touch aiming" : "Stick"; }
                        @Override void step(int d) { mOverlay.setAimLayout(!mOverlay.getAimLayout()); }
                    });
                l.add(new SideMenu.Item("Controllers") { @Override void run() { mMenu.push(padPage()); } });
                l.add(new SideMenu.Item("Move and resize touch buttons") {
                    @Override void run() { mMenu.close(); mOverlay.setEditing(true); }
                });
                l.add(new SideMenu.Item("Button size") {       // all buttons, 50-150 %
                    @Override String value() { return pct(mOverlay.getScale()); }
                    @Override void step(int d) { mOverlay.setScale(Math.max(0.5f, Math.min(1.5f, mOverlay.getScale() + 0.25f * d))); }
                });
                l.add(new SideMenu.Item("Hide single touch buttons") { @Override void run() { mMenu.push(hidePage()); } });
                l.add(new SideMenu.Item("Opacity") {           // 25-100 %
                    @Override String value() { return mOverlay.getOpacity() + " %"; }
                    @Override void step(int d) { mOverlay.setOpacity(Math.max(25, Math.min(100, mOverlay.getOpacity() + 25 * d))); }
                });
                l.add(new SideMenu.Item("Stick sensitivity") {
                    @Override String value() { return pct(mOverlay.getSensitivity()); }
                    @Override void step(int d) { mOverlay.setSensitivity(Math.max(1f, Math.min(3f, mOverlay.getSensitivity() + 0.5f * d))); }
                });
                l.add(new SideMenu.Item("Reset touch layout") { @Override void run() { mOverlay.resetLayout(); mMenu.refresh(); } });
                return l;
            }
        };
    }

    /** Which controller is which player (toolkit agent-l/pads). A single-player title such as Orta: every controller is player 1 (merged), so the
     *  page only lists them; a multi-player title: a controller line moves that pad to player 1 (swap). */
    private SideMenu.Page padPage() {
        return new SideMenu.Page() {
            public String title() { return "Controllers"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                String info;
                try { info = nativePadPorts(); } catch (UnsatisfiedLinkError e) { info = ""; }
                String[] lines = info.split("\n");
                if (info.isEmpty()) {
                    l.add(new SideMenu.Item("(this game build lists no controllers yet)"));
                    return l;
                }
                final boolean merged = lines[0].equals("merged");
                String builtIn = "\n" + builtInPadNames();
                if (merged) l.add(new SideMenu.Item("Every controller plays as player 1"));
                int n = 0;
                for (int i = 1; i < lines.length; i++) {
                    String[] f = lines[i].split("\t", 2);
                    if (f.length < 2 || f[1].isEmpty()) continue;
                    final int port = Integer.parseInt(f[0]);
                    String kind = f[1].equals(getString(R.string.touch_name)) ? "touch" : builtIn.contains("\n" + f[1] + "\n") ? "built in" : "external";
                    String t = (merged ? "" : "Player " + (port + 1) + ":  ") + f[1] + "  (" + kind + ")";
                    l.add(new SideMenu.Item(t + (merged || port == 0 ? "" : "  - make player 1")) {
                        @Override void run() {
                            if (!merged && port != 0) { nativeSwapPorts(port, 0); mMenu.refresh(); }
                        }
                    });
                    n++;
                }
                if (n == 0) l.add(new SideMenu.Item("No controller connected"));
                return l;
            }
        };
    }

    /** Graphics driver (maintainer 2026-11-11: always available; 2026-11-21: up to five kept, as ZOE): the one in use,
     *  the kept Turnip packages, add / remove one, the system driver. A switch restarts the game (the Vulkan library
     *  picks its driver once per process). */
    private SideMenu.Page driverPage() {
        return new SideMenu.Page() {
            public String title() { return "Graphics driver"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                final String drv = driverName(PortActivity.this), inUse = driverSlotInUse(PortActivity.this);
                l.add(new SideMenu.Item("In use: " + (drv != null ? drv : "System driver")));
                File[] kept = keptDrivers(PortActivity.this);
                for (final File k : kept) {
                    final boolean cur = k.getName().equals(inUse);
                    l.add(new SideMenu.Item(keptDriverName(k) + (cur ? "  (in use)" : "")) {
                        @Override void run() { if (!cur) switchToKept(k.getName()); }
                    });
                }
                if (kept.length < MAX_DRIVERS)
                    l.add(new SideMenu.Item("Add driver (.adpkg.zip)...") { @Override void run() { mMenu.close(); pickDriver(); } });
                else
                    l.add(new SideMenu.Item("(" + MAX_DRIVERS + " drivers kept - remove one to add another)"));
                if (kept.length > 0)
                    l.add(new SideMenu.Item("Remove a kept driver...") { @Override void run() { mMenu.push(removeDriverPage()); } });
                l.add(new SideMenu.Item("Use system driver" + (drv == null ? "  (in use)" : "")) {
                    @Override void run() {
                        if (drv == null) return;
                        new File(turnipDir(PortActivity.this), "driver.txt").delete();
                        Log.i(TAG, "[DRIVER] system driver chosen");
                        restartApp();
                        System.exit(0);
                    }
                });
                l.add(new SideMenu.Item("(the game restarts to switch the driver)"));
                return l;
            }
        };
    }

    private SideMenu.Page removeDriverPage() {
        return new SideMenu.Page() {
            public String title() { return "Remove a kept driver"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                final String inUse = driverSlotInUse(PortActivity.this);
                for (final File k : keptDrivers(PortActivity.this)) {
                    final String name = keptDriverName(k);
                    final boolean cur = k.getName().equals(inUse);
                    l.add(new SideMenu.Item("Remove " + name + (cur ? "  (in use)" : "")) {
                        @Override void run() {
                            new android.app.AlertDialog.Builder(PortActivity.this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                                .setTitle("Graphics driver")
                                .setMessage("Remove " + name + " from the kept drivers?" + (cur ? " It stays in use until "
                                            + "you choose another driver." : ""))
                                .setPositiveButton("Remove", (dlg, w) -> {
                                    deleteTree(k);
                                    if (cur) {          // the copy in turnip/ stays in use, no longer tied to a kept folder
                                        File txt = new File(turnipDir(PortActivity.this), "driver.txt");
                                        String[] t = readText(txt).split("\n");
                                        try (OutputStream o = new FileOutputStream(txt)) {
                                            o.write((t[0].trim() + "\n" + (t.length > 1 ? t[1].trim() : "") + "\n").getBytes());
                                        } catch (IOException ignored) { }
                                    }
                                    Log.i(TAG, "[DRIVER] removed kept driver " + k.getName() + " (" + name + ")");
                                    mMenu.back();
                                })
                                .setNegativeButton("Cancel", null)
                                .show();
                        }
                    });
                }
                if (l.isEmpty()) l.add(new SideMenu.Item("No driver kept"));
                return l;
            }
        };
    }

    /** A kept driver into turnip/ (a copy: removing the kept one later never breaks the one in use), then restart. */
    private void switchToKept(String slot) {
        String err = activateKept(this, slot);
        if (!err.isEmpty()) { message("Graphics driver", err); return; }
        restartApp();
        System.exit(0);
    }

    /** Boot animation: media/boot.mpg (MPEG-1 + MP2) through the system picker - for players without adb / a file
     *  manager; the in-game OPTIONS > RECOMP OPTIONS > BOOT ANIMATION turns it on / off. */
    private SideMenu.Page bootPage() {
        return new SideMenu.Page() {
            public String title() { return "Boot animation"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                final File f = bootVideo(PortActivity.this);
                // the player's choice, like the graphics driver: none, or his own boot.mpg
                l.add(new SideMenu.Item("None" + (f.exists() ? "" : "  (in use)")) {
                    @Override void run() { if (f.exists()) { f.delete(); mMenu.refresh(); } }
                });
                l.add(new SideMenu.Item("Import file..." + (f.exists() ? "  (in use: boot.mpg, " + (f.length() >> 20) + " MB)" : "")) {
                    @Override void run() { mMenu.close(); pickBootVideo(); }
                });
                return l;
            }
        };
    }

    /** About and licences: assets/licenses/README.txt line by line (the full texts are in assets/licenses, staged by
     *  tools/build_android.sh from each component's own licence file). */
    private SideMenu.Page aboutPage() {
        return new SideMenu.Page() {
            public String title() { return "About and licences"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(
                         getAssets().open("licenses/README.txt"), java.nio.charset.StandardCharsets.UTF_8))) {
                    String line;
                    while ((line = r.readLine()) != null) if (!line.trim().isEmpty()) l.add(new SideMenu.Item(line.trim()));
                } catch (IOException e) {
                    l.add(new SideMenu.Item("(no licence list in this build)"));
                }
                return l;
            }
        };
    }

    private SideMenu.Page hidePage() {
        return new SideMenu.Page() {
            public String title() { return "Hide single touch buttons"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                for (int i = 0; i < TouchOverlay.N; i++) {
                    final int b = i;
                    l.add(new SideMenu.Item(TouchOverlay.NAMES[b]) {
                        @Override String value() { return mOverlay.isHidden(b) ? "hidden" : "shown"; }
                        @Override void step(int d) { mOverlay.setHidden(b, !mOverlay.isHidden(b)); }
                    });
                }
                return l;
            }
        };
    }

    // ---- the setup screen (jni/loader/setup_ui_android.c, from the launcher's thread): PortSetup + VistaSetupView ----
    public static String setupImagePage() { return PortSetup.imagePage((PortActivity) mSingleton); }
    public static void setupProgress(String text, float f) { PortSetup.progress((PortActivity) mSingleton, text, f); }
    public static boolean setupCancelled() { return PortSetup.cancelled(); }
    public static void setupMessage(String text) { PortSetup.message((PortActivity) mSingleton, text); }
    public static void setupClose(boolean ok) { PortSetup.close(ok); }

    /** The launcher's "Choose image...": SDL's file dialog gives a content:// URI, the installer reads files by path
     *  (open / fopen / stat). A path to the picked document itself does not exist for the app (no storage
     *  permission; reopening /proc/self/fd/N of the picker's descriptor goes through the FUSE path again and is
     *  refused - S23 2026-11-11 "permission denied"), so the image is copied once into <files>/data/import/
     *  under its own name; loader.c deletes the copy when the launcher is done (the player's file stays where it
     *  was). Returns the copy's path, "" on failure. Called through JNI from the launcher's thread. */
    public static String importContent(String uri) {
        Context ctx = mSingleton.getApplicationContext();
        android.net.Uri u = android.net.Uri.parse(uri);
        String name = "disc.iso";
        long size = -1;
        try (android.database.Cursor c = ctx.getContentResolver().query(u,
                new String[] { android.provider.OpenableColumns.DISPLAY_NAME, android.provider.OpenableColumns.SIZE }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                if (c.getString(0) != null) name = c.getString(0).replace('/', '_');
                if (!c.isNull(1)) size = c.getLong(1);
            }
        } catch (Exception e) { /* keep the default name */ }
        File dir = new File(getImportDir(ctx));
        // free storage (maintainer's list item 3): the copy + the extracted game (about the image's size again) + the
        // build (~0.5 GB) - asked before the copy, so nobody runs out of space minutes into the first start
        if (size > 0) {
            dir.getParentFile().mkdirs();
            long need = 2 * size + (512L << 20), free = dir.getParentFile().getUsableSpace();
            if (free < need) {
                Log.e(TAG, "[PICK] not enough storage: " + (free >> 20) + " MB free, " + (need >> 20) + " MB needed");
                final String msg = String.format(java.util.Locale.ROOT, "The first start needs about %.1f GB of free "
                    + "storage (the image is copied in and extracted), but only %.1f GB are free. Please free some "
                    + "storage and choose the image again.", need / 1073741824.0, free / 1073741824.0);
                mSingleton.runOnUiThread(() -> new android.app.AlertDialog.Builder(mSingleton,
                    android.R.style.Theme_DeviceDefault_Dialog_Alert).setTitle("Not enough storage").setMessage(msg)
                    .setPositiveButton("OK", null).show());
                return "";
            }
        }
        deleteTree(dir);
        dir.mkdirs();
        File out = new File(dir, name);
        long t0 = System.currentTimeMillis(), n = 0;
        try (InputStream in = ctx.getContentResolver().openInputStream(u);
             OutputStream o = new FileOutputStream(out)) {
            if (in == null) return "";
            byte[] buf = new byte[1 << 20];
            int r;
            while ((r = in.read(buf)) > 0) { o.write(buf, 0, r); n += r; }
        } catch (Exception e) {
            Log.e(TAG, "[PICK] cannot copy " + uri + ": " + e);
            deleteTree(dir);
            return "";
        }
        Log.i(TAG, "[PICK] " + uri + " -> " + out + " (" + n + " bytes in " + (System.currentTimeMillis() - t0) + " ms)");
        buildHint(ctx);
        return out.getAbsolutePath();
    }

    // ---- Graphics driver: the system driver or a Turnip package the player added (.adpkg.zip, e.g. purple-turnip).
    // Up to MAX_DRIVERS packages are kept, each unpacked in its own folder <files>/drivers/<n>/ (the .so + meta.json).
    // The one in use is a copy in <files>/turnip/ with driver.txt (line 1 the library's file name - what
    // vkshim/vulkan_port.c reads -, line 2 the display name, line 3 the kept folder it came from). As ZOE's
    // (2026-11-23: the same driver management in every Android port).
    static final int MAX_DRIVERS = 5;

    static File driversDir(Context ctx) { return new File(ctx.getFilesDir(), "drivers"); }
    static File turnipDir(Context ctx) { return new File(ctx.getFilesDir(), "turnip"); }

    static File[] keptDrivers(Context ctx) {
        File[] d = driversDir(ctx).listFiles(f -> f.isDirectory() && f.getName().matches("[0-9]+")
                                                  && new File(f, "meta.json").isFile());
        if (d == null) return new File[0];
        java.util.Arrays.sort(d, (a, b) -> Integer.compare(Integer.parseInt(a.getName()), Integer.parseInt(b.getName())));
        return d;
    }

    private static String metaName(File dir) {
        try {
            org.json.JSONObject j = new org.json.JSONObject(readText(new File(dir, "meta.json")));
            String n = j.optString("name", "").trim();
            if (n.isEmpty()) return null;
            String v = j.optString("driverVersion", "").trim();
            return v.isEmpty() ? n : n + " " + v;
        } catch (Exception e) { return null; }
    }

    private static String metaLib(File dir) {
        try {
            String lib = new org.json.JSONObject(readText(new File(dir, "meta.json"))).optString("libraryName", "");
            return lib.isEmpty() ? null : new File(lib).getName();
        } catch (Exception e) { return null; }
    }

    static String keptDriverName(File dir) {
        String n = metaName(dir);
        if (n != null) return n;
        String lib = metaLib(dir);
        return lib != null ? lib : "Driver " + dir.getName();
    }

    /** The kept folder the driver in use came from (driver.txt line 3), "" if none. */
    static String driverSlotInUse(Context ctx) {
        if (driverName(ctx) == null) return "";
        String[] l = readText(new File(turnipDir(ctx), "driver.txt")).split("\n");
        return l.length > 2 ? l[2].trim() : "";
    }

    /** v0.2.0 kept one driver only (turnip/, no line 3): kept as drivers/1 so the page lists it. */
    private void migrateDriver() {
        File txt = new File(turnipDir(this), "driver.txt");
        if (!txt.isFile() || keptDrivers(this).length > 0 || !driverSlotInUse(this).isEmpty()) return;
        String[] l = readText(txt).split("\n");
        File dst = new File(driversDir(this), "1");
        try {
            deleteTree(dst);
            dst.mkdirs();
            File[] fs = turnipDir(this).listFiles();
            if (fs != null) for (File f : fs) if (!f.getName().equals("driver.txt")) copyTree(f, new File(dst, f.getName()));
            if (!new File(dst, "meta.json").isFile()) { deleteTree(dst); return; }
            try (OutputStream o = new FileOutputStream(txt)) {
                o.write((l[0].trim() + "\n" + (l.length > 1 ? l[1].trim() : "") + "\n1\n").getBytes());
            }
            Log.i(TAG, "[DRIVER] the driver in use kept as drivers/1");
        } catch (IOException e) { deleteTree(dst); }
    }

    /** The kept driver drivers/<slot> made the one in use (a copy in turnip/ with driver.txt); "" or a sentence.
     *  (The game picks its driver once per process: the caller restarts it - or, the setup, starts it after.) */
    static String activateKept(Context ctx, String slot) {
        File src = new File(driversDir(ctx), slot), dst = turnipDir(ctx);
        String lib = metaLib(src);
        if (lib == null || !new File(src, lib).isFile()) return "This driver package is incomplete. Please remove it and add it again.";
        File tmp = new File(ctx.getFilesDir(), "data/tmp/turnip_new");
        deleteTree(tmp);
        try {
            copyTree(src, tmp);
            try (OutputStream o = new FileOutputStream(new File(tmp, "driver.txt"))) {
                o.write((lib + "\n" + keptDriverName(src) + "\n" + slot + "\n").getBytes());
            }
        } catch (IOException e) {
            deleteTree(tmp);
            return "The driver could not be installed (storage): " + e.getMessage();
        }
        deleteTree(dst);
        if (!tmp.renameTo(dst)) { deleteTree(tmp); return "The driver could not be installed (storage)."; }
        Log.i(TAG, "[DRIVER] chosen: " + keptDriverName(src) + " (" + lib + ", kept " + slot + ")");
        return "";
    }

    /** Turnip runs only on Adreno GPUs (system_gpu.txt: jni/loader/driver_check.c, written at every start; else the
     *  SoC maker on Android 12+; unknown: allowed). */
    static boolean maybeAdreno(Context ctx) {
        String gpu = readText(new File(ctx.getFilesDir(), "system_gpu.txt"));
        if (!gpu.isEmpty() && !gpu.startsWith("no device")) return gpu.contains("Adreno");
        if (android.os.Build.VERSION.SDK_INT < 31) return true;
        String m = android.os.Build.SOC_MANUFACTURER;
        if (m == null || m.isEmpty() || m.equalsIgnoreCase("unknown")) return true;
        m = m.toLowerCase(java.util.Locale.ROOT);
        return m.contains("qti") || m.contains("qualcomm");
    }

    /** An adrenotools .adpkg.zip (meta.json + the driver .so) the player picked, unpacked into the lowest free
     *  drivers/<n>/. Returns "" on success (slot[0] = the folder's name), else a sentence for the player. */
    static String importDriver(Context ctx, android.net.Uri uri, String[] slot) {
        if (!maybeAdreno(ctx)) {
            String gpu = readText(new File(ctx.getFilesDir(), "system_gpu.txt"));
            return "Turnip drivers work only on Adreno GPUs (Snapdragon)." + (gpu.isEmpty() ? "" : " This device has: "
                   + gpu.split("\n")[0] + ".") + " Please keep the system driver.";
        }
        File[] kept = keptDrivers(ctx);
        File tmp = new File(ctx.getFilesDir(), "data/tmp/driver_new");
        deleteTree(tmp);
        tmp.mkdirs();
        String lib;
        try (InputStream in = ctx.getContentResolver().openInputStream(uri);
             java.util.zip.ZipInputStream z = new java.util.zip.ZipInputStream(in)) {
            java.util.zip.ZipEntry e;
            while ((e = z.getNextEntry()) != null) {
                String n = new File(e.getName()).getName();      // flat: no folders, no ".."
                if (e.isDirectory() || !(n.equals("meta.json") || n.endsWith(".so"))) continue;
                try (OutputStream o = new FileOutputStream(new File(tmp, n))) {
                    byte[] buf = new byte[65536];
                    int r;
                    while ((r = z.read(buf)) > 0) o.write(buf, 0, r);
                }
            }
            if (!new File(tmp, "meta.json").exists()) {
                deleteTree(tmp);
                return "This file is not a driver package (no meta.json). Please choose a Turnip .adpkg.zip.";
            }
            lib = metaLib(tmp);
            if (lib == null || !new File(tmp, lib).exists()) {
                deleteTree(tmp);
                return "The driver package names a library it does not contain.";
            }
        } catch (Exception ex) {
            deleteTree(tmp);
            Log.e(TAG, "[DRIVER] cannot import " + uri + ": " + ex);
            return "The driver package could not be read: " + ex.getMessage();
        }
        // the same package twice (same name, same library size): the kept one is used
        String name = keptDriverName(tmp);
        long size = new File(tmp, lib).length();
        for (File k : kept) {
            String kl = metaLib(k);
            if (name.equals(keptDriverName(k)) && kl != null && new File(k, kl).length() == size) {
                deleteTree(tmp);
                slot[0] = k.getName();
                return "";
            }
        }
        if (kept.length >= MAX_DRIVERS) { deleteTree(tmp); return MAX_DRIVERS + " drivers are kept already - please remove one first."; }
        java.util.Set<String> used = new java.util.HashSet<>();
        File[] all = driversDir(ctx).listFiles();                   // also half-made folders (no meta.json)
        if (all != null) for (File a : all) used.add(a.getName());
        int n = 1;
        while (used.contains(String.valueOf(n))) n++;
        File dst = new File(driversDir(ctx), String.valueOf(n));
        driversDir(ctx).mkdirs();
        if (!tmp.renameTo(dst)) { deleteTree(tmp); return "The driver could not be stored (storage)."; }
        slot[0] = dst.getName();
        Log.i(TAG, "[DRIVER] kept " + name + " (" + lib + ") as drivers/" + slot[0]);
        return "";
    }

    /** driver_check.c's "Choose driver..." (JNI, Adreno below Vulkan 1.3): kept + made the one in use; "" or a sentence. */
    public static String importDriver(String uri) {
        Context ctx = mSingleton.getApplicationContext();
        String[] slot = { null };
        String err = importDriver(ctx, android.net.Uri.parse(uri), slot);
        return err.isEmpty() ? activateKept(ctx, slot[0]) : err;
    }

    private void message(String title, String text) {
        new android.app.AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle(title).setMessage(text).setPositiveButton("OK", null).show();
    }

    // ---- Saves export / import (2026-11-13, maintainer's Android open item 1: uninstalling deletes
    // Android/data/<package>/files, so saves/ needs a way out). Export = one zip of saves/ (entries "saves/..."),
    // written through the system file picker anywhere the player likes (Downloads, a cloud folder). Import = the zip
    // unpacked next to saves/ (SAVES_STAGE), then a restart; applyPendingSaves swaps it in at the next start, BEFORE
    // the game runs - the running game keeps its save files and settings open / cached, so it is never swapped under
    // it. The saves replaced by an import are zipped to saves_backup/ first (the newest 5 kept).
    private static final String SAVES_STAGE = ".saves_import";

    private static String stamp() {
        return new java.text.SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.ROOT).format(new java.util.Date());
    }

    private SideMenu.Page savesPage() {
        return new SideMenu.Page() {
            public String title() { return "Saves"; }
            public java.util.List<SideMenu.Item> items() {
                java.util.List<SideMenu.Item> l = new java.util.ArrayList<>();
                l.add(new SideMenu.Item("Export saves (zip)...") { @Override void run() { mMenu.close(); pickSaveExport(); } });
                l.add(new SideMenu.Item("Import saves (zip)...") { @Override void run() { mMenu.close(); pickSaveImport(); } });
                l.add(new SideMenu.Item("(uninstalling the app deletes its saves - export them first)"));
                l.add(new SideMenu.Item("(an import restarts the game; the old saves go to saves_backup/)"));
                return l;
            }
        };
    }

    private void pickSaveExport() {
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("application/zip");
        i.putExtra(Intent.EXTRA_TITLE, getString(R.string.saves_prefix) + "-saves-" + stamp() + ".zip");
        i.putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
                    android.net.Uri.parse("content://com.android.providers.downloads.documents/root/downloads"));
        startActivityForResult(i, PICK_SAVE_EXPORT);
    }

    private void pickSaveImport() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
                    android.net.Uri.parse("content://com.android.providers.downloads.documents/root/downloads"));
        startActivityForResult(i, PICK_SAVE_IMPORT);
    }

    /** saves/ as a zip (entries "saves/<path>") into out; returns the file count. */
    private static int zipSaves(File saves, OutputStream out) throws IOException {
        int[] n = { 0 };
        try (java.util.zip.ZipOutputStream z = new java.util.zip.ZipOutputStream(out)) {
            zipAdd(z, saves, "saves", n);
        }
        return n[0];
    }

    private static void zipAdd(java.util.zip.ZipOutputStream z, File f, String name, int[] n) throws IOException {
        if (f.isDirectory()) {
            z.putNextEntry(new java.util.zip.ZipEntry(name + "/"));
            z.closeEntry();
            File[] kids = f.listFiles();
            if (kids != null) {
                java.util.Arrays.sort(kids);
                for (File k : kids) zipAdd(z, k, name + "/" + k.getName(), n);
            }
            return;
        }
        java.util.zip.ZipEntry e = new java.util.zip.ZipEntry(name);
        e.setTime(f.lastModified());
        z.putNextEntry(e);
        java.nio.file.Files.copy(f.toPath(), z);
        z.closeEntry();
        n[0]++;
    }

    /** Export: "" on success, else a sentence for the player. */
    private String exportSaves(android.net.Uri u) {
        File saves = new File(userDir(this), "saves");
        if (!new File(saves, "UDATA").isDirectory()) return "There are no saves yet (start a game and save first).";
        try (OutputStream o = getContentResolver().openOutputStream(u, "wt")) {
            if (o == null) return "The file could not be written.";
            int n = zipSaves(saves, o);
            Log.i(TAG, "[SAVES] exported " + n + " files -> " + u);
            return "";
        } catch (Exception e) {
            Log.e(TAG, "[SAVES] export failed: " + e);
            return "The saves could not be exported: " + e.getMessage();
        }
    }

    /** Import, step 1: the picked zip unpacked into SAVES_STAGE (checked: only relative paths, a UDATA folder);
     *  "" on success, else a sentence for the player. Accepts our export ("saves/UDATA/...") and a zip of the
     *  folder's contents ("UDATA/..."). */
    private String stageSaves(android.net.Uri u) {
        File stage = new File(userDir(this), SAVES_STAGE);
        deleteTree(stage);
        stage.mkdirs();
        int n = 0;
        try (InputStream in = getContentResolver().openInputStream(u);
             java.util.zip.ZipInputStream z = new java.util.zip.ZipInputStream(in)) {
            java.util.zip.ZipEntry e;
            while ((e = z.getNextEntry()) != null) {
                String name = e.getName().replace('\\', '/');
                if (name.startsWith("saves/")) name = name.substring(6);
                if (name.isEmpty()) continue;
                if (name.startsWith("/") || name.contains(":") || ("/" + name + "/").contains("/../")) {
                    deleteTree(stage);
                    return "This zip file has paths outside its own folder - it is not a saves export.";
                }
                File f = new File(stage, name);
                if (e.isDirectory()) { f.mkdirs(); continue; }
                f.getParentFile().mkdirs();
                try (OutputStream o = new FileOutputStream(f)) {
                    byte[] buf = new byte[65536];
                    int r;
                    while ((r = z.read(buf)) > 0) o.write(buf, 0, r);
                }
                if (e.getTime() > 0) f.setLastModified(e.getTime());
                n++;
            }
        } catch (Exception ex) {
            deleteTree(stage);
            Log.e(TAG, "[SAVES] import failed: " + ex);
            return "The file could not be read as a zip: " + ex.getMessage();
        }
        if (!new File(stage, "UDATA").isDirectory()) {
            deleteTree(stage);
            return "This zip file holds no saves (no UDATA folder). Please choose a file made with Export saves.";
        }
        Log.i(TAG, "[SAVES] import staged: " + n + " files from " + u);
        return "";
    }

    /** Import, step 2 (onCreate, before the game): a staged import replaces saves/; the old saves are zipped to
     *  saves_backup/ first. A failed backup leaves everything as it was (the stage stays for the next try). */
    private void applyPendingSaves() {
        File root = userDir(this), stage = new File(root, SAVES_STAGE), saves = new File(root, "saves");
        if (!new File(stage, "UDATA").isDirectory()) { if (stage.exists()) deleteTree(stage); return; }
        File bdir = new File(root, "saves_backup");
        if (saves.isDirectory()) {
            bdir.mkdirs();
            File b = new File(bdir, "saves-before-import-" + stamp() + ".zip");
            try (OutputStream o = new FileOutputStream(b)) {
                zipSaves(saves, o);
            } catch (IOException e) {
                Log.e(TAG, "[SAVES] backup failed, import not applied: " + e);
                b.delete();
                return;
            }
            File[] old = bdir.listFiles((d, nm) -> nm.startsWith("saves-before-import-") && nm.endsWith(".zip"));
            if (old != null && old.length > 5) {
                java.util.Arrays.sort(old);                       // the stamp sorts by time
                for (int i = 0; i < old.length - 5; i++) old[i].delete();
            }
            deleteTree(saves);
        }
        if (!stage.renameTo(saves)) {
            try { copyTree(stage, saves); } catch (IOException e) { Log.e(TAG, "[SAVES] import copy failed: " + e); return; }
            deleteTree(stage);
        }
        shareTree(saves);
        shareTree(bdir);
        Log.i(TAG, "[SAVES] import applied (old saves in " + bdir + ")");
    }

    /** The driver in use: null = the system driver; else meta.json's name in turnip/, else driver.txt's line 2 / 1. */
    static String driverName(Context ctx) {
        File dir = turnipDir(ctx);
        String[] l = readText(new File(dir, "driver.txt")).split("\n");
        if (l[0].trim().isEmpty() || !new File(dir, l[0].trim()).isFile()) return null;
        String n = metaName(dir);
        return n != null ? n : l.length > 1 && !l[1].trim().isEmpty() ? l[1].trim() : l[0].trim();
    }

    static String getImportDir(Context ctx) {
        return new File(ctx.getFilesDir(), "data/import").getAbsolutePath();
    }

    /** The APK's assets/<name> into internal storage, once per APK version (the marker file holds the versionCode +
     *  update time). Android is not portable (maintainer 2026-11-11): everything lives in the app's internal storage. */
    private boolean extractOnce(String name) {
        try { if (getAssets().list(name).length == 0) return false; } catch (IOException e) { return false; }
        File dest = new File(getFilesDir(), name);
        File marker = new File(dest, ".apk_version");
        String want;
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            want = pi.getLongVersionCode() + ":" + pi.lastUpdateTime;
        } catch (Exception e) {
            want = "unknown";
        }
        try {
            if (marker.exists() && want.equals(new String(java.nio.file.Files.readAllBytes(marker.toPath())))) return false;
            long t0 = System.currentTimeMillis();
            deleteTree(dest);
            int n = copyAssets(getAssets(), name, dest);
            try (OutputStream o = new FileOutputStream(marker)) { o.write(want.getBytes()); }
            Log.i(TAG, "[ASSETS] " + name + " extracted: " + n + " files in " + (System.currentTimeMillis() - t0) + " ms");
            return true;
        } catch (IOException e) {
            Log.e(TAG, "[ASSETS] " + name + " extraction failed: " + e);
        }
        return false;
    }

    private static int copyAssets(AssetManager am, String path, File to) throws IOException {
        String[] kids = am.list(path);
        if (kids == null || kids.length == 0) {                 // a file
            to.getParentFile().mkdirs();
            try (InputStream in = am.open(path); OutputStream out = new FileOutputStream(to)) {
                byte[] buf = new byte[65536];
                int r;
                while ((r = in.read(buf)) > 0) out.write(buf, 0, r);
            }
            return 1;
        }
        int n = 0;
        to.mkdirs();
        for (String k : kids) n += copyAssets(am, path + "/" + k, new File(to, k));
        return n;
    }

    static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }

    /** RESTART NOW (host_restart in src/platform/host_linux.c, called through JNI), the driver switch and the saves
     *  import: an app cannot exec itself, and a PendingIntent alarm is a blocked background start on Android 14+
     *  (S23, 2026-11-13) - so RestartActivity (its own process) is started now, while this one is in the foreground;
     *  the caller ends this process right after, RestartActivity waits for that and starts the game again. */
    public void restartApp() {
        Intent i = new Intent(this, RestartActivity.class);
        i.putExtra(RestartActivity.EXTRA_PID, android.os.Process.myPid());
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        startActivity(i);
    }
}

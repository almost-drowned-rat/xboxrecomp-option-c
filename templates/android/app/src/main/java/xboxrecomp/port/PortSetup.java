// PortSetup - the setup screen between the toolkit's launcher and VistaSetupView (2026-11-23; the
// maintainer: ZOE's Android launcher is the design standard of all Android ports, Orta takes it over).
//
// The launcher (option-c src/launcher/launcher.c, in libmain.so) runs on SDL's thread and calls the setup UI of
// game_setup_ui.h; jni/loader/setup_ui_android.c turns those calls into the static methods here (through
// PortActivity.setup*). They block that thread where the launcher waits for the player (the disc page, a message,
// Play) - the view itself lives on the UI thread, above SDL's surface.
//   imagePage  the form: the disc image (system picker) and the graphics driver; Install -> the driver kept + made
//              the one in use, the image copied into <files>/data/import (the launcher reads by path; loader.c
//              deletes the copy), its path returned; Quit -> null
//   progress   the launcher's step text + fraction (also without the form: a rebuild after an app update)
//   cancelled  "Quit anyway?" answered Yes: the launcher stops its tools and ends
//   message    a box with OK
//   close      after a finished build: "is ready" + Play; then the view goes and the game starts
// SPDX-License-Identifier: GPL-3.0-or-later
package xboxrecomp.port;

import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.KeyEvent;
import android.view.ViewGroup;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.concurrent.FutureTask;

final class PortSetup implements VistaSetupView.Host {
    private static final String TAG = "XRPORT";
    private static final int PICK_DISC = 4720, PICK_DRIVER = 4721;

    private static volatile PortSetup sCur;

    private final PortActivity act;
    private final VistaSetupView view;
    private final Object lock = new Object();
    private Uri disc, driver;

    // the launcher's waits (guarded by lock)
    private boolean pageOpen, pageDone, msgDone, playDone;
    private String pageResult;
    private volatile boolean cancelled, copying, copyCancel, building;

    private PortSetup(PortActivity a) {
        act = a;
        view = new VistaSetupView(a, this, PortActivity.maybeAdreno(a));
    }

    // ---- on the UI thread ----------------------------------------------------------------------------------------

    private static void ui(PortActivity a, Runnable r) { a.runOnUiThread(r); }

    /** Runs r on the UI thread and waits for it (the launcher's thread). */
    private static void uiWait(PortActivity a, Runnable r) {
        FutureTask<Void> t = new FutureTask<>(r, null);
        a.runOnUiThread(t);
        try { t.get(); } catch (Exception e) { Log.e(TAG, "[SETUP] " + e); }
    }

    /** The view over SDL's surface (created once, on the first call that needs it). */
    private static PortSetup show(PortActivity a) {
        PortSetup c = sCur;
        if (c != null) return c;
        final PortSetup[] made = new PortSetup[1];
        uiWait(a, () -> {
            made[0] = new PortSetup(a);
            ViewGroup root = (ViewGroup) a.getWindow().getDecorView().findViewById(android.R.id.content);
            root.addView(made[0].view, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                                                                   ViewGroup.LayoutParams.MATCH_PARENT));
            made[0].view.setDriverInUse(PortActivity.driverName(a));
            made[0].view.requestFocus();
            a.immersive();
        });
        sCur = made[0];
        return sCur;
    }

    static boolean visible() { return sCur != null; }
    static boolean key(KeyEvent e) { PortSetup c = sCur; return c != null && c.view.key(e); }
    static void back() { PortSetup c = sCur; if (c != null) c.view.back(); }

    /** PortActivity.onActivityResult: a pick for the form. */
    static boolean onPicked(int request, int result, Intent data) {
        if (request != PICK_DISC && request != PICK_DRIVER) return false;
        PortSetup c = sCur;
        if (c == null || result != android.app.Activity.RESULT_OK || data == null || data.getData() == null) return true;
        Uri u = data.getData();
        if (request == PICK_DISC) {
            c.disc = u;
            c.view.setRow(VistaSetupView.ROW_DISC, c.displayName(u));
        } else {
            c.driver = u;
            c.view.setRow(VistaSetupView.PICK_DRIVER, c.displayName(u));
        }
        return true;
    }

    // ---- from the launcher (SDL's thread, through PortActivity.setup*) -----------------------------------------------

    /** The form until Install (the image copied: its path) or Quit (null). */
    static String imagePage(PortActivity a) {
        PortSetup c = show(a);
        synchronized (c.lock) { c.pageOpen = true; c.pageDone = false; c.pageResult = null; }
        ui(a, c.view::form);
        synchronized (c.lock) {
            while (!c.pageDone) {
                try { c.lock.wait(); } catch (InterruptedException e) { break; }
            }
            c.pageOpen = false;
            c.building = c.pageResult != null;
            return c.pageResult;
        }
    }

    static void progress(PortActivity a, String text, float f) {
        // every start checks the installed game (the installer's part, 0-10 %): without the form open that is no
        // build - nothing shown; a build (after an app update: unpacking the launcher's files at 10 %) shows the view
        if (sCur == null && f < 0.10f) return;
        boolean fresh = sCur == null;
        PortSetup c = show(a);
        if (fresh)                      // (a rebuild: the disc is installed already)
            ui(a, () -> c.view.setRow(VistaSetupView.ROW_DISC, "installed - the game is built again for this app version"));
        if (f >= 0.10f) c.building = true;
        ui(a, () -> c.view.launcherProgress(text == null ? "" : text, f));
    }

    static boolean cancelled() { PortSetup c = sCur; return c != null && c.cancelled; }

    static void message(PortActivity a, String text) {
        PortSetup c = show(a);
        synchronized (c.lock) { c.msgDone = false; }
        ui(a, () -> c.view.showMessage(text));
        c.waitFor(() -> c.msgDone);
    }

    /** The launcher is done: ok = the game was built (Play first), else the view just goes. */
    static void close(boolean ok) {
        PortSetup c = sCur;
        if (c == null) return;
        if (ok && c.building && !c.cancelled) {
            synchronized (c.lock) { c.playDone = false; }
            ui(c.act, c.view::finished);
            c.waitFor(() -> c.playDone);
        }
        uiWait(c.act, () -> {
            ViewGroup p = (ViewGroup) c.view.getParent();
            if (p != null) p.removeView(c.view);
            c.act.immersive();
        });
        sCur = null;
    }

    private interface Cond { boolean met(); }

    private void waitFor(Cond cond) {
        synchronized (lock) {
            while (!cond.met()) {
                try { lock.wait(); } catch (InterruptedException e) { return; }
            }
        }
    }

    private void wake(Runnable set) {
        synchronized (lock) { set.run(); lock.notifyAll(); }
    }

    // ---- VistaSetupView.Host (UI thread) ---------------------------------------------------------------------------

    @Override
    public void pickFile(int row) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        act.startActivityForResult(i, row == VistaSetupView.PICK_DRIVER ? PICK_DRIVER : PICK_DISC);
    }

    @Override
    public void install() {
        final Uri d = disc, drv = view.driverFromFile() ? driver : null;
        final boolean system = view.systemChosen();
        new Thread(() -> {
            String err = "";
            if (system && new File(PortActivity.turnipDir(act), "driver.txt").delete())
                Log.i(TAG, "[SETUP] system driver chosen");
            if (drv != null) {
                final String[] slot = { null };
                err = PortActivity.importDriver(act, drv, slot);
                if (err.isEmpty()) err = PortActivity.activateKept(act, slot[0]);
            }
            String path = null;
            if (err.isEmpty()) {
                String[] e = { "" };
                path = copyImage(d, e);
                err = e[0];
            }
            if (path == null) {
                if (copyCancel) { finishPage(null); return; }
                final String msg = err.isEmpty() ? "The disc image could not be read - please choose it again." : err;
                ui(act, () -> view.failed(msg, true));
                return;                 // the form stays open
            }
            finishPage(path);
        }, "setup install").start();
    }

    private void finishPage(String path) {
        wake(() -> { pageResult = path; pageDone = true; });
    }

    @Override
    public void quit() {
        boolean page;
        synchronized (lock) { page = pageOpen; }
        if (page) {
            if (copying) { copyCancel = true; return; }     // the copy ends, the page answers "quit"
            finishPage(null);
            return;
        }
        if (view.installing()) {        // "Quit anyway?" answered Yes: the launcher stops its tools and ends
            cancelled = true;
            Log.i(TAG, "[SETUP] quit during the installation (the player)");
            return;
        }
        if (!playDone && building) {    // "is ready" and Quit instead of Play: the game is built, the app ends
            act.finishAndRemoveTask();
            System.exit(0);
        }
    }

    @Override
    public void play() { wake(() -> playDone = true); }

    @Override
    public void messageClosed() { wake(() -> msgDone = true); }

    // ---- the image copy (the launcher reads by path; a picked document has none the app may open) --------------------

    private String copyImage(Uri u, String[] err) {
        Context ctx = act.getApplicationContext();
        String name = "disc.iso";
        long size = -1;
        try (Cursor c = ctx.getContentResolver().query(u, new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE },
                                                        null, null, null)) {
            if (c != null && c.moveToFirst()) {
                if (c.getString(0) != null) name = c.getString(0).replace('/', '_');
                if (!c.isNull(1)) size = c.getLong(1);
            }
        } catch (Exception ignored) { }
        File dir = new File(PortActivity.getImportDir(ctx));
        dir.getParentFile().mkdirs();
        // the copy + the extracted game (about the image's size again) + the build (~0.5 GB), asked before the copy
        if (size > 0) {
            long need = 2 * size + (512L << 20), free = dir.getParentFile().getUsableSpace();
            if (free < need) {
                Log.e(TAG, "[SETUP] not enough storage: " + (free >> 20) + " MB free, " + (need >> 20) + " MB needed");
                err[0] = String.format(java.util.Locale.ROOT, "The installation needs about %.1f GB of free storage (the "
                    + "image is copied in and extracted), but only %.1f GB are free. Please free some storage and "
                    + "install again.", need / 1073741824.0, free / 1073741824.0);
                return null;
            }
        }
        PortActivity.deleteTree(dir);
        dir.mkdirs();
        File out = new File(dir, name);
        long t0 = System.currentTimeMillis(), n = 0, shown = 0;
        copying = true;
        copyCancel = false;
        try (InputStream in = ctx.getContentResolver().openInputStream(u); OutputStream o = new FileOutputStream(out)) {
            if (in == null) return null;
            byte[] buf = new byte[1 << 20];
            int r;
            while ((r = in.read(buf)) > 0) {
                if (copyCancel) { o.close(); PortActivity.deleteTree(dir); return null; }
                o.write(buf, 0, r);
                n += r;
                long now = System.currentTimeMillis();
                if (now - shown > 200) {
                    shown = now;
                    final String t = "Copying the disc image - " + (n >> 20) + (size > 0 ? " of " + (size >> 20) : "") + " MB";
                    final float f = size > 0 ? 0.02f * n / size : 0;
                    ui(act, () -> view.launcherProgress(t, f));
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "[SETUP] cannot copy " + u + ": " + e);
            PortActivity.deleteTree(dir);
            return null;
        } finally {
            copying = false;
        }
        Log.i(TAG, "[SETUP] " + u + " -> " + out + " (" + n + " bytes in " + (System.currentTimeMillis() - t0) + " ms)");
        return out.getAbsolutePath();
    }

    private String displayName(Uri u) {
        try (Cursor c = act.getContentResolver().query(u, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                String n = c.getString(0);
                if (n != null && !n.isEmpty()) return n;
            }
        } catch (Exception ignored) { }
        String s = u.getLastPathSegment();
        return s != null ? s : u.toString();
    }
}

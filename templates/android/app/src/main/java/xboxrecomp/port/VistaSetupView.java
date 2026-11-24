// VistaSetupView - the Android setup screen in the Windows Vista look (2026-11-23; the maintainer's rule:
// Windows = Win95, Linux = Win2000, Android = Vista). Everything is drawn here, procedurally - own art, no Microsoft
// graphics or fonts: a light-blue glass frame, the dark navy -> teal header band with its glass panes (they drift,
// grow, flip a white corner, a cyan glow pulses behind them), the glossy green progress bar with a white glint
// sweeping over its filled part, glossy rounded buttons, a Vista-style question box.
//
// The standard setup screen of all Android ports (maintainer 2026-11-23: ZOE's Android launcher is the design
// standard; ZOE's view with Orta's rows, in the toolkit's Android template since 2026-11-24). The rows: the disc image - and, Android only, the graphics driver: no driver ships with the app (the maintainer 2026-11-23), so
// two choices - the system driver (the default) or a Turnip driver from the player's own .adpkg.zip (Snapdragon only;
// kept in the same slots as the BACK menu's Graphics driver page). Quit / Back / the frame's X during the installation
// ask first ("The installation is not finished. Quit anyway?").
//
// The steps themselves: the toolkit's launcher (option-c src/launcher) in libmain.so; PortSetup.java passes its
// progress, messages and the "which disc image?" page between it (JNI, jni/loader/setup_ui_android.c) and this view.
// SPDX-License-Identifier: GPL-3.0-or-later
package xboxrecomp.port;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

final class VistaSetupView extends View {

    /** What the activity does for the view. */
    interface Host {
        void pickFile(int row);         // ROW_DISC / PICK_DRIVER
        void quit();
        void play();
        void install();                 // Install pressed (the disc chosen): the image goes to the launcher
        void messageClosed();           // a message box's OK
    }

    static final int ROW_DISC = 0, PICK_DRIVER = 3;

    private final String GAME;                  // the game's name (-PsetupGame, R.string.setup_game)
    private final String DISC;                  // the disc asked for, with its region (-PsetupDisc, R.string.setup_disc)
    private static final int ROWS = 1;
    private static final int FORM = 0, INSTALLING = 1, DONE = 2;
    private static final int M_NONE = 0, M_QUIT = 1, M_MESSAGE = 2, M_DRIVER = 3;

    // control ids
    private static final int C_BROWSE0 = 0, C_BROWSE1 = 1, C_BROWSE2 = 2, C_DRIVER = 3, C_GO = 4, C_QUIT = 5,
            C_CLOSE = 6, C_YES = 10, C_NO = 11, C_OK = 12, C_CANCEL = 13, C_ITEM = 100;

    private final Host host;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Typeface regular = Typeface.create("sans-serif", Typeface.NORMAL);
    private final Typeface bold = Typeface.create("sans-serif", Typeface.BOLD);

    private int state = FORM, modal = M_NONE;
    private String modalText = "";
    private final String[] rowName = { "" };
    private static final String[] ROW_LABEL = { "Disc image:" };

    // the graphics driver row
    private final boolean adreno;
    private String driver;              // shown name
    private boolean driverByPlayer, systemChosen, driverKept;
    private final List<String> driverChoices = new ArrayList<>();

    private float fraction;
    private String stepText = "";
    private long animStart = SystemClock.uptimeMillis();

    // controls (rebuilt every frame from the layout)
    private static final class Ctl {
        final int id; final RectF r; final boolean enabled;
        Ctl(int id, RectF r, boolean enabled) { this.id = id; this.r = r; this.enabled = enabled; }
    }
    private final List<Ctl> ctls = new ArrayList<>();
    private int focus = C_GO, pressed = -1;
    private boolean keyNav;             // the focus ring only after a key was used (as Windows)

    private Bitmap backdrop;
    private float s;                    // pixels per layout unit

    VistaSetupView(Context c, Host host, boolean adreno) {
        super(c);
        GAME = c.getString(R.string.setup_game);
        DISC = c.getString(R.string.setup_disc);
        this.host = host;
        this.adreno = adreno;
        driver = "system driver";
        driverChoices.add("System driver");
        driverChoices.add("Turnip driver from a file (.adpkg.zip)...");
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    // ---- from the activity -------------------------------------------------------------------------------------

    void setRow(int row, String name) {
        if (row == PICK_DRIVER) {
            String n = name.replaceAll("(?i)\\.adpkg\\.zip$|\\.zip$", "");
            driver = n;
            driverByPlayer = true;
            systemChosen = false;
            driverKept = false;
        } else {
            rowName[row] = name;
        }
        invalidate();
    }

    /** The Turnip line chosen (a file picked), not the system driver. */
    boolean driverFromFile() { return driverByPlayer; }

    /** "System driver" chosen in the box (a Turnip in use from an earlier install is switched off at Install). */
    boolean systemChosen() { return systemChosen; }

    /** The driver already in use (an earlier install / the BACK menu): shown until the player changes it. */
    void setDriverInUse(String name) {
        if (name == null || driverByPlayer || systemChosen) return;
        driver = name;
        driverKept = true;
        invalidate();
    }

    /** BACK: a box open -> its "no"; installing -> ask; else quit. */
    void back() {
        if (modal == M_QUIT) { modal = M_NONE; focus = C_QUIT; }
        else if (modal != M_NONE) { modal = M_NONE; focus = C_DRIVER; }
        else if (state == INSTALLING) askQuit();
        else host.quit();
        invalidate();
    }

    boolean key(KeyEvent e) {
        int k = e.getKeyCode();
        switch (k) {
            case KeyEvent.KEYCODE_DPAD_UP: case KeyEvent.KEYCODE_DPAD_LEFT: case KeyEvent.KEYCODE_DPAD_DOWN:
            case KeyEvent.KEYCODE_DPAD_RIGHT: case KeyEvent.KEYCODE_TAB: case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_SPACE:
            case KeyEvent.KEYCODE_BUTTON_START: case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BACK:
            case KeyEvent.KEYCODE_ESCAPE:
                break;
            default:
                return false;           // volume etc. stay the system's
        }
        if (e.getAction() != KeyEvent.ACTION_DOWN || e.getRepeatCount() > 0 && k != KeyEvent.KEYCODE_DPAD_UP
            && k != KeyEvent.KEYCODE_DPAD_DOWN) return true;
        switch (k) {
            case KeyEvent.KEYCODE_DPAD_UP: case KeyEvent.KEYCODE_DPAD_LEFT: move(-1, k == KeyEvent.KEYCODE_DPAD_UP); return true;
            case KeyEvent.KEYCODE_DPAD_DOWN: case KeyEvent.KEYCODE_DPAD_RIGHT: move(1, k == KeyEvent.KEYCODE_DPAD_DOWN); return true;
            case KeyEvent.KEYCODE_TAB: move(e.isShiftPressed() ? -1 : 1, false); return true;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_SPACE: case KeyEvent.KEYCODE_BUTTON_START:
                if (!keyNav) { keyNav = true; invalidate(); return true; }   // first press only shows the focus
                activate(focus);
                return true;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_ESCAPE:
                back();
                return true;
            default:
                return false;
        }
    }

    // ---- logic --------------------------------------------------------------------------------------------------

    private void askQuit() {
        modal = M_QUIT;
        modalText = "The installation is not finished. Quit anyway?\n\nThe game is not installed until the setup "
                  + "has finished; the next start begins again.";
        focus = C_NO;                   // the safe answer is the default
    }

    /** A message box from the launcher (OK calls Host.messageClosed). */
    void showMessage(String text) { message(text, state == INSTALLING ? C_QUIT : C_GO); invalidate(); }

    /** The launcher works without the form first (a rebuild after an app update: the game is installed already). */
    void busy() {
        if (state == INSTALLING) return;
        animStart = SystemClock.uptimeMillis();
        state = INSTALLING;
        fraction = 0;
        stepName = "";
        stepText = "";
        focus = C_QUIT;
        invalidate();
    }

    boolean installing() { return state == INSTALLING; }

    /** The form again (the launcher asks for the disc image: the first time, or after a wrong one). */
    void form() {
        state = FORM;
        fraction = 0;
        stepName = "";
        stepText = "";
        if (modal == M_QUIT) modal = M_NONE;
        if (modal == M_NONE) focus = C_GO;
        invalidate();
    }

    /** The launcher's text as it is ("<step> - <n> s" -> "<step> - m:ss"). */
    void launcherProgress(String text, float f) {
        busy();
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("^(.*) - (\\d+) s$").matcher(text);
        if (m.matches()) {
            int so = Integer.parseInt(m.group(2));
            stepText = m.group(1) + " - " + (so / 60) + ":" + String.format(java.util.Locale.ROOT, "%02d", so % 60);
        } else {
            stepText = text;
        }
        stepName = "";              // (tick() keeps the launcher's own text)
        fraction = Math.max(fraction, Math.min(1, f));
        invalidate();
    }

    private void message(String text, int back) {
        modal = M_MESSAGE;
        modalText = text;
        focus = C_OK;
        msgBack = back;
    }
    private int msgBack = C_GO;

    private void activate(int id) {
        Ctl c = find(id);
        if (c == null || !c.enabled) return;
        switch (id) {
            case C_BROWSE0: host.pickFile(ROW_DISC); break;
            case C_DRIVER: modal = M_DRIVER; focus = C_ITEM; break;
            case C_GO:
                if (state == DONE) { host.play(); break; }
                if (rowName[ROW_DISC].isEmpty()) {
                    message("Please choose your " + GAME + " disc image (.iso) with Browse...", C_BROWSE0);
                    break;
                }
                startInstall();
                break;
            case C_QUIT: case C_CLOSE:
                if (state == INSTALLING) askQuit(); else host.quit();
                break;
            case C_YES: modal = M_NONE; host.quit(); break;     // (the launcher stops; its text says "stopping")
            case C_NO: modal = M_NONE; focus = C_QUIT; break;
            case C_OK: modal = M_NONE; focus = msgBack; host.messageClosed(); break;
            case C_CANCEL: modal = M_NONE; focus = C_DRIVER; break;
            default:
                if (id >= C_ITEM) {
                    String ch = driverChoices.get(id - C_ITEM);
                    modal = M_NONE;
                    focus = C_DRIVER;
                    if (ch.startsWith("Turnip driver")) {
                        if (!adreno) {
                            message("Turnip drivers work only on Adreno GPUs (Snapdragon). This device needs the "
                                    + "system driver.", C_DRIVER);
                            break;
                        }
                        host.pickFile(PICK_DRIVER);
                        break;
                    }
                    driver = "system driver";
                    driverByPlayer = false;
                    driverKept = false;
                    systemChosen = true;
                }
        }
        invalidate();
    }

    private void startInstall() {
        animStart = SystemClock.uptimeMillis();
        state = INSTALLING;
        fraction = 0;
        stepName = "";
        stepText = "";
        focus = C_QUIT;
        host.install();
    }

    // ---- from the setup (PortSetup, on the UI thread) ---------------------------------------------------------------

    private String stepName = "";
    private long stepStart;

    /** A step and the whole setup's fraction. As the PC launcher: the step and its time so far, no estimate (the
     *  maintainer: estimates only worry). */
    void progress(String step, float f) {
        if (state != INSTALLING) return;
        if (!step.equals(stepName)) { stepName = step; stepStart = SystemClock.uptimeMillis(); }
        fraction = Math.max(fraction, Math.min(1, f));
        invalidate();
    }

    void finished() {
        state = DONE;
        fraction = 1;
        stepName = "Done";
        stepText = "Done";
        modal = M_NONE;
        focus = C_GO;
        invalidate();
    }

    void failed(String why, boolean wrongDisc) {
        state = FORM;
        fraction = 0;
        stepName = "";
        message(why, wrongDisc ? C_BROWSE0 : C_GO);
        invalidate();
    }

    /** A line above the rows' intro (why the setup shows again), or "". */
    void setNote(String n) { note = n == null ? "" : n; invalidate(); }
    private String note = "";

    private void tick() {
        if (state != INSTALLING || stepName.isEmpty()) return;
        int so = (int) ((SystemClock.uptimeMillis() - stepStart) / 1000);
        stepText = stepName + " - " + (so / 60) + ":" + String.format(java.util.Locale.ROOT, "%02d", so % 60);
    }

    private Ctl find(int id) {
        for (Ctl c : ctls) if (c.id == id) return c;
        return null;
    }

    private void move(int d, boolean vertical) {
        if (!keyNav) { keyNav = true; invalidate(); return; }
        List<Ctl> en = new ArrayList<>();
        for (Ctl c : ctls) if (c.enabled && c.id != C_CLOSE) en.add(c);
        if (en.isEmpty()) return;
        int at = -1;
        for (int i = 0; i < en.size(); i++) if (en.get(i).id == focus) at = i;
        at = at < 0 ? 0 : (at + d + en.size()) % en.size();
        focus = en.get(at).id;
        invalidate();
    }

    // ---- touch ---------------------------------------------------------------------------------------------------

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        float x = e.getX(), y = e.getY();
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                keyNav = false;
                pressed = hit(x, y);
                invalidate();
                return true;
            case MotionEvent.ACTION_MOVE:
                if (pressed >= 0 && hit(x, y) != pressed) { pressed = -1; invalidate(); }
                return true;
            case MotionEvent.ACTION_UP:
                int h = hit(x, y);
                int was = pressed;
                pressed = -1;
                if (h >= 0 && h == was) { focus = h; activate(h); }
                invalidate();
                return true;
            case MotionEvent.ACTION_CANCEL:
                pressed = -1;
                invalidate();
                return true;
        }
        return false;
    }

    private int hit(float x, float y) {
        for (int i = ctls.size() - 1; i >= 0; i--) {
            Ctl c = ctls.get(i);
            if (c.enabled && c.r.contains(x, y)) return c.id;
        }
        return -1;
    }

    // ---- drawing -------------------------------------------------------------------------------------------------

    private static final float DW = 660;      // the window's width in layout units

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        backdrop = null;
    }

    @Override
    protected void onDraw(Canvas c) {
        tick();
        float W = getWidth(), H = getHeight();
        if (backdrop == null || backdrop.getWidth() != (int) W || backdrop.getHeight() != (int) H) backdrop = aurora((int) W, (int) H);
        c.drawBitmap(backdrop, 0, 0, null);
        ctls.clear();

        float bodyH = bodyHeight();
        float dh = FRAME + TITLE + HEADER + bodyH + FOOTER + FRAME;
        s = Math.min(W * 0.94f / DW, H * 0.95f / dh);
        float x = (W - DW * s) / 2, y = (H - dh * s) / 2;
        drawMain(c, x, y, dh);

        if (modal != M_NONE) drawModal(c, W, H);
        if (state == INSTALLING || modal == M_QUIT) postInvalidateOnAnimation();
    }

    private static final float FRAME = 7, TITLE = 26, HEADER = 58, FOOTER = 48, PAD = 22, ROW = 33;

    private float bodyHeight() {
        // intro (up to 3 lines) + the rows + driver button + note + bar + the status line
        return 14 + 3 * 16 + 10 + ROWS * ROW + 6 + 30 + 20 + 12 + 18 + 8 + 16 + 14;
    }

    private String intro() {
        switch (state) {
            case INSTALLING: return "The game is being built from your disc - this happens only once. Please keep the "
                                    + "app open; the device may get warm.";
            case DONE: return GAME + " is installed. Play starts the game - from now on the app starts it directly.";
            default: return (note.isEmpty() ? "" : note + " ") + "Choose your " + DISC + " disc image (.iso), "
                            + "then Install. The game is built from it once, on this device (the image is only read, "
                            + "never changed).";
        }
    }

    private void drawMain(Canvas c, float x, float y, float dh) {
        float w = DW * s, h = dh * s;
        String title = GAME + " Setup";
        frame(c, x, y, w, h, title, true);

        float cx = x + FRAME * s, cw = w - 2 * FRAME * s;
        float hy = y + (FRAME + TITLE) * s;
        String head = state == FORM ? "Install " + GAME : state == INSTALLING ? "Building " + GAME : GAME + " is ready";
        header(c, cx, hy, cw, HEADER * s, head, state == INSTALLING);

        // body
        float by = hy + HEADER * s, bh = bodyHeight() * s;
        p.setShader(null);
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFFFFFFFF);
        c.drawRect(cx, by, cx + cw, by + bh, p);

        float L = cx + PAD * s, R = cx + cw - PAD * s;
        float ty = by + 14 * s;
        p.setTypeface(regular);
        p.setTextSize(11.5f * s);
        p.setColor(0xFF1E395B);
        float line = 16 * s;
        int n = 0;
        for (String ln : wrap(intro(), R - L, 3)) { c.drawText(ln, L, ty + 11 * s + n * line, p); n++; }
        ty += 3 * line + 10 * s;

        boolean form = state == FORM;
        float labelW = 168 * s, browseW = 86 * s, fh = 24 * s;
        for (int r = 0; r < ROWS; r++) {
            float ry = ty + r * ROW * s;
            p.setTypeface(regular);
            p.setTextSize(11.5f * s);
            p.setColor(form ? 0xFF000000 : 0xFF6D6D6D);
            c.drawText(ROW_LABEL[r], L, ry + fh * 0.68f, p);
            RectF f = new RectF(L + labelW, ry, R - browseW - 8 * s, ry + fh);
            field(c, f, rowName[r], r == 0 ? "Choose your disc image with Browse..." : "(none)", form);
            RectF b = new RectF(R - browseW, ry, R, ry + fh);
            button(c, b, "Browse...", C_BROWSE0 + r, form, false);
        }
        ty += ROWS * ROW * s + 6 * s;

        // the graphics driver: one wide button, the note under it
        RectF d = new RectF(L, ty, R, ty + 30 * s);
        driverButton(c, d, form);
        ty += 30 * s + 15 * s;
        p.setShader(null);
        p.setTypeface(regular);
        p.setTextSize(10.5f * s);
        p.setColor(0xFF6D6D6D);
        c.drawText(driverByPlayer || driverKept ? "Turnip from your file - change it any time in the BACK menu."
                                  : "No driver comes with the app. A Turnip driver (Snapdragon) can be chosen here or later in the BACK menu.",
                   L, ty, p);
        ty += 17 * s;

        // the progress bar, the status under it with the percentage on the right
        RectF bar = new RectF(L, ty, R, ty + 18 * s);
        progress(c, bar, state == FORM ? 0 : fraction);
        ty += 18 * s + 18 * s;
        p.setShader(null);
        p.setTypeface(regular);
        p.setTextSize(11.5f * s);
        p.setColor(0xFF000000);
        String st = state == FORM ? "Ready to install." : stepText;
        c.drawText(ellipsize(st, R - L - 60 * s), L, ty, p);
        if (state != FORM) {
            String pc = Math.round(fraction * 100) + "%";
            c.drawText(pc, R - p.measureText(pc), ty, p);
        }


        // footer
        float fy = by + bh;
        p.setShader(null);
        p.setColor(0xFFF0F0F0);
        c.drawRect(cx, fy, cx + cw, fy + FOOTER * s, p);
        p.setColor(0xFFDFDFDF);
        c.drawRect(cx, fy, cx + cw, fy + Math.max(1, s), p);
        float bw = 92 * s, bhh = 27 * s, byy = fy + (FOOTER * s - bhh) / 2;
        RectF q = new RectF(R - bw, byy, R, byy + bhh);
        RectF g = new RectF(R - 2 * bw - 10 * s, byy, R - bw - 10 * s, byy + bhh);
        button(c, g, state == DONE ? "Play" : "Install", C_GO, state != INSTALLING, state != INSTALLING);
        button(c, q, "Quit", C_QUIT, true, state == INSTALLING);
        p.setShader(null);
        p.setTypeface(regular);
        p.setTextSize(10f * s);
        p.setColor(0xFF8A8A8A);
    }

    // the Aero-like glass frame with its caption and the red close box; returns nothing, adds C_CLOSE
    private void frame(Canvas c, float x, float y, float w, float h, String title, boolean closeBox) {
        float r = 7 * s;
        RectF o = new RectF(x, y, x + w, y + h);
        // soft shadow
        p.setShader(null);
        p.setStyle(Paint.Style.FILL);
        p.setColor(0x55000000);
        p.setMaskFilter(new BlurMaskFilter(12 * s, BlurMaskFilter.Blur.NORMAL));
        c.drawRoundRect(new RectF(x + 2 * s, y + 6 * s, x + w + 2 * s, y + h + 8 * s), r, r, p);
        p.setMaskFilter(null);
        // glass
        p.setShader(new LinearGradient(0, y, 0, y + h, new int[] { 0xFFD3E5F5, 0xFFB9D3EC, 0xFFAECBE8, 0xFFC4DCF1 },
                                       new float[] { 0, 0.08f, 0.6f, 1 }, Shader.TileMode.CLAMP));
        c.drawRoundRect(o, r, r, p);
        // a diagonal sheen across the glass
        p.setShader(new LinearGradient(x, y, x + w * 0.5f, y + h, new int[] { 0x00FFFFFF, 0x40FFFFFF, 0x00FFFFFF },
                                       new float[] { 0.25f, 0.4f, 0.55f }, Shader.TileMode.CLAMP));
        c.drawRoundRect(o, r, r, p);
        p.setShader(null);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(Math.max(1, s));
        p.setColor(0xFF2B3F57);
        c.drawRoundRect(o, r, r, p);
        p.setColor(0xB0FFFFFF);
        RectF in = new RectF(o);
        in.inset(Math.max(1, s), Math.max(1, s));
        c.drawRoundRect(in, r - s, r - s, p);
        // the client edge
        p.setColor(0xFF7E9CBC);
        c.drawRect(x + FRAME * s - s, y + (FRAME + TITLE) * s - s, x + w - FRAME * s + s, y + h - FRAME * s + s, p);
        p.setStyle(Paint.Style.FILL);
        // the caption: a small icon and the title with a white glow
        float ix = x + 12 * s, iy = y + 8 * s;
        icon(c, ix, iy, 16 * s);
        p.setTypeface(regular);
        p.setTextSize(12f * s);
        p.setColor(0xFFFFFFFF);
        p.setMaskFilter(new BlurMaskFilter(4 * s, BlurMaskFilter.Blur.NORMAL));
        c.drawText(title, ix + 22 * s, y + 21 * s, p);
        p.setMaskFilter(null);
        p.setColor(0xFF000000);
        c.drawText(title, ix + 22 * s, y + 21 * s, p);
        if (!closeBox) return;
        RectF cb = new RectF(x + w - 54 * s, y + 1 * s, x + w - 10 * s, y + 20 * s);
        closeButton(c, cb);
        ctls.add(new Ctl(C_CLOSE, cb, true));
    }

    private void icon(Canvas c, float x, float y, float z) {
        // a little disc with a green arrow (own drawing)
        p.setShader(new RadialGradient(x + z * 0.4f, y + z * 0.35f, z * 0.7f, 0xFFFFFFFF, 0xFF9DB7CF, Shader.TileMode.CLAMP));
        c.drawCircle(x + z / 2, y + z / 2, z / 2, p);
        p.setShader(null);
        p.setColor(0xFF5D7894);
        c.drawCircle(x + z / 2, y + z / 2, z * 0.12f, p);
        p.setColor(0xFF2EAF32);
        Path a = new Path();
        a.moveTo(x + z * 0.55f, y + z * 0.55f);
        a.lineTo(x + z * 1.05f, y + z * 0.78f);
        a.lineTo(x + z * 0.55f, y + z * 1.02f);
        a.close();
        c.drawPath(a, p);
    }

    private void closeButton(Canvas c, RectF r) {
        float rr = 3 * s;
        boolean down = pressed == C_CLOSE;
        p.setShader(new LinearGradient(0, r.top, 0, r.bottom, down
                ? new int[] { 0xFFC77866, 0xFFAD3A2A, 0xFF8E1F10, 0xFFB5442F }
                : new int[] { 0xFFE9A99B, 0xFFD47462, 0xFFC0391F, 0xFFD8674E },
                new float[] { 0, 0.48f, 0.5f, 1 }, Shader.TileMode.CLAMP));
        c.drawRoundRect(r, rr, rr, p);
        p.setShader(null);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(Math.max(1, s));
        p.setColor(0xFF5E1A10);
        c.drawRoundRect(r, rr, rr, p);
        p.setColor(0xFFFFFFFF);
        p.setStrokeWidth(2.2f * s);
        p.setStrokeCap(Paint.Cap.ROUND);
        float mx = r.centerX(), my = r.centerY(), k = 4.2f * s;
        c.drawLine(mx - k, my - k * 0.8f, mx + k, my + k * 0.8f, p);
        c.drawLine(mx - k, my + k * 0.8f, mx + k, my - k * 0.8f, p);
        p.setStyle(Paint.Style.FILL);
        p.setStrokeCap(Paint.Cap.BUTT);
    }

    /** The header band: navy -> teal, the title in white, and the glass panes on the right (animated while busy). */
    private void header(Canvas c, float x, float y, float w, float h, String text, boolean busy) {
        p.setShader(new LinearGradient(x, 0, x + w, 0, new int[] { 0xFF0C3760, 0xFF114B6E, 0xFF1A6A79, 0xFF23807F },
                                       new float[] { 0, 0.45f, 0.8f, 1 }, Shader.TileMode.CLAMP));
        c.drawRect(x, y, x + w, y + h, p);
        // a faint light along the top
        p.setShader(new LinearGradient(0, y, 0, y + h, new int[] { 0x22FFFFFF, 0x00FFFFFF, 0x18000000 },
                                       new float[] { 0, 0.5f, 1 }, Shader.TileMode.CLAMP));
        c.drawRect(x, y, x + w, y + h, p);
        p.setShader(null);
        p.setTypeface(regular);
        p.setTextSize(19f * s);
        p.setColor(0xFFFFFFFF);
        c.drawText(text, x + PAD * s, y + h / 2 + 7 * s, p);

        // the panes: a loop of ~2 s. One big sheet drifts right and fades out while a small one behind it grows into
        // the next big one; halfway, the big sheet's corner flips white; a cyan glow pulses behind both.
        float t = busy ? ((SystemClock.uptimeMillis() - animStart) % 2000) / 2000f : 0.15f;
        float px = x + w - 82 * s, py = y + 8 * s, pw = 30 * s, ph = h - 16 * s;
        c.save();
        c.clipRect(x, y, x + w, y + h);
        float glow = busy ? 0.55f + 0.45f * (float) Math.sin(t * 2 * Math.PI) : 0.6f;
        p.setShader(new RadialGradient(px + pw * 0.6f, py + ph * 0.5f, 46 * s,
                                       new int[] { ((int) (110 * glow) << 24) | 0x7FF6FF, 0x0040D0E0 }, null,
                                       Shader.TileMode.CLAMP));
        c.drawCircle(px + pw * 0.6f, py + ph * 0.5f, 46 * s, p);
        p.setShader(null);
        // the small one growing behind (it becomes the big one at the loop's end)
        float g = t;
        pane(c, px - 14 * s + 14 * s * g, py + ph * 0.5f * (1 - (0.55f + 0.45f * g)), pw * (0.55f + 0.45f * g),
             ph * (0.55f + 0.45f * g), 0.15f + 0.45f * g, 0);
        // the big one: drifts right, fades
        float flip = busy ? Math.max(0, Math.min(1, (t - 0.3f) / 0.35f)) : 0;
        pane(c, px + 22 * s * t, py, pw, ph, 0.6f * (1 - t * t), flip);
        c.restore();
    }

    /** a glass sheet: translucent white, brighter at the top, a thin white edge; `flip` 0..1 turns its top-right corner */
    private void pane(Canvas c, float x, float y, float w, float h, float a, float flip) {
        if (a <= 0.01f) return;
        int A = (int) (255 * a);
        p.setShader(new LinearGradient(x, y, x + w, y + h, new int[] { (A * 7 / 10) << 24 | 0xFFFFFF, (A * 2 / 10) << 24 | 0xE0FFFF,
                                       (A * 3 / 10) << 24 | 0xC8F4FF }, new float[] { 0, 0.55f, 1 }, Shader.TileMode.CLAMP));
        c.drawRect(x, y, x + w, y + h, p);
        p.setShader(null);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(Math.max(1, s));
        p.setColor((A * 8 / 10) << 24 | 0xFFFFFF);
        c.drawRect(x, y, x + w, y + h, p);
        p.setStyle(Paint.Style.FILL);
        if (flip > 0 && flip < 1) {
            // the corner folds over: a white triangle that grows then shrinks (cosine of a half turn)
            float k = (float) Math.sin(flip * Math.PI) * w * 0.45f;
            Path tri = new Path();
            tri.moveTo(x + w, y);
            tri.lineTo(x + w - k, y);
            tri.lineTo(x + w, y + k);
            tri.close();
            p.setColor(A << 24 | 0xFFFFFF);
            c.drawPath(tri, p);
        }
    }

    /** the sunken edit field: white, a thin grey-blue border; empty -> the hint in grey */
    private void field(Canvas c, RectF r, String text, String hint, boolean enabled) {
        p.setShader(null);
        p.setStyle(Paint.Style.FILL);
        p.setColor(enabled ? 0xFFFFFFFF : 0xFFF4F4F4);
        c.drawRect(r, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(Math.max(1, s));
        p.setColor(0xFFABADB3);
        c.drawRect(r, p);
        p.setColor(0xFFE3E9EF);
        c.drawLine(r.left + s, r.bottom - s, r.right - s, r.bottom - s, p);
        p.setStyle(Paint.Style.FILL);
        p.setTypeface(regular);
        p.setTextSize(11.5f * s);
        boolean empty = text.isEmpty();
        p.setColor(empty ? 0xFF8D8D8D : enabled ? 0xFF000000 : 0xFF6D6D6D);
        String shown = ellipsizeStart(empty ? hint : text, r.width() - 10 * s);
        c.drawText(shown, r.left + 5 * s, r.top + r.height() * 0.68f, p);
    }

    /** A Vista push button: rounded, light grey glossy (top half lighter); focused / default = the blue glow;
     *  pressed = deeper blue. */
    private void button(Canvas c, RectF r, String label, int id, boolean enabled, boolean isDefault) {
        ctls.add(new Ctl(id, r, enabled));
        boolean down = enabled && pressed == id, foc = enabled && keyNav && focus == id && modalOwns(id);
        float rr = 3 * s;
        int[] col;
        int edge;
        if (!enabled) { col = new int[] { 0xFFF4F4F4, 0xFFF4F4F4, 0xFFF4F4F4, 0xFFF4F4F4 }; edge = 0xFFADB2B5; }
        else if (down) { col = new int[] { 0xFFE5F4FC, 0xFFC4E5F6, 0xFF98D1EF, 0xFF68B3DB }; edge = 0xFF2C628B; }
        else if (foc || isDefault) { col = new int[] { 0xFFEAF6FD, 0xFFD9F0FC, 0xFFBEE6FD, 0xFFA7D9F5 }; edge = 0xFF3C7FB1; }
        else { col = new int[] { 0xFFF2F2F2, 0xFFEBEBEB, 0xFFDDDDDD, 0xFFCFCFCF }; edge = 0xFF707070; }
        p.setShader(new LinearGradient(0, r.top, 0, r.bottom, col, new float[] { 0, 0.49f, 0.5f, 1 }, Shader.TileMode.CLAMP));
        p.setStyle(Paint.Style.FILL);
        c.drawRoundRect(r, rr, rr, p);
        p.setShader(null);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(Math.max(1, s));
        p.setColor(edge);
        c.drawRoundRect(r, rr, rr, p);
        RectF in = new RectF(r);
        in.inset(Math.max(1, s), Math.max(1, s));
        p.setColor(0xCCFFFFFF);
        c.drawRoundRect(in, rr, rr, p);
        if (foc) {
            // the keyboard focus: Vista's soft blue glow around + the dotted rectangle inside
            p.setColor(0x8048A8E8);
            p.setStrokeWidth(2.5f * s);
            RectF out = new RectF(r);
            out.inset(-2 * s, -2 * s);
            c.drawRoundRect(out, rr + 2 * s, rr + 2 * s, p);
            p.setStrokeWidth(Math.max(1, s));
            p.setColor(0xFF000000);
            p.setPathEffect(new android.graphics.DashPathEffect(new float[] { s, s }, 0));
            RectF dot = new RectF(r);
            dot.inset(3.5f * s, 3.5f * s);
            c.drawRect(dot, p);
            p.setPathEffect(null);
        }
        p.setStyle(Paint.Style.FILL);
        p.setTypeface(regular);
        p.setTextSize(11.5f * s);
        p.setColor(enabled ? 0xFF000000 : 0xFF838383);
        c.drawText(label, r.centerX() - p.measureText(label) / 2, r.centerY() + 4.2f * s, p);
    }

    private boolean modalOwns(int id) { return modal == M_NONE ? id < 10 : id >= 10; }

    private void driverButton(Canvas c, RectF r, boolean enabled) {
        button(c, r, "", C_DRIVER, enabled, false);
        p.setTypeface(regular);
        p.setTextSize(11.5f * s);
        p.setColor(enabled ? 0xFF000000 : 0xFF838383);
        float tx = r.left + 12 * s, by = r.centerY() + 4.2f * s;
        String a = "Graphics driver: ";
        c.drawText(a, tx, by, p);
        tx += p.measureText(a);
        p.setTypeface(bold);
        c.drawText(driver, tx, by, p);
        tx += p.measureText(driver);
        p.setTypeface(regular);
        p.setColor(enabled ? 0xFF0066CC : 0xFF838383);
        c.drawText("   (change...)", tx, by, p);
    }

    /** The Vista progress bar: a light grey sunken track, the glossy green fill (light top half, darker lower band,
     *  a dark green right edge) and a soft white-green glint sweeping left -> right over the filled part (0.6 s,
     *  every 1.9 s) while busy. */
    private void progress(Canvas c, RectF r, float f) {
        float rr = 2.5f * s;
        p.setStyle(Paint.Style.FILL);
        p.setShader(new LinearGradient(0, r.top, 0, r.bottom, new int[] { 0xFFF3F3F3, 0xFFE6E6E6, 0xFFD6D6D6, 0xFFE2E2E2 },
                                       new float[] { 0, 0.45f, 0.55f, 1 }, Shader.TileMode.CLAMP));
        c.drawRoundRect(r, rr, rr, p);
        p.setShader(null);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(Math.max(1, s));
        p.setColor(0xFFB2B2B2);
        c.drawRoundRect(r, rr, rr, p);
        p.setStyle(Paint.Style.FILL);
        if (f <= 0) return;
        float in = Math.max(1, s);
        RectF fill = new RectF(r.left + in, r.top + in, r.left + in + (r.width() - 2 * in) * Math.min(1, f), r.bottom - in);
        c.save();
        c.clipRect(fill);
        p.setShader(new LinearGradient(0, fill.top, 0, fill.bottom,
                new int[] { 0xFFC8F7BC, 0xFF6FE35B, 0xFF33C52A, 0xFF22AE1E, 0xFF3BD236 },
                new float[] { 0, 0.42f, 0.5f, 0.78f, 1 }, Shader.TileMode.CLAMP));
        c.drawRect(fill, p);
        // a darker green right edge
        p.setShader(new LinearGradient(fill.right - 6 * s, 0, fill.right, 0, 0x00107A10, 0xC0107A10, Shader.TileMode.CLAMP));
        c.drawRect(fill.right - 6 * s, fill.top, fill.right, fill.bottom, p);
        // the glint
        if (state == INSTALLING) {
            long ms = (SystemClock.uptimeMillis() - animStart) % 1900;
            if (ms < 600) {
                float k = ms / 600f;
                float gw = Math.max(40 * s, fill.width() * 0.35f);
                float gx = fill.left - gw + (fill.width() + 2 * gw) * k;
                c.save();
                c.translate(gx, fill.centerY());
                c.scale(1f, fill.height() / gw);
                p.setShader(new RadialGradient(0, 0, gw / 2, new int[] { 0xD0FFFFFF, 0x80E8FFE0, 0x00E8FFE0 },
                                               new float[] { 0, 0.45f, 1 }, Shader.TileMode.CLAMP));
                c.drawCircle(0, 0, gw / 2, p);
                c.restore();
            }
        }
        c.restore();
        p.setShader(null);
    }

    // ---- the boxes over the window -------------------------------------------------------------------------------

    private void drawModal(Canvas c, float W, float H) {
        p.setShader(null);
        p.setColor(0x30000000);
        c.drawRect(0, 0, W, H, p);
        ctls.clear();                       // only the box takes input now
        if (modal == M_DRIVER) { driverBox(c, W, H); return; }

        float bw = 430 * s;
        p.setTypeface(regular);
        p.setTextSize(11.5f * s);
        List<String> lines = new ArrayList<>();
        for (String para : modalText.split("\n", -1)) {
            if (para.isEmpty()) { lines.add(""); continue; }
            lines.addAll(wrap(para, bw - (2 * FRAME + 80) * s, 6));
        }
        float bodyH = Math.max(58, 28 + lines.size() * 16) * s;
        float bh = (FRAME + TITLE + FRAME + FOOTER) * s + bodyH;
        float x = (W - bw) / 2, y = (H - bh) / 2;
        boxBase(c, x, y, bw, bh);
        frame(c, x, y, bw, bh, GAME + " Setup", false);
        float cx = x + FRAME * s, cw = bw - 2 * FRAME * s, by = y + (FRAME + TITLE) * s;
        p.setShader(null);
        p.setColor(0xFFFFFFFF);
        c.drawRect(cx, by, cx + cw, by + bodyH, p);
        // the icon: a question (blue circle) or information
        float ic = 32 * s, ix = cx + 18 * s, iy = by + 16 * s;
        p.setShader(new RadialGradient(ix + ic * 0.35f, iy + ic * 0.3f, ic * 0.8f, 0xFF8FC8FF, 0xFF1F5FB8, Shader.TileMode.CLAMP));
        c.drawCircle(ix + ic / 2, iy + ic / 2, ic / 2, p);
        p.setShader(null);
        p.setColor(0xFFFFFFFF);
        p.setTypeface(bold);
        p.setTextSize(20 * s);
        String sym = modal == M_QUIT ? "?" : "i";
        c.drawText(sym, ix + ic / 2 - p.measureText(sym) / 2, iy + ic / 2 + 7 * s, p);
        p.setTypeface(regular);
        p.setTextSize(11.5f * s);
        float tx = ix + ic + 16 * s, ty = by + 26 * s;
        for (int i = 0; i < lines.size(); i++) {
            p.setColor(i == 0 && modal == M_QUIT ? 0xFF003399 : 0xFF000000);
            if (i == 0 && modal == M_QUIT) { p.setTypeface(regular); p.setTextSize(13f * s); }
            else { p.setTextSize(11.5f * s); }
            c.drawText(lines.get(i), tx, ty + i * 16 * s, p);
        }
        float fy = by + bodyH;
        p.setColor(0xFFF0F0F0);
        c.drawRect(cx, fy, cx + cw, fy + FOOTER * s, p);
        p.setColor(0xFFDFDFDF);
        c.drawRect(cx, fy, cx + cw, fy + Math.max(1, s), p);
        float bbw = 80 * s, bbh = 27 * s, byy = fy + (FOOTER * s - bbh) / 2, R = cx + cw - 16 * s;
        if (modal == M_QUIT) {
            button(c, new RectF(R - 2 * bbw - 10 * s, byy, R - bbw - 10 * s, byy + bbh), "Yes", C_YES, true, false);
            button(c, new RectF(R - bbw, byy, R, byy + bbh), "No", C_NO, true, true);
        } else {
            button(c, new RectF(R - bbw, byy, R, byy + bbh), "OK", C_OK, true, true);
        }
    }

    /** A box's glass is drawn see-through (the main window's look); under a box an opaque base, so the window's text
     *  behind it does not show through its caption (2026-11-24, Orta on the Odin). */
    private void boxBase(Canvas c, float x, float y, float w, float h) {
        p.setShader(null);
        p.setMaskFilter(null);
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFFB9D3EC);
        c.drawRoundRect(new RectF(x, y, x + w, y + h), 7 * s, 7 * s, p);
    }

    private void driverBox(Canvas c, float W, float H) {
        float bw = 440 * s, rowH = 30 * s;
        float bodyH = 44 * s + driverChoices.size() * rowH + 12 * s;
        float bh = (FRAME + TITLE + FRAME + FOOTER) * s + bodyH;
        float x = (W - bw) / 2, y = (H - bh) / 2;
        boxBase(c, x, y, bw, bh);
        frame(c, x, y, bw, bh, "Graphics driver", false);
        float cx = x + FRAME * s, cw = bw - 2 * FRAME * s, by = y + (FRAME + TITLE) * s;
        p.setShader(null);
        p.setColor(0xFFFFFFFF);
        c.drawRect(cx, by, cx + cw, by + bodyH, p);
        p.setTypeface(regular);
        p.setTextSize(13f * s);
        p.setColor(0xFF003399);
        c.drawText("Which graphics driver should the game use?", cx + 16 * s, by + 26 * s, p);
        float ly = by + 44 * s;
        for (int i = 0; i < driverChoices.size(); i++) {
            RectF r = new RectF(cx + 12 * s, ly + i * rowH, cx + cw - 12 * s, ly + (i + 1) * rowH - 2 * s);
            int id = C_ITEM + i;
            ctls.add(new Ctl(id, r, true));
            String ch = driverChoices.get(i);
            boolean cur = isCurrent(ch);
            boolean hot = pressed == id || (keyNav && focus == id);
            if (hot || cur) {
                p.setShader(new LinearGradient(0, r.top, 0, r.bottom, hot ? 0xFFF2F9FE : 0xFFF5F9FC, hot ? 0xFFD6EEFC : 0xFFE5EEF6,
                                               Shader.TileMode.CLAMP));
                c.drawRoundRect(r, 3 * s, 3 * s, p);
                p.setShader(null);
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeWidth(Math.max(1, s));
                p.setColor(hot ? 0xFF7DA2CE : 0xFFC4D5E6);
                c.drawRoundRect(r, 3 * s, 3 * s, p);
                p.setStyle(Paint.Style.FILL);
            }
            // a green arrow like Vista's command links
            p.setColor(0xFF2E9A35);
            Path a = new Path();
            float ax = r.left + 10 * s, ay = r.centerY();
            a.moveTo(ax, ay - 5 * s); a.lineTo(ax + 7 * s, ay); a.lineTo(ax, ay + 5 * s); a.close();
            c.drawPath(a, p);
            p.setTypeface(cur ? bold : regular);
            p.setTextSize(12f * s);
            p.setColor(0xFF15428B);
            c.drawText(ch + (cur ? "   (chosen)" : ""), r.left + 26 * s, r.centerY() + 4.5f * s, p);
        }
        float fy = by + bodyH;
        p.setColor(0xFFF0F0F0);
        c.drawRect(cx, fy, cx + cw, fy + FOOTER * s, p);
        p.setColor(0xFFDFDFDF);
        c.drawRect(cx, fy, cx + cw, fy + Math.max(1, s), p);
        float bbw = 80 * s, bbh = 27 * s, byy = fy + (FOOTER * s - bbh) / 2, R = cx + cw - 16 * s;
        button(c, new RectF(R - bbw, byy, R, byy + bbh), "Cancel", C_CANCEL, true, false);
        if (focus < C_ITEM && focus != C_CANCEL) focus = C_ITEM;
    }

    private boolean isCurrent(String choice) {
        if (choice.equals("System driver")) return driver.equals("system driver");
        return driverByPlayer || driverKept;        // the Turnip line: a file was chosen (now or earlier)
    }

    // ---- the backdrop: a procedural "aurora" - dark teal to green with soft light bands (own art) ------------------

    private static Bitmap aurora(int w, int h) {
        Bitmap b = Bitmap.createBitmap(Math.max(1, w), Math.max(1, h), Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(b);
        Paint q = new Paint(Paint.ANTI_ALIAS_FLAG);
        q.setShader(new LinearGradient(0, 0, w * 0.6f, h, new int[] { 0xFF0A4458, 0xFF0F6C70, 0xFF16857A, 0xFF0B5862 },
                                       new float[] { 0, 0.4f, 0.65f, 1 }, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, w, h, q);
        q.setShader(new RadialGradient(w * 0.15f, h * 1.05f, h * 0.7f, 0x7060E070, 0x0060E070, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, w, h, q);
        q.setShader(new RadialGradient(w * 0.85f, h * 0.1f, h * 0.6f, 0x3040B8D8, 0x0040B8D8, Shader.TileMode.CLAMP));
        c.drawRect(0, 0, w, h, q);
        q.setShader(null);
        // the soft curved light bands
        q.setStyle(Paint.Style.STROKE);
        int[] cols = { 0x2CB8FFC8, 0x22FFFFFF, 0x1E9CF0B0, 0x18FFFFFF, 0x2690F0A0 };
        float[] widths = { 0.10f, 0.03f, 0.18f, 0.012f, 0.06f };
        for (int i = 0; i < cols.length; i++) {
            Path path = new Path();
            float y0 = h * (0.72f + 0.06f * i), y1 = h * (0.30f + 0.08f * i);
            path.moveTo(-w * 0.05f, y0);
            path.cubicTo(w * 0.3f, y0 - h * 0.05f, w * 0.55f, y1 + h * 0.35f, w * 1.05f, y1);
            q.setColor(cols[i]);
            q.setStrokeWidth(h * widths[i]);
            q.setMaskFilter(new BlurMaskFilter(Math.max(1, h * widths[i] * 0.6f), BlurMaskFilter.Blur.NORMAL));
            c.drawPath(path, q);
        }
        // a few faint vertical streaks on the left
        q.setStrokeWidth(Math.max(2, w * 0.004f));
        q.setMaskFilter(new BlurMaskFilter(Math.max(1, w * 0.004f), BlurMaskFilter.Blur.NORMAL));
        for (int i = 0; i < 4; i++) {
            q.setColor(0x14FFFFFF);
            float sx = w * (0.02f + 0.035f * i);
            c.drawLine(sx, 0, sx + w * 0.02f, h, q);
        }
        return b;
    }

    // ---- text helpers --------------------------------------------------------------------------------------------

    private List<String> wrap(String text, float width, int maxLines) {
        List<String> lines = new ArrayList<>();
        String rest = text == null ? "" : text;
        while (!rest.isEmpty() && lines.size() < maxLines) {
            int n = p.breakText(rest, true, width, null);
            if (n < rest.length()) {
                int sp = rest.lastIndexOf(' ', n);
                if (sp > 0) n = sp;
            }
            lines.add(rest.substring(0, n).trim());
            rest = rest.substring(n).trim();
        }
        return lines;
    }

    private String ellipsize(String t, float width) {
        if (p.measureText(t) <= width) return t;
        int n = p.breakText(t, true, width - p.measureText("..."), null);
        return t.substring(0, Math.max(0, n)) + "...";
    }

    /** a long file name: its end kept ("...name.iso") */
    private String ellipsizeStart(String t, float width) {
        if (p.measureText(t) <= width) return t;
        float room = width - p.measureText("...");
        int i = t.length();
        while (i > 0 && p.measureText(t, i - 1, t.length()) <= room) i--;
        return "..." + t.substring(i);
    }
}

// SPDX-License-Identifier: MIT
// SideMenu - the BACK menu as a panel that slides in from the left over the running game (maintainer 2026-11-11:
// "swipe a menu in from the left, as in Dolphin"; the game is NOT paused yet - freezing is a later toolkit item).
// Plain views, no extra library. Touch: tap a line; a value line steps on each tap. Gamepad / keys (handled by
// PortActivity while the menu is open, so nothing reaches the game): up / down (D-pad or left stick) move, left /
// right step a value, A / Enter chooses, B / BACK closes (in a sub-list: goes back one level).
package xboxrecomp.port;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.DecelerateInterpolator;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

final class SideMenu extends FrameLayout {
    /** One line: a title, optionally a value; step(+1 / -1) changes the value, run() is a plain action. */
    static class Item {
        final String title;
        Item(String title) { this.title = title; }
        String value() { return null; }
        void step(int dir) { run(); }
        void run() { }
        boolean isValue() { return value() != null; }
    }

    interface Page { String title(); List<Item> items(); }

    private static final int ANIM_MS = 180;
    private final float dp;
    private final View scrim;
    private final LinearLayout panel, list;
    private final ScrollView scroll;
    private final TextView header;
    private final List<Page> stack = new ArrayList<>();
    private List<Item> items = new ArrayList<>();
    private final List<TextView> rows = new ArrayList<>();
    private int sel;
    private boolean open;
    private long lastAxisMove;
    private Runnable onClosed;

    SideMenu(Context ctx) {
        super(ctx);
        dp = ctx.getResources().getDisplayMetrics().density;
        setVisibility(INVISIBLE);
        scrim = new View(ctx);
        scrim.setBackgroundColor(0x99000000);
        scrim.setAlpha(0f);
        scrim.setOnTouchListener((v, e) -> { if (e.getActionMasked() == MotionEvent.ACTION_UP) close(); return true; });
        addView(scrim, new LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        panel = new LinearLayout(ctx);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setBackgroundColor(0xF2181C24);
        panel.setPadding(0, (int) (12 * dp), 0, (int) (12 * dp));
        panel.setClickable(true);                         // touches on the panel never fall through to the game
        header = new TextView(ctx);
        header.setTextColor(0xFFE8B04A);
        header.setTextSize(18);
        header.setTypeface(Typeface.DEFAULT_BOLD);
        header.setPadding((int) (20 * dp), (int) (8 * dp), (int) (20 * dp), (int) (12 * dp));
        panel.addView(header);
        scroll = new ScrollView(ctx);
        list = new LinearLayout(ctx);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list);
        panel.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        addView(panel, new LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.MATCH_PARENT, Gravity.START));
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        super.onSizeChanged(w, h, ow, oh);
        // about a third of a landscape screen, at least 300 dp, never more than 85 %
        int pw = (int) Math.min(w * 0.85f, Math.max(300 * dp, w * 0.34f));
        ViewGroup.LayoutParams lp = panel.getLayoutParams();
        if (lp.width != pw) {
            lp.width = pw;
            panel.setLayoutParams(lp);
        }
        if (!open) panel.setTranslationX(-pw);
    }

    boolean isOpen() { return open; }

    void setOnClosed(Runnable r) { onClosed = r; }

    /** Opens the menu with its root page (a page is rebuilt on every show, so values are current). */
    void open(Page root) {
        stack.clear();
        stack.add(root);
        show(0);
        if (open) return;
        open = true;
        setVisibility(VISIBLE);
        bringToFront();
        float w = panel.getWidth() > 0 ? panel.getWidth() : 300 * dp;
        panel.setTranslationX(-w);
        panel.animate().translationX(0).setDuration(ANIM_MS).setInterpolator(new DecelerateInterpolator()).start();
        scrim.animate().alpha(1f).setDuration(ANIM_MS).start();
    }

    void close() {
        if (!open) return;
        open = false;
        panel.animate().translationX(-panel.getWidth()).setDuration(ANIM_MS).setInterpolator(new DecelerateInterpolator())
            .withEndAction(() -> { if (!open) setVisibility(INVISIBLE); }).start();
        scrim.animate().alpha(0f).setDuration(ANIM_MS).start();
        if (onClosed != null) onClosed.run();
    }

    /** A sub-list (e.g. "Hide single touch buttons"); B / BACK returns to the page below it. */
    void push(Page p) { stack.add(p); show(0); }

    /** Back one level; on the root page this closes the menu. */
    void back() {
        if (stack.size() > 1) { stack.remove(stack.size() - 1); show(0); } else close();
    }

    /** Rebuilds the current page, keeping the cursor line (values changed). */
    void refresh() { show(sel); }

    private void show(int cursor) {
        Page p = stack.get(stack.size() - 1);
        header.setText(p.title());
        items = p.items();
        if (stack.size() > 1) {
            items = new ArrayList<>(items);
            items.add(new Item("‹  Back") { @Override void run() { back(); } });
        }
        list.removeAllViews();
        rows.clear();
        for (int i = 0; i < items.size(); i++) {
            final int idx = i;
            TextView t = new TextView(getContext());
            t.setTextSize(16);
            t.setTextColor(Color.WHITE);
            t.setPadding((int) (20 * dp), (int) (10 * dp), (int) (20 * dp), (int) (10 * dp));
            t.setOnClickListener(v -> { sel = idx; activate(+1); });
            list.addView(t, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
            rows.add(t);
        }
        sel = Math.max(0, Math.min(cursor, items.size() - 1));
        if (sel == 0) scroll.post(() -> scroll.scrollTo(0, 0));   // a new page starts at its top line
        paint();
    }

    private void paint() {
        for (int i = 0; i < rows.size(); i++) {
            Item it = items.get(i);
            String v = it.value();
            rows.get(i).setText(v == null ? it.title : it.title + ":  ‹ " + v + " ›");
            rows.get(i).setBackgroundColor(i == sel ? 0x40E8B04A : Color.TRANSPARENT);
        }
        if (sel >= 0 && sel < rows.size()) {
            final View r = rows.get(sel);
            scroll.post(() -> scroll.requestChildRectangleOnScreen(r, new android.graphics.Rect(0, 0, r.getWidth(), r.getHeight()), false));
        }
    }

    private void activate(int dir) {
        if (sel < 0 || sel >= items.size()) return;
        Item it = items.get(sel);
        if (it.isValue()) it.step(dir); else it.run();
        if (open && sel < rows.size() && items == itemsOfTop()) paint();
    }

    private List<Item> itemsOfTop() { return items; }

    private void move(int d) {
        if (items.isEmpty()) return;
        sel = (sel + d + items.size()) % items.size();
        paint();
    }

    /** Keys while open (PortActivity routes every key here). Returns true: nothing goes to the game. */
    boolean onKey(KeyEvent e) {
        if (e.getAction() != KeyEvent.ACTION_DOWN) return true;
        switch (e.getKeyCode()) {
            case KeyEvent.KEYCODE_DPAD_UP: move(-1); break;
            case KeyEvent.KEYCODE_DPAD_DOWN: move(+1); break;
            case KeyEvent.KEYCODE_DPAD_LEFT: if (cur() != null && cur().isValue()) activate(-1); break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: if (cur() != null && cur().isValue()) activate(+1); break;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER:
                activate(+1); break;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_ESCAPE:
                back(); break;
            case KeyEvent.KEYCODE_BUTTON_START: case KeyEvent.KEYCODE_BUTTON_SELECT:
                close(); break;
            default: break;
        }
        return true;
    }

    /** Stick / hat motion while open: the left stick and the hat move the cursor, with a repeat delay. */
    boolean onMotion(MotionEvent e) {
        float y = e.getAxisValue(MotionEvent.AXIS_HAT_Y), x = e.getAxisValue(MotionEvent.AXIS_HAT_X);
        if (Math.abs(y) < 0.5f && Math.abs(x) < 0.5f) { y = e.getAxisValue(MotionEvent.AXIS_Y); x = e.getAxisValue(MotionEvent.AXIS_X); }
        long now = e.getEventTime();
        if (Math.abs(y) < 0.6f && Math.abs(x) < 0.6f) { lastAxisMove = 0; return true; }
        if (now - lastAxisMove < 220) return true;
        lastAxisMove = now;
        if (Math.abs(y) >= Math.abs(x)) move(y > 0 ? +1 : -1);
        else if (cur() != null && cur().isValue()) activate(x > 0 ? +1 : -1);
        return true;
    }

    private Item cur() { return sel >= 0 && sel < items.size() ? items.get(sel) : null; }
}

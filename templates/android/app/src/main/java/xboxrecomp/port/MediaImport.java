// SPDX-License-Identifier: MIT
// MediaImport.java - BACK > Settings > Soundtracks (2026-11-24, the maintainer's design): the player picks a whole
// folder with the system's folder picker (Storage Access Framework, also SD cards) and the app copies its music into
// <userDir>/media/<folder name>/, keeping the sub folders. The toolkit's media scan (src/media/media_scan.h) then makes
// every folder that holds music itself one album - so a folder with music files is one album, and a folder with album
// sub folders gives one album per sub folder. Only what the game plays is copied (MP3, FLAC, WAV, Ogg Vorbis) and the
// .m3u / .m3u8 track lists. Copies, not links: the game opens plain file paths (a SAF URI cannot be opened by the
// native code), and the albums keep working when the SD card or the source folder is gone.
// A port turns it on with Gradle -PmediaTracks=<the game's songs per album> (THPS 2X: 128); 0 / unset = no page.
package xboxrecomp.port;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.TreeMap;

final class MediaImport {
    private MediaImport() { }

    static final String[] MUSIC = { ".mp3", ".flac", ".wav", ".ogg", ".oga" };
    static final String[] LISTS = { ".m3u", ".m3u8" };

    static boolean isMusic(String name) { return endsWithAny(name, MUSIC); }
    static boolean isWanted(String name) { return endsWithAny(name, MUSIC) || endsWithAny(name, LISTS); }

    private static boolean endsWithAny(String name, String[] ext) {
        String n = name.toLowerCase(Locale.ROOT);
        for (String e : ext) if (n.endsWith(e)) return true;
        return false;
    }

    /** One file of the picked folder: its path below the picked folder ('/' separated) and its document. */
    static final class Entry {
        final String rel; final Uri uri; final long size;
        Entry(String rel, Uri uri, long size) { this.rel = rel; this.uri = uri; this.size = size; }
    }

    /** What a picked folder holds: the wanted files, the albums they make (folder below the picked one -> songs; ""
     *  = the picked folder itself) and the total size. */
    static final class Plan {
        String name = "Music";
        final List<Entry> files = new ArrayList<>();
        final TreeMap<String, Integer> albums = new TreeMap<>();
        long bytes;
        int skipped;                // files of other types (pictures, text, AAC ...)
    }

    /** Walks the picked tree (depth-first, at most 16 levels, as the media scan). */
    static Plan scan(ContentResolver cr, Uri tree) {
        Plan p = new Plan();
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        Uri root = DocumentsContract.buildDocumentUriUsingTree(tree, rootId);
        try (Cursor c = cr.query(root, new String[] { Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) p.name = safeName(c.getString(0));
        } catch (Exception ignored) { }
        walk(cr, tree, rootId, "", 0, p);
        return p;
    }

    private static void walk(ContentResolver cr, Uri tree, String docId, String rel, int depth, Plan p) {
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        String[] cols = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
                          Document.COLUMN_SIZE };
        try (Cursor c = cr.query(kids, cols, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext()) {
                String id = c.getString(0), name = c.getString(1), mime = c.getString(2);
                if (name == null || name.startsWith(".")) continue;
                String r = rel.isEmpty() ? safeName(name) : rel + "/" + safeName(name);
                if (Document.MIME_TYPE_DIR.equals(mime)) {
                    if (depth < 16) walk(cr, tree, id, r, depth + 1, p);
                } else if (isWanted(name)) {
                    long size = c.isNull(3) ? 0 : c.getLong(3);
                    p.files.add(new Entry(r, DocumentsContract.buildDocumentUriUsingTree(tree, id), size));
                    p.bytes += size;
                    if (isMusic(name)) p.albums.merge(rel, 1, Integer::sum);
                } else {
                    p.skipped++;
                }
            }
        } catch (Exception ignored) { }
    }

    /** A file / folder name safe on every file system the player may copy media/ to (no / \ : * ? " < > |). */
    static String safeName(String s) {
        String t = s.replaceAll("[/\\\\:*?\"<>|\\p{Cntrl}]", "_").trim();
        return t.isEmpty() || t.equals(".") || t.equals("..") ? "_" : t;
    }

    interface Progress { boolean step(long done, long total, String file); }   // false = cancel

    /** Copies the plan into media/<name>/ through media/.import/ (a cancelled or failed copy leaves nothing behind;
     *  an existing album folder of the same name is replaced only at the end). "" = done, "cancel", else a sentence. */
    static String copy(ContentResolver cr, Plan p, File media, Progress cb) {
        File tmp = new File(media, ".import"), dst = new File(media, p.name);
        PortActivity.deleteTree(tmp);
        long done = 0;
        byte[] buf = new byte[1 << 20];
        try {
            for (Entry e : p.files) {
                File out = new File(tmp, e.rel);
                out.getParentFile().mkdirs();
                try (InputStream in = cr.openInputStream(e.uri); OutputStream o = new FileOutputStream(out)) {
                    if (in == null) throw new IOException("cannot read " + e.rel);
                    int r;
                    while ((r = in.read(buf)) > 0) {
                        o.write(buf, 0, r);
                        done += r;
                        if (!cb.step(done, Math.max(p.bytes, done), e.rel)) { PortActivity.deleteTree(tmp); return "cancel"; }
                    }
                }
            }
        } catch (Exception ex) {
            PortActivity.deleteTree(tmp);
            return "The music could not be copied (" + ex.getMessage() + "). Is there enough free space?";
        }
        PortActivity.deleteTree(dst);
        if (!tmp.renameTo(dst)) { PortActivity.deleteTree(tmp); return "The music could not be stored."; }
        return "";
    }

    /** An album in media/: its path below media/, its songs, its size. */
    static final class Album {
        final String rel; final int songs; final long bytes;
        Album(String rel, int songs, long bytes) { this.rel = rel; this.songs = songs; this.bytes = bytes; }
    }

    /** Every folder below media/ that holds music itself (the media scan's rule), sorted by path. */
    static List<Album> albums(File media) {
        List<Album> l = new ArrayList<>();
        File[] kids = media.listFiles();
        if (kids != null) for (File k : kids) if (k.isDirectory() && !k.getName().startsWith(".")) collect(k, k.getName(), 0, l);
        Collections.sort(l, (a, b) -> a.rel.compareToIgnoreCase(b.rel));
        return l;
    }

    private static void collect(File dir, String rel, int depth, List<Album> out) {
        File[] kids = dir.listFiles();
        if (kids == null) return;
        int songs = 0;
        long bytes = 0;
        for (File k : kids) if (k.isFile() && isMusic(k.getName())) { songs++; bytes += k.length(); }
        if (songs > 0) out.add(new Album(rel, songs, bytes));
        if (depth < 16) for (File k : kids) if (k.isDirectory() && !k.getName().startsWith(".")) collect(k, rel + "/" + k.getName(), depth + 1, out);
    }

    /** Removes one album: its music and track lists (its sub folders - albums of their own - stay), then the folders
     *  that are left empty up to media/. */
    static void remove(File media, String rel) {
        File dir = new File(media, rel);
        File[] kids = dir.listFiles();
        if (kids != null) for (File k : kids) if (k.isFile() && isWanted(k.getName())) k.delete();
        for (File d = dir; d != null && !d.equals(media); d = d.getParentFile()) {
            String[] left = d.list();
            if (left == null || left.length > 0 || !d.delete()) break;
        }
    }

    static String mb(long bytes) {
        return bytes >= (1L << 30) ? String.format(Locale.ROOT, "%.1f GB", bytes / (double) (1L << 30))
                                   : Math.max(1, bytes >> 20) + " MB";
    }
}

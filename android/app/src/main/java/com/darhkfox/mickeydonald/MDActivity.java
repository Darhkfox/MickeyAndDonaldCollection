package com.darhkfox.mickeydonald;

import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;
import org.libsdl.app.SDLActivity;

/**
 * The port's activity: SDL runs the games (libmain.so); this adds the ROM picker and the UI font rasteriser.
 * Android has no folder next to the program to drop ROMs in, so the player picks them with the system file
 * picker and they are copied into app storage (files/roms), where the launcher's ROM search looks.
 */
public class MDActivity extends SDLActivity {
    private static final int PICK_ROMS = 0x4D44;
    private static final int PICK_SOUNDTRACK = 0x4D45;
    private static final long MAX_FILE = 64L << 20;
    // set when the picker returns: the number of files copied (-1 while the picker is open / nothing yet)
    private static volatile int pickResult = -1;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    // ---- ROM picker (called from native code, app_android.cpp) ----------------------------------------
    public static void pickRoms() {
        pickResult = -1;
        final SDLActivity act = mSingleton;
        act.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                try {
                    act.startActivityForResult(i, PICK_ROMS);
                } catch (Exception e) {
                    pickResult = 0;
                }
            }
        });
    }

    // polled from native code: -1 while the picker is open, else the number of files copied (once)
    public static int pollPickResult() {
        int r = pickResult;
        if (r >= 0) pickResult = -1;
        return r == -1 ? -1 : r;
    }

    private String displayName(Uri uri) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) name = c.getString(0);
        } catch (Exception e) {
            name = null;
        }
        if (name == null || name.isEmpty()) name = "rom_" + System.currentTimeMillis() + ".bin";
        return name.replace('/', '_').replace('\\', '_');
    }

    private boolean copyIn(Uri uri, File dir) {
        File out = new File(dir, displayName(uri));
        File tmp = new File(dir, out.getName() + ".tmp");
        try (InputStream in = getContentResolver().openInputStream(uri); OutputStream os = new FileOutputStream(tmp)) {
            byte[] buf = new byte[65536];
            long total = 0;
            int n;
            while ((n = in.read(buf)) > 0) {
                os.write(buf, 0, n);
                total += n;
                if (total > MAX_FILE) throw new Exception("too big for a Mega Drive ROM");
            }
        } catch (Exception e) {
            tmp.delete();
            return false;
        }
        out.delete();
        return tmp.renameTo(out);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == PICK_SOUNDTRACK) {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) { soundtrackResult = 0; return; }
            final Uri tree = data.getData();
            new Thread(new Runnable() {
                @Override
                public void run() { copySoundtrack(tree); }
            }).start();
            return;
        }
        if (requestCode != PICK_ROMS) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        int copied = 0;
        if (resultCode == RESULT_OK && data != null) {
            File dir = new File(getFilesDir(), "roms");
            dir.mkdirs();
            List<Uri> uris = new ArrayList<>();
            ClipData clip = data.getClipData();
            if (clip != null) for (int k = 0; k < clip.getItemCount(); k++) uris.add(clip.getItemAt(k).getUri());
            else if (data.getData() != null) uris.add(data.getData());
            for (Uri u : uris) if (copyIn(u, dir)) copied++;
        }
        pickResult = copied;
    }

    // ---- soundtrack picker (called from native code, app_android.cpp) ---------------------------------
    // An MD+ pack is a folder (a .cue, a .bps / .ips patch and the music, ~400 MB): the player picks the folder and
    // it is copied, on a background thread, to files/soundtracks/<folder name>, where the soundtrack search looks.
    // Result: -1 picker open / copying, 0 cancelled or failed, 1 copied, 2 not enough free space.
    private static volatile int soundtrackResult = -1;
    private static volatile long soundtrackDone = 0, soundtrackTotal = 0;
    private static volatile String soundtrackName = "";

    public static void pickSoundtrack() {
        soundtrackResult = -1;
        soundtrackDone = soundtrackTotal = 0;
        final SDLActivity act = mSingleton;
        act.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    act.startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_SOUNDTRACK);
                } catch (Exception e) {
                    soundtrackResult = 0;
                }
            }
        });
    }

    public static int pollSoundtrackResult() {
        int r = soundtrackResult;
        if (r >= 0) soundtrackResult = -1;
        return r;
    }

    // percent copied, or -1 before the copy starts
    public static int soundtrackProgress() {
        long t = soundtrackTotal;
        return t > 0 ? (int)(soundtrackDone * 100 / t) : -1;
    }

    public static String soundtrackFolder() { return soundtrackName; }

    private static class Doc {
        String id, name;
        long size;
        boolean dir;
    }

    private List<Doc> children(Uri tree, String parentId) {
        List<Doc> out = new ArrayList<>();
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] cols = { DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                          DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE };
        try (Cursor c = getContentResolver().query(kids, cols, null, null, null)) {
            while (c != null && c.moveToNext()) {
                Doc d = new Doc();
                d.id = c.getString(0);
                d.name = c.getString(1).replace('/', '_').replace('\\', '_');
                d.dir = DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2));
                d.size = c.isNull(3) ? 0 : c.getLong(3);
                out.add(d);
            }
        } catch (Exception e) {
            out.clear();
        }
        return out;
    }

    private static boolean wanted(String name) {
        String n = name.toLowerCase();
        return n.endsWith(".cue") || n.endsWith(".bps") || n.endsWith(".ips") || n.endsWith(".wav") || n.endsWith(".bin")
            || n.endsWith(".txt");
    }

    private void copySoundtrack(Uri tree) {
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        String rootName = "soundtrack";
        try (Cursor c = getContentResolver().query(DocumentsContract.buildDocumentUriUsingTree(tree, rootId),
                new String[] { DocumentsContract.Document.COLUMN_DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) rootName = c.getString(0).replace('/', '_').replace('\\', '_');
        } catch (Exception e) {
            rootName = "soundtrack";
        }
        soundtrackName = rootName;
        // the folder and the folders in it (a download that wraps the pack in one more folder)
        List<String[]> files = new ArrayList<>();      // { document id, relative path }
        long total = 0;
        for (Doc d : children(tree, rootId)) {
            if (d.dir) {
                for (Doc e : children(tree, d.id))
                    if (!e.dir && wanted(e.name)) { files.add(new String[] { e.id, d.name + "/" + e.name }); total += e.size; }
            } else if (wanted(d.name)) {
                files.add(new String[] { d.id, d.name });
                total += d.size;
            }
        }
        File base = new File(new File(getFilesDir(), "soundtracks"), rootName);
        if (files.isEmpty()) { soundtrackResult = 0; return; }
        if (getFilesDir().getUsableSpace() < total + (64L << 20)) { soundtrackResult = 2; return; }
        soundtrackTotal = Math.max(1, total);
        byte[] buf = new byte[1 << 20];
        try {
            for (String[] f : files) {
                File out = new File(base, f[1]);
                out.getParentFile().mkdirs();
                File tmp = new File(out.getPath() + ".tmp");
                try (InputStream in = getContentResolver().openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, f[0]));
                     OutputStream os = new FileOutputStream(tmp)) {
                    int n;
                    while ((n = in.read(buf)) > 0) { os.write(buf, 0, n); soundtrackDone += n; }
                }
                out.delete();
                if (!tmp.renameTo(out)) throw new Exception("rename");
            }
            soundtrackResult = 1;
        } catch (Exception e) {
            soundtrackResult = 0;
        }
    }

    // ---- UI font glyphs (called from native code, video_gles.cpp) ------------------------------------
    // Renders each code point with the system font at `px` pixels. meta (filled in): [0] ascent, [1] line
    // height, then per code point: advance x 64, left, top (relative to the pen and baseline), width, height;
    // width 0 = nothing to draw, advance -1 = no glyph. Returns the 8-bit coverage bitmaps, one after another.
    public static byte[] renderGlyphs(int[] cps, float px, boolean bold, int[] meta) {
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
        Typeface tf = Typeface.create(Typeface.DEFAULT, bold ? Typeface.BOLD : Typeface.NORMAL);
        if (bold && Build.VERSION.SDK_INT >= 28) tf = Typeface.create(Typeface.DEFAULT, 700, false);
        p.setTypeface(tf);
        p.setTextSize(px);
        p.setColor(0xFFFFFFFF);
        Paint.FontMetrics fm = p.getFontMetrics();
        meta[0] = (int) Math.ceil(-fm.ascent);
        meta[1] = (int) Math.ceil(fm.descent - fm.ascent + fm.leading);
        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
        Rect r = new Rect();
        for (int i = 0; i < cps.length; i++) {
            String s = new String(Character.toChars(cps[i]));
            int m = 2 + i * 5;
            if (cps[i] != ' ' && Build.VERSION.SDK_INT >= 23 && !p.hasGlyph(s)) {
                meta[m] = -1;
                continue;
            }
            meta[m] = Math.round(p.measureText(s) * 64);
            p.getTextBounds(s, 0, s.length(), r);
            if (r.width() <= 0 || r.height() <= 0) {
                meta[m + 3] = meta[m + 4] = 0;
                continue;
            }
            int w = r.width() + 2, h = r.height() + 2;   // a pixel of room for anti-aliasing on each side
            Bitmap b = Bitmap.createBitmap(w, h, Bitmap.Config.ALPHA_8);
            new Canvas(b).drawText(s, 1 - r.left, 1 - r.top, p);
            ByteBuffer buf = ByteBuffer.allocate(b.getRowBytes() * h);
            b.copyPixelsToBuffer(buf);
            byte[] all = buf.array();
            for (int y = 0; y < h; y++) bytes.write(all, y * b.getRowBytes(), w);
            b.recycle();
            meta[m + 1] = r.left - 1;
            meta[m + 2] = r.top - 1;
            meta[m + 3] = w;
            meta[m + 4] = h;
        }
        return bytes.toByteArray();
    }
}

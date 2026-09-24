package com.iisacc.iiphotolibrary;

import android.Manifest;
import android.app.Activity;
import android.app.PendingIntent;
import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.MediaMetadataRetriever;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.CancellationSignal;
import android.provider.MediaStore;
import android.util.Size;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.ConcurrentHashMap;
import java.security.MessageDigest;

// MediaStore is the system gallery. No absolute gallery paths are exported to
// Society peers, and originals owned by other apps are never moved into Files.
public final class PhotoLibrary {
    public static final int PERMISSION_REQUEST = 9076;
    public static final int TRASH_REQUEST = 9077;
    private static volatile CountDownLatch consentResult;
    private static volatile boolean consentGranted;
    private static final class Session {
        volatile boolean stopped;
        final CancellationSignal signal = new CancellationSignal();
    }
    private static final ConcurrentHashMap<String, Session> sessions = new ConcurrentHashMap<>();
    private static final ThreadLocal<Session> current = new ThreadLocal<>();
    public static void cancelSession(String identifier) {
        Session session = sessions.computeIfAbsent(identifier, key -> new Session());
        session.stopped = true; session.signal.cancel();
        CountDownLatch pending = consentResult; if (pending != null) pending.countDown();
    }
    private static void checkSession() throws InterruptedException {
        Session session = current.get();
        if (session != null && session.stopped) throw new InterruptedException("The photo session ended.");
    }
    public static native void photoAccessFinished();
    public static void consentFinished(int result) {
        CountDownLatch pending = consentResult;
        if (pending != null) { consentGranted = result == Activity.RESULT_OK; pending.countDown(); }
    }
    private static boolean consent(Context context, PendingIntent intent) throws Exception {
        if (!(context instanceof Activity)) throw new SecurityException("Open Society to confirm this gallery change.");
        Activity activity = (Activity) context;
        if (activity.isFinishing()) return false;
        CountDownLatch pending = new CountDownLatch(1); consentGranted = false; consentResult = pending;
        activity.runOnUiThread(() -> {
            try { activity.startIntentSenderForResult(intent.getIntentSender(), TRASH_REQUEST, null, 0, 0, 0); }
            catch (Exception error) { pending.countDown(); }
        });
        try { boolean ready = pending.await(120, TimeUnit.SECONDS); checkSession(); return ready && consentGranted; }
        finally { if (consentResult == pending) consentResult = null; }
    }
    public static String access(Context context) {
        if (Build.VERSION.SDK_INT >= 33) {
            boolean photos = context.checkSelfPermission(Manifest.permission.READ_MEDIA_IMAGES) == PackageManager.PERMISSION_GRANTED;
            boolean videos = context.checkSelfPermission(Manifest.permission.READ_MEDIA_VIDEO) == PackageManager.PERMISSION_GRANTED;
            if (photos && videos) return "full";
            if (photos || videos || (Build.VERSION.SDK_INT >= 34 && context.checkSelfPermission(Manifest.permission.READ_MEDIA_VISUAL_USER_SELECTED) == PackageManager.PERMISSION_GRANTED)) return "limited";
        } else if (context.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED) return "full";
        return "denied";
    }
    public static void requestAccess(Activity activity) {
        activity.runOnUiThread(() -> {
            ArrayList<String> permissions = new ArrayList<>();
            if (Build.VERSION.SDK_INT >= 33) {
                permissions.add(Manifest.permission.READ_MEDIA_IMAGES); permissions.add(Manifest.permission.READ_MEDIA_VIDEO);
                if (Build.VERSION.SDK_INT >= 34) permissions.add(Manifest.permission.READ_MEDIA_VISUAL_USER_SELECTED);
            } else {
                permissions.add(Manifest.permission.READ_EXTERNAL_STORAGE);
                if (Build.VERSION.SDK_INT <= 28) permissions.add(Manifest.permission.WRITE_EXTERNAL_STORAGE);
            }
            if (Build.VERSION.SDK_INT >= 29) permissions.add(Manifest.permission.ACCESS_MEDIA_LOCATION);
            activity.requestPermissions(permissions.toArray(new String[0]), PERMISSION_REQUEST);
        });
    }
    private static Uri original(Context context, Uri uri) {
        return Build.VERSION.SDK_INT >= 29 && context.checkSelfPermission(Manifest.permission.ACCESS_MEDIA_LOCATION) == PackageManager.PERMISSION_GRANTED
            ? MediaStore.setRequireOriginal(uri) : uri;
    }
    private static String photoId(String name) {
        if (name == null || !name.matches("Society-[a-f0-9]{64}-[0-7]-.*")) return "";
        return name.substring(8, 72);
    }
    private static JSONArray scan(Context context) throws Exception {
        return scan(context, "");
    }
    private static String revision(Context context) {
        ArrayList<String> volumes = new ArrayList<>(Build.VERSION.SDK_INT >= 29 ? MediaStore.getExternalVolumeNames(context) : java.util.Collections.singleton("external"));
        java.util.Collections.sort(volumes); StringBuilder value = new StringBuilder();
        for (String volume : volumes) value.append(volume).append(':').append(Build.VERSION.SDK_INT >= 30
            ? MediaStore.getVersion(context, volume) : Build.VERSION.SDK_INT >= 29 ? MediaStore.getVersion(context) : "legacy").append(';');
        return value.toString();
    }
    private static JSONArray scan(Context context, String id) throws Exception {
        if (access(context).equals("denied")) throw new SecurityException("Allow Society to access photos and videos in Settings.");
        JSONArray result = new JSONArray(); Map<String, JSONObject> groups = new LinkedHashMap<>();
        ArrayList<String> volumes = new ArrayList<>(Build.VERSION.SDK_INT >= 29 ? MediaStore.getExternalVolumeNames(context) : java.util.Collections.singleton("external"));
        java.util.Collections.sort(volumes);
        for (String volume : volumes) {
            String version = Build.VERSION.SDK_INT >= 30 ? MediaStore.getVersion(context, volume) : Build.VERSION.SDK_INT >= 29 ? MediaStore.getVersion(context) : "legacy";
            for (boolean isVideo : new boolean[] {false, true}) {
            Uri collection = isVideo ? MediaStore.Video.Media.getContentUri(volume) : MediaStore.Images.Media.getContentUri(volume);
            ArrayList<String> columns = new ArrayList<>(java.util.Arrays.asList(MediaStore.MediaColumns._ID, MediaStore.MediaColumns.DISPLAY_NAME,
                MediaStore.MediaColumns.MIME_TYPE, MediaStore.MediaColumns.DATE_ADDED,
                MediaStore.MediaColumns.DATE_MODIFIED, MediaStore.MediaColumns.SIZE));
            if (Build.VERSION.SDK_INT >= 30) columns.add(MediaStore.MediaColumns.GENERATION_MODIFIED);
            String[] projection = columns.toArray(new String[0]);
            String selection = "1=1";
            if (Build.VERSION.SDK_INT >= 29) selection += " AND " + MediaStore.MediaColumns.IS_PENDING + "=0";
            if (Build.VERSION.SDK_INT >= 30) selection += " AND " + MediaStore.MediaColumns.IS_TRASHED + "=0";
            if (!id.isEmpty()) selection += " AND " + MediaStore.MediaColumns.DISPLAY_NAME + " LIKE ?";
            try (Cursor rows = context.getContentResolver().query(collection, projection, selection,
                id.isEmpty() ? null : new String[] {"Society-" + id + "-%"}, MediaStore.MediaColumns._ID + " ASC")) {
                if (rows == null) throw new IllegalStateException("The system gallery could not be read.");
                while (rows.moveToNext()) {
                    checkSession();
                    String name = rows.getString(1), mime = rows.getString(2);
                    if (name == null || mime == null || rows.getLong(5) <= 0) continue;
                    String media = mime.startsWith("video/") ? "video" : "photo";
                    String uri = ContentUris.withAppendedId(collection, rows.getLong(0)).toString();
                    String imported = photoId(name); int index = imported.isEmpty() ? 0 : name.charAt(73) - '0';
                    String key = imported.isEmpty() ? uri : imported;
                    JSONObject asset = groups.get(key);
                    if (asset == null) {
                        asset = new JSONObject().put("identifier", uri + "#" + version).put("name", name).put("media", media)
                            .put("created", Long.toString(rows.getLong(3) * 1000)).put("stamp", "").put("resources", new JSONArray());
                        groups.put(key, asset);
                    }
                    String role = index == 0 ? media : media.equals("video") ? "pairedVideo" : "alternatePhoto";
                    JSONArray resources = asset.getJSONArray("resources");
                    resources.put(new JSONObject().put("token", uri).put("name", name).put("role", role).put("index", index));
                    if (index == 0) asset.put("identifier", uri + "#" + version).put("name", name).put("media", media);
                    asset.put("stamp", asset.getString("stamp") + version + ":" + (Build.VERSION.SDK_INT >= 30 ? rows.getLong(6) : rows.getLong(4)) + ":" + rows.getLong(5) + ";");
                }
            }
            }
        }
        for (JSONObject asset : groups.values()) {
            JSONArray resources = asset.getJSONArray("resources"); ArrayList<JSONObject> sorted = new ArrayList<>();
            for (int i = 0; i < resources.length(); ++i) sorted.add(resources.getJSONObject(i));
            sorted.sort((a, b) -> Integer.compare(a.optInt("index"), b.optInt("index")));
            asset.put("resources", new JSONArray(sorted)); result.put(asset);
        }
        return result;
    }
    private static void copy(InputStream input, OutputStream output) throws Exception {
        if (input == null) throw new IllegalStateException("The gallery original is unavailable.");
        byte[] buffer = new byte[256 * 1024]; int count;
        while ((count = input.read(buffer)) != -1) { checkSession(); output.write(buffer, 0, count); }
        output.flush();
    }
    private static Bitmap thumbnail(Context context, Uri uri) throws Exception {
        if (Build.VERSION.SDK_INT >= 29) return context.getContentResolver().loadThumbnail(uri, new Size(1024, 1024), current.get() == null ? null : current.get().signal);
        String mime = context.getContentResolver().getType(uri);
        if (mime != null && mime.startsWith("video/")) {
            MediaMetadataRetriever retriever = new MediaMetadataRetriever();
            try { retriever.setDataSource(context, uri); return retriever.getFrameAtTime(); } finally { retriever.release(); }
        }
        BitmapFactory.Options options = new BitmapFactory.Options(); options.inJustDecodeBounds = true;
        try (InputStream input = context.getContentResolver().openInputStream(uri)) { BitmapFactory.decodeStream(input, null, options); }
        options.inSampleSize = Math.max(1, Math.max(options.outWidth, options.outHeight) / 1024); options.inJustDecodeBounds = false;
        try (InputStream input = context.getContentResolver().openInputStream(uri)) { return BitmapFactory.decodeStream(input, null, options); }
    }
    private static String importPhoto(Context context, JSONObject request) throws Exception {
        JSONObject record = request.getJSONObject("record"); String id = record.getString("id");
        if (!id.matches("[a-f0-9]{64}")) throw new IllegalArgumentException("Invalid photo identifier.");
        JSONArray existing = scan(context, id);
        JSONArray resources = record.getJSONArray("resources"), paths = request.getJSONArray("paths");
        if (resources.length() == 0 || resources.length() > 8 || resources.length() != paths.length()) throw new IllegalArgumentException("The photo original is incomplete.");
        Map<Integer, Uri> present = new LinkedHashMap<>();
        for (int i = 0; i < existing.length(); ++i) {
            JSONArray items = existing.getJSONObject(i).getJSONArray("resources");
            for (int j = 0; j < items.length(); ++j) {
                JSONObject item = items.getJSONObject(j); int index = item.getInt("index");
                Uri uri = Uri.parse(item.getString("token"));
                if (index >= resources.length() || present.containsKey(index)) throw new IllegalStateException("This native photo has conflicting resources.");
                MessageDigest digest = MessageDigest.getInstance("SHA-256");
                try (InputStream input = context.getContentResolver().openInputStream(original(context, uri))) {
                    if (input == null) throw new IllegalStateException("The native photo original is unavailable.");
                    byte[] bytes = new byte[256 * 1024]; int count;
                    while ((count = input.read(bytes)) != -1) { checkSession(); digest.update(bytes, 0, count); }
                }
                StringBuilder hash = new StringBuilder(); for (byte b : digest.digest()) hash.append(String.format(java.util.Locale.ROOT, "%02x", b));
                if (!hash.toString().equals(resources.getJSONObject(index).getString("hash")))
                    throw new IllegalStateException("This photo has a different native original. Its existing original was preserved.");
                present.put(index, uri);
            }
        }
        ArrayList<Uri> inserted = new ArrayList<>();
        try {
            for (int i = 0; i < resources.length(); ++i) {
                if (present.containsKey(i)) continue;
                JSONObject resource = resources.getJSONObject(i); String role = resource.getString("role");
                boolean video = role.equals("video") || role.equals("pairedVideo");
                String name = "Society-" + id + "-" + i + "-" + resource.getString("name");
                String extension = name.substring(name.lastIndexOf('.') + 1).toLowerCase(java.util.Locale.ROOT);
                String mime = android.webkit.MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);
                if (mime == null) mime = video ? "video/mp4" : "image/jpeg";
                ContentValues values = new ContentValues(); values.put(MediaStore.MediaColumns.DISPLAY_NAME, name); values.put(MediaStore.MediaColumns.MIME_TYPE, mime);
                if (Build.VERSION.SDK_INT >= 29) {
                    values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/Society");
                    values.put(MediaStore.MediaColumns.IS_PENDING, 1);
                } else {
                    File directory = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_PICTURES), "Society");
                    if (!directory.isDirectory() && !directory.mkdirs()) throw new IllegalStateException("Could not create the Society gallery folder.");
                    values.put(MediaStore.MediaColumns.DATA, new File(directory, name).getAbsolutePath());
                }
                Uri collection = video ? MediaStore.Video.Media.EXTERNAL_CONTENT_URI : MediaStore.Images.Media.EXTERNAL_CONTENT_URI;
                Uri uri = context.getContentResolver().insert(collection, values);
                if (uri == null) throw new IllegalStateException("Could not create the gallery photo.");
                inserted.add(uri);
                try (InputStream input = new FileInputStream(paths.getString(i)); OutputStream output = context.getContentResolver().openOutputStream(uri, "w")) { copy(input, output); }
                present.put(i, uri);
            }
            if (Build.VERSION.SDK_INT >= 29) for (Uri uri : inserted) {
                ContentValues values = new ContentValues(); values.put(MediaStore.MediaColumns.IS_PENDING, 0);
                if (context.getContentResolver().update(uri, values, null, null) != 1) throw new IllegalStateException("Could not publish the gallery photo.");
            }
            Uri first = present.get(0);
            String volume = Build.VERSION.SDK_INT >= 29 ? MediaStore.VOLUME_EXTERNAL_PRIMARY : "external";
            String version = Build.VERSION.SDK_INT >= 30 ? MediaStore.getVersion(context, volume) : Build.VERSION.SDK_INT >= 29 ? MediaStore.getVersion(context) : "legacy";
            return first.toString().replace("/external/", "/" + volume + "/") + "#" + version;
        } catch (Exception error) {
            for (Uri uri : inserted) try { context.getContentResolver().delete(uri, null, null); } catch (Exception ignored) {}
            throw error;
        }
    }
    private static void trash(Context context, String identifier) throws Exception {
        if (Build.VERSION.SDK_INT < 30) throw new UnsupportedOperationException("Move to trash requires Android 11 or later. Manage this item in the system gallery.");
        ArrayList<Uri> uris = new ArrayList<>(); JSONArray assets = scan(context);
        for (int i = 0; i < assets.length(); ++i) {
            JSONObject asset = assets.getJSONObject(i);
            if (!asset.getString("identifier").equals(identifier)) continue;
            JSONArray resources = asset.getJSONArray("resources");
            for (int j = 0; j < resources.length(); ++j) uris.add(Uri.parse(resources.getJSONObject(j).getString("token")));
        }
        if (uris.isEmpty()) return;
        try {
            for (Uri uri : uris) {
                ContentValues values = new ContentValues(); values.put(MediaStore.MediaColumns.IS_TRASHED, 1);
                if (context.getContentResolver().update(uri, values, null, null) != 1) throw new IllegalStateException("The gallery trash operation could not be completed.");
            }
        } catch (SecurityException error) {
            if (!consent(context, MediaStore.createTrashRequest(context.getContentResolver(), uris, true)))
                throw new SecurityException("The gallery trash operation was not confirmed.");
        }
    }
    public static String execute(Context context, String payload) {
        String sessionId = ""; Session session = null;
        try {
            JSONObject request = new JSONObject(payload), result = new JSONObject().put("ok", true);
            sessionId = request.optString("session");
            if (!sessionId.isEmpty()) { session = sessions.computeIfAbsent(sessionId, key -> new Session()); current.set(session); }
            checkSession();
            String action = request.getString("action");
            if (action.equals("scan")) {
                String before = revision(context);
                result.put("assets", scan(context));
                String after = revision(context);
                result.put("complete", access(context).equals("full") && before.equals(after)).put("revision", after);
            }
            else if (action.equals("export")) {
                Uri uri = Uri.parse(request.getString("token"));
                if (!uri.getScheme().equals("content") || !uri.getAuthority().equals("media")) throw new SecurityException("Invalid gallery reference.");
                try (InputStream input = context.getContentResolver().openInputStream(original(context, uri)); OutputStream output = new FileOutputStream(request.getString("path"))) { copy(input, output); }
            } else if (action.equals("preview")) {
                Bitmap bitmap = thumbnail(context, Uri.parse(request.getString("identifier")).buildUpon().fragment(null).build());
                if (bitmap == null) throw new IllegalStateException("The gallery preview is unavailable.");
                try (OutputStream output = new FileOutputStream(request.getString("path"))) {
                    if (!bitmap.compress(Bitmap.CompressFormat.JPEG, 85, output)) throw new IllegalStateException("Could not store the gallery preview.");
                } finally { bitmap.recycle(); }
            } else if (action.equals("import")) result.put("identifier", importPhoto(context, request));
            else if (action.equals("trash")) {
                trash(context, request.getString("identifier"));
            } else throw new IllegalArgumentException("Unsupported photo library operation.");
            return result.toString();
        } catch (Exception error) {
            try { return new JSONObject().put("ok", false).put("error", error instanceof SecurityException
                ? "Android requires permission to change this gallery item. Open the system gallery to finish the operation."
                : error.getMessage() == null ? "The gallery operation failed." : error.getMessage()).toString(); }
            catch (Exception ignored) { return "{\"ok\":false}"; }
        } finally {
            current.remove();
            if (session != null && session.stopped) sessions.remove(sessionId, session);
        }
    }
}

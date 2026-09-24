package com.iisacc.iiphotolibrary.tests;
import android.app.Instrumentation;
import android.content.ContentResolver;
import android.database.Cursor;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Bundle;
import android.provider.MediaStore;
import com.iisacc.iiphotolibrary.PhotoLibrary;
import java.io.*;
import java.security.MessageDigest;
import java.util.*;
import org.json.*;

public final class PhotoInstrumentation extends Instrumentation {
    private int checks;
    private final ArrayList<Uri> created = new ArrayList<>();
    @Override public void onCreate(Bundle arguments) { super.onCreate(arguments); start(); }
    private void check(boolean value, String message) { ++checks; if (!value) throw new AssertionError(message); }
    private JSONObject call(JSONObject command) throws Exception {
        JSONObject result = new JSONObject(PhotoLibrary.execute(getTargetContext(), command.toString()));
        check(result.optBoolean("ok"), result.optString("error", "Photo library operation failed")); return result;
    }
    private String hash(File file) throws Exception {
        MessageDigest hash = MessageDigest.getInstance("SHA-256");
        try (InputStream input = new FileInputStream(file)) { byte[] bytes = new byte[65536]; int count; while ((count = input.read(bytes)) >= 0) hash.update(bytes, 0, count); }
        StringBuilder value = new StringBuilder(); for (byte b : hash.digest()) value.append(String.format("%02x", b)); return value.toString();
    }
    private JSONObject find(String id) throws Exception {
        JSONArray assets = call(new JSONObject().put("action", "scan")).getJSONArray("assets");
        JSONObject found = null;
        for (int i = 0; i < assets.length(); ++i) {
            JSONObject asset = assets.getJSONObject(i);
            if (asset.getString("name").startsWith("Society-" + id + "-")) {
                check(found == null, "A native import appeared as duplicate Society objects"); found = asset;
            }
        }
        check(found != null, "Native gallery item was not discovered"); return found;
    }
    private void media(File image, File video, boolean live) throws Exception {
        String id = hash(image).substring(0, 32) + UUID.randomUUID().toString().replace("-", "");
        ArrayList<File> files = new ArrayList<>(); files.add(live ? image : video);
        if (live) files.add(video);
        JSONArray resources = new JSONArray(), paths = new JSONArray();
        for (int i = 0; i < files.size(); ++i) {
            File file = files.get(i); paths.put(file.getAbsolutePath());
            resources.put(new JSONObject().put("name", file.getName()).put("role", live ? i == 0 ? "photo" : "pairedVideo" : "video")
                .put("hash", hash(file)).put("size", Long.toString(file.length())));
        }
        JSONObject record = new JSONObject().put("schema", 1).put("id", id).put("name", "Society native verification")
            .put("media", live ? "photo" : "video").put("resources", resources);
        JSONObject request = new JSONObject().put("action", "import").put("record", record).put("paths", paths);
        String identifier = call(request).getString("identifier");
        JSONObject asset = find(id); check(asset.getString("identifier").equals(identifier), "Import did not return the stable gallery identifier");
        JSONArray nativeResources = asset.getJSONArray("resources"); check(nativeResources.length() == files.size(), "Live photo resources were split or lost");
        for (int i = 0; i < nativeResources.length(); ++i) {
            JSONObject resource = nativeResources.getJSONObject(i); Uri uri = Uri.parse(resource.getString("token")); created.add(uri);
            try (Cursor row = getTargetContext().getContentResolver().query(uri, new String[]{MediaStore.MediaColumns.IS_PENDING}, null, null, null)) {
                check(row.moveToFirst() && row.getInt(0) == 0, "The gallery item was not published");
            }
            File exported = new File(getTargetContext().getCacheDir(), "export-" + i);
            call(new JSONObject().put("action", "export").put("token", uri.toString()).put("path", exported.getAbsolutePath()));
            check(hash(exported).equals(hash(files.get(i))), "Exported original bytes changed");
        }
        File preview = new File(getTargetContext().getCacheDir(), "preview.jpg");
        call(new JSONObject().put("action", "preview").put("identifier", identifier).put("path", preview.getAbsolutePath()));
        check(BitmapFactory.decodeFile(preview.getAbsolutePath()) != null, "Photo or video preview is not an image");
        check(call(request).getString("identifier").equals(identifier), "A retry created a duplicate native item");
        check(find(id).getJSONArray("resources").length() == files.size(), "A retry lost resources");
        JSONObject changed = new JSONObject(record.toString());
        changed.getJSONArray("resources").getJSONObject(0).put("hash", String.join("", Collections.nCopies(64, "0")));
        JSONObject conflict = new JSONObject(PhotoLibrary.execute(getTargetContext(),
            new JSONObject().put("action", "import").put("record", changed).put("paths", paths).toString()));
        check(!conflict.optBoolean("ok"), "A different native original was incorrectly accepted as the requested version");
        if (live) {
            Uri missing = Uri.parse(nativeResources.getJSONObject(1).getString("token"));
            check(getTargetContext().getContentResolver().delete(missing, null, null) == 1, "Could not simulate a partially registered Live Photo");
            check(call(request).getString("identifier").equals(identifier), "Repair replaced the existing primary original");
            nativeResources = find(id).getJSONArray("resources");
            check(nativeResources.length() == files.size(), "A partial import was falsely treated as complete");
            for (int i = 0; i < nativeResources.length(); ++i) created.add(Uri.parse(nativeResources.getJSONObject(i).getString("token")));
        }
        call(new JSONObject().put("action", "trash").put("identifier", identifier));
        for (int i = 0; i < nativeResources.length(); ++i) {
            Uri uri = Uri.parse(nativeResources.getJSONObject(i).getString("token"));
            try (Cursor row = getTargetContext().getContentResolver().query(uri, new String[]{MediaStore.MediaColumns.IS_TRASHED}, null, null, null)) {
                check(row.moveToFirst() && row.getInt(0) == 1, "A gallery resource was not moved to trash");
            }
        }
    }
    private void failedImportRemovesPendingResources(File image, File video) throws Exception {
        String id = hash(image).substring(0, 32) + UUID.randomUUID().toString().replace("-", "");
        JSONArray resources = new JSONArray().put(new JSONObject().put("name", "partial.png").put("role", "photo").put("hash", hash(image)))
            .put(new JSONObject().put("name", "partial.mp4").put("role", "pairedVideo").put("hash", hash(video)));
        JSONObject request = new JSONObject().put("action", "import").put("record", new JSONObject().put("id", id).put("resources", resources))
            .put("paths", new JSONArray().put(image.getAbsolutePath()).put(new File(getTargetContext().getCacheDir(), "missing-original").getAbsolutePath()));
        JSONObject failed = new JSONObject(PhotoLibrary.execute(getTargetContext(), request.toString()));
        check(!failed.optBoolean("ok"), "An incomplete original was published");
        int remaining = 0;
        for (Uri collection : new Uri[]{MediaStore.Images.Media.EXTERNAL_CONTENT_URI, MediaStore.Video.Media.EXTERNAL_CONTENT_URI}) {
            try (Cursor rows = getTargetContext().getContentResolver().query(collection, new String[]{MediaStore.MediaColumns._ID},
                MediaStore.MediaColumns.DISPLAY_NAME + " LIKE ?", new String[]{"Society-" + id + "-%"}, null)) {
                while (rows.moveToNext()) { ++remaining; created.add(android.content.ContentUris.withAppendedId(collection, rows.getLong(0))); }
            }
        }
        check(remaining == 0, "Failed import left gallery resources behind");
    }
    @Override public void onStart() {
        Bundle output = new Bundle();
        try {
            check(PhotoLibrary.access(getTargetContext()).equals("full"), "Grant photo and video permissions before the native probe");
            File image = new File(getTargetContext().getCacheDir(), "image.png"), video = new File(getTargetContext().getCacheDir(), "video.mp4");
            Bitmap bitmap = Bitmap.createBitmap(32, 32, Bitmap.Config.ARGB_8888); bitmap.eraseColor(0xff2468ab);
            try (OutputStream stream = new FileOutputStream(image)) { bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream); } bitmap.recycle();
            try (InputStream input = getTargetContext().getAssets().open("video.mp4"); OutputStream stream = new FileOutputStream(video)) {
                byte[] bytes = new byte[4096]; int count; while ((count = input.read(bytes)) >= 0) stream.write(bytes, 0, count);
            }
            media(image, video, false); media(image, video, true);
            failedImportRemovesPendingResources(image, video);
            String session = UUID.randomUUID().toString(); PhotoLibrary.cancelSession(session);
            JSONObject cancelled = new JSONObject(PhotoLibrary.execute(getTargetContext(), new JSONObject().put("action", "scan").put("session", session).toString()));
            check(!cancelled.optBoolean("ok"), "A retired photo session continued reading the gallery");
            output.putString("result", "PASS"); output.putInt("checks", checks);
        } catch (Throwable error) { output.putString("result", "FAIL"); output.putString("error", error.toString()); output.putInt("checks", checks); }
        finally { for (Uri uri : created) try { getTargetContext().getContentResolver().delete(uri, null, null); } catch (Exception ignored) {} }
        finish("PASS".equals(output.getString("result")) ? -1 : 1, output);
    }
}
